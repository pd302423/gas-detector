// mq_curves.h — MQ-series sensor response curves + hazard thresholds
//
// Every MQ sensor follows a power law on log-log axes:
//     ppm = a * (Rs/R0)^b
// where Rs = sensing resistance now, R0 = sensing resistance in clean air.
// The (a, b) pairs below are least-squares fits to the curves printed in the
// Hanwei/Winsen datasheets. They are approximations of a graph in a PDF, not
// a calibrated instrument. Treat the ppm numbers as an order of magnitude.
//
// cleanAirRatio = Rs/R0 in clean air, straight from the datasheet. This is how
// R0 gets derived during calibration: R0 = Rs_cleanair / cleanAirRatio.

#ifndef MQ_CURVES_H
#define MQ_CURVES_H

#include <Arduino.h>

struct GasCurve {
  const char *name;      // gas label (<= 6 chars keeps the LCD tidy)
  float a;               // scale factor
  float b;               // exponent (always negative)
  float warnPpm;         // raise WARNING at or above this
  float dangerPpm;       // raise DANGER at or above this
  float minPpm;          // datasheet floor — below this the fit is extrapolation
  float maxPpm;          // datasheet ceiling — above this the fit is extrapolation
};

// Why minPpm/maxPpm matter: the power law is a fit to a curve the datasheet
// only draws across a limited band. Outside it the fit is fantasy. An MQ-2 at
// Rs/R0 = 0.6 reports 180,000 ppm of CO if you let it — a number with no
// physical meaning, which would then dominate the alarm. Everything is clamped
// to the band, and a gas whose WARNING threshold sits below the sensor's own
// floor is excluded from alarm logic entirely: that sensor genuinely cannot
// resolve that gas at a hazardous-but-not-yet-obvious level.

struct MqModel {
  const char *name;
  float cleanAirRatio;
  const GasCurve *gases;
  uint8_t gasCount;
  const char *note;
};

// ---------------------------------------------------------------------------
// Thresholds rationale
//   Combustibles (LPG/CH4/H2/propane): DANGER is 10% of the Lower Explosive
//   Limit, the standard industrial alarm point. WARNING is 5% LEL.
//     LPG  LEL 1.8% = 18000 ppm -> warn 900,  danger 1800
//     CH4  LEL 5.0% = 50000 ppm -> warn 2500, danger 5000
//     H2   LEL 4.0% = 40000 ppm -> warn 2000, danger 4000
//   Toxics (CO, NH3, benzene): thresholds are health-based, not explosion-based.
//     CO   35 ppm = 8h exposure limit, 200 ppm = headache, 800 ppm = 2h lethal
//     NH3  25 ppm = 8h limit, 300 ppm = immediately dangerous to life
//     C6H6 benzene is a carcinogen; any confirmed reading is a problem
// ---------------------------------------------------------------------------

//                name        a        b      warn   danger    min      max
static const GasCurve MQ2_GASES[] = {
  {"LPG",   574.25f, -2.222f,  900.0f,  1800.0f,  200.0f, 10000.0f},
  {"Smoke", 3616.1f, -2.675f,  500.0f,  1000.0f,  200.0f, 10000.0f},
  {"CO",    36974.0f,-3.109f,   35.0f,   200.0f,  200.0f, 10000.0f},  // floor > warn: not alarm-capable
  {"H2",    987.99f, -2.162f, 2000.0f,  4000.0f,  200.0f, 10000.0f},
};

static const GasCurve MQ3_GASES[] = {
  // MQ-3 datasheet is in mg/L of alcohol. 0.2 mg/L breath ~ 0.4 g/L blood.
  {"Alcoh", 0.4091f, -1.504f,   0.2f,     0.5f,    0.05f,    10.0f},
};

static const GasCurve MQ4_GASES[] = {
  {"CH4",   1012.7f, -2.786f, 2500.0f,  5000.0f,  300.0f, 10000.0f},
  {"LPG",   3811.9f, -3.113f,  900.0f,  1800.0f,  300.0f, 10000.0f},
};

static const GasCurve MQ5_GASES[] = {
  {"LPG",   80.897f, -2.431f,  900.0f,  1800.0f,  200.0f, 10000.0f},
  {"CH4",   177.65f, -2.560f, 2500.0f,  5000.0f,  200.0f, 10000.0f},
};

static const GasCurve MQ6_GASES[] = {
  {"LPG",   1009.2f, -2.350f,  900.0f,  1800.0f,  200.0f, 10000.0f},
  {"CH4",   2127.2f, -2.526f, 2500.0f,  5000.0f,  200.0f, 10000.0f},
};

static const GasCurve MQ7_GASES[] = {
  {"CO",    99.042f, -1.518f,   35.0f,   200.0f,   20.0f,  2000.0f},
};

static const GasCurve MQ8_GASES[] = {
  {"H2",    976.97f, -1.606f, 2000.0f,  4000.0f,  100.0f, 10000.0f},
};

static const GasCurve MQ9_GASES[] = {
  {"CO",    599.65f, -2.244f,   35.0f,   200.0f,   10.0f,  1000.0f},
  {"CH4",   1000.5f, -2.186f, 2500.0f,  5000.0f,  100.0f, 10000.0f},
  {"LPG",   1000.5f, -2.186f,  900.0f,  1800.0f,  100.0f, 10000.0f},
};

static const GasCurve MQ135_GASES[] = {
  {"CO2",   110.47f, -2.862f, 1000.0f,  5000.0f,   10.0f,  1000.0f},  // danger above ceiling
  {"NH3",   102.20f, -2.473f,   25.0f,   300.0f,   10.0f,  1000.0f},
  {"Alcoh", 77.255f, -3.180f,  100.0f,  1000.0f,   10.0f,  1000.0f},
  {"Benz",  34.668f, -3.369f,    1.0f,    50.0f,   10.0f,  1000.0f},  // floor > warn
  {"Smoke", 43.749f, -3.420f,  500.0f,  1000.0f,   10.0f,  1000.0f},
};

// --- Pick your sensor here (also settable from config.h) --------------------
#ifndef MQ_SELECT
#define MQ_SELECT 2
#endif

#if   MQ_SELECT == 2
  static const MqModel MQ = {"MQ-2", 9.83f, MQ2_GASES, 4,
    "General combustible + smoke. Most common Flying Fish board."};
#elif MQ_SELECT == 3
  static const MqModel MQ = {"MQ-3", 60.0f, MQ3_GASES, 1,
    "Alcohol/ethanol vapour. Breathalyser use. mg/L not ppm."};
#elif MQ_SELECT == 4
  static const MqModel MQ = {"MQ-4", 4.40f, MQ4_GASES, 2,
    "Methane / natural gas (CNG, PNG piped gas)."};
#elif MQ_SELECT == 5
  static const MqModel MQ = {"MQ-5", 6.50f, MQ5_GASES, 2,
    "LPG and natural gas, low alcohol sensitivity."};
#elif MQ_SELECT == 6
  static const MqModel MQ = {"MQ-6", 10.00f, MQ6_GASES, 2,
    "LPG / butane / propane. Best for Indian cylinder gas."};
#elif MQ_SELECT == 7
  static const MqModel MQ = {"MQ-7", 27.00f, MQ7_GASES, 1,
    "Carbon monoxide. NEEDS the 60s/90s heater cycle to be accurate."};
#elif MQ_SELECT == 8
  static const MqModel MQ = {"MQ-8", 70.00f, MQ8_GASES, 1,
    "Hydrogen."};
#elif MQ_SELECT == 9
  static const MqModel MQ = {"MQ-9", 9.60f, MQ9_GASES, 3,
    "CO + combustibles. Also a dual-heater-cycle part."};
#elif MQ_SELECT == 135
  static const MqModel MQ = {"MQ-135", 3.60f, MQ135_GASES, 5,
    "Air quality: CO2, NH3, benzene, solvents. Very broad, very unspecific."};
#else
  #error "MQ_SELECT must be one of 2,3,4,5,6,7,8,9,135"
#endif

// Raw curve evaluation, before any clamping. Guards against nonsense inputs.
inline float mqPpmRaw(const GasCurve &g, float ratio) {
  if (ratio <= 0.0f || isnan(ratio)) return 0.0f;
  float ppm = g.a * pow(ratio, g.b);
  if (isnan(ppm) || isinf(ppm)) return 0.0f;
  return ppm;
}

// Where a reading sits relative to the datasheet band.
enum GasRange { RANGE_IN = 0, RANGE_BELOW = 1, RANGE_ABOVE = 2 };

inline GasRange mqRange(const GasCurve &g, float rawPpm) {
  if (rawPpm < g.minPpm) return RANGE_BELOW;
  if (rawPpm > g.maxPpm) return RANGE_ABOVE;
  return RANGE_IN;
}

// Clamped to the band the datasheet actually characterises.
inline float mqPpm(const GasCurve &g, float ratio) {
  float p = mqPpmRaw(g, ratio);
  if (p < g.minPpm) return g.minPpm;
  if (p > g.maxPpm) return g.maxPpm;
  return p;
}

// True when this sensor can actually resolve a hazardous level of this gas.
// An MQ-2 cannot: its floor is 200 ppm and CO becomes a health concern at 35.
// Non-resolvable gases are shown for context but never drive the alarm.
inline bool mqResolvable(const GasCurve &g) { return g.warnPpm >= g.minPpm; }

enum GasStatus { STATUS_SAFE = 0, STATUS_WARNING = 1, STATUS_DANGER = 2 };

inline GasStatus mqStatus(const GasCurve &g, float ppm) {
  if (!mqResolvable(g))   return STATUS_SAFE;
  if (ppm >= g.dangerPpm) return STATUS_DANGER;
  if (ppm >= g.warnPpm)   return STATUS_WARNING;
  return STATUS_SAFE;
}

inline const char *statusName(GasStatus s) {
  switch (s) {
    case STATUS_DANGER:  return "DANGER";
    case STATUS_WARNING: return "WARNING";
    default:             return "SAFE";
  }
}

#endif  // MQ_CURVES_H
