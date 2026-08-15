/*
 * Gas Detection System — Arduino Uno + MQ-series sensor + I2C 16x2 LCD
 *
 * The simple, safe build: everything runs at 5V, so the MQ output goes
 * straight into A0 and the LCD backpack straight onto A4/A5. No dividers,
 * no level shifters, nothing to get wrong. No WiFi dashboard either — if you
 * want that, use the ESP8266 sketch.
 *
 * The detector is autonomous: LCD, buzzer and LED all work with no computer
 * attached, powered from a 5V phone charger. The USB link only adds the
 * browser dashboard, which reads the telemetry line below over Web Serial.
 *
 * Wiring — ten wires, no resistors, no transistors, no extra components.
 *   MQ module    VCC -> 5V    GND -> GND    AOUT -> A0    DOUT -> D7 (optional)
 *   LCD backpack VCC -> 5V    GND -> GND    SDA  -> A4    SCL  -> A5
 *   Buzzer       +   -> D8    -   -> GND    (longer leg is +)
 *   Alarm LED    the Uno's own pin-13 LED. Nothing to wire.
 *
 *   The buzzer runs straight off the pin. That is slightly above the 20 mA a
 *   pin is rated to source continuously, so if you ever add a louder buzzer,
 *   drive it through a transistor instead. A small one is fine.
 *   Set BUZZER_PASSIVE below to match the type you have.
 *
 * Serial (115200): 'c' recalibrate R0, 'r' raw diagnostics.
 *   Emits "GS1,..." telemetry once a second — see emitTelemetry().
 *
 * Library: "LiquidCrystal I2C" by Frank de Brabander
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// Which sensor do you have? Read the number on the metal can: MQ-2, MQ-135...
// Valid: 2, 3, 4, 5, 6, 7, 8, 9, 135
#define MQ_SELECT 2
#include "mq_curves.h"

// --- configuration ---------------------------------------------------------
#define PIN_MQ_ANALOG  A0
#define PIN_MQ_DIGITAL 7
#define PIN_BUZZER     8
// The Uno's own on-board LED. It already has a series resistor and a buffer on
// the board, so the alarm gets a visual indicator with nothing to wire and no
// resistor to find.
#define PIN_LED_ALARM  LED_BUILTIN   // pin 13

// Buzzer type — see driveAlarm() for how to tell which one you have.
//   0 = active buzzer  (steady tone from plain DC)
//   1 = passive buzzer (needs tone(), a square wave)
#define BUZZER_PASSIVE  0
#define BUZZER_TONE_HZ  2700   // passive only; ~2.7 kHz is where piezos are loudest

#define RL_OHMS             10000.0f
#define VC_VOLTS            5.0f
#define ADC_FULLSCALE_VOLTS 5.0f

// Telemetry to the browser dashboard over USB. See emitTelemetry().
#define SERIAL_BAUD       115200
#define TELEMETRY_MS      1000

#define LCD_COLS 16
#define LCD_ROWS 2

#define WARMUP_SECONDS      180  // cold boot: the heater really is cold
#define WARMUP_SECONDS_WARM   8  // reset with power maintained: just let the ADC settle
#define CALIB_SAMPLES      64
#define SAMPLE_INTERVAL_MS 500
#define LCD_PAGE_MS        2500
#define EMA_ALPHA          0.20f
#define ALARM_CONFIRM_COUNT 4
#define ALARM_HOLD_MS      10000UL

#define EEPROM_ADDR  0
#define EEPROM_MAGIC 0xC0DEBA5EUL

// --- state -----------------------------------------------------------------
// Lives in .noinit, so the compiler never zeroes it at startup. SRAM keeps its
// contents through a RESET (power is never interrupted) but comes up as noise
// after a real power cycle — which is exactly the distinction we need to tell
// "the browser just opened my serial port" from "someone plugged me in".
#define WARM_MAGIC 0x5AFE1EE7UL
static uint32_t warmFlag __attribute__((section(".noinit")));

LiquidCrystal_I2C *lcd = nullptr;
static float r0 = NAN, emaVolts = NAN, lastRs = 0, lastRatio = 0;
static float ppm[8], ppmRaw[8];
static GasRange gasRange[8];
static GasStatus perGas[8], overall = STATUS_SAFE;
static uint8_t worstGas = 0, confirmCount = 0;
static unsigned long alarmLatchedAt = 0;
static bool alarmOn = false;

// --- sensor maths ----------------------------------------------------------
static float readSensorVolts(uint8_t samples) {
  unsigned long acc = 0;
  for (uint8_t i = 0; i < samples; i++) { acc += analogRead(PIN_MQ_ANALOG); delay(2); }
  return ((float)acc / samples) * (ADC_FULLSCALE_VOLTS / 1023.0f);
}

static float voltsToRs(float vout) {
  if (vout <= 0.01f) return 1e9f;
  if (vout >= VC_VOLTS - 0.01f) return 0.1f;
  return RL_OHMS * (VC_VOLTS - vout) / vout;
}

static float calibrateR0() {
  float acc = 0;
  for (uint8_t i = 0; i < CALIB_SAMPLES; i++) { acc += voltsToRs(readSensorVolts(4)); delay(20); }
  return (acc / CALIB_SAMPLES) / MQ.cleanAirRatio;
}

static void saveR0(float v) {
  unsigned long magic = EEPROM_MAGIC;
  EEPROM.put(EEPROM_ADDR, magic);
  EEPROM.put(EEPROM_ADDR + 4, v);
}

static bool loadR0(float &out) {
  unsigned long magic = 0; float v = NAN;
  EEPROM.get(EEPROM_ADDR, magic);
  EEPROM.get(EEPROM_ADDR + 4, v);
  if (magic != EEPROM_MAGIC || isnan(v) || v <= 0 || v > 1e8) return false;
  out = v; return true;
}

// --- LCD -------------------------------------------------------------------
static uint8_t probeLcdAddress() {
  const uint8_t candidates[] = {0x27, 0x3F, 0x20, 0x38};
  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) return candidates[i];
  }
  return 0;
}

static void lcdLine(uint8_t row, const String &text) {
  if (!lcd) return;
  String t = text;
  while (t.length() < LCD_COLS) t += ' ';
  lcd->setCursor(0, row);
  lcd->print(t.substring(0, LCD_COLS));
}

// --- alarm -----------------------------------------------------------------
// Two kinds of 2-terminal buzzer exist and they need different drive:
//   ACTIVE  — has its own oscillator inside. DC across it makes a steady tone.
//   PASSIVE — a bare transducer. DC makes one click; it needs a square wave.
// Touch yours briefly across 5V and GND: continuous tone = active, single
// click = passive. Set BUZZER_PASSIVE accordingly.
static void driveAlarm(bool on) {
#if BUZZER_PASSIVE
  if (on) tone(PIN_BUZZER, BUZZER_TONE_HZ);
  else    noTone(PIN_BUZZER);
#else
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
#endif
  digitalWrite(PIN_LED_ALARM, on ? HIGH : LOW);
}

static void updateAlarm() {
  if (overall != STATUS_SAFE) { if (confirmCount < 255) confirmCount++; }
  else confirmCount = 0;

  if (confirmCount >= ALARM_CONFIRM_COUNT) { alarmOn = true; alarmLatchedAt = millis(); }
  else if (alarmOn && millis() - alarmLatchedAt > ALARM_HOLD_MS) alarmOn = false;

  bool drive = false;
  if (alarmOn) drive = (overall == STATUS_DANGER) ? true : ((millis() / 500) % 2 == 0);
  driveAlarm(drive);
}

// --- measurement -----------------------------------------------------------
static void measure() {
  float v = readSensorVolts(8);
  emaVolts = isnan(emaVolts) ? v : (EMA_ALPHA * v + (1 - EMA_ALPHA) * emaVolts);
  lastRs = voltsToRs(emaVolts);
  lastRatio = (isnan(r0) || r0 <= 0) ? 0 : lastRs / r0;

  overall = STATUS_SAFE; worstGas = 0;
  float worstSeverity = -1;
  for (uint8_t i = 0; i < MQ.gasCount; i++) {
    const GasCurve &g = MQ.gases[i];
    ppmRaw[i] = mqPpmRaw(g, lastRatio);
    gasRange[i] = mqRange(g, ppmRaw[i]);
    ppm[i] = mqPpm(g, lastRatio);
    perGas[i] = mqStatus(g, ppm[i]);
    if (perGas[i] > overall) overall = perGas[i];
    if (!mqResolvable(g)) continue;   // sensor can't resolve it; never alarm on it
    float sev = ppm[i] / g.dangerPpm;
    if (sev > worstSeverity) { worstSeverity = sev; worstGas = i; }
  }
}

// --- telemetry -------------------------------------------------------------
// One line per second to the browser dashboard, which opens this serial port
// directly through the Web Serial API. Deliberately all-integer: avr-libc's
// printf has no %f, and integers keep the line short and the RAM cost near
// zero on a 2 KB part.
//
//   GS1,<millis>,<mq>,<adc>,<millivolts>,<rs>,<r0>,<ratio*1000>,<dout>,<status>
//
// Only Rs/R0 is sent, not ppm. The browser holds the same curve constants and
// reconstructs every gas from the ratio, so there is one source of truth for
// the maths instead of two that can drift apart.
// r0 == 0 means "not calibrated yet" and the dashboard says so.
static void emitTelemetry() {
  bool haveV = !isnan(emaVolts);
  int  mv    = haveV ? (int)(emaVolts * 1000.0f) : 0;
  int  adc   = haveV ? (int)(emaVolts / ADC_FULLSCALE_VOLTS * 1023.0f) : 0;
  long rs    = (isnan(lastRs) || lastRs > 9999999.0f) ? 9999999L : (long)lastRs;
  long r0i   = (isnan(r0) || r0 <= 0) ? 0L : (long)r0;
  long ratioM = isnan(lastRatio) ? 0L : (long)(lastRatio * 1000.0f);

  char buf[80];
  snprintf(buf, sizeof(buf), "GS1,%lu,%d,%d,%d,%ld,%ld,%ld,%d,%d",
           millis(), MQ_SELECT, adc, mv, rs, r0i, ratioM,
           digitalRead(PIN_MQ_DIGITAL), (int)overall);
  Serial.println(buf);
}

// --- setup / loop ----------------------------------------------------------
void setup() {
  // Was this a genuine power-on, or just a reset? Opening the USB serial port
  // toggles DTR, which resets the board — but never removes power, so the
  // heater has been hot the whole time and does not need warming again.
  // Without this check every browser connection would restart a 3-minute
  // countdown in front of whoever is watching.
  bool coldBoot = (warmFlag != WARM_MAGIC);
  warmFlag = WARM_MAGIC;

  Serial.begin(SERIAL_BAUD);
  Serial.print(F("\n=== Gas Detector | ")); Serial.print(MQ.name); Serial.println(F(" ==="));
  Serial.println(MQ.note);
  Serial.println(coldBoot ? F("cold boot: full heater warm-up")
                          : F("warm reset: heater already hot, short settle"));

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_ALARM, OUTPUT);
  pinMode(PIN_MQ_DIGITAL, INPUT);
  driveAlarm(false);

  Wire.begin();
  uint8_t found = probeLcdAddress();
  if (found) {
    lcd = new LiquidCrystal_I2C(found, LCD_COLS, LCD_ROWS);
    lcd->init(); lcd->backlight();
    Serial.print(F("LCD at 0x")); Serial.println(found, HEX);
  } else {
    Serial.println(F("No I2C device. Check SDA=A4 SCL=A5, 5V, and backpack solder."));
  }

  lcdLine(0, String(MQ.name) + " detector");
  lcdLine(1, "starting...");
  delay(1200);

  for (int s = coldBoot ? WARMUP_SECONDS : WARMUP_SECONDS_WARM; s > 0; s--) {
    lcdLine(0, "Heating sensor");
    lcdLine(1, "Ready in " + String(s) + "s");
    Serial.print(F("GSW,")); Serial.println(s);   // dashboard shows the countdown
    delay(1000);
  }

  if (loadR0(r0)) {
    Serial.print(F("Stored R0 = ")); Serial.println(r0);
    lcdLine(0, "R0 loaded");
    lcdLine(1, String(r0 / 1000.0f, 2) + " kOhm");
  } else {
    lcdLine(0, "Calibrating R0");
    lcdLine(1, "clean air only!");
    r0 = calibrateR0();
    saveR0(r0);
    Serial.print(F("New R0 = ")); Serial.println(r0);
  }
  delay(1500);
}

void loop() {
  static unsigned long lastSample = 0, lastPage = 0, lastTelem = 0;
  static uint8_t page = 0;

  if (millis() - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample = millis();
    measure();
    updateAlarm();
  }

  if (millis() - lastTelem >= TELEMETRY_MS) {
    lastTelem = millis();
    emitTelemetry();
  }

  if (millis() - lastPage >= LCD_PAGE_MS) {
    lastPage = millis();
    if (overall != STATUS_SAFE) {
      const GasCurve &g = MQ.gases[worstGas];
      lcdLine(0, String("!! ") + statusName(overall) + " !!");
      lcdLine(1, String(g.name) + " " + String(ppm[worstGas], 0) + " ppm");
    } else {
      const GasCurve &g = MQ.gases[page];
      lcdLine(0, String(MQ.name) + "  " + g.name);
      String val = (ppm[page] >= 100.0f) ? String(ppm[page], 0) : String(ppm[page], 1);
      lcdLine(1, val + " ppm  SAFE");
      page = (page + 1) % MQ.gasCount;
    }
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'c') {
      lcdLine(0, "Recalibrating"); lcdLine(1, "clean air only!");
      r0 = calibrateR0(); saveR0(r0);
      Serial.print(F("R0 = ")); Serial.println(r0);
    } else if (c == 'r') {
      Serial.print(F("V=")); Serial.print(emaVolts, 4);
      Serial.print(F(" Rs=")); Serial.print(lastRs, 0);
      Serial.print(F(" R0=")); Serial.print(r0, 0);
      Serial.print(F(" ratio=")); Serial.println(lastRatio, 4);
      for (uint8_t i = 0; i < MQ.gasCount; i++) {
        Serial.print(F("  ")); Serial.print(MQ.gases[i].name);
        Serial.print(F("\t")); Serial.print(ppm[i], 2);
        Serial.print(F(" ppm\traw ")); Serial.print(ppmRaw[i], 1);
        Serial.print(F("\t")); Serial.print(gasRange[i] == RANGE_BELOW ? F("below")
                                          : gasRange[i] == RANGE_ABOVE ? F("above") : F("in"));
        Serial.print(F("\t")); Serial.print(statusName(perGas[i]));
        if (!mqResolvable(MQ.gases[i])) Serial.print(F("\t(below this sensor's floor)"));
        Serial.println();
      }
    }
  }
}
