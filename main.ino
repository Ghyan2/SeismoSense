#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Buttons
#define BTN_UP     19
#define BTN_DOWN   18
#define BTN_SELECT 17

#define HOLD_TIME 800

// Screen states
enum ScreenState {
  MAIN_MENU,
  THRESHOLD_MENU,
  INTERVAL_MENU
};

ScreenState currentScreen = MAIN_MENU;

// Indexes
int mainIndex = 0;
int subIndex = 0;
bool editMode = false;

// Values
int thresholdValue = 5;
int intervalValue = 1500;

// Y positions
const uint8_t mainMenuY[4] = {12, 23, 34, 45};
const uint8_t subMenuY[3]  = {12, 22, 44};

// Bitmaps
static const unsigned char PROGMEM image_Select_Arrow_bits[] = {
  0x80,0xC0,0xA0,0x90,0xA0,0xC0,0x80
};

static const unsigned char PROGMEM image_L_Arrow_bits[] = {
  0x20,0x40,0x80,0x40,0x20
};

static const unsigned char PROGMEM image_R_Arrow_bits[] = {
  0x80,0x40,0x20,0x40,0x80
};

static const unsigned char PROGMEM image_Underline_bits[] = {
  0xFF,0xFF,0xFF,0xFF,0xFE
};

static const unsigned char PROGMEM image_Wifi_Display_bits[] = {
  0x3E,0x00,0x41,0x00,0x9C,0x80,0x22,0x00,0x08,0x00
};

void beginText() {
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setTextWrap(false);
}

void drawCommon() {
  display.drawLine(0, 9, 127, 9, SH110X_WHITE);
  display.drawLine(0, 54, 127, 54, SH110X_WHITE);
  display.setCursor(2, 56);
  display.println("SeismoSense");
  display.drawBitmap(117, 57, image_Wifi_Display_bits, 9, 5, SH110X_WHITE);
}

void drawMainMenu() {
  display.clearDisplay();
  beginText();
  drawCommon();

  display.setCursor(10, 12); display.println("Sleep");
  display.setCursor(10, 23); display.println("Threshold");
  display.setCursor(10, 34); display.println("Time Interval");
  display.setCursor(10, 45); display.println("WiFi Connectivity");

  display.drawBitmap(3, mainMenuY[mainIndex],
                     image_Select_Arrow_bits, 4, 7, SH110X_WHITE);

  display.display();
}

void drawThresholdMenu() {
  display.clearDisplay();
  beginText();
  drawCommon();

  display.setCursor(3, 1);
  display.println("Threshold");

  display.setCursor(9, 12); display.println("Default (2)");
  display.setCursor(9, 22); display.println("Custom");
  display.setCursor(9, 44); display.println("Back");

  display.setCursor(26, 32);
  display.println(thresholdValue);

  display.drawBitmap(23, 40, image_Underline_bits, 11, 1, SH110X_WHITE);
  display.drawBitmap(16, 34, image_L_Arrow_bits, 3, 5, SH110X_WHITE);
  display.drawBitmap(38, 34, image_R_Arrow_bits, 3, 5, SH110X_WHITE);

  display.drawBitmap(3, subMenuY[subIndex],
                     image_Select_Arrow_bits, 4, 7, SH110X_WHITE);

  display.display();
}

void drawIntervalMenu() {
  display.clearDisplay();
  beginText();
  drawCommon();

  display.setCursor(3, 1);
  display.println("Time Interval");

  display.setCursor(9, 12); display.println("Default (1000ms)");
  display.setCursor(9, 22); display.println("Custom");
  display.setCursor(9, 44); display.println("Back");

  display.setCursor(24, 32);
  display.print(intervalValue);
  display.println("ms");

  display.drawBitmap(22, 40, image_Underline_bits, 39, 1, SH110X_WHITE);
  display.drawBitmap(16, 34, image_L_Arrow_bits, 3, 5, SH110X_WHITE);
  display.drawBitmap(64, 34, image_R_Arrow_bits, 3, 5, SH110X_WHITE);

  display.drawBitmap(3, subMenuY[subIndex],
                     image_Select_Arrow_bits, 4, 7, SH110X_WHITE);

  display.display();
}

void handleButtons() {
  static uint8_t lastUp = HIGH, lastDown = HIGH, lastSel = HIGH;
  static unsigned long selTime = 0;
  static bool selHeld = false;

  uint8_t up = digitalRead(BTN_UP);
  uint8_t down = digitalRead(BTN_DOWN);
  uint8_t sel = digitalRead(BTN_SELECT);

  if (lastUp == HIGH && up == LOW && !editMode) {
    if (currentScreen == MAIN_MENU) mainIndex = (mainIndex + 3) % 4;
    else subIndex = (subIndex + 2) % 3;
  }

  if (lastDown == HIGH && down == LOW && !editMode) {
    if (currentScreen == MAIN_MENU) mainIndex = (mainIndex + 1) % 4;
    else subIndex = (subIndex + 1) % 3;
  }

  if (editMode) {
    if (lastUp == HIGH && up == LOW) {
      if (currentScreen == THRESHOLD_MENU) thresholdValue++;
      if (currentScreen == INTERVAL_MENU) intervalValue += 100;
    }
    if (lastDown == HIGH && down == LOW) {
      if (currentScreen == THRESHOLD_MENU && thresholdValue > 1) thresholdValue--;
      if (currentScreen == INTERVAL_MENU && intervalValue > 100) intervalValue -= 100;
    }
  }

  if (lastSel == HIGH && sel == LOW) {
    selTime = millis();
    selHeld = false;
  }

  if (sel == LOW && !selHeld && millis() - selTime >= HOLD_TIME) {
    selHeld = true;
    editMode = false;
    currentScreen = MAIN_MENU;
    subIndex = 0;
  }

  if (lastSel == LOW && sel == HIGH && !selHeld) {
    if (currentScreen == MAIN_MENU) {
      if (mainIndex == 1) { currentScreen = THRESHOLD_MENU; subIndex = 0; }
      if (mainIndex == 2) { currentScreen = INTERVAL_MENU; subIndex = 0; }
    } else {
      if (subIndex == 1) editMode = !editMode;
      if (subIndex == 2) currentScreen = MAIN_MENU;
    }
  }

  lastUp = up;
  lastDown = down;
  lastSel = sel;
}

void setup() {
  Wire.begin(21, 22);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  display.begin(0x3C, true);
}

void loop() {
  handleButtons();

  if (currentScreen == MAIN_MENU) drawMainMenu();
  if (currentScreen == THRESHOLD_MENU) drawThresholdMenu();
  if (currentScreen == INTERVAL_MENU) drawIntervalMenu();
}
