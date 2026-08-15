# Setup — from parts to a working detector

Every step, in order. Nothing here assumes you have seen the project before.

Sections 1–5 give you a working detector and dashboard. Section 6 puts it on the
internet. Section 7 turns on leak alerts by email or SMS. You can stop after any
of them.

---

## 1. What you need

**Hardware**

| Part | Notes |
|---|---|
| Arduino Uno | Any clone works |
| MQ-2 gas sensor module | The 4-pin kind with `VCC GND DOUT AOUT`. "Flying Fish" is a common seller |
| 16×2 LCD with I²C backpack | The backpack is the small board with 4 pins — `GND VCC SDA SCL` |
| Active buzzer, 2 terminals | |
| Jumper wires | 10 male-to-female |
| USB cable | **A data cable.** Many charging cables have no data lines and will not work |
| 5 V / 2 A phone charger | Optional but better than a laptop port for the exhibition |

No resistors and no breadboard are required. The alarm LED is the one already
on the Uno at pin 13.

**Software**

- [Arduino IDE](https://www.arduino.cc/en/software) (2.x)
- Chrome or Edge on a desktop or laptop — the dashboard reads the board over
  USB using the Web Serial API, which Firefox and Safari do not have, and no
  mobile browser has

**Accounts** — only for sections 6 and 7

- A [Vercel](https://vercel.com) account to host the dashboard (free)
- A [Resend](https://resend.com) account for email alerts (free)
- A [Twilio](https://twilio.com) account for SMS alerts (paid, and see the
  India caveat in §7)

---

## 2. Wire it

Ten wires. Do this with the board unplugged.

| From | To |
|---|---|
| MQ-2 `VCC` | Uno `5V` |
| MQ-2 `GND` | Uno `GND` |
| MQ-2 `AOUT` | Uno `A0` |
| MQ-2 `DOUT` | Uno `D7` *(optional)* |
| LCD backpack `VCC` | Uno `5V` |
| LCD backpack `GND` | Uno `GND` |
| LCD backpack `SDA` | Uno `A4` |
| LCD backpack `SCL` | Uno `A5` |
| Buzzer `+` (longer leg) | Uno `D8` |
| Buzzer `−` (shorter leg) | Uno `GND` |

The Uno has three `GND` pins and they are the same rail — use whichever is
closest. `A4` and `A5` are the hardware I²C bus and cannot be moved.

Full reference with diagrams: [WIRING.md](WIRING.md), or the **Wiring** tab of
the dashboard, where you can tap any wire to see what it carries.

---

## 3. Flash the firmware

1. Open Arduino IDE → **Tools → Manage Libraries** → search
   `LiquidCrystal I2C` → install the one by **Frank de Brabander**.
2. Open `gas_detector_uno/gas_detector_uno.ino`.
3. Check two settings at the top of the file:
   - `MQ_SELECT` — the number stamped on your sensor's metal can. `2` for an
     MQ-2.
   - `BUZZER_PASSIVE` — touch your buzzer briefly across 5 V and GND. A
     **steady tone** means active, leave it `0`. A **single click** means
     passive, set it to `1`.
4. **Tools → Board → Arduino Uno**, then **Tools → Port** and pick the port
   that appears when you plug the board in.
5. Press Upload.

> **No port listed?** The cable is the usual culprit — try one you have used to
> move files off a phone. If that does not help, the CH340 driver may be
> missing on Uno clones.

Open **Tools → Serial Monitor** and set the baud rate to **115200**. You should
see the LCD's I²C address, then a countdown, then a `GS1,…` line once a second.

---

## 4. Burn in and calibrate — do not skip this

A new MQ sensor's baseline drifts for a long time. Readings taken before it
settles mean nothing.

1. **Leave the detector powered for 24–48 hours.** Overnight twice. The sensor
   will be warm to the touch — that is the internal heater and it is correct.
2. After that, take it **outdoors, into clean air**. Not the kitchen, not a
   room where anyone has cooked, sprayed deodorant, or used sanitiser.
3. Open the Serial Monitor at 115200 and send **`c`**.

That measures R₀, the sensor's resistance in clean air, and stores it in the
Uno's EEPROM so it survives being unplugged. Every number the system reports is
a ratio against R₀ — calibrate in a contaminated room and everything downstream
is quietly wrong.

Send **`r`** at any time for raw diagnostics.

---

## 5. Run the dashboard

The dashboard is one HTML file with no build step and no dependencies.

**Locally**, from the repository root:

```bash
cd dashboard && npx serve
```

Then open the `http://localhost:...` address it prints.

> It must be **localhost or https** — opening `index.html` by double-clicking
> it gives a `file://` page, which browsers do not allow to touch a USB port.
> The page will tell you this rather than just hiding the button.

**To connect the detector:**

1. Leave the Uno plugged into the same computer.
2. Open the dashboard in Chrome or Edge.
3. Click **Connect detector** in the header and choose the port.

The pill in the header turns to `USB · live`.

> **Close the Arduino IDE's Serial Monitor first.** Only one program can hold a
> serial port, and the Serial Monitor holding it is the most common reason
> Connect fails.

Opening the port resets the Uno — that is how USB serial works and cannot be
avoided. The firmware notices that power never actually dropped and does an
8-second settle instead of the full 180-second warm-up.

---

## 6. Deploy it

```bash
npm i -g vercel
cd dashboard
vercel --prod
```

Follow the prompts the first time — Vercel will link a project. Deploy **from
inside `dashboard/`** so `index.html` lands at the root of the deployment.

If you connect the GitHub repository to the Vercel project instead, set
**Root Directory = `dashboard`** in the project settings, or Vercel will look
for the site at the repository root and find nothing.

Redeploy any time with `vercel --prod` from `dashboard/`.

---

## 7. Turn on leak alerts

The dashboard can email or text someone when the alarm latches.

**How the keys stay safe.** The dashboard is a public web page — anything in it
can be read by anyone who opens developer tools. So the page never holds a key.
It decides *that* an alert is warranted and posts to `/api/alert`, a small
serverless function that runs on Vercel and reads its credentials from
environment variables. Those variables live only in your Vercel project. They
are not in this repository and never will be.

The consequence, which is intended: **a fresh clone of this repository has no
alert credentials, so the feature reports itself unavailable.** The detector,
the dashboard, and everything else work exactly as before. Only alerts are off
until you add your own keys.

### 7a. Email — recommended, free, five minutes

1. Sign up at [resend.com](https://resend.com).
2. **API Keys → Create API Key.** Copy it — it starts `re_` and is shown once.
3. In Vercel: your project → **Settings → Environment Variables**. Add two:

   | Name | Value |
   |---|---|
   | `RESEND_API_KEY` | the `re_…` key |
   | `ALERT_FROM` | `Air Sentinel <onboarding@resend.dev>` |

4. **Redeploy** — environment variables are read at deploy time, so an existing
   deployment will not pick them up. `vercel --prod` from `dashboard/`.

> `onboarding@resend.dev` is Resend's shared testing sender. It works
> immediately but **only delivers to the email address you signed up with**.
> To send anywhere else, add and verify your own domain in Resend (they give
> you DNS records to add), then set `ALERT_FROM` to an address at that domain.
> For an exhibition, sending to your own address is usually all you need.

### 7b. SMS — optional, paid, and harder in India

1. Sign up at [twilio.com](https://twilio.com) and buy a number.
2. Add three environment variables in Vercel and redeploy:

   | Name | Value |
   |---|---|
   | `TWILIO_ACCOUNT_SID` | starts `AC…` |
   | `TWILIO_AUTH_TOKEN` | from the Twilio console |
   | `TWILIO_FROM` | your Twilio number, e.g. `+15551234567` |

> **Two things will bite you.** A Twilio **trial** account can only send to
> numbers you have verified in the console — you cannot text a judge's phone on
> a trial. And **sending SMS to Indian numbers requires DLT registration** under
> TRAI rules: you register your entity, sender ID, and message templates before
> anything is delivered. That takes days and is not worth it for a one-day
> exhibition. **Use email.**

Phone numbers must be full international format — `+919876543210`, not
`9876543210`.

### 7c. Locking it down

Once the URL is public, anyone who finds it can in principle use your alert
endpoint to send messages to arbitrary addresses on your quota. Set one more
variable to prevent that:

| Name | Value |
|---|---|
| `ALERT_ALLOWED_TO` | `you@example.com,+919876543210` |

Any recipient not on that comma-separated list is refused. Set this before the
exhibition. The endpoint also enforces a five-minute cooldown per recipient, so
an alarm chattering around its threshold cannot send fifty messages.

### 7d. Using it

On the **Live** tab, the **Alerts** card:

1. Type an email address or phone number.
2. Press **Send test** to confirm it arrives.
3. Press **Arm alerts**.

The recipient is remembered in the browser. An alert is sent **once**, on the
transition from clear into an alarm state — not repeatedly while the alarm
holds.

> **What this is not.** Alerts fire from the browser tab, so they only work
> while the dashboard is open and connected to the detector. This is not a
> standalone monitoring service — close the tab and no alerts are sent. Making
> it independent of the browser would mean the detector reaching the internet
> on its own, which is the ESP8266 build described in the README.

---

## 8. Troubleshooting

| Symptom | Cause |
|---|---|
| LCD blank, or two rows of blocks | Contrast — turn the blue trimmer on the back of the I²C backpack |
| `No I2C device found` on serial | `SDA`/`SCL` swapped, backpack unpowered, or a cold solder joint on the 16-pin header |
| Only the top LCD row shows | Reflow the header joints; the firmware still works, and `lcd_quote/` is a single-row demo |
| No **Connect detector** button | Page is on `file://` or plain `http`, is embedded in another page, or the browser is not Chrome/Edge. The page names which |
| Connect fails, port list empty | Charge-only USB cable, or missing CH340 driver |
| Connect fails with an error | Arduino IDE Serial Monitor is holding the port — close it |
| ppm pinned at maximum | R₀ was calibrated in dirty air. Redo §4 outdoors |
| Readings drift over hours | Normal on a new sensor. Finish the 24–48 h burn-in |
| Readings shift with the weather | Real. SnO₂ is humidity-sensitive — see the **Roadmap** tab for compensation |
| Alerts card says "unavailable" | No credentials on that deployment. §7, and remember to redeploy after adding variables |
| Test alert fails, "domain not verified" | `ALERT_FROM` uses a domain you have not verified in Resend. Use `onboarding@resend.dev` and send to your own address |

---

## 9. Before you rely on any of this

This is a demonstration and teaching instrument. It has no self-test, no
sensor-failure detection, no battery backup, and no certification, and its
calibration traces to a curve fit against a datasheet graph rather than a
reference gas.

An open-circuit sensor reads as infinite resistance, which the arithmetic
interprets as perfectly clean air — the detector would report all-clear while
broken, and it currently cannot tell the difference. That limitation is listed
first on the **Roadmap** tab because it is the most important thing left to fix.

For actual protection of a home with an LPG cylinder, buy a BIS or CE certified
alarm. Build this one to understand how that one works.
