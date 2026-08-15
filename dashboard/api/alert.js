/*
 * /api/alert — sends a leak notification by email or SMS.
 *
 * This is the only server-side code in the project, and it exists for exactly
 * one reason: an API key must never reach the browser. The dashboard is a
 * public page — anything in it can be read by anyone who opens DevTools. So
 * the page decides *that* an alert should go out, and this function, which
 * runs on Vercel and reads its keys from environment variables, decides *how*.
 *
 * Nothing here is committed with a value. Clone the repo, deploy it, and the
 * alert feature reports itself unconfigured until you add your own keys in
 * the Vercel dashboard. That is the intended behaviour, not a missing step.
 *
 * Environment variables (all optional — set the ones for the channel you want):
 *
 *   Email, via Resend
 *     RESEND_API_KEY      re_...
 *     ALERT_FROM          "Air Sentinel <alerts@yourdomain.com>"
 *                         or onboarding@resend.dev while testing
 *
 *   SMS, via Twilio
 *     TWILIO_ACCOUNT_SID  AC...
 *     TWILIO_AUTH_TOKEN   your auth token
 *     TWILIO_FROM         +1... (a number you own on Twilio)
 *
 *   Optional hardening
 *     ALERT_ALLOWED_TO    comma-separated allowlist of permitted recipients.
 *                         When set, anything else is refused. Strongly
 *                         recommended once the URL is public — see below.
 *
 * GET  → configuration status. No secrets, ever — booleans only.
 * POST → send one alert. Body: {to, gas, ppm, unit, status, pctLEL, ratio}
 */

// Best-effort in-process cooldown. Serverless instances are recycled, so this
// is not a hard guarantee — it exists to stop an alarm that chatters around a
// threshold from sending fifty messages, not to stop a determined abuser.
// The real control against abuse is ALERT_ALLOWED_TO.
const lastSent = new Map();
const COOLDOWN_MS = 5 * 60 * 1000;

const isEmail = (s) => /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(s);
// E.164: a leading + and 8–15 digits. Twilio rejects anything else anyway.
const isPhone = (s) => /^\+[1-9]\d{7,14}$/.test(s);

function channels() {
  return {
    email: Boolean(process.env.RESEND_API_KEY && process.env.ALERT_FROM),
    sms: Boolean(
      process.env.TWILIO_ACCOUNT_SID &&
      process.env.TWILIO_AUTH_TOKEN &&
      process.env.TWILIO_FROM
    ),
  };
}

function allowlist() {
  return (process.env.ALERT_ALLOWED_TO || "")
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
}

// Every message says what this is. An alert that looks like it came from a
// certified safety product would be worse than no alert at all.
const DISCLAIMER =
  "Air Sentinel is a student demonstration project built on an MQ-2 sensor. " +
  "It is not a certified life-safety device and can be wrong. Treat this as a " +
  "prompt to go and look, not as a verified measurement.";

function buildMessage(d) {
  const when = new Date();
  const ist = when.toLocaleString("en-IN", {
    timeZone: "Asia/Kolkata",
    dateStyle: "medium",
    timeStyle: "medium",
  });

  const lines = [
    `${d.status === "DANGER" ? "GAS ALARM" : "Elevated reading"} — ${d.gas}`,
    `${d.ppm} ${d.unit || "ppm"}${d.pctLEL ? `  (${d.pctLEL}% of LEL)` : ""}`,
    `Rs/R0 ${d.ratio}`,
    ist + " IST",
  ];

  const subject =
    d.status === "DANGER"
      ? `GAS ALARM: ${d.gas} at ${d.ppm} ${d.unit || "ppm"}`
      : `Air Sentinel: ${d.gas} elevated (${d.ppm} ${d.unit || "ppm"})`;

  // SMS is billed per segment, so keep it to one where possible.
  const sms = `${lines[0]}. ${lines[1]}. ${ist} IST. Not a certified detector — go and check.`;

  const esc = (s) =>
    String(s).replace(/[&<>"]/g, (c) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c])
    );

  const html = `<div style="font-family:system-ui,-apple-system,'Segoe UI',sans-serif;max-width:520px">
  <div style="background:${d.status === "DANGER" ? "#d70015" : "#b25000"};color:#fff;
       padding:18px 22px;border-radius:12px 12px 0 0">
    <div style="font-size:12px;letter-spacing:.08em;text-transform:uppercase;opacity:.85">Air Sentinel</div>
    <div style="font-size:24px;font-weight:700;margin-top:4px">${esc(lines[0])}</div>
  </div>
  <div style="border:1px solid #e5e5ea;border-top:0;border-radius:0 0 12px 12px;padding:22px">
    <div style="font-size:30px;font-weight:700;letter-spacing:-.02em">${esc(lines[1])}</div>
    <div style="color:#6e6e73;font-size:14px;margin-top:10px">
      Sensor ratio Rs/R₀ ${esc(d.ratio)}<br>${esc(ist)} IST
    </div>
    <p style="color:#6e6e73;font-size:12.5px;line-height:1.55;margin:20px 0 0;
       border-left:2px solid #e5e5ea;padding-left:12px">${esc(DISCLAIMER)}</p>
  </div>
</div>`;

  return { subject, html, sms, text: lines.join("\n") + "\n\n" + DISCLAIMER };
}

async function sendEmail(to, msg) {
  const r = await fetch("https://api.resend.com/emails", {
    method: "POST",
    headers: {
      Authorization: `Bearer ${process.env.RESEND_API_KEY}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      from: process.env.ALERT_FROM,
      to: [to],
      subject: msg.subject,
      html: msg.html,
      text: msg.text,
    }),
  });
  if (!r.ok) {
    // Surface the provider's own message — "domain not verified" is by far the
    // most common first-run failure and guessing at it wastes an afternoon.
    const body = await r.text();
    throw new Error(`Resend ${r.status}: ${body.slice(0, 300)}`);
  }
}

async function sendSms(to, msg) {
  const sid = process.env.TWILIO_ACCOUNT_SID;
  const auth = Buffer.from(`${sid}:${process.env.TWILIO_AUTH_TOKEN}`).toString("base64");
  const r = await fetch(`https://api.twilio.com/2010-04-01/Accounts/${sid}/Messages.json`, {
    method: "POST",
    headers: {
      Authorization: `Basic ${auth}`,
      "Content-Type": "application/x-www-form-urlencoded",
    },
    body: new URLSearchParams({
      To: to,
      From: process.env.TWILIO_FROM,
      Body: msg.sms,
    }),
  });
  if (!r.ok) {
    const body = await r.text();
    throw new Error(`Twilio ${r.status}: ${body.slice(0, 300)}`);
  }
}

export default async function handler(req, res) {
  const ch = channels();
  const allow = allowlist();

  if (req.method === "GET") {
    return res.status(200).json({
      configured: ch.email || ch.sms,
      channels: ch,
      allowlisted: allow.length > 0,
    });
  }

  if (req.method !== "POST") {
    res.setHeader("Allow", "GET, POST");
    return res.status(405).json({ error: "method_not_allowed" });
  }

  if (!ch.email && !ch.sms) {
    return res.status(503).json({
      error: "not_configured",
      message:
        "No alert channel is set up on this deployment. Add RESEND_API_KEY and " +
        "ALERT_FROM (email) or the TWILIO_* variables (SMS) in your Vercel " +
        "project settings, then redeploy. See docs/SETUP.md.",
    });
  }

  // Vercel parses JSON bodies for Node functions, but not when the client
  // omits or mistypes Content-Type — so handle the raw-string case too.
  let body = req.body;
  if (typeof body === "string") {
    try { body = JSON.parse(body); } catch { body = null; }
  }
  if (!body || typeof body !== "object") {
    return res.status(400).json({ error: "bad_request", message: "Expected a JSON body." });
  }

  const to = String(body.to || "").trim();
  if (!to) {
    return res.status(400).json({ error: "no_recipient", message: "No recipient given." });
  }

  const wantsEmail = isEmail(to);
  const wantsSms = isPhone(to);
  if (!wantsEmail && !wantsSms) {
    return res.status(400).json({
      error: "bad_recipient",
      message:
        "Enter an email address, or a phone number in full international " +
        "format including the country code — +919876543210, not 9876543210.",
    });
  }

  if (allow.length && !allow.includes(to)) {
    return res.status(403).json({
      error: "not_allowlisted",
      message: "That recipient is not in this deployment's ALERT_ALLOWED_TO list.",
    });
  }

  if (wantsEmail && !ch.email) {
    return res.status(503).json({ error: "email_not_configured",
      message: "Email alerts are not set up on this deployment. Add RESEND_API_KEY and ALERT_FROM." });
  }
  if (wantsSms && !ch.sms) {
    return res.status(503).json({ error: "sms_not_configured",
      message: "SMS alerts are not set up on this deployment. Add the TWILIO_* variables." });
  }

  const now = Date.now();
  const previous = lastSent.get(to) || 0;
  if (!body.test && now - previous < COOLDOWN_MS) {
    const wait = Math.ceil((COOLDOWN_MS - (now - previous)) / 1000);
    return res.status(429).json({
      error: "cooling_down",
      retry_after_s: wait,
      message: `Already alerted this recipient. Next alert allowed in ${wait}s.`,
    });
  }

  const msg = buildMessage({
    gas: body.gas || "gas",
    ppm: body.ppm ?? "—",
    unit: body.unit || "ppm",
    status: body.status === "DANGER" ? "DANGER" : "WARNING",
    pctLEL: body.pctLEL,
    ratio: body.ratio ?? "—",
  });

  try {
    if (wantsEmail) await sendEmail(to, msg);
    else await sendSms(to, msg);
  } catch (e) {
    console.error("alert send failed:", e.message);
    return res.status(502).json({ error: "send_failed", message: e.message });
  }

  lastSent.set(to, now);
  // Bound the map — a long-lived instance shouldn't accumulate recipients.
  if (lastSent.size > 500) {
    for (const [k, t] of lastSent) if (now - t > COOLDOWN_MS) lastSent.delete(k);
  }

  return res.status(200).json({ sent: true, via: wantsEmail ? "email" : "sms" });
}
