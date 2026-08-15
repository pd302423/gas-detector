/*
 * LCD animation showcase — Arduino Uno + 16x2 HD44780 on a PCF8574 I2C backpack
 *
 * Upload this on its own. It does two jobs:
 *   1. Proves the LCD is wired and addressed correctly, before you fight with
 *      sensor code. If this runs, your I2C wiring is right.
 *   2. Shows seven animations you can lift straight into the detector — the
 *      progress bar in particular is a drop-in replacement for the plain
 *      "Ready in 173s" warm-up countdown in gas_detector_uno.ino.
 *
 * Wiring — four wires, nothing else:
 *   LCD backpack VCC -> Uno 5V
 *                GND -> Uno GND
 *                SDA -> Uno A4
 *                SCL -> Uno A5
 *
 * Library: "LiquidCrystal I2C" by Frank de Brabander (Library Manager)
 *
 * Blank screen or two rows of solid blocks? That is contrast, not code.
 * Turn the blue trimmer on the back of the backpack.
 *
 * How the custom glyphs work: the HD44780 keeps 8 user-defined characters in
 * CGRAM, each 5 pixels wide by 8 tall. Each row is one byte, low 5 bits, MSB
 * on the left. createChar() uploads one; write((uint8_t)n) prints it. Eight is
 * the hard limit, so every animation below loads its own set when it starts.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define LCD_COLS 16
#define LCD_ROWS 2

LiquidCrystal_I2C *lcd = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// The backpack is either a PCF8574T (0x27) or a PCF8574AT (0x3F). Rather than
// make you find out which, ask the bus.
static uint8_t probeLcdAddress() {
  const uint8_t candidates[] = {0x27, 0x3F, 0x20, 0x38};
  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) return candidates[i];
  }
  return 0;
}

// Print text padded to the full width, so leftovers from the previous frame
// never linger. Cheaper and steadier than clearing the whole display.
static void lcdLine(uint8_t row, const char *text) {
  lcd->setCursor(0, row);
  uint8_t n = 0;
  while (text[n] && n < LCD_COLS) { lcd->print(text[n]); n++; }
  while (n++ < LCD_COLS) lcd->print(' ');
}

static void lcdCentre(uint8_t row, const char *text) {
  uint8_t len = strlen(text);
  if (len > LCD_COLS) len = LCD_COLS;
  char buf[LCD_COLS + 1];
  uint8_t pad = (LCD_COLS - len) / 2;
  memset(buf, ' ', LCD_COLS);
  memcpy(buf + pad, text, len);
  buf[LCD_COLS] = '\0';
  lcdLine(row, buf);
}

// ---------------------------------------------------------------------------
// 1. Typewriter — text appears a character at a time behind a blinking cursor
// ---------------------------------------------------------------------------
static void typeOut(uint8_t row, const char *text, uint16_t perChar) {
  lcdLine(row, "");
  uint8_t n = strlen(text);
  if (n > LCD_COLS) n = LCD_COLS;
  for (uint8_t i = 0; i < n; i++) {
    lcd->setCursor(i, row);
    lcd->print(text[i]);
    if (i + 1 < LCD_COLS) { lcd->setCursor(i + 1, row); lcd->print('_'); }
    delay(perChar);
  }
  // Blink the cursor a few times, then clear it. Only if there is room left —
  // setCursor(16, ...) on a 16-column display writes off the end of the line.
  if (n < LCD_COLS) {
    for (uint8_t b = 0; b < 4; b++) {
      lcd->setCursor(n, row);
      lcd->print(b & 1 ? '_' : ' ');
      delay(160);
    }
    lcd->setCursor(n, row);
    lcd->print(' ');
  } else {
    delay(600);
  }
}

static void animTypewriter() {
  lcd->clear();
  typeOut(0, "AIR SENTINEL", 70);
  typeOut(1, "MQ-2 detector", 45);
  delay(900);
}

// ---------------------------------------------------------------------------
// 2. Progress bar — 80 steps across 16 cells, five times smoother than
//    whole-character blocks. This is the one worth stealing for the warm-up.
// ---------------------------------------------------------------------------
static void loadBarGlyphs() {
  // Glyph n is filled from the left with n+1 of its 5 columns.
  for (uint8_t g = 0; g < 5; g++) {
    uint8_t rows[8], bits = 0;
    for (uint8_t c = 0; c <= g; c++) bits |= (0b10000 >> c);
    for (uint8_t r = 0; r < 8; r++) rows[r] = bits;
    lcd->createChar(g, rows);
  }
  lcd->setCursor(0, 0);   // createChar leaves the address counter in CGRAM
}

// pixels: 0 to 80
static void drawBar(uint8_t row, uint8_t pixels) {
  uint8_t full = pixels / 5, part = pixels % 5;
  lcd->setCursor(0, row);
  for (uint8_t c = 0; c < LCD_COLS; c++) {
    if (c < full)            lcd->write((uint8_t)4);        // all 5 columns lit
    else if (c == full && part) lcd->write((uint8_t)(part - 1));
    else                     lcd->print(' ');
  }
}

static void animProgressBar() {
  lcd->clear();
  loadBarGlyphs();
  // Left-aligned, and short: the percentage lands on columns 12-15 and would
  // otherwise chew the end off a centred label.
  lcdLine(0, "Warming up");
  int8_t lastPct = -1;   // deliberately not static: this must reset each run
  for (uint16_t p = 0; p <= 80; p++) {
    drawBar(1, p);
    // Percentage in the top-right corner, redrawn only when it changes.
    int8_t pct = (int8_t)(p * 100L / 80);
    if (pct != lastPct) {
      lastPct = pct;
      char buf[6];
      snprintf(buf, sizeof(buf), "%3d%%", pct);
      lcd->setCursor(12, 0);
      lcd->print(buf);
    }
    delay(28);
  }
  delay(500);
}

// ---------------------------------------------------------------------------
// 3. Scanner sweep — a comet with a fading tail, built from vertical bars
// ---------------------------------------------------------------------------
static void loadLevelGlyphs() {
  // Glyph n is filled from the bottom with n+1 of its 8 rows.
  for (uint8_t g = 0; g < 8; g++) {
    uint8_t rows[8];
    for (uint8_t r = 0; r < 8; r++) rows[r] = (r >= 7 - g) ? 0b11111 : 0b00000;
    lcd->createChar(g, rows);
  }
  lcd->setCursor(0, 0);
}

static void animSweep() {
  lcd->clear();
  loadLevelGlyphs();
  lcdCentre(0, "Scanning air");
  // The beam runs out to 15 and back; cells near it are drawn taller.
  for (uint8_t pass = 0; pass < 3; pass++) {
    for (int8_t step = 0; step < 30; step++) {
      int8_t pos = step < 16 ? step : 30 - step;   // out, then back
      lcd->setCursor(0, 1);
      for (int8_t c = 0; c < LCD_COLS; c++) {
        int8_t d = c - pos; if (d < 0) d = -d;
        int8_t level = 8 - d * 3;                  // 8, 5, 2, then nothing
        if (level <= 0) lcd->print(' ');
        else lcd->write((uint8_t)(level - 1));
      }
      delay(45);
    }
  }
}

// ---------------------------------------------------------------------------
// 4. Gas cloud — particles drifting upward, redrawn into CGRAM every frame
// ---------------------------------------------------------------------------

// Build one 5x8 glyph holding three specks, each raised by `phase` rows.
static void makeCloudGlyph(uint8_t *rows, uint8_t phase) {
  const uint8_t col[3]  = {0b10000, 0b00100, 0b00001};
  const uint8_t seed[3] = {0, 3, 6};
  for (uint8_t r = 0; r < 8; r++) rows[r] = 0;
  for (uint8_t p = 0; p < 3; p++) {
    uint8_t r = (seed[p] + phase) & 7;
    rows[7 - r] |= col[p];      // row 7 is the bottom, so this rises
  }
}

static void animGasCloud() {
  lcd->clear();
  lcdCentre(0, "Gas detected");
  // Four glyphs at staggered phases, tiled across the row. Re-uploading all
  // four each frame is what makes the whole cloud drift rather than blink.
  for (uint8_t frame = 0; frame < 40; frame++) {
    uint8_t rows[8];
    for (uint8_t g = 0; g < 4; g++) {
      makeCloudGlyph(rows, (frame + g * 2) & 7);
      lcd->createChar(g, rows);
    }
    lcd->setCursor(0, 1);
    for (uint8_t c = 0; c < LCD_COLS; c++) lcd->write((uint8_t)(c & 3));
    delay(70);
  }
}

// ---------------------------------------------------------------------------
// 5. Marquee — a message longer than the display, scrolled through a window
// ---------------------------------------------------------------------------
static const char MARQUEE[] PROGMEM =
  "   One sensor. One resistance. Four possible gases -- and it cannot tell "
  "you which one.   ";

static void animMarquee() {
  lcd->clear();
  lcdCentre(0, "AIR SENTINEL");
  uint16_t len = strlen_P(MARQUEE);
  for (uint16_t start = 0; start < len; start++) {
    char win[LCD_COLS + 1];
    for (uint8_t c = 0; c < LCD_COLS; c++)
      win[c] = pgm_read_byte(&MARQUEE[(start + c) % len]);
    win[LCD_COLS] = '\0';
    lcdLine(1, win);
    delay(160);
  }
}

// ---------------------------------------------------------------------------
// 6. Alarm — inverse blocks plus a strobing backlight
// ---------------------------------------------------------------------------
static void animAlarm() {
  lcd->clear();
  uint8_t solid[8];
  for (uint8_t r = 0; r < 8; r++) solid[r] = 0b11111;
  lcd->createChar(0, solid);
  lcd->setCursor(0, 0);

  for (uint8_t beat = 0; beat < 6; beat++) {
    // Bars sweep in from both edges toward the centre.
    for (int8_t c = 0; c < 4; c++) {
      lcd->setCursor(c, 0);            lcd->write((uint8_t)0);
      lcd->setCursor(LCD_COLS - 1 - c, 0); lcd->write((uint8_t)0);
      delay(35);
    }
    lcdCentre(1, "!! DANGER !!");
    lcd->noBacklight(); delay(90);
    lcd->backlight();   delay(140);
    lcdLine(0, "");
    lcdLine(1, "");
    delay(80);
  }
  lcd->backlight();
}

// ---------------------------------------------------------------------------
// 7. Live readout — what the detector actually looks like in normal operation
// ---------------------------------------------------------------------------
static void animReadout() {
  lcd->clear();
  const char *gases[4] = {"LPG", "Smoke", "CO", "H2"};
  const int   ppm[4]   = {212, 340, 200, 260};
  for (uint8_t i = 0; i < 4; i++) {
    char top[LCD_COLS + 1], bot[LCD_COLS + 1];
    snprintf(top, sizeof(top), "MQ-2  %s", gases[i]);
    // CO is the one this sensor cannot resolve, and the firmware says so.
    if (i == 2) snprintf(bot, sizeof(bot), "<200 ppm  n/a");
    else        snprintf(bot, sizeof(bot), "%d ppm  SAFE", ppm[i]);
    lcdLine(0, top);
    lcdLine(1, bot);
    delay(1400);
  }
}

// ---------------------------------------------------------------------------
// Setup and loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println(F("\nLCD animation showcase"));

  Wire.begin();
  uint8_t addr = probeLcdAddress();
  if (!addr) {
    Serial.println(F("No I2C device found."));
    Serial.println(F("Check SDA->A4, SCL->A5, VCC->5V, GND->GND,"));
    Serial.println(F("and the solder joints on the 16-pin header."));
    while (true) delay(1000);          // nothing useful left to do
  }
  Serial.print(F("LCD found at 0x")); Serial.println(addr, HEX);

  lcd = new LiquidCrystal_I2C(addr, LCD_COLS, LCD_ROWS);
  lcd->init();
  lcd->backlight();
  lcd->clear();
}

void loop() {
  animTypewriter();
  animProgressBar();
  animSweep();
  animGasCloud();
  animReadout();
  animMarquee();
  animAlarm();
}
