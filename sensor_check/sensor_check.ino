/*
 * sensor_check — is the MQ sensor actually working?
 *
 * Upload this before anything else. It needs only three wires (VCC, GND,
 * AOUT) and deliberately does not touch the LCD, so it still tells you the
 * truth when the display is dead or unwired.
 *
 * It prints a reading twice a second, tracks the range it has seen, and states
 * a verdict in plain language rather than leaving you to interpret numbers.
 *
 * Wiring
 *   MQ module  VCC -> Uno 5V     GND -> Uno GND     AOUT -> Uno A0
 *
 * Serial Monitor at 115200.
 *
 * What you should see, in order:
 *   1. The sensor becomes warm to the touch within about a minute. That is the
 *      internal heater. A sensor that stays cold is not powered.
 *   2. A steady reading somewhere between roughly 50 and 1000 counts.
 *   3. The reading moves when you wave isopropyl alcohol or hand sanitiser
 *      near the mesh, and drifts back afterwards.
 *
 * If all three happen, the sensor works. Everything else is calibration.
 */

#define PIN_MQ_ANALOG  A0
#define RL_OHMS        10000.0f   // load resistor on the module
#define VC_VOLTS       5.0f
#define ADC_FULLSCALE  5.0f

// A floating (unconnected) analog pin drifts around mid-scale and wanders,
// which is easy to mistake for a working sensor. These bounds catch the
// obviously-wrong cases; the movement test below catches the rest.
#define COUNTS_DEAD       8       // at or below this, nothing is driving the pin
#define COUNTS_SATURATED  1015    // at or above this, the pin is pinned high
#define SWING_RESPONDING  20      // counts of movement that prove a real response

static int minCounts = 1023, maxCounts = 0;
static unsigned long startedAt = 0;
static bool announcedWarm = false;

static int readCounts(uint8_t samples) {
  long acc = 0;
  for (uint8_t i = 0; i < samples; i++) { acc += analogRead(PIN_MQ_ANALOG); delay(2); }
  return (int)(acc / samples);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("=== MQ sensor check ==="));
  Serial.println(F("Wiring: VCC->5V  GND->GND  AOUT->A0"));
  Serial.println();
  Serial.println(F("Touch the sensor can gently in a minute — it should be warm."));
  Serial.println(F("A cold sensor is not powered, whatever these numbers say."));
  Serial.println();
  Serial.println(F("counts   mV     Rs(ohm)   state"));
  Serial.println(F("------------------------------------------------------"));
  startedAt = millis();
}

void loop() {
  int counts = readCounts(8);
  if (counts < minCounts) minCounts = counts;
  if (counts > maxCounts) maxCounts = counts;

  float volts = counts * (ADC_FULLSCALE / 1023.0f);
  // Rs = RL * (Vc - Vout) / Vout, the module's divider solved for the sensor.
  long rs = (volts <= 0.01f) ? -1
          : (volts >= VC_VOLTS - 0.01f) ? 0
          : (long)(RL_OHMS * (VC_VOLTS - volts) / volts);

  const __FlashStringHelper *state;
  if (counts <= COUNTS_DEAD)            state = F("NO SIGNAL");
  else if (counts >= COUNTS_SATURATED)  state = F("SATURATED");
  else if (maxCounts - minCounts >= SWING_RESPONDING) state = F("responding");
  else                                  state = F("steady");

  char line[64];
  snprintf(line, sizeof(line), "%4d  %5d  %8ld   ", counts, (int)(volts * 1000), rs);
  Serial.print(line);
  Serial.println(state);

  unsigned long secs = (millis() - startedAt) / 1000;

  if (!announcedWarm && secs >= 60) {
    announcedWarm = true;
    Serial.println();
    Serial.println(F(">> Is the sensor warm to the touch yet?"));
    Serial.println(F("   Warm  = heater running, 5V is reaching it. Good."));
    Serial.println(F("   Cold  = no power. Check VCC and GND before anything else."));
    Serial.println();
  }

  // Verdict every 20 seconds, so the log stays readable.
  if (secs > 0 && secs % 20 == 0) {
    Serial.println();
    Serial.print(F(">> After ")); Serial.print(secs); Serial.print(F("s: range seen "));
    Serial.print(minCounts); Serial.print(F(" to ")); Serial.println(maxCounts);

    if (maxCounts <= COUNTS_DEAD) {
      Serial.println(F("   NO SIGNAL. The pin is reading nothing at all."));
      Serial.println(F("   - Is AOUT wired to A0 (not DOUT)?"));
      Serial.println(F("   - Is the module's power LED lit?"));
      Serial.println(F("   - Try a different jumper wire; they fail more than you expect."));
    } else if (minCounts >= COUNTS_SATURATED) {
      Serial.println(F("   SATURATED. The pin is pinned at the top of its range."));
      Serial.println(F("   - AOUT may be shorted to 5V."));
      Serial.println(F("   - Or the load resistor on the module is wrong."));
      Serial.println(F("   - Or there really is a lot of gas. Move it outside and retry."));
    } else if (maxCounts - minCounts >= SWING_RESPONDING) {
      Serial.println(F("   WORKING. It moved, so it is sensing, not just sitting there."));
      Serial.println(F("   Note the clean-air value — that is what calibration stores."));
    } else {
      Serial.println(F("   Plausible reading, but it has not moved yet."));
      Serial.println(F("   Wave isopropyl alcohol or hand sanitiser near the mesh."));
      Serial.println(F("   A working sensor swings hard within a second or two."));
      Serial.println(F("   No movement at all after that = the sensing layer is dead,"));
      Serial.println(F("   even though the heater may still be warm."));
    }
    Serial.println();
    delay(1000);   // don't repeat the block within the same second
  }

  delay(500);
}
