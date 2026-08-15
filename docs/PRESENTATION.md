# Booth guide

Everything you need at the table, in the order you'll need it.

---

## The hook — say this first

> **"Most gas detector projects report a number they can't justify. This one tells you what it doesn't know."**

Then stop talking and let them ask "what do you mean?" That question is the whole presentation, and you want them to ask it rather than sit through it.

If they don't bite, the follow-up is:

> "This sensor gives me exactly one number — a resistance. Everything else on this screen is me interpreting that one number. So the interesting question isn't what it detects. It's what it's allowed to claim."

**Do not open with** "this is a gas detection system using an MQ-135 sensor and an Arduino." Every third project at the fair opens that way, and it tells the judge nothing they can't see.

---

## The demo — run it in this order

The order matters. Step 3 is the moment the project lands, and it only works if steps 1 and 2 have set it up.

**1. Start clear.** Point at Rs/R₀ on the Live tab.

> "That's the measurement. One resistance, divided by what the same resistance was in clean air. Everything else is derived from it."

**2. Hand sanitiser.** A cotton bud dipped in sanitiser or isopropyl alcohol, 10 cm from the mesh.

Watch: the reading climbs hard, the buzzer chirps at WARNING, then holds solid at DANGER. The LCD pins to the gas driving it. The event log records both transitions.

> "That's ethanol evaporating. The sensor's resistance just dropped by a factor of five."

**3. Now break it — breathe on it.** Let it settle first, then breathe steadily across the mesh for a few seconds.

It spikes again. Same alarm, same kind of number — from human breath.

> "Nothing about those two is alike. One is alcohol vapour, the other is the carbon dioxide and moisture in my lungs. The sensor reports them the same way, and no software I write on top of it can separate them. It's one resistor — anything that gives up electrons at its surface pushes it the same direction."

**This is the moment.** You have just deliberately fooled your own project in front of a judge and explained exactly why. Most students hide this. Showing it is the strongest thing you can do — and it needs no flame, so you can repeat it all day indoors.

**4. Open the Sensor tab.** Five gas rows, all computed from that one measurement.

> "Five interpretations of one number. At most one of them is true, and the sensor doesn't know which. So the alarm runs on the worst case."

**5. Show benzene, greyed out and marked *not resolvable*.** Then go to the Safety tab and show the CO card.

> "Benzene is a carcinogen — it matters at 1 ppm. This sensor bottoms out at 10. It would only see benzene once you were already ten times over the limit, so it shows the row and refuses to alarm on it."

> "And carbon monoxide it doesn't measure at all — there's no response curve for it. That's the one that actually kills people in homes, because it's odourless and a faulty geyser makes it. My detector will not warn you. An MQ-7 would; that's the first thing on the roadmap."

**6. Land the ending.**

> "To actually identify a gas you need to break the single-measurement problem. Four to six different MQ sensors read at once — each has a different sensitivity ratio to each gas, so the *pattern* across the array is gas-specific even though no single sensor is. Then you train a classifier on it. That's the next version."

Total: about three minutes. Practise until you can do it without looking at notes.

---

## Judge questions, and how to answer them

**"Why not just buy a gas detector?"**
> A bought one gives you a number and no way to check it. This one shows the raw resistance, the curve, the valid range and the threshold, so every number on screen can be traced back. That was the point — not to beat a commercial detector, but to understand what one is actually doing.

**"How accurate is it?"**
> Order of magnitude, and I'll show you why. The coefficients are least-squares fits to a curve printed in the datasheet PDF — not a calibration against a reference gas. That's stated on the Sensor tab. It's accurate enough to alarm on and not accurate enough to report as data, and the interface says so.

**"How did you calibrate it?"**
> R₀ is the sensor's resistance in clean air, averaged over 64 samples outdoors and stored in EEPROM. Everything is a ratio against it. The sensor also needs 24 to 48 hours of continuous power before its baseline stops drifting — readings before that are meaningless, so I burned it in first.

**"What's Rs over R₀?"**
> Rs is what the sensor reads now, R₀ is what it read in clean air. Using the ratio instead of the raw resistance cancels out unit-to-unit variation — two MQ-135s can differ by a factor of two in absolute resistance but track each other closely in ratio. That's why the datasheet plots ratio, not ohms.

**"Why can't it identify the gas?"**
> It's a heated tin-dioxide bead. Any reducing gas that lands on it lowers the resistance. There's one output — no wavelength, no mass, no separation. Identification needs a second independent axis, which one sensor doesn't have.

**"What's the AQI in this room?"** *(This is a trap. Answer it honestly.)*
> I can't tell you, and I'll show you why. Real AQI needs PM2.5, PM10, ozone, NO2, SO2 and CO from six separate reference-grade instruments averaged over hours. Mine responds to ammonia, which is one of the eight, but nowhere near across the range the index needs. What I show is a sensor-derived index using CPCB's band names and colours so it's readable — and the page says on it that it isn't the CPCB AQI.

**"Is it safe to be in here, then?"**
> By this sensor, yes. But "safe" here only means nothing it can detect is above threshold — and it's blind to particulates, ozone, oxygen depletion, and carbon monoxide entirely. That caveat is printed on the Safety tab permanently, because a detector that overstates its coverage is worse than no detector.

**"Does it need the computer?"**
> No. *(Pull the USB cable. The LCD and buzzer keep running on the charger.)* The detector is autonomous — LCD, buzzer, alarm latching. The laptop only adds the analysis dashboard.

**"What happens if the sensor fails?"**
> Right now, not enough — that's the honest answer and it's my main known weakness. An open circuit reads as infinite resistance and would show as clean air. A real detector has end-of-life self-test. Adding a heater-current check would catch most failure modes and it's the first thing I'd build next.

**"What did it cost?"**
> Under ₹1,500 for the whole thing. The point isn't the cost though — a certified alarm is about the same and you should own one. This is for understanding how it works.

**"Where do your alarm thresholds come from?"**
> Published exposure limits, not numbers I picked. Ammonia warns at 25 ppm — the NIOSH eight-hour limit — and hits danger at 300, which is the concentration immediately dangerous to life. The code also implements the ten-percent-of-explosive-limit convention for combustible gases, which is what a commercial LPG alarm uses; my sensor just doesn't measure any of those, so that branch never fires here.

**"What would you do next?"**
> The sensor array with a classifier, and a heater self-test. In that order.

---

## What to have on the table

| Item | Why |
|---|---|
| Laptop, dashboard open, connected over USB | The demo |
| **Spare USB data cable** | The single most likely thing to fail |
| 5 V / 2 A phone charger | Powers the detector; also proves it runs without the laptop |
| Hand sanitiser or isopropyl alcohol + cotton buds | Step 2 |
| Your own breath | Step 3 — the punchline, and it costs nothing |
| Printed CO effect-ladder graphic | So people can look at it while you talk |
| Printed one-page abstract | For judges who want to read rather than listen |

**Ventilation:** the sanitiser test is safe indoors, but let the sensor recover between runs — a saturated sensor takes a minute to come back and a flat reading makes a poor demo. No flame is involved anywhere in this sequence.

---

## Board layout

Reading left to right, so someone walking past gets the argument in order:

1. **The question** — "What is this sensor actually allowed to claim?"
2. **How it works** — SnO₂ bead, heater, Rs/R₀, one wiring diagram
3. **The problem** — four gases, one measurement, the cross-sensitivity result from the sanitiser-versus-breath test
4. **The CO effect ladder** — biggest graphic on the board: every concentration at which carbon monoxide harms someone, and this sensor blind to all of it
5. **What we built** — screenshot of the dashboard, the range clamping and resolvability gating
6. **What's next** — the sensor array

Put the effect ladder at eye height. It's the one thing that works without you standing there explaining it.

---

## If something goes wrong

**Dashboard won't connect over USB.** Close the Arduino IDE Serial Monitor — only one program can hold a serial port. Then reload the page and press Connect again.

**Board resets and shows a 3-minute countdown.** It shouldn't — the firmware detects a warm reset and settles in 8 seconds. If you see 180, the board genuinely lost power. Talk through the board while it warms; the wait is a fine time to explain burn-in.

**Readings drift high all day.** Room got warmer or more humid, or too many people breathed near it. Recalibrate outdoors: serial terminal at 115200, send `c`.

**Nothing works at all.** The dashboard runs on simulated data with no hardware attached, and there's a Simulate leak button. Say plainly that you're showing the interface on simulated data, then talk through the physical build. Judges respect that far more than watching you fight a cable.
