#include <Wire.h>
#include <WiFi.h>
#include "I2Cdev.h"
#include "MPU6050.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

/* ================= MPU6050 ================= */
MPU6050 mpu;
int16_t ax, ay, az;

/* ================= ALERT ================= */
#define BUZZER_PIN 2
#define VIBRATION_PIN 23   

bool alertActive = false;
unsigned long alertStart = 0;
const unsigned long alertDuration = 15000;
const unsigned long beepInterval = 400;
const unsigned long beepOnTime = 200;

/* ================= THRESHOLD ================= */
float thresholdValue = 1.5;
bool editingThreshold = false;

#define THRESHOLD_MIN 0.5
#define THRESHOLD_MAX 5.0
#define THRESHOLD_STEP 0.1

/* ================= DISPLAY ================= */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_GFX *gfx = &display;

/* ================= BUTTONS ================= */
#define BTN_UP     19
#define BTN_DOWN   17
#define BTN_MID    18

#define HOLD_TIME      800
#define DEBOUNCE_TIME  150

/* ================= DISPLAY SLEEP ================= */
#define DISPLAY_SLEEP_TIME 15000
bool displaySleeping = false;
unsigned long lastActivity = 0;

/* ================= WIFI ================= */
const char* WIFI_SSID = "PLDTHOMEFIBR452d0";
const char* WIFI_PASS = "PLDTWIFIm7enk";

/* ================= MENU ================= */
enum MenuState {
  MENU_MAIN,
  MENU_THRESHOLD,
  MENU_SETTINGS
};

MenuState currentMenu = MENU_MAIN;
int menuIndex = 0;

/* ================= BUTTON STATE ================= */
bool lastUp = HIGH;
bool lastDown = HIGH;
bool lastMid = HIGH;
unsigned long lastDebounce = 0;
unsigned long midPressTime = 0;

/* ================= BITMAPS ================= */
static const unsigned char PROGMEM image_Select_Cursor_bits[] =
{0xc0,0x20,0x20,0x10,0x20,0x20,0xc0};

static const unsigned char PROGMEM image_Wifi_Indicator_bits[] =
{0x3e,0x00,0x41,0x00,0x9c,0x80,0x22,0x00,0x08,0x00};

static const unsigned char PROGMEM image_Decrease_bits[] =
{0x18,0x60,0xc0,0xc0,0x60,0x18};

static const unsigned char PROGMEM image_Increase_bits[] =
{0xc0,0x30,0x18,0x18,0x30,0xc0};

static const unsigned char PROGMEM image_Underline_bits[] =
{0xff,0xff,0xff,0xfc};

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  Wire.setClock(400000);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(VIBRATION_PIN, INPUT);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_MID, INPUT_PULLUP);

  mpu.initialize();

  display.begin(0x3C, true);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  lastActivity = millis();
}

/* ================= LOOP ================= */
void loop() {
  readMotion();
  handleAlert();
  handleButtons();
  handleDisplaySleep();

  if (!displaySleeping) {
    drawMenu();
  }
}

/* ================= MOTION ================= */
void readMotion() {
  mpu.getAcceleration(&ax, &ay, &az);

  float x = ax / 16384.0;
  float y = ay / 16384.0;
  float z = az / 16384.0 - 1.0;

  float magnitude = sqrt(x*x + y*y + z*z);
  bool vibrationDetected = digitalRead(VIBRATION_PIN);

  Serial.println("M: " + String(magnitude));

  if (magnitude > thresholdValue && vibrationDetected && !alertActive) {
    alertActive = true;
    alertStart = millis();
  }
  delay(250);
}

/* ================= ALERT ================= */
void handleAlert() {
  if (!alertActive) {
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  unsigned long elapsed = millis() - alertStart;

  if (elapsed < alertDuration) {
    if ((elapsed % beepInterval) < beepOnTime) {
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    alertActive = false;
  }
}

/* ================= DISPLAY SLEEP ================= */
void handleDisplaySleep() {
  if (!displaySleeping && millis() - lastActivity > DISPLAY_SLEEP_TIME) {
    display.oled_command(SH110X_DISPLAYOFF);
    displaySleeping = true;
  }
}

void wakeDisplay() {
  if (displaySleeping) {
    display.oled_command(SH110X_DISPLAYON);
    displaySleeping = false;
  }
  lastActivity = millis();
}

/* ================= BUTTONS ================= */
void handleButtons() {
  unsigned long now = millis();
  if (now - lastDebounce < DEBOUNCE_TIME) return;

  bool up = digitalRead(BTN_UP);
  bool down = digitalRead(BTN_DOWN);
  bool mid = digitalRead(BTN_MID);

  if (up == LOW || down == LOW || mid == LOW) wakeDisplay();

  if (lastUp == HIGH && up == LOW) {
    if (currentMenu == MENU_THRESHOLD && menuIndex == 1 && editingThreshold) {
      thresholdValue = min(thresholdValue + THRESHOLD_STEP, THRESHOLD_MAX);
    } else {
      menuIndex--;
      constrainMenu();
    }
    lastDebounce = now;
  }

  if (lastDown == HIGH && down == LOW) {
    if (currentMenu == MENU_THRESHOLD && menuIndex == 1 && editingThreshold) {
      thresholdValue = max(thresholdValue - THRESHOLD_STEP, THRESHOLD_MIN);
    } else {
      menuIndex++;
      constrainMenu();
    }
    lastDebounce = now;
  }

  if (lastMid == HIGH && mid == LOW) midPressTime = now;

  if (lastMid == LOW && mid == HIGH) {
    if (now - midPressTime >= HOLD_TIME) {
      currentMenu = MENU_MAIN;
      menuIndex = 0;
      editingThreshold = false;
    } else {
      handleSelect();
    }
    lastDebounce = now;
  }

  lastUp = up;
  lastDown = down;
  lastMid = mid;
}

/* ================= MENU ================= */
void constrainMenu() {
  int maxItems = 3;
  if (currentMenu == MENU_THRESHOLD) maxItems = 2;
  if (menuIndex < 0) menuIndex = 0;
  if (menuIndex >= maxItems) menuIndex = maxItems - 1;
}

void handleSelect() {
  if (currentMenu == MENU_MAIN) {
    if (menuIndex == 0) {            // Sleep selected
      display.oled_command(SH110X_DISPLAYOFF);
      displaySleeping = true;
    }
    else if (menuIndex == 1) {       // Threshold
      currentMenu = MENU_THRESHOLD;
      menuIndex = 0;
    }
    else if (menuIndex == 2) {       // Settings
      currentMenu = MENU_SETTINGS;
      menuIndex = 0;
    }
  }
  else if (currentMenu == MENU_THRESHOLD) {
    if (menuIndex == 1) {
      editingThreshold = !editingThreshold;
    }
  }
}

/* ================= DRAW ================= */
void drawMenu() {
  display.clearDisplay();

  if (currentMenu == MENU_MAIN) {
    drawMainMenu();
    drawCursor(14 + menuIndex * 12);
  }

  if (currentMenu == MENU_THRESHOLD) {
    drawThresholdMenu();
    drawCursor(14 + menuIndex * 12);
  }

  display.display();
}

void drawCursor(uint8_t y) {
  gfx->drawBitmap(2, y, image_Select_Cursor_bits, 4, 7, SH110X_WHITE);
}

void drawWifiIcon() {
  if (WiFi.status() == WL_CONNECTED) {
    gfx->drawBitmap(117, 57, image_Wifi_Indicator_bits, 9, 5, SH110X_WHITE);
  }
}

void drawMainMenu() {
  drawWifiIcon();
  gfx->setCursor(2, 56); gfx->println("SeismoSense");
  gfx->drawLine(0, 54, 127, 54, SH110X_WHITE);
  gfx->drawLine(0, 10, 127, 10, SH110X_WHITE);
  gfx->setCursor(8, 14); gfx->println("Sleep");
  gfx->setCursor(8, 26); gfx->println("Threshold");
  gfx->setCursor(8, 38); gfx->println("Settings");
  gfx->setCursor(2, 1);  gfx->println("Menu");
}

void drawThresholdMenu() {
  drawWifiIcon();
  gfx->setCursor(2, 56); gfx->println("SeismoSense");
  gfx->drawLine(0, 54, 127, 54, SH110X_WHITE);
  gfx->drawLine(0, 10, 127, 10, SH110X_WHITE);

  gfx->setCursor(8, 14); gfx->println("Default (1.5)");
  gfx->setCursor(8, 26); gfx->println("Custom");

  gfx->drawBitmap(21, 38, image_Decrease_bits, 5, 6, SH110X_WHITE);
  gfx->drawBitmap(29, 44, image_Underline_bits, 30, 1, SH110X_WHITE);
  gfx->drawBitmap(62, 38, image_Increase_bits, 5, 6, SH110X_WHITE);

  gfx->setCursor(35, 36); gfx->print(thresholdValue, 1);
  gfx->setCursor(2, 1);  gfx->println("Threshold");
}
