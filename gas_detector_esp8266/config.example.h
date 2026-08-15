// config.example.h — copy this to config.h and fill in your own values.
//
//     copy config.example.h config.h      (Windows)
//     cp   config.example.h config.h      (macOS / Linux)
//
// config.h is gitignored precisely because it holds your WiFi password.
// Never commit it. Edit the copy, not this file.

#ifndef CONFIG_H
#define CONFIG_H

// --- 1. WHICH SENSOR DO YOU ACTUALLY HAVE? ---------------------------------
// Read the number stamped on the metal can under the steel mesh: MQ-2, MQ-135...
// If the mesh hides it, the PCB silkscreen usually says it too.
// Valid: 2, 3, 4, 5, 6, 7, 8, 9, 135
#define MQ_SELECT 135

// --- 2. WIFI ---------------------------------------------------------------
// The ESP8266 is 2.4 GHz only. If your router broadcasts one name for both
// bands it will usually work; if 2.4 and 5 GHz have separate names, use the
// 2.4 GHz one. A phone hotspot set to 2.4 GHz works too.
//
// Leave SSID empty ("") to run fully offline: LCD + buzzer only, no dashboard.
#define WIFI_SSID     "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
#define WIFI_TIMEOUT_MS 15000

// Hostname for mDNS. With this set you reach the dashboard at http://gas.local
// instead of hunting for an IP address, and it survives the router handing out
// a different IP after a reboot.
//
// Works out of the box on Windows 10/11, macOS, iOS and most Linux. Some
// Android versions do not resolve .local — use the IP address there, which the
// LCD and the serial monitor both print at boot.
#define MDNS_HOSTNAME "gas"

// Optional fixed IP. Leave USE_STATIC_IP at 0 to let the router assign one
// (recommended). Set it to 1 only if you want the address never to change and
// mDNS is not an option — then pick an address outside your router's DHCP pool.
#define USE_STATIC_IP 0
#define STATIC_IP     192, 168, 1, 50
#define STATIC_GW     192, 168, 1, 1
#define STATIC_MASK   255, 255, 255, 0
#define STATIC_DNS    192, 168, 1, 1

// --- 3. HARDWARE -----------------------------------------------------------
// Load resistor on the Flying Fish module, in ohms. Nearly all of them use a
// 10k trimmer/resistor between AOUT and GND. If your ppm readings are wildly
// off across the whole range, measure it: power the board down, ohmmeter
// between the AOUT pin and GND.
#define RL_OHMS 10000.0f

// Sensor heater supply voltage. The MQ heater needs a real 5V.
#define VC_VOLTS 5.0f

// Voltage that corresponds to a full-scale (1023) reading on A0.
// NodeMCU/Wemos already have an on-board 100k/220k divider making A0 = 0..3.3V.
// With the extra 68k/100k divider from the wiring guide, full scale becomes:
//     3.3 / (100 / (100 + 68)) = 5.54V
// If you are NOT using the external divider (dangerous, see README), use 3.3f.
#define ADC_FULLSCALE_VOLTS 5.54f

#define PIN_MQ_ANALOG   A0
#define PIN_MQ_DIGITAL  D6   // module's DOUT (comparator trip) — optional
#define PIN_BUZZER      D5   // active buzzer +, or leave unconnected
#define PIN_LED_ALARM   D7   // optional external LED, 220R to GND

#define BUZZER_ACTIVE_HIGH true

// I2C for the LCD backpack
#define PIN_SDA D2
#define PIN_SCL D1
#define LCD_ADDR 0x27        // 0x27 (PCF8574T) or 0x3F (PCF8574AT); auto-probed
#define LCD_COLS 16
#define LCD_ROWS 2

// --- 4. TIMING -------------------------------------------------------------
#define WARMUP_SECONDS      180   // heater stabilisation on every boot
#define CALIB_SAMPLES       64    // clean-air samples when deriving R0
#define SAMPLE_INTERVAL_MS  500
#define LCD_PAGE_MS         2500  // how long each gas stays on screen
#define EMA_ALPHA           0.20f // smoothing on the raw voltage, 0..1

// --- 5. ALARM BEHAVIOUR ----------------------------------------------------
// Consecutive readings above threshold before the alarm latches. Stops a
// single noisy sample from screaming at 3am.
#define ALARM_CONFIRM_COUNT 4
// Once latched, the alarm holds for at least this long even if gas clears.
#define ALARM_HOLD_MS 10000UL

#endif  // CONFIG_H
