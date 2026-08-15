/*
 * Gas Detection System — ESP8266 (ESP-12E / NodeMCU) + MQ-series sensor + I2C 16x2 LCD
 *
 * What it does
 *   - Reads the MQ sensor's analog output, converts to sensing resistance Rs
 *   - Derives R0 by calibrating in clean air, stores it in flash (EEPROM emulation)
 *   - Converts Rs/R0 to ppm for every gas the sensor is characterised for
 *   - Shows the readings on a 16x2 LCD, one gas per page
 *   - Latches a buzzer/LED alarm on WARNING or DANGER, with debounce and hold
 *   - Serves a live web dashboard + /api JSON over WiFi
 *
 * HONEST LIMITATION — read this before you trust a number.
 *   A single MQ sensor cannot identify which gas it is smelling. It is one
 *   resistor that drops in the presence of any reducing gas. The per-gas ppm
 *   figures below are "IF the gas were LPG, it would be this much" — they are
 *   all computed from the same single measurement. Only one of them can be
 *   true at a time, and the sensor does not know which. Use the highest-risk
 *   reading as your alarm trigger and treat the rest as context.
 *
 * Serial commands (115200 baud):
 *   c  recalibrate R0 in clean air     r  print raw diagnostics
 *
 * Libraries needed (Library Manager):
 *   "LiquidCrystal I2C" by Frank de Brabander
 * Board manager URL:
 *   https://arduino.esp8266.com/stable/package_esp8266com_index.json
 */

#include "config.h"
#include "mq_curves.h"

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
LiquidCrystal_I2C *lcd = nullptr;
ESP8266WebServer server(80);

static uint8_t  lcdAddr    = LCD_ADDR;
static float    r0         = NAN;   // clean-air sensing resistance, ohms
static float    emaVolts   = NAN;   // smoothed sensor output voltage
static float    lastRs     = 0.0f;
static float    lastRatio  = 0.0f;
static float    ppm[8];             // clamped to the datasheet band
static float    ppmRaw[8];          // unclamped curve output, for diagnostics
static GasRange gasRange[8];
static GasStatus perGas[8];
static GasStatus overall  = STATUS_SAFE;
static uint8_t  worstGas  = 0;
static uint8_t  confirmCount = 0;
static uint32_t alarmLatchedAt = 0;
static bool     alarmOn   = false;
static bool     muted     = false;   // user silenced the buzzer from the dashboard
static bool     wifiUp    = false;
static uint32_t bootMillis = 0;
static uint32_t sampleCount = 0;
static uint32_t calibratedAt = 0;    // millis() when R0 was last derived
static uint32_t buzzTestUntil = 0;

// Rolling history so the dashboard has a chart the moment it loads, instead of
// starting empty. Only Rs/R0 is stored — the browser already has the curve
// constants and can reconstruct ppm for any gas from the ratio.
#define HIST_LEN      240
#define HIST_PERIOD_MS 5000UL
static float    histRatio[HIST_LEN];
static uint16_t histCount = 0;       // total ever written
static uint8_t  histHead  = 0;       // next slot to write
static uint32_t lastHist  = 0;

#define EEPROM_SIZE  32
#define EEPROM_MAGIC 0xC0DEBA5EUL

// ---------------------------------------------------------------------------
// Sensor maths
// ---------------------------------------------------------------------------

// Average N raw ADC samples, return volts at the sensor's AOUT pin.
static float readSensorVolts(uint8_t samples) {
  uint32_t acc = 0;
  for (uint8_t i = 0; i < samples; i++) {
    acc += analogRead(PIN_MQ_ANALOG);
    delay(2);
  }
  float counts = (float)acc / (float)samples;
  return counts * (ADC_FULLSCALE_VOLTS / 1023.0f);
}

// The MQ module is a voltage divider: Vc -- Rs -- AOUT -- RL -- GND
//   Vout = Vc * RL / (Rs + RL)   =>   Rs = RL * (Vc - Vout) / Vout
static float voltsToRs(float vout) {
  if (vout <= 0.01f) return 1e9f;              // open circuit / not powered
  if (vout >= VC_VOLTS - 0.01f) return 0.1f;   // saturated, huge gas or short
  return RL_OHMS * (VC_VOLTS - vout) / vout;
}

// R0 = Rs measured in clean air, divided by the datasheet's clean-air ratio.
static float calibrateR0() {
  float acc = 0.0f;
  for (uint8_t i = 0; i < CALIB_SAMPLES; i++) {
    acc += voltsToRs(readSensorVolts(4));
    delay(20);
  }
  return (acc / CALIB_SAMPLES) / MQ.cleanAirRatio;
}

static void saveR0(float v) {
  EEPROM.begin(EEPROM_SIZE);
  uint32_t magic = EEPROM_MAGIC;
  EEPROM.put(0, magic);
  EEPROM.put(4, v);
  EEPROM.commit();
  EEPROM.end();
}

static bool loadR0(float &out) {
  EEPROM.begin(EEPROM_SIZE);
  uint32_t magic = 0;
  float v = NAN;
  EEPROM.get(0, magic);
  EEPROM.get(4, v);
  EEPROM.end();
  if (magic != EEPROM_MAGIC || isnan(v) || v <= 0.0f || v > 1e8f) return false;
  out = v;
  return true;
}

// ---------------------------------------------------------------------------
// LCD
// ---------------------------------------------------------------------------

static uint8_t probeLcdAddress() {
  const uint8_t candidates[] = {0x27, 0x3F, 0x20, 0x38};
  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) return candidates[i];
  }
  return 0;  // nothing answered
}

static void lcdLine(uint8_t row, const String &text) {
  if (!lcd) return;
  String t = text;
  while (t.length() < LCD_COLS) t += ' ';
  lcd->setCursor(0, row);
  lcd->print(t.substring(0, LCD_COLS));
}

// ---------------------------------------------------------------------------
// Alarm
// ---------------------------------------------------------------------------

static void driveAlarm(bool on) {
  digitalWrite(PIN_BUZZER, (on == BUZZER_ACTIVE_HIGH) ? HIGH : LOW);
  digitalWrite(PIN_LED_ALARM, on ? HIGH : LOW);
}

static void updateAlarm() {
  if (overall != STATUS_SAFE) {
    if (confirmCount < 255) confirmCount++;
  } else {
    confirmCount = 0;
  }

  if (confirmCount >= ALARM_CONFIRM_COUNT) {
    alarmOn = true;
    alarmLatchedAt = millis();
  } else if (alarmOn && (millis() - alarmLatchedAt > ALARM_HOLD_MS)) {
    alarmOn = false;
  }

  // Silencing is per-episode: it clears the moment the air is clean again, so
  // a muted detector cannot stay muted through the next leak.
  if (overall == STATUS_SAFE) muted = false;

  // DANGER = solid tone, WARNING = 1 Hz chirp, SAFE = silent
  bool drive = false;
  if (alarmOn && !muted) {
    drive = (overall == STATUS_DANGER) ? true : ((millis() / 500) % 2 == 0);
  }
  if (millis() < buzzTestUntil) drive = true;   // dashboard buzzer test
  driveAlarm(drive);

  // The LED keeps showing the true state even when the buzzer is silenced.
  digitalWrite(PIN_LED_ALARM, alarmOn ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
// Measurement cycle
// ---------------------------------------------------------------------------

static void measure() {
  sampleCount++;
  float v = readSensorVolts(8);
  emaVolts = isnan(emaVolts) ? v : (EMA_ALPHA * v + (1.0f - EMA_ALPHA) * emaVolts);

  lastRs    = voltsToRs(emaVolts);
  lastRatio = (isnan(r0) || r0 <= 0.0f) ? 0.0f : (lastRs / r0);

  overall  = STATUS_SAFE;
  worstGas = 0;
  float worstSeverity = -1.0f;

  for (uint8_t i = 0; i < MQ.gasCount; i++) {
    const GasCurve &g = MQ.gases[i];
    ppmRaw[i]   = mqPpmRaw(g, lastRatio);
    gasRange[i] = mqRange(g, ppmRaw[i]);
    ppm[i]      = mqPpm(g, lastRatio);
    perGas[i]   = mqStatus(g, ppm[i]);
    if (perGas[i] > overall) overall = perGas[i];

    // "Worst" = furthest past its own danger threshold, so a 3x-over CO
    // reading outranks a barely-over smoke reading. Gases this sensor cannot
    // resolve are excluded — they would otherwise pin the display forever.
    if (!mqResolvable(g)) continue;
    float sev = ppm[i] / g.dangerPpm;
    if (sev > worstSeverity) { worstSeverity = sev; worstGas = i; }
  }
}

// ---------------------------------------------------------------------------
// Web
// ---------------------------------------------------------------------------

#include "web_page.h"   // dashboard markup, generated from dashboard/index.html

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache");
  // charset matters: the page uses ° ² ₀ Ω and em dashes.
  server.send_P(200, "text/html; charset=utf-8", PAGE_HTML);
}

static const char *rangeName(GasRange r) {
  return r == RANGE_BELOW ? "below" : (r == RANGE_ABOVE ? "above" : "in");
}

// Everything the dashboard displays comes from this one endpoint.
static void handleApi() {
  String j;
  j.reserve(1600);
  j  = "{";
  j += "\"model\":\"" + String(MQ.name) + "\",";
  j += "\"mq\":" + String(MQ_SELECT) + ",";
  j += "\"note\":\"" + String(MQ.note) + "\",";
  j += "\"status\":\"" + String(statusName(overall)) + "\",";
  j += "\"worst\":" + String(worstGas) + ",";
  j += "\"adc\":" + String((int)(emaVolts / ADC_FULLSCALE_VOLTS * 1023.0f)) + ",";
  j += "\"volts\":" + String(emaVolts, 4) + ",";
  j += "\"rs\":" + String(lastRs, 1) + ",";
  j += "\"r0\":" + String(isnan(r0) ? 0.0f : r0, 1) + ",";
  j += "\"ratio\":" + String(lastRatio, 4) + ",";
  j += "\"rl\":" + String(RL_OHMS, 0) + ",";
  j += "\"vc\":" + String(VC_VOLTS, 2) + ",";
  j += "\"cleanAirRatio\":" + String(MQ.cleanAirRatio, 2) + ",";
  j += "\"dout\":" + String(digitalRead(PIN_MQ_DIGITAL)) + ",";
  j += "\"alarm\":" + String(alarmOn ? "true" : "false") + ",";
  j += "\"muted\":" + String(muted ? "true" : "false") + ",";
  j += "\"uptime\":" + String((millis() - bootMillis) / 1000) + ",";
  j += "\"samples\":" + String(sampleCount) + ",";
  j += "\"calibAge\":" + String((millis() - calibratedAt) / 1000) + ",";
  j += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  j += "\"ip\":\"" + (wifiUp ? WiFi.localIP().toString() : String("offline")) + "\",";
  j += "\"ssid\":\"" + String(wifiUp ? WiFi.SSID() : String("—")) + "\",";
  j += "\"rssi\":" + String(wifiUp ? WiFi.RSSI() : 0) + ",";
  j += "\"gases\":[";
  for (uint8_t i = 0; i < MQ.gasCount; i++) {
    const GasCurve &g = MQ.gases[i];
    if (i) j += ",";
    j += "{\"name\":\"" + String(g.name) + "\",";
    j += "\"a\":" + String(g.a, 4) + ",";
    j += "\"b\":" + String(g.b, 4) + ",";
    j += "\"warn\":" + String(g.warnPpm, 2) + ",";
    j += "\"danger\":" + String(g.dangerPpm, 2) + ",";
    j += "\"min\":" + String(g.minPpm, 2) + ",";
    j += "\"max\":" + String(g.maxPpm, 2) + ",";
    j += "\"ppm\":" + String(ppm[i], 2) + ",";
    j += "\"raw\":" + String(ppmRaw[i], 2) + ",";
    j += "\"range\":\"" + String(rangeName(gasRange[i])) + "\",";
    j += "\"resolvable\":" + String(mqResolvable(g) ? "true" : "false") + ",";
    j += "\"status\":\"" + String(statusName(perGas[i])) + "\"}";
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", j);
}

// Only Rs/R0 is stored. The browser holds the same curve constants and
// reconstructs ppm for whichever gas the user is looking at.
static void handleHistory() {
  String j;
  j.reserve(HIST_LEN * 8 + 96);
  j  = "{\"periodMs\":" + String(HIST_PERIOD_MS) + ",";
  j += "\"count\":" + String(histCount < HIST_LEN ? histCount : HIST_LEN) + ",";
  j += "\"ratios\":[";
  uint16_t n = histCount < HIST_LEN ? histCount : HIST_LEN;
  uint8_t  start = histCount < HIST_LEN ? 0 : histHead;   // oldest first
  for (uint16_t k = 0; k < n; k++) {
    if (k) j += ",";
    j += String(histRatio[(start + k) % HIST_LEN], 4);
  }
  j += "]}";
  server.send(200, "application/json", j);
}

static void handleCalibrate() {
  server.send(200, "application/json", "{\"ok\":true}");
  lcdLine(0, "Recalibrating");
  lcdLine(1, "clean air only!");
  r0 = calibrateR0();
  saveR0(r0);
  calibratedAt = millis();
  Serial.printf("Recalibrated from dashboard: R0 = %.0f ohm\n", r0);
}

static void handleMute() {
  muted = !muted;
  server.send(200, "application/json", muted ? "{\"muted\":true}" : "{\"muted\":false}");
}

static void handleBuzzTest() {
  buzzTestUntil = millis() + 600;
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

static void warmup() {
  for (int s = WARMUP_SECONDS; s > 0; s--) {
    lcdLine(0, "Heating sensor");
    lcdLine(1, "Ready in " + String(s) + "s");
    if ((s % 15) == 0) Serial.printf("warmup %ds left\n", s);
    delay(1000);
  }
}

void setup() {
  bootMillis = millis();
  Serial.begin(115200);
  Serial.println();
  Serial.printf("\n=== Gas Detector | %s ===\n%s\n", MQ.name, MQ.note);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_ALARM, OUTPUT);
  pinMode(PIN_MQ_DIGITAL, INPUT);
  driveAlarm(false);

  Wire.begin(PIN_SDA, PIN_SCL);
  uint8_t found = probeLcdAddress();
  if (found) {
    lcdAddr = found;
    Serial.printf("LCD found at 0x%02X\n", lcdAddr);
    lcd = new LiquidCrystal_I2C(lcdAddr, LCD_COLS, LCD_ROWS);
    lcd->init();
    lcd->backlight();
  } else {
    Serial.println("No I2C device found. Check SDA/SCL/5V and the backpack solder joints.");
  }

  lcdLine(0, String(MQ.name) + " detector");
  lcdLine(1, "starting...");
  delay(1200);

  warmup();

  if (loadR0(r0)) {
    Serial.printf("Loaded stored R0 = %.0f ohm\n", r0);
    lcdLine(0, "R0 loaded");
    lcdLine(1, String(r0 / 1000.0f, 2) + " kOhm");
  } else {
    lcdLine(0, "Calibrating R0");
    lcdLine(1, "clean air only!");
    r0 = calibrateR0();
    saveR0(r0);
    Serial.printf("New R0 = %.0f ohm\n", r0);
  }
  calibratedAt = millis();
  delay(1500);

  if (strlen(WIFI_SSID) > 0) {
    lcdLine(0, "WiFi connecting");
    lcdLine(1, WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(MDNS_HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);       // don't wear out flash rewriting credentials
#if USE_STATIC_IP
    WiFi.config(IPAddress(STATIC_IP), IPAddress(STATIC_GW),
                IPAddress(STATIC_MASK), IPAddress(STATIC_DNS));
#endif
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) delay(250);
    wifiUp = (WiFi.status() == WL_CONNECTED);
    if (wifiUp) {
      Serial.println();
      Serial.println("WiFi connected.");
      Serial.print("  Dashboard:  http://"); Serial.println(WiFi.localIP());
      Serial.print("  or by name: http://"); Serial.print(MDNS_HOSTNAME); Serial.println(".local");
      Serial.printf("  signal:     %d dBm\n", WiFi.RSSI());

      // mDNS lets you reach the device by name, so a new IP after a router
      // reboot does not send you hunting through the admin page.
      if (MDNS.begin(MDNS_HOSTNAME)) MDNS.addService("http", "tcp", 80);
      else Serial.println("  (mDNS failed to start — use the IP address)");

      lcdLine(0, String(MDNS_HOSTNAME) + ".local");
      lcdLine(1, WiFi.localIP().toString());
      server.on("/",             HTTP_GET,  handleRoot);
      server.on("/api",          HTTP_GET,  handleApi);
      server.on("/api/history",  HTTP_GET,  handleHistory);
      server.on("/api/calibrate",HTTP_POST, handleCalibrate);
      server.on("/api/mute",     HTTP_POST, handleMute);
      server.on("/api/test",     HTTP_POST, handleBuzzTest);
      server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
      server.begin();
    } else {
      // Common causes: wrong password, a 5 GHz-only network (the ESP8266 is
      // 2.4 GHz only), or a captive portal that needs a login page.
      Serial.printf("WiFi failed (status %d). Detector still works, no dashboard.\n",
                    WiFi.status());
      lcdLine(0, "WiFi failed");
      lcdLine(1, "offline mode");
    }
    delay(2500);
  }
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
  static uint32_t lastSample = 0, lastPage = 0;
  static uint8_t page = 0;

  if (wifiUp) { server.handleClient(); MDNS.update(); }

  if (millis() - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample = millis();
    measure();
    updateAlarm();
  }

  if (millis() - lastHist >= HIST_PERIOD_MS) {
    lastHist = millis();
    histRatio[histHead] = lastRatio;
    histHead = (histHead + 1) % HIST_LEN;
    if (histCount < 0xFFFF) histCount++;
  }

  if (millis() - lastPage >= LCD_PAGE_MS) {
    lastPage = millis();

    if (overall != STATUS_SAFE) {
      // Alarm view: pin the screen to the offending gas.
      const GasCurve &g = MQ.gases[worstGas];
      lcdLine(0, String("!! ") + statusName(overall) + " !!");
      lcdLine(1, String(g.name) + " " + String(ppm[worstGas], 0) + " ppm");
    } else {
      const GasCurve &g = MQ.gases[page];
      lcdLine(0, String(MQ.name) + " " + String(g.name) +
                 (wifiUp ? " " + WiFi.localIP().toString().substring(
                     WiFi.localIP().toString().lastIndexOf('.')) : ""));
      String val = (ppm[page] >= 100.0f) ? String(ppm[page], 0) : String(ppm[page], 1);
      lcdLine(1, val + " ppm  SAFE");
      page = (page + 1) % MQ.gasCount;
    }
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'c') {
      Serial.println("Recalibrating — make sure the air is clean.");
      lcdLine(0, "Recalibrating");
      lcdLine(1, "clean air only!");
      r0 = calibrateR0();
      saveR0(r0);
      calibratedAt = millis();
      Serial.printf("R0 = %.0f ohm\n", r0);
    } else if (c == 'r') {
      Serial.printf("V=%.4f Rs=%.0f R0=%.0f ratio=%.4f DOUT=%d\n",
                    emaVolts, lastRs, r0, lastRatio, digitalRead(PIN_MQ_DIGITAL));
      for (uint8_t i = 0; i < MQ.gasCount; i++)
        Serial.printf("  %-6s %10.2f ppm  raw %-12.2f %-5s %-7s %s\n",
                      MQ.gases[i].name, ppm[i], ppmRaw[i], rangeName(gasRange[i]),
                      statusName(perGas[i]),
                      mqResolvable(MQ.gases[i]) ? "" : "(below this sensor's floor)");
    }
  }
}
