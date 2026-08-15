/*
 * Single-row quote display — Arduino Uno + 16x2 HD44780 on a PCF8574 backpack
 *
 * Written for a display whose second row is dead: every frame is drawn on
 * row 0 and row 1 is never touched, so a broken bottom half is invisible
 * rather than embarrassing.
 *
 * Four movements, looping:
 *   1. Decode   — noise resolving into the quote, column by column
 *   2. Phrases  — the quote broken at word boundaries, wiped in one at a time
 *   3. Marquee  — the whole thing scrolling through the 16-character window
 *   4. Signature— the attribution typed out under a blinking cursor
 *
 * Wiring — four wires:
 *   LCD backpack VCC -> Uno 5V    SDA -> Uno A4
 *                GND -> Uno GND   SCL -> Uno A5
 *
 * Library: "LiquidCrystal I2C" by Frank de Brabander
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define LCD_COLS 16
#define ROW      0        // the only row this sketch will ever write to

// --- Pick your quote -------------------------------------------------------
// 0  Jobs closed his 2005 Stanford commencement address with this, quoting
//    the farewell message on the back of the final Whole Earth Catalog.
// 1  From a 1985 Playboy interview.
// 2  Condensed from his 2003 New York Times remark about design.
// 3  Apple's 1997 "Think Different" campaign, which Jobs narrated himself.
#define QUOTE_INDEX 0

static const char *QUOTES[] = {
  "Stay hungry. Stay foolish.",
  "Innovation distinguishes a leader from a follower.",
  "Design is not how it looks. Design is how it works.",
  "The people crazy enough to think they can change the world are the ones who do.",
};
static const char AUTHOR[] = "- Steve Jobs";

// Characters the decode effect flickers through before locking in.
static const char NOISE[] = "#@$%&*+=?/|<>~^:;01";

LiquidCrystal_I2C *lcd = nullptr;
const char *quote = nullptr;

// ---------------------------------------------------------------------------
// Helpers — all of them write to ROW and nothing else
// ---------------------------------------------------------------------------
static uint8_t probeLcdAddress() {
  const uint8_t candidates[] = {0x27, 0x3F, 0x20, 0x38};
  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) return candidates[i];
  }
  return 0;
}

// Pad to the full width so nothing survives from the previous frame. Cheaper
// and far steadier than clear(), which makes the whole display flicker.
static void row(const char *text) {
  lcd->setCursor(0, ROW);
  uint8_t n = 0;
  while (text[n] && n < LCD_COLS) { lcd->print(text[n]); n++; }
  while (n++ < LCD_COLS) lcd->print(' ');
}

// Centre `text` into a 16-character buffer, space-padded.
static void centre(const char *text, char *out) {
  uint8_t len = strlen(text);
  if (len > LCD_COLS) len = LCD_COLS;
  memset(out, ' ', LCD_COLS);
  memcpy(out + (LCD_COLS - len) / 2, text, len);
  out[LCD_COLS] = '\0';
}

// Walk `text` and hand back the next chunk that fits, broken at a space
// rather than mid-word. Returns false once the text is exhausted.
static bool nextPhrase(const char *text, uint16_t *pos, char *out, uint8_t width) {
  while (text[*pos] == ' ') (*pos)++;
  if (!text[*pos]) return false;

  uint16_t start = *pos, end = *pos, lastSpace = 0;
  while (text[end] && (end - start) < width) {
    if (text[end] == ' ') lastSpace = end;
    end++;
  }
  // Only rewind to a word boundary if we actually ran out of room mid-word.
  if (text[end] && lastSpace > start) end = lastSpace;

  uint8_t n = end - start;
  memcpy(out, text + start, n);
  out[n] = '\0';
  *pos = end;
  return true;
}

// ---------------------------------------------------------------------------
// 1. Decode — each column flickers through noise, then locks into place
// ---------------------------------------------------------------------------
// Returns where in the quote it stopped, so the next movement can carry on
// rather than repeating the phrase that was just revealed.
static uint16_t movementDecode(const char *text) {
  // Decode the first phrase, not the first sixteen characters — otherwise a
  // quote longer than the display opens on a word chopped in half.
  char phrase[LCD_COLS + 1];
  uint16_t pos = 0;
  if (!nextPhrase(text, &pos, phrase, LCD_COLS)) return 0;

  char target[LCD_COLS + 1];
  centre(phrase, target);

  // Columns settle left to right, but with enough jitter that the front edge
  // looks organic rather than like a wipe.
  uint8_t lockAt[LCD_COLS];
  for (uint8_t c = 0; c < LCD_COLS; c++) lockAt[c] = 4 + c * 2 + random(0, 5);

  uint8_t last = 0;
  for (uint8_t c = 0; c < LCD_COLS; c++) if (lockAt[c] > last) last = lockAt[c];

  const uint8_t noiseLen = sizeof(NOISE) - 1;
  for (uint8_t frame = 0; frame <= last; frame++) {
    lcd->setCursor(0, ROW);
    for (uint8_t c = 0; c < LCD_COLS; c++) {
      if (frame >= lockAt[c])      lcd->print(target[c]);
      else if (target[c] == ' ')   lcd->print(' ');       // leave gaps as gaps
      else                         lcd->print(NOISE[random(0, noiseLen)]);
    }
    delay(45);
  }
  delay(1100);
  return pos;
}

// ---------------------------------------------------------------------------
// 2. Phrases — a solid block sweeps across, leaving the next phrase behind it
// ---------------------------------------------------------------------------
static void loadBlockGlyph() {
  uint8_t solid[8];
  for (uint8_t r = 0; r < 8; r++) solid[r] = 0b11111;
  lcd->createChar(0, solid);
  lcd->setCursor(0, ROW);      // createChar leaves the address counter in CGRAM
}

static void wipeIn(const char *text) {
  char buf[LCD_COLS + 1];
  centre(text, buf);
  for (uint8_t edge = 0; edge <= LCD_COLS; edge++) {
    lcd->setCursor(0, ROW);
    for (uint8_t c = 0; c < LCD_COLS; c++) {
      if (c < edge)       lcd->print(buf[c]);          // already revealed
      else if (c == edge) lcd->write((uint8_t)0);      // the leading block
      else                lcd->print(' ');
    }
    delay(28);
  }
}

static void movementPhrases(const char *text, uint16_t pos) {
  loadBlockGlyph();
  char phrase[LCD_COLS + 1];
  while (nextPhrase(text, &pos, phrase, LCD_COLS)) {
    wipeIn(phrase);
    delay(1300);
  }
}

// ---------------------------------------------------------------------------
// 3. Marquee — the full quote and attribution scrolling through the window
// ---------------------------------------------------------------------------
static void movementMarquee(const char *text) {
  // Built once into a buffer so the window can wrap cleanly at the end.
  char scroll[160];
  snprintf(scroll, sizeof(scroll), "%s   %s        ", text, AUTHOR);
  uint16_t len = strlen(scroll);

  char win[LCD_COLS + 1];
  win[LCD_COLS] = '\0';
  for (uint16_t start = 0; start < len; start++) {
    for (uint8_t c = 0; c < LCD_COLS; c++) win[c] = scroll[(start + c) % len];
    row(win);
    delay(170);
  }
}

// ---------------------------------------------------------------------------
// 4. Signature — typed out, then a cursor blinking after it
// ---------------------------------------------------------------------------
static void movementSignature() {
  char buf[LCD_COLS + 1];
  centre(AUTHOR, buf);

  // Find where the text sits inside the centred buffer, so the typing reveals
  // it in place rather than sliding it across.
  uint8_t start = (LCD_COLS - strlen(AUTHOR)) / 2;
  uint8_t end   = start + strlen(AUTHOR);

  row("");
  for (uint8_t c = start; c < end; c++) {
    lcd->setCursor(c, ROW);
    lcd->print(buf[c]);
    delay(85);
  }
  for (uint8_t b = 0; b < 6; b++) {
    lcd->setCursor(end < LCD_COLS ? end : LCD_COLS - 1, ROW);
    lcd->print(b & 1 ? ' ' : '_');
    delay(320);
  }
  delay(700);
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println(F("\nSingle-row quote display"));

  // Seed from a floating analog pin so the decode effect differs each power-up.
  randomSeed(analogRead(A0));

  Wire.begin();
  uint8_t addr = probeLcdAddress();
  if (!addr) {
    Serial.println(F("No I2C device found."));
    Serial.println(F("Check SDA->A4, SCL->A5, VCC->5V, GND->GND."));
    while (true) delay(1000);
  }
  Serial.print(F("LCD found at 0x")); Serial.println(addr, HEX);

  lcd = new LiquidCrystal_I2C(addr, LCD_COLS, 2);
  lcd->init();
  lcd->backlight();
  lcd->clear();

  quote = QUOTES[QUOTE_INDEX];
}

void loop() {
  uint16_t after = movementDecode(quote);
  movementPhrases(quote, after);
  movementMarquee(quote);
  movementSignature();
}
