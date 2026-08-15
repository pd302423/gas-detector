# Air Sentinel

A gas detector that reports its own blind spots — Arduino Uno, MQ-135 sensor, 16×2 I²C LCD, and a
browser dashboard that reads the board directly over USB.

**Live dashboard:** https://gas-amber-eta.vercel.app

---

## Run it

**Already flashed and calibrated?** Four steps to live readings:

1. Plug the Uno into the computer with a **data** cable — charge-only cables have no data lines and the port never appears.
2. **Close the Arduino IDE's Serial Monitor.** One program per serial port; this is the most common reason step 4 fails.
3. Open the dashboard in **Chrome or Edge on a desktop** — [the live one](https://gas-amber-eta.vercel.app), or your own copy on `localhost`. It must be https or localhost; a file opened from disk cannot touch a USB port.
4. Click **Connect detector**, choose the port. The header pill turns to `USB · live`.

**Starting from a box of parts?** In order:

```bash
# 1. Wire it — ten wires, no resistors.  See docs/WIRING.md
# 2. Confirm the sensor is alive before anything else
#    Upload sensor_check/sensor_check.ino, Serial Monitor at 115200.
#    It must get warm, read a plausible number, and MOVE when you wave
#    hand sanitiser at it. The third one is the test that matters.
# 3. Arduino IDE → Manage Libraries → "LiquidCrystal I2C" by Frank de Brabander
# 4. Upload gas_detector_uno/gas_detector_uno.ino
# 5. Leave it powered 24–48 h to burn in
# 6. Outdoors, Serial Monitor at 115200, send  c   to calibrate R0
```

Serve the dashboard locally, or deploy your own:

```bash
cd dashboard && npx serve
```

```bash
npm i -g vercel && cd dashboard && vercel --prod
```

Full detail: **[docs/SETUP.md](docs/SETUP.md)** — including email and SMS leak alerts. The same
instructions are on the **Setup** tab of the dashboard itself.

---

## Read this first: what an MQ sensor can and cannot do

**It cannot tell you which gas is in the air.** An MQ sensor is a heated tin-dioxide (SnO₂) bead whose electrical resistance falls when *any* reducing gas adsorbs onto its surface. The output is **one resistance value**. Not a spectrum, not a fingerprint — one number.

That means:

- LPG at 500 ppm, ethanol vapour at 200 ppm, and cigarette smoke can all produce the identical reading.
- The per-gas ppm figures this project prints are all computed from that *same single measurement*. Each row says "**if** the gas were LPG, it would be this much." At most one row is physically true, and the sensor doesn't know which one.
- Humidity and temperature shift the baseline. So does the sensor ageing. Recalibrate periodically.

What you **can** honestly build, and what this repo builds: a **reducing-gas alarm** with an estimated concentration and a hazard classification. That is exactly what a ₹1,200 kitchen gas alarm does, and it is genuinely useful.

### If you actually need gas identification

You need to break the single-measurement bottleneck. In order of cost/effort:

| Approach | How it works | Effort |
|---|---|---|
| **Sensor array + classifier ("e-nose")** | 4–6 different MQ types (2, 3, 4, 7, 8, 135) read simultaneously. Each has a different sensitivity ratio to each gas, so the *pattern* across the array is gas-specific. Train a k-NN or small neural net on labelled samples. | Moderate — this is a strong science-fair/ISEF-grade project |
| **Heater cycling on one sensor** | Drive the heater between two temperatures (MQ-7: 5V/60s then 1.4V/90s). SnO₂ selectivity changes with temperature, so one sensor gives you two semi-independent readings. | Low hardware, needs PWM + timing discipline |
| **NDIR sensor** | Infrared absorption at a gas-specific wavelength. Genuinely selective. MH-Z19 for CO₂ (~₹1,500). | Easy but one gas per sensor |
| **Electrochemical cell** | e.g. MiCS/Winsen ZE07-CO. Selective and calibrated in real ppm. | Easy, ~₹1,500–3,000 per gas |
| **PID / GC / spectrometer** | Real analytical chemistry. | Lab equipment |

If you want the array approach, say so — the code here is already structured to extend to it (the model table in `mq_curves.h` becomes an array of models, and the classifier reads the ratio vector).

---

## What each MQ sensor detects

Every MQ is broadband; the "target" column is what it's *most* sensitive to, not what it exclusively responds to.

| Sensor | Primary target | Also responds to | Typical range | Clean-air Rs/R0 |
|---|---|---|---|---|
| **MQ-2** | LPG, propane, butane | Methane, hydrogen, alcohol, smoke, CO | 200–10,000 ppm | 9.83 |
| **MQ-3** | **Alcohol / ethanol** | Benzene, hexane, LPG (weakly) | 0.05–10 mg/L | 60 |
| **MQ-4** | **Methane / CNG / PNG** | LPG, smoke, alcohol (weakly) | 300–10,000 ppm | 4.4 |
| **MQ-5** | LPG + natural gas | Alcohol, smoke, hydrogen | 200–10,000 ppm | 6.5 |
| **MQ-6** | **LPG, butane, propane** | Natural gas, alcohol | 200–10,000 ppm | 10 |
| **MQ-7** | **Carbon monoxide** | Hydrogen, methane | 20–2,000 ppm | 27 |
| **MQ-8** | **Hydrogen** | LPG, CO, alcohol (all weak) | 100–10,000 ppm | 70 |
| **MQ-9** | CO + combustibles | Methane, LPG | 10–1,000 (CO), 100–10,000 (comb.) | 9.6 |
| **MQ-135** | **Air quality**: NH₃, NOₓ, benzene, CO₂, smoke | Alcohol, sulphides — extremely broad | 10–1,000 ppm | 3.6 |
| MQ-131 | Ozone, NOₓ, Cl₂ | — | 10–1,000 ppb | 15 |
| MQ-136 | Hydrogen sulphide (H₂S) | — | 1–200 ppm | 3.6 |
| MQ-137 | Ammonia (NH₃) | — | 5–500 ppm | 3.6 |
| MQ-138 | VOCs: benzene, toluene, acetone, formaldehyde | — | 5–500 ppm | 3.6 |

**Which one do you have?** Read the number stamped on the metal can under the steel mesh, or on the PCB silkscreen. "Flying Fish" is the board vendor, not the sensor — they sell modules for the whole MQ range. Set `MQ_SELECT` in `config.h` (ESP8266) or at the top of the `.ino` (Uno) to match.

If you're picking one to buy for a household hazard detector in India: **MQ-6** for LPG cylinders, **MQ-4** for piped natural gas, **MQ-7** for CO from a geyser or generator, **MQ-2** if you want one sensor that catches most combustibles plus smoke.

### Hazard thresholds used in the code

Combustibles alarm at **10% of the Lower Explosive Limit** — the standard industrial trip point, giving you margin before the atmosphere can ignite.

| Gas | LEL | DANGER at (10% LEL) | Why |
|---|---|---|---|
| LPG | 1.8% (18,000 ppm) | 1,800 ppm | Explosion |
| Methane | 5.0% (50,000 ppm) | 5,000 ppm | Explosion |
| Hydrogen | 4.0% (40,000 ppm) | 4,000 ppm | Explosion |

Toxics alarm on health limits instead:

| Gas | WARNING | DANGER | Why |
|---|---|---|---|
| CO | 35 ppm | 200 ppm | 35 = 8h exposure limit; 200 = headache in 2–3h; 800 = lethal in 2h |
| NH₃ | 25 ppm | 300 ppm | 300 = immediately dangerous to life and health |
| Benzene | 1 ppm | 50 ppm | Carcinogen, no safe level |
| CO₂ | 1,000 ppm | 5,000 ppm | 1,000 = stuffy/drowsy; 5,000 = 8h limit |

---

### Curve fits are only valid inside a band

The `ppm = a·(Rs/R₀)^b` fit comes from a curve the datasheet only draws across a limited range. Outside it, the fit is fiction — an MQ-2 at Rs/R₀ = 0.6 will happily report **180,000 ppm of CO** if you let it, a number with no physical meaning that would then dominate the alarm.

So every gas carries a `minPpm`/`maxPpm` band, and:

- Readings are **clamped** to the band and flagged `below` / `in` / `above`.
- A gas whose **warning threshold sits below the sensor's own floor** is excluded from alarm logic entirely and labelled *not resolvable*.

For the MQ-135 in this build, those two rules produce three results worth stating plainly:

- **Benzene is unresolvable.** It matters at 1 ppm and the sensor's floor is 10 ppm, so the row is displayed for context and can never raise an alarm. A sensor that cannot see a carcinogen until it is ten times over the limit should not claim to detect it.
- **CO₂ saturates before it can alarm.** Its danger threshold is 5,000 ppm; the sensor's ceiling is 1,000 ppm. It will pin at WARNING and never reach DANGER, and the Sensor tab labels the threshold "above ceiling" rather than pretending otherwise.
- **Carbon monoxide is not measured at all.** The MQ-135 has no CO response curve — not a poor one, none. That is the most important gap in this build, because CO is odourless and is what a faulty geyser or generator produces. **Add an MQ-7** if you want CO; its 20 ppm floor sits below the 35 ppm exposure limit.

---

## Your parts, decoded

**The LCD.** Pins `GND VDD VO RS RW E D0 D1 D2 D3 D4 D5 D6 D7 BLA BLK` are a standard 16-pin **HD44780 16×2 character LCD**. `VO` is contrast, `RS`/`RW`/`E` are control, `D0–D7` are the parallel data bus, `BLA`/`BLK` are backlight anode/cathode.

**The module you soldered to it** (`GND VCC SDA SCL`) is a **PCF8574 I2C backpack**. It converts those 16 parallel pins into a 2-wire I2C bus, which is why you only have 4 pins now. Good — driving the LCD in 4-bit parallel mode would eat 6 GPIOs you don't have to spare on an ESP-12E. The blue trimpot on the backpack sets contrast; if the screen shows two rows of blocks or nothing at all, turn it.

Its I2C address is `0x27` (PCF8574T chip) or `0x3F` (PCF8574AT). The code probes both, so you don't need to know.

---

## The build: Arduino Uno

Everything is 5 V end to end. No voltage divider, no level shifter, no supply
sitting out of spec — three hardware hazards that simply don't exist here.

Full detail in **[docs/WIRING.md](docs/WIRING.md)**. The short version:

Ten wires, no resistors, no transistors, nothing else to buy.

```
MQ-135 module  VCC  -> Uno 5V        LCD backpack VCC -> Uno 5V
             GND  -> Uno GND                    GND -> Uno GND
             AOUT -> Uno A0                     SDA -> Uno A4
             DOUT -> Uno D7  (optional)         SCL -> Uno A5

Buzzer  + (long leg)  -> Uno D8
        - (short leg) -> Uno GND

Alarm LED: the Uno's own pin-13 LED. Nothing to wire — it already has a resistor.
```

Open `gas_detector_uno/gas_detector_uno.ino`, set `MQ_SELECT` and
`BUZZER_PASSIVE`, upload.

**The detector is autonomous.** LCD, buzzer, LED and alarm latching all run
from a 5 V charger with no computer attached. The USB link to a laptop only
adds the dashboard.

### Why not the ESP8266?

The ESP8266 build is still here and still works, but it is **v2, not the main
build**. It buys one thing — operation with no computer at all — and costs a
voltage divider on `A0` (5 V into a 3.3 V pin destroys the ADC), I²C level
shifting, a VIN rail that sits near 4.7 V when the MQ-135's heater wants
5.0 V ± 0.1 V, WiFi credentials, and a dependency on whatever network is
available. Five failure modes for one benefit that the Uno already provides.

### Appendix: ESP8266 ESP-12E / NodeMCU (v2)

Two hazards to handle, both real:

**Hazard 1 — the ADC.** The MQ heater needs 5V, so its AOUT swings 0–5V. The ESP8266's A0 tolerates ~3.3V max (NodeMCU boards have an on-board 100k/220k divider; the bare ESP-12E chip pin is 1.0V max). Feeding 5V into it will damage the chip. Add an external divider:

```
MQ AOUT ---[ 68k ]---+--- NodeMCU A0
                     |
                   [ 100k ]
                     |
                    GND
```

That scales 5.0V down to 2.98V. `ADC_FULLSCALE_VOLTS` in `config.h` is already set to `5.54` to account for it. If you use different resistors, recompute: `3.3 / (R2 / (R1 + R2))`.

**Hazard 2 — 5V I2C.** The PCF8574 backpack wants 5V for a bright display, and its pull-up resistors then pull SDA/SCL to 5V — into 3.3V ESP pins. In practice thousands of projects do this and survive; formally it's out of spec. Three options, best first:

1. **Bi-directional level shifter** (BSS138 module, ~₹60) between the ESP and the backpack. Correct and cheap.
2. **Power the backpack from 3.3V.** Works on most PCF8574 boards, display is noticeably dimmer. Zero extra parts.
3. **Power at 5V and accept the risk.** Common, usually fine, not something to build a permanent safety device around.

Full wiring:

```
NodeMCU Vin (5V from USB) -> MQ VCC, LCD backpack VCC
NodeMCU GND               -> MQ GND, LCD backpack GND
MQ AOUT -> 68k/100k divider -> A0
MQ DOUT -> D6     (optional)
LCD SDA -> D2 (GPIO4)
LCD SCL -> D1 (GPIO5)
Buzzer  -> D5
LED     -> D7 via 220R
```

Open `gas_detector_esp8266/gas_detector_esp8266.ino`, set `MQ_SELECT` and your WiFi credentials in `config.h`, upload. The LCD and serial monitor both print the dashboard IP.

Leave `WIFI_SSID` as `""` to run fully offline.

---

## Setup

1. **Arduino IDE** → Library Manager → install **"LiquidCrystal I2C" by Frank de Brabander**. Everything else (`Wire`, `EEPROM`, `ESP8266WiFi`, `ESP8266WebServer`, `ESP8266mDNS`) ships with the board core.
2. For ESP8266: Preferences → Additional Board URLs → `https://arduino.esp8266.com/stable/package_esp8266com_index.json`, then Boards Manager → install `esp8266`. Select **NodeMCU 1.0 (ESP-12E Module)**.
3. Wire per your chosen build.
4. Set `MQ_SELECT`.
5. Upload.

> **Cloned this repo?** The Uno build needs nothing extra. The ESP8266 build needs a `config.h`,
> which is gitignored because it holds a WiFi password — copy the template and edit the copy:
>
> ```bash
> cp gas_detector_esp8266/config.example.h gas_detector_esp8266/config.h
> ```

### Burn-in and calibration — do not skip this

A brand-new MQ sensor **needs 24–48 hours of continuous power** before its baseline stops drifting. Readings before that are meaningless. Leave it plugged in overnight.

After burn-in, in **clean outdoor air** (not your kitchen, not a room where anyone has been cooking, spraying deodorant, or using sanitiser):

- First boot auto-calibrates and stores R₀ in EEPROM.
- To redo it: open Serial Monitor at **115200**, send `c`. Close the monitor again before connecting the dashboard — only one program can hold the port.

R₀ is the anchor for every ppm number the system produces. Calibrate it in contaminated air and every reading afterwards is wrong.

Each boot also runs a 180-second heater warm-up before it will report anything.

---

## Using it

**LCD, normal state** — cycles through each gas, one every 2.5s:
```
MQ-135  LPG
12.4 ppm  SAFE
```

**LCD, alarm state** — pins to the offending gas, buzzer chirps (WARNING) or holds solid (DANGER):
```
!! DANGER !!
LPG 2140 ppm
```

**Serial `r`** dumps raw voltage, Rs, R₀, ratio, the clamped and unclamped ppm, and the range flag for every gas. This is your debugging tool.

---

## The dashboard

Seven tabs:

| Tab | What's on it |
|---|---|
| **Live** | Severity gauge, the gas driving it, per-gas rows with meters marked at both thresholds, the full signal chain (ADC counts → volts → Rs → R₀ → ratio → % of LEL), and a five-minute sparkline |
| **Safety** | Occupancy verdict, sensor-derived gas index against the CPCB AQI scale, the CO effect ladder drawn against this sensor's own blind spot, and every exposure limit in use |
| **History** | Per-gas chart with threshold lines, linear or log scale, session statistics, and an event log of every state change |
| **Sensor** | Live curve constants with their valid bands, the whole MQ family reference table, and the reasoning behind every alarm threshold |
| **Wiring** | Interactive wiring map — hover or tap any wire to isolate it and read what it carries. Uno by default, ESP8266 behind the toggle |
| **System** | Uptime, calibration age and drift, every firmware constant, CSV export |
| **Limits** | The honest account of what one MQ sensor can and cannot tell you |
| **Roadmap** | AQI coverage by build stage, sensors worth adding with part numbers, metrics available from the existing data, reliability work, and the sensor-array upgrade in full |

### Connecting the detector — over USB

The dashboard reads the Arduino directly over its USB cable using the
**Web Serial API**. No server, no network, no WiFi, nothing in between.

1. Upload the firmware and leave the Uno plugged into the laptop.
2. Open the dashboard — the deployed URL, or `dashboard/index.html` from
   `localhost`.
3. Click **Connect detector**, pick the Arduino's port.

That's it. The pill in the header switches to `USB · live`.

**Requirements, and what happens when they aren't met:**

| Needs | Why | If not |
|---|---|---|
| Chrome, Edge or Opera, on desktop | Web Serial isn't in Firefox, Safari, or any mobile browser | Button is hidden, with an explanation, and the page runs on simulated data |
| HTTPS or `localhost` | Web Serial requires a secure context | Same |
| Nothing else holding the port | Only one program can open a serial port | Connect fails — **close the Arduino IDE Serial Monitor** |

**Opening the port resets the board** — that's DTR, and it's unavoidable. The
firmware detects it: a reset with power maintained gets an **8-second** settle
instead of the full 180-second heater warm-up, because the heater never went
cold. A genuine power-on still gets the full warm-up.

### The telemetry line

One line a second, all integers:

```
GS1,<millis>,<mq>,<adc>,<millivolts>,<rs>,<r0>,<ratio×1000>,<dout>,<status>
```

Only Rs/R₀ is transmitted, never ppm. The browser holds the same curve
constants and reconstructs every gas from the ratio, so the maths has one home
instead of two that can drift apart. The device also sends its own status, and
the page recomputes it independently — if they disagree, firmware and page have
diverged and the page says so.

During warm-up the board sends `GSW,<seconds>` and the dashboard shows the
countdown.

### Editing it

The dashboard source is [dashboard/index.html](dashboard/index.html). **Open it directly in a browser** — with no detector connected it falls back to a simulated sensor, so you can iterate on the interface with no hardware attached. There's a *Simulate leak* button in that mode to drive it through warning and danger.

This is also the deployed page. It's a single static file with no dependencies, no build step and no framework — the charts and the wiring diagram are hand-generated SVG. That's a constraint, not a preference: the same file also has to survive being embedded in a C++ raw string literal for the ESP8266 build.

To bake it into the ESP8266 firmware (v2 only):

```bash
powershell -ExecutionPolicy Bypass -File tools/build_web_page.ps1
```

That regenerates `gas_detector_esp8266/web_page.h` as a PROGMEM string (~70 KB of flash, not RAM) and syncs `mq_curves.h` into both sketch folders. Never edit `web_page.h` by hand.

### Connecting over WiFi

1. **Put your network in `config.h`:**

   ```c
   #define WIFI_SSID     "YourNetworkName"
   #define WIFI_PASSWORD "YourPassword"
   ```

   The ESP8266 is **2.4 GHz only**. If your router gives 2.4 and 5 GHz separate names, use the 2.4 GHz one. A phone hotspot works if you set it to 2.4 GHz.

2. **Upload,** then open the Serial Monitor at **115200 baud**. After the 180-second warm-up it prints:

   ```
   WiFi connected.
     Dashboard:  http://192.168.1.37
     or by name: http://gas.local
     signal:     -52 dBm
   ```

   The LCD shows the same thing — hostname on the top row, IP on the bottom.

3. **Open either address** in any browser on the same network. Phone, laptop, tablet — the page is responsive and works on all of them.

**Use `http://gas.local`** where you can. It survives your router handing out a different IP after a reboot. Works on Windows 10/11, macOS, iOS and most Linux; some Android versions don't resolve `.local`, so use the IP there. Change the name with `MDNS_HOSTNAME` in `config.h`.

**If you want a fixed IP instead**, set `USE_STATIC_IP` to `1` in `config.h` and pick an address outside your router's DHCP pool.

**Same network, both devices.** The detector serves the page from your LAN — it is not on the internet, and it is not reachable from outside your house unless you deliberately forward a port (don't; there's no authentication on it).

**If WiFi fails,** the serial monitor prints the status code and the detector keeps running as a standalone alarm — LCD, buzzer and LED all work without a network. The usual causes are a wrong password, a 5 GHz-only network, or a captive portal that wants a login page.

### HTTP API

| Endpoint | Method | Returns |
|---|---|---|
| `/` | GET | The dashboard |
| `/api` | GET | Everything: status, volts, Rs, R₀, ratio, RL, Vc, DOUT, uptime, heap, RSSI, calibration age, and per-gas `{a, b, warn, danger, min, max, ppm, raw, range, resolvable, status}` |
| `/api/history` | GET | Rolling buffer of Rs/R₀ — 240 points at 5 s, about 20 minutes. The browser reconstructs ppm from the ratio, so the chart is populated the moment the page opens |
| `/api/calibrate` | POST | Re-derives R₀ and writes it to flash |
| `/api/mute` | POST | Toggles the buzzer. Silencing clears automatically once the air is clear, so a muted detector can't stay muted through the next leak |
| `/api/test` | POST | Sounds the buzzer for 600 ms |

### Testing it without a gas leak

- **Butane lighter, valve open, no flame**, held ~10 cm away for 2 seconds. Reliable for MQ-135/4/5/6. Do this outdoors or by an open window.
- **Isopropyl alcohol / hand sanitiser** on a cotton bud, waved near the mesh. Works on MQ-3 and will also light up MQ-135 and MQ-135 — a good demonstration of exactly why cross-sensitivity means you can't identify the gas.
- **Do not** test a CO sensor with a real CO source indoors.

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| LCD blank or shows two rows of blocks | Contrast — turn the blue trimpot on the backpack. If still blank, check the 5V and the address (serial prints what it found) |
| "No I2C device found" | SDA/SCL swapped, backpack not powered, or a cold solder joint on the 16-pin header |
| ppm reads 0 constantly | Sensor not powered (it should be warm to the touch within a minute), or AOUT not connected |
| ppm pinned at maximum | R₀ calibrated in dirty air — redo it outdoors. Or `RL_OHMS` doesn't match your board; measure it with the power off, between AOUT and GND |
| Readings drift for hours | Normal on a new sensor. Burn it in for 24–48h |
| Readings jump with the weather | Real. SnO₂ is humidity-sensitive. Add a DHT22 and compensate, or recalibrate seasonally |
| ESP8266 reboots when the buzzer fires | Buzzer drawing more than the GPIO can supply — drive it through an NPN transistor (2N2222 + 1k base resistor) |

---

## Do not rely on this for life safety

This is a learning and demonstration project. It is not a certified gas detector. It has no self-test, no sensor-failure detection, no battery backup, and its calibration is a curve fit to a datasheet graph. For actual protection of a home with an LPG cylinder, buy a BIS/CE-certified alarm. Build this one to understand how they work — and keep the certified one on the wall.

---

## Files

| File | Purpose |
|---|---|
| `mq_curves.h` | Response curves, valid bands, and hazard thresholds for MQ-135/3/4/5/6/7/8/9/135. Edit here to add a sensor or change a threshold |
| `gas_detector_esp8266/config.h` | Pins, WiFi, timing, alarm behaviour |
| `gas_detector_esp8266/gas_detector_esp8266.ino` | ESP8266 build with LCD, alarm, and web dashboard |
| `gas_detector_esp8266/web_page.h` | **Generated.** The dashboard as a PROGMEM string |
| `gas_detector_uno/gas_detector_uno.ino` | Uno build with LCD and alarm |
| `dashboard/index.html` | Dashboard source, and the deployed page. Runs standalone against simulated data |
| `tools/build_web_page.ps1` | Bakes the dashboard into `web_page.h` and syncs `mq_curves.h` |
| `dashboard/api/alert.js` | The only server-side code — sends leak alerts. Reads its keys from environment variables, never from the repo |
| `docs/SETUP.md` | Full setup, from parts list to deployed site with alerts |
| `docs/ABSTRACT.md` | One-page project abstract |
| `docs/PRESENTATION.md` | Booth guide: the pitch, the demo sequence, judge Q&A |
| `docs/WIRING.md` | Printable wiring reference for the Uno build |

`mq_curves.h` is duplicated into each sketch folder because the Arduino IDE only compiles headers that sit beside the `.ino`. The copy at the repo root is the **master**; `tools/build_web_page.ps1` syncs it into both sketch folders every time it runs, so edit the root copy and run the script.
