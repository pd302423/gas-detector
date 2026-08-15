# Wiring — Arduino Uno build

**Ten wires. No resistors, no transistors, no breadboard, nothing else to buy.**

Everything runs at 5 V, so there is no voltage divider and no level shifter.
The visual alarm is the Uno's own pin-13 LED, which already has a series
resistor on the board.

```
                       ┌──────────────────────────┐
   MQ-135 module         │      ARDUINO UNO         │        PCF8574 backpack
  ┌───────────┐        │                          │        ┌──────────────┐
  │ VCC ──────┼────────┤ 5V                    5V ├────────┼─ VCC         │
  │ GND ──────┼────────┤ GND                  GND ├────────┼─ GND         │
  │ AOUT ─────┼────────┤ A0            A4 (SDA)   ├────────┼─ SDA         │
  │ DOUT ─────┼────────┤ D7            A5 (SCL)   ├────────┼─ SCL         │
  └───────────┘        │                          │        └──────┬───────┘
                       │                          │               │
   Buzzer  + ──────────┤ D8                       │        16×2 HD44780 LCD
           − ──────────┤ GND                      │
                       │  pin 13 LED — on board,  │
                       │  nothing to wire         │
                       │  USB ── laptop (dashboard)
                       └──────────────────────────┘
```

## Connections

| # | From | To | Carries |
|---|---|---|---|
| 1 | MQ-135 `VCC` | Uno `5V` | Heater supply, ~150 mA |
| 2 | MQ-135 `GND` | Uno `GND` | Ground |
| 3 | MQ-135 `AOUT` | Uno `A0` | Analog 0–5 V, straight in |
| 4 | MQ-135 `DOUT` | Uno `D7` | Optional — the module's own comparator |
| 5 | LCD backpack `VCC` | Uno `5V` | Full-brightness backlight |
| 6 | LCD backpack `GND` | Uno `GND` | Ground |
| 7 | LCD backpack `SDA` | Uno `A4` | I²C data — fixed by the hardware |
| 8 | LCD backpack `SCL` | Uno `A5` | I²C clock — fixed by the hardware |
| 9 | Buzzer `+` (long leg) | Uno `D8` | Alarm |
| 10 | Buzzer `−` (short leg) | Uno `GND` | Ground |

The Uno has three `GND` pins and they are the same rail — use whichever is
closest. Same for the two `5V`/`3V3` side pins, except you want `5V`, not
`3V3`.

`A4` and `A5` are the I²C bus on an ATmega328P and cannot be reassigned. Don't
try to use them as spare analog inputs.

## The buzzer

Two terminals, straight to the board. **Longer leg — or the terminal marked
`+` — goes to D8. Shorter leg to GND.**

A small buzzer pulls somewhere around 25–30 mA, which is a little over the
20 mA an ATmega pin is rated to source continuously, though well under the
40 mA absolute maximum. In practice this is what everyone does and it works.
Worth knowing only for two reasons: if you ever swap in a louder buzzer, drive
it through a transistor instead; and if the board ever behaves oddly *only*
while the alarm is sounding, this is the first thing to suspect.

## The alarm LED

Don't wire one. The firmware drives `LED_BUILTIN` — the LED already on the Uno
next to pin 13, which has its own resistor on the board. It flashes with the
buzzer and stays lit while the alarm is latched.

**Never connect a bare LED directly to a pin without a series resistor.**
Without one it draws far more current than the LED or the pin is rated for.
With no resistors to hand, the on-board LED is the right answer anyway.

### Which buzzer do you have?

Briefly touch it across 5 V and GND:

| What happens | Type | Firmware setting |
|---|---|---|
| Continuous steady tone | **Active** — has its own oscillator | `#define BUZZER_PASSIVE 0` |
| One faint click, then nothing | **Passive** — a bare transducer | `#define BUZZER_PASSIVE 1` |

Both settings live at the top of `gas_detector_uno.ino`. A passive buzzer is
driven with `tone()` at `BUZZER_TONE_HZ`, which defaults to 2.7 kHz — near the
resonant peak of most piezo elements, so it is loudest there for the same power.

## Power

| Load | Current |
|---|---|
| MQ-135 heater | ~150 mA |
| LCD + backlight | ~25 mA |
| Buzzer, peak | ~30 mA |
| Uno itself | ~50 mA |
| **Total** | **~255 mA** |

Comfortably inside the Uno's 500 mA USB polyfuse, so **the laptop's USB port
powers the whole thing** — which is convenient, because you need that cable
connected anyway for the dashboard.

For the exhibition, a **5 V / 2 A phone charger** is better than a laptop port.
The MQ-135's heater is specified at 5.0 V ± 0.1 V, and a laptop port under load
can sag toward 4.8 V — 4 % less heater power puts the sensing bead at a
slightly wrong temperature, which shifts the Rs/R₀ characteristic the datasheet
curves describe. If you own a multimeter, measure the 5 V pin with everything
running and write the number down; it is worth showing a judge.

**The USB cable to the laptop is only for the dashboard.** Pull it out, power
the Uno from the charger, and the LCD, buzzer, on-board LED and alarm latching
all keep working. Verify this before you claim it.

## The LCD, decoded

The sixteen pins you soldered — `GND VDD VO RS RW E D0–D7 BLA BLK` — are a
standard **HD44780 16×2 character LCD**. The four-pin module on the back is a
**PCF8574 I²C backpack**; it drives all sixteen over two wires.

That was the right call. Driving the panel directly in 4-bit mode costs six
GPIOs.

Its address is `0x27` (PCF8574T) or `0x3F` (PCF8574AT). The firmware probes
both, so you never need to know which you have — the serial monitor prints the
one it found.

**A blank screen, or two rows of solid blocks, is almost always contrast.**
Turn the blue trimmer on the back of the backpack before suspecting anything
else.

## First power-up

1. Connect everything with the board unpowered.
2. Apply power. The MQ-135 should be noticeably warm within a minute — that's the
   heater, and it means the 5 V rail is reaching it.
3. Serial monitor at **115200**. You should see the LCD address, then a
   `GSW,` countdown, then `GS1,` telemetry once a second.
4. **Leave it powered for 24–48 hours** before trusting any reading. A new MQ
   sensor's baseline drifts for that long.
5. After burn-in, take it outdoors and send `c` over serial to calibrate R₀ in
   clean air. Everything downstream depends on that one measurement being taken
   somewhere genuinely clean.
