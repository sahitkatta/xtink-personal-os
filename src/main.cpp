#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#include "board_pins.h"
#include "BatteryMonitor.h"

GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT> display(
    GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static BatteryMonitor battery(BAT_ADC_PIN);

// Checks the SD card shares the bus cleanly with the display: mounts it,
// writes a marker file, reads it back. Settings/todos storage builds on
// this once confirmed working on real hardware.
String checkSdCard() {
  if (!SD.begin(SD_CS, SPI, SHARED_SPI_HZ)) {
    uint8_t cardType = SD.cardType();
    Serial.printf("SD.begin failed, SD.cardType() = %d\n", cardType);
    return "SD: not detected";
  }

  uint64_t sizeMb = SD.cardSize() / (1024ULL * 1024ULL);

  File f = SD.open("/xte_test.txt", FILE_WRITE);
  if (!f) {
    return "SD: mount ok, write failed";
  }
  f.print("xte-ink-dashboard test ok");
  f.close();

  f = SD.open("/xte_test.txt", FILE_READ);
  if (!f) {
    return "SD: mount ok, reopen failed";
  }
  String readBack = f.readString();
  f.close();

  if (readBack != "xte-ink-dashboard test ok") {
    return "SD: mount ok, read mismatch";
  }

  return "SD: OK, " + String((unsigned long)sizeMb) + "MB, rw verified";
}

// Decodes the currently pressed button from the two ADC ladders + the
// digital power button. Thresholds come from board_pins.h.
const char *readButtonName() {
  int ladder4 = analogRead(BTN_LADDER_4);
  int ladder2 = analogRead(BTN_LADDER_2);

  if (digitalRead(BTN_POWER) == LOW) return "POWER";

  if (ladder4 < BTN_RIGHT_VAL + BTN_THRESHOLD) return "RIGHT";
  if (ladder4 < BTN_LEFT_VAL + BTN_THRESHOLD) return "LEFT";
  if (ladder4 < BTN_CONFIRM_VAL + BTN_THRESHOLD) return "CONFIRM";
  if (ladder4 < BTN_BACK_VAL + BTN_THRESHOLD) return "BACK";

  if (ladder2 < BTN_VOLUME_DOWN_VAL + BTN_THRESHOLD) return "VOLUME_DOWN";
  if (ladder2 < BTN_VOLUME_UP_VAL + BTN_THRESHOLD) return "VOLUME_UP";

  return nullptr;
}

const char *BUTTON_LOG_PATH = "/button_log.csv";

// Dumps every button-press signature logged while the device was running
// unplugged (no Serial available then) - lets the full session be read back
// over USB the next time it's reconnected, instead of relying on someone
// transcribing tiny on-screen text in real time.
void dumpButtonLog() {
  File f = SD.open(BUTTON_LOG_PATH, FILE_READ);
  if (!f) {
    Serial.println("(no button_log.csv yet)");
    return;
  }
  Serial.println("--- button_log.csv ---");
  while (f.available()) {
    Serial.write(f.read());
  }
  Serial.println("--- end of log ---");
  f.close();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("XTE terminal dashboard - flash check");

  pinMode(BTN_LADDER_4, INPUT);
  pinMode(BTN_LADDER_2, INPUT);
  pinMode(BTN_POWER, INPUT_PULLUP);
  pinMode(USB_SENSE_PIN, INPUT);

  SPI.begin(SHARED_SCLK, SHARED_MISO, SHARED_MOSI, EPD_CS);
  SPISettings spiSettings(SHARED_SPI_HZ, MSBFIRST, SPI_MODE0);
  display.init(115200, true, 2, false, SPI, spiSettings);
  display.setRotation(3); // portrait: 480 wide x 800 tall

  String sdStatus = checkSdCard();
  Serial.println(sdStatus);
  dumpButtonLog();

  bool charging = digitalRead(USB_SENSE_PIN) == HIGH;
  uint16_t battPct = battery.readPercentage();
  Serial.printf("Battery: %u%% (raw mV=%u) charging=%s\n", battPct,
                battery.readRawMillivolts(), charging ? "yes" : "no");

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMono9pt7b);
    display.setCursor(20, 60);
    display.print(sdStatus);
    display.setCursor(20, 90);
    display.printf("BATT: %u%% %s", battPct, charging ? "(charging)" : "");
    display.setCursor(20, 120);
    display.print("Press a button to calibrate.");
  } while (display.nextPage());

  // Deliberately not calling display.hibernate() here: this build stays
  // awake in loop() to do live partial refreshes for button calibration.
  // hibernate() powers down the panel controller - calling it here would
  // make every later partial-refresh draw silently do nothing. It belongs
  // right before the real firmware deep-sleeps the whole device, not here.
  Serial.println("Done. Button names will print below as you press them.");
}

// Requires a reading to stay the same for DEBOUNCE_MS before reporting it -
// a real press ramps the ADC through other buttons' threshold windows on
// the way to its settled value, which prints bogus intermediate buttons
// without this.
const unsigned long DEBOUNCE_MS = 120;

// Appends one line unconditionally - caller dedupes. Opening/closing per
// write is wasteful but this only fires on debounced state changes (at
// most a few times a second), not in a hot loop.
void logButtonState(int ladder4, int ladder2, int power, const char *decoded) {
  File f = SD.open(BUTTON_LOG_PATH, FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%d,%d,%d,%s\n", millis(), ladder4, ladder2, power,
           decoded ? decoded : "NONE");
  f.close();
}

void loop() {
  static int lastLadder4 = -1000;
  static int lastLadder2 = -1000;
  static unsigned long stableSince = 0;
  static bool reported = false;
  static int lastLoggedLadder4 = -1000;
  static int lastLoggedLadder2 = -1000;
  static int lastLoggedPower = -1;

  int ladder4 = analogRead(BTN_LADDER_4);
  int ladder2 = analogRead(BTN_LADDER_2);

  bool changed = abs(ladder4 - lastLadder4) > 30 || abs(ladder2 - lastLadder2) > 30;
  if (changed) {
    lastLadder4 = ladder4;
    lastLadder2 = ladder2;
    stableSince = millis();
    reported = false;
  } else if (!reported && millis() - stableSince >= DEBOUNCE_MS) {
    reported = true;
    bool powerPressed = digitalRead(BTN_POWER) == LOW;
    int powerLevel = digitalRead(BTN_POWER);
    const char *decoded = readButtonName();
    Serial.printf("[t=%lums] RAW ladder4=%d ladder2=%d power=%d  -> decoded: %s\n",
                   millis(), ladder4, ladder2, powerLevel,
                   decoded ? decoded : "NONE");

    // Dedupe against the last *logged* signature (not just the last raw
    // reading) so holding a button doesn't write a new line every
    // debounce tick, but every genuinely distinct state - including every
    // release back to idle - still gets one line.
    bool worthLogging = abs(ladder4 - lastLoggedLadder4) > 30 ||
                         abs(ladder2 - lastLoggedLadder2) > 30 ||
                         powerLevel != lastLoggedPower;
    if (worthLogging) {
      logButtonState(ladder4, ladder2, powerLevel, decoded);
      lastLoggedLadder4 = ladder4;
      lastLoggedLadder2 = ladder2;
      lastLoggedPower = powerLevel;
    }

    // Only flash the screen on an actual press (idle also reads as a
    // "stable" state at ladder=4095, but we don't want a refresh every
    // time a button is released).
    if (decoded != nullptr || powerPressed) {
      display.setPartialWindow(0, 150, 480, 140);
      display.firstPage();
      do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeMono9pt7b);
        display.setCursor(20, 180);
        display.printf("ladder4=%d", ladder4);
        display.setCursor(20, 205);
        display.printf("ladder2=%d", ladder2);
        display.setCursor(20, 230);
        display.printf("power=%d", digitalRead(BTN_POWER));
        display.setFont(&FreeMonoBold12pt7b);
        display.setCursor(20, 265);
        display.printf("-> %s", decoded ? decoded : (powerPressed ? "POWER" : "NONE"));
      } while (display.nextPage());
    }
  }
  delay(20);
}
