#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include "I2Cdev.h"
#include "MPU6050.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

/* ===================== Hardware ===================== */
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

#define SW420_PIN 19
#define BUZZER_PIN 23
#define LED_PIN 25

#define BUTTON_LEFT 12
#define BUTTON_MID  13
#define BUTTON_RIGHT 14

/* ===================== Display ===================== */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_GFX *gfx = &display;

static const unsigned char PROGMEM image_Select_Cursor_bits[] =
{0xc0,0x20,0x20,0x10,0x20,0x20,0xc0};

static const unsigned char PROGMEM image_Wifi_Indicator_bits[] =
{0x3e,0x00,0x41,0x00,0x9c,0x80,0x22,0x00,0x08,0x00};

static const unsigned char PROGMEM image_Layer_10_bits[] =
{0xff,0xff,0xff,0xc0};

static const unsigned char PROGMEM image_Layer_11_bits[] =
{0x18,0x70,0xc0,0xc0,0x70,0x18};

static const unsigned char PROGMEM image_Layer_11_copy_1_bits[] =
{0xc0,0x70,0x18,0x18,0x70,0xc0};

/* ===================== Wi-Fi / Cloud ===================== */
const char* WIFI_SSID = "PLDTHOMEFIBR452d0";
const char* WIFI_PASS = "PLDTWIFIm7enk";

const char* SUPABASE_FUNCTION_URL =
  "https://saybcjfzpipmrhipllqd.supabase.co/functions/v1/esp32-api";
const char* DEVICE_API_KEY = "fe1cf60d-d894-4f4e-b0cc-6e518f080294";
const unsigned long FETCH_INTERVAL_MS = 10000;

WiFiClientSecure secureClient;
const int MAX_ACK_IDS = 10;
const int MAX_REGISTERED_NUMBERS = 10;
String ackIds[MAX_ACK_IDS];
int ackCount = 0;
String registeredNumbers[MAX_REGISTERED_NUMBERS];
int registeredNumberCount = 0;
unsigned long lastCloudTick = 0;
bool smsAlertsEnabled = true;

/* ===================== Detection ===================== */
MPU6050 mpu;

enum DetectionMode { MAGNITUDE, STA_LTA };
DetectionMode currentMode = MAGNITUDE;

const int SAMPLE_RATE = 100;
const int STA_WINDOW = 30;
const int LTA_WINDOW = 300;
const float MIN_LTA = 0.002;
const float MIN_SIGNAL = 0.0001;
const int REQUIRED_TRIGGERS = 1;
const unsigned long COOLDOWN_TIME = 10000;
const unsigned long ALERT_DURATION = 5000;

float magnitudeThreshold = 1.5;
float ratioThreshold = 3.0;
bool useSW420 = true;

float staBuffer[STA_WINDOW];
float ltaBuffer[LTA_WINDOW];
float staSum = 0;
float ltaSum = 0;
int staIndex = 0;
int ltaIndex = 0;
int triggerCount = 0;

float lastMagnitude = 0;
float lastRatio = 0;
float lastSta = 0;
float lastLta = 0;

unsigned long lastSampleTime = 0;
unsigned long lastEventTime = 0;
unsigned long alertStart = 0;
bool alertActive = false;
bool eventToReport = false;
bool displaySleeping = false;

/* ===================== Menu ===================== */
enum MenuState { MAIN_MENU, THRESHOLD_MENU };
MenuState currentMenu = MAIN_MENU;
int cursorPosition = 0;

bool prevLeft = HIGH;
bool prevMid = HIGH;
bool prevRight = HIGH;
unsigned long midPressStart = 0;
const unsigned long LONG_PRESS_MS = 700;
const unsigned long BUTTON_DEBOUNCE_MS = 180;
bool editingThreshold = false;
unsigned long lastLeftPressMs = 0;
unsigned long lastMidPressMs = 0;
unsigned long lastRightPressMs = 0;

/* ===================== Helpers ===================== */
void ensureWiFi();
bool sendHttp(const char* method, const String& body, int& code, String& resp);
bool postEventTelemetry(float magnitude, unsigned long eventTimestamp);
void pollDownlink();
void acknowledgeCommands();
bool sendSmsToNumber(const String& number, const String& message);
void sendSmsAlertsToRegisteredNumbers(float magnitude, unsigned long eventTimestamp);

void handleButtons();
void drawMainMenu();
void drawThresholdMenu();
void drawStatusBar();
void drawEventScreen();

void updateSTA(float value);
void updateLTA(float value);
void detectMagnitudeEvent(float magnitude, int swState);
void detectSTAEvent(float ratio, float signal, int swState, float magnitude);
void startAlert(float magnitude);
void handleAlert();
void sampleSensors();

void setup() {
  Serial.begin(115200);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);

  mpu.initialize();

  pinMode(SW420_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_LEFT, INPUT_PULLUP);
  pinMode(BUTTON_MID, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  for (int i = 0; i < STA_WINDOW; i++) staBuffer[i] = 0;
  for (int i = 0; i < LTA_WINDOW; i++) ltaBuffer[i] = 0;

  display.begin(0x3C, true);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.display();

  secureClient.setInsecure();
  ensureWiFi();
}

void loop() {
  handleButtons();
  sampleSensors();
  handleAlert();

  if (!displaySleeping || alertActive) {
    if (alertActive) drawEventScreen();
    else if (currentMenu == MAIN_MENU) drawMainMenu();
    else drawThresholdMenu();
    display.display();
  }

  ensureWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();

    if (now - lastCloudTick >= FETCH_INTERVAL_MS) {
      lastCloudTick = now;
      pollDownlink();
      acknowledgeCommands();
    }

    if (eventToReport) {
      if (postEventTelemetry(lastMagnitude, lastEventTime)) {
        sendSmsAlertsToRegisteredNumbers(lastMagnitude, lastEventTime);
        eventToReport = false;
      }
    }
  }

  delay(25);
}

/* ===================== UI ===================== */
void drawStatusBar() {
  if (WiFi.status() == WL_CONNECTED) {
    gfx->drawBitmap(117, 57, image_Wifi_Indicator_bits, 9, 5, SH110X_WHITE);
  }
  gfx->drawLine(0, 10, 127, 10, SH110X_WHITE);
  gfx->drawLine(0, 54, 127, 54, SH110X_WHITE);
  gfx->setCursor(2, 56);
  gfx->println("SeismoSense");
}

void drawMainMenu() {
  display.clearDisplay();
  gfx->setTextWrap(false);
  gfx->setCursor(2, 1);
  gfx->println("Menu");

  drawStatusBar();
  gfx->drawBitmap(2, 14 + cursorPosition * 12, image_Select_Cursor_bits, 4, 7, SH110X_WHITE);

  gfx->setCursor(8, 14);
  gfx->println("Sleep");
  gfx->setCursor(8, 26);
  gfx->println("Threshold");

  gfx->setCursor(8, 40);
  gfx->print("Mag:");
  gfx->print(lastMagnitude, 2);
}

void drawThresholdMenu() {
  display.clearDisplay();
  gfx->setTextColor(SH110X_WHITE);
  gfx->setTextWrap(false);
  gfx->setCursor(2, 2);
  gfx->println("Threshold");

  drawStatusBar();
  gfx->drawBitmap(2, 14 + cursorPosition * 10, image_Select_Cursor_bits, 4, 7, SH110X_WHITE);

  gfx->setCursor(8, 14);
  gfx->println((currentMode == MAGNITUDE) ? "Mode 1 (Mag. Based)" : "Mode 2 (STA/LTA)");

  gfx->setCursor(8, 24);
  gfx->print("SW-420 (");
  gfx->print(useSW420 ? "On" : "Off");
  gfx->println(")");

  gfx->setCursor(8, 34);
  gfx->println("Threshold Val.");

  gfx->setCursor(27, 44);
  if (currentMode == MAGNITUDE) gfx->println(magnitudeThreshold, 1);
  else gfx->println(ratioThreshold, 1);

  if (cursorPosition == 2 && editingThreshold) {
    gfx->setCursor(60, 44);
    gfx->print("*");
  }

  gfx->drawBitmap(23, 52, image_Layer_10_bits, 26, 1, SH110X_WHITE);
  gfx->drawBitmap(16, 45, image_Layer_11_bits, 5, 6, SH110X_WHITE);
  gfx->drawBitmap(51, 45, image_Layer_11_copy_1_bits, 5, 6, SH110X_WHITE);
}

void drawEventScreen() {
  display.clearDisplay();
  gfx->setTextWrap(false);
  gfx->setCursor(2, 2);
  gfx->println("SEISMIC EVENT");

  drawStatusBar();
  gfx->setCursor(8, 24);
  gfx->print("Magnitude:");
  gfx->setCursor(8, 36);
  gfx->print(lastMagnitude, 3);
}

void handleButtons() {
  bool left = digitalRead(BUTTON_LEFT);
  bool mid = digitalRead(BUTTON_MID);
  bool right = digitalRead(BUTTON_RIGHT);
  unsigned long now = millis();

  if (left == LOW && prevLeft == HIGH && (now - lastLeftPressMs) >= BUTTON_DEBOUNCE_MS) {
    lastLeftPressMs = now;
    if (currentMenu == THRESHOLD_MENU && cursorPosition == 2 && editingThreshold) {
      if (currentMode == MAGNITUDE) {
        magnitudeThreshold -= 0.1;
        if (magnitudeThreshold < 1.0) magnitudeThreshold = 5.0;
      } else {
        ratioThreshold -= 0.1;
        if (ratioThreshold < 1.0) ratioThreshold = 5.0;
      }
    } else {
      cursorPosition = max(cursorPosition - 1, 0);
    }
  }

  if (right == LOW && prevRight == HIGH && (now - lastRightPressMs) >= BUTTON_DEBOUNCE_MS) {
    lastRightPressMs = now;
    if (currentMenu == THRESHOLD_MENU && cursorPosition == 2 && editingThreshold) {
      if (currentMode == MAGNITUDE) {
        magnitudeThreshold += 0.1;
        if (magnitudeThreshold > 5.0) magnitudeThreshold = 1.0;
      } else {
        ratioThreshold += 0.1;
        if (ratioThreshold > 5.0) ratioThreshold = 1.0;
      }
    } else {
      int maxCursor = (currentMenu == MAIN_MENU) ? 1 : 2;
      cursorPosition = min(cursorPosition + 1, maxCursor);
    }
  }

  if (mid == LOW && prevMid == HIGH && (now - lastMidPressMs) >= BUTTON_DEBOUNCE_MS) {
    midPressStart = now;
    lastMidPressMs = now;
  }

  if (mid == HIGH && prevMid == LOW && (now - lastMidPressMs) >= BUTTON_DEBOUNCE_MS) {
    bool isLong = (now - midPressStart) >= LONG_PRESS_MS;
    lastMidPressMs = now;

    if (isLong) {
      currentMenu = MAIN_MENU;
      cursorPosition = 0;
      editingThreshold = false;
    } else {
      if (currentMenu == MAIN_MENU) {
        if (cursorPosition == 0) {
          displaySleeping = !displaySleeping;
          display.oled_command(displaySleeping ? SH110X_DISPLAYOFF : SH110X_DISPLAYON);
        } else if (cursorPosition == 1) {
          currentMenu = THRESHOLD_MENU;
          cursorPosition = 0;
          editingThreshold = false;
        }
      } else if (currentMenu == THRESHOLD_MENU) {
        if (cursorPosition == 0) {
          currentMode = (currentMode == MAGNITUDE) ? STA_LTA : MAGNITUDE;
          editingThreshold = false;
        } else if (cursorPosition == 1) {
          useSW420 = !useSW420;
          editingThreshold = false;
        } else if (cursorPosition == 2) {
          editingThreshold = !editingThreshold;
        }
      }
    }
  }

  prevLeft = left;
  prevMid = mid;
  prevRight = right;
}

/* ===================== Detection ===================== */
void sampleSensors() {
  if (millis() - lastSampleTime < (1000 / SAMPLE_RATE)) return;
  lastSampleTime = millis();

  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);

  float accelX = ax / 16384.0;
  float accelY = ay / 16384.0;
  float accelZ = az / 16384.0;
  float magnitude = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
  lastMagnitude = magnitude;

  int swState = digitalRead(SW420_PIN);
  if (!useSW420) swState = HIGH;

  if (currentMode == MAGNITUDE) {
    detectMagnitudeEvent(magnitude, swState);
  } else {
    float signal = pow(magnitude - 1.0, 2);
    updateSTA(signal);
    updateLTA(signal);

    float sta = staSum / STA_WINDOW;
    float lta = ltaSum / LTA_WINDOW;
    float ratio = 0;
    if (lta > MIN_LTA) {
      ratio = (sta / lta) - 1.0;
      if (ratio < 0) ratio = 0;
    }

    lastSta = sta;
    lastLta = lta;
    lastRatio = ratio;

    detectSTAEvent(ratio, signal, swState, magnitude);
  }
}

void detectMagnitudeEvent(float magnitude, int swState) {
  if (millis() - lastEventTime < COOLDOWN_TIME) return;

  if (magnitude > magnitudeThreshold && swState == HIGH) {
    triggerCount++;
    if (triggerCount >= REQUIRED_TRIGGERS) {
      startAlert(magnitude);
      triggerCount = 0;
    }
  } else {
    triggerCount = 0;
  }
}

void updateSTA(float value) {
  staSum -= staBuffer[staIndex];
  staBuffer[staIndex] = value;
  staSum += value;
  staIndex = (staIndex + 1) % STA_WINDOW;
}

void updateLTA(float value) {
  ltaSum -= ltaBuffer[ltaIndex];
  ltaBuffer[ltaIndex] = value;
  ltaSum += value;
  ltaIndex = (ltaIndex + 1) % LTA_WINDOW;
}

void detectSTAEvent(float ratio, float signal, int swState, float magnitude) {
  if (millis() - lastEventTime < COOLDOWN_TIME) return;

  if (ratio > ratioThreshold && signal > MIN_SIGNAL && swState == HIGH) {
    triggerCount++;
    if (triggerCount >= REQUIRED_TRIGGERS) {
      startAlert(magnitude);
      triggerCount = 0;
    }
  } else {
    triggerCount = 0;
  }
}

void startAlert(float magnitude) {
  if (displaySleeping) {
    displaySleeping = false;
    display.oled_command(SH110X_DISPLAYON);
  }

  lastMagnitude = magnitude;
  alertActive = true;
  alertStart = millis();
  lastEventTime = millis();
  eventToReport = true;

  Serial.println("EVENT DETECTED");
  Serial.print("Magnitude: ");
  Serial.println(magnitude, 3);
  Serial.print("Mode: ");
  Serial.println((currentMode == MAGNITUDE) ? "Magnitude" : "STA/LTA");
  Serial.println("----------------------------");
}

void handleAlert() {
  if (!alertActive) {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    return;
  }

  digitalWrite(LED_PIN, HIGH);
  digitalWrite(BUZZER_PIN, ((millis() / 200) % 2 == 0) ? HIGH : LOW);

  if (millis() - alertStart > ALERT_DURATION) {
    alertActive = false;
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
  }
}


bool sendSmsToNumber(const String& number, const String& message) {
  // TODO: replace this stub with your GSMsms.h integration.
  // Example target call: gsmSms.sendSMS(number.c_str(), message.c_str());
  if (number.length() == 0) return false;

  Serial.printf("[SMS] to=%s msg=%s\n", number.c_str(), message.c_str());
  return true;
}

void sendSmsAlertsToRegisteredNumbers(float magnitude, unsigned long eventTimestamp) {
  if (!smsAlertsEnabled || registeredNumberCount == 0) return;

  String message = "SeismoSense Alert M=" + String(magnitude, 2) +
                   " T=" + String(eventTimestamp);

  for (int i = 0; i < registeredNumberCount; i++) {
    sendSmsToNumber(registeredNumbers[i], message);
  }
}

/* ===================== Cloud ===================== */
void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    tries++;
  }
}

bool sendHttp(const char* method, const String& body, int& code, String& resp) {
  if (WiFi.status() != WL_CONNECTED) {
    code = -1;
    resp = "";
    return false;
  }

  HTTPClient http;
  http.begin(secureClient, SUPABASE_FUNCTION_URL);
  http.addHeader("x-api-key", DEVICE_API_KEY);

  if (String(method) != "GET") {
    http.addHeader("Content-Type", "application/json");
  }

  if (String(method) == "POST") code = http.POST(body);
  else if (String(method) == "GET") code = http.GET();
  else if (String(method) == "PATCH") code = http.sendRequest("PATCH", body);
  else {
    http.end();
    return false;
  }

  resp = http.getString();
  http.end();
  return true;
}

bool postEventTelemetry(float magnitude, unsigned long eventTimestamp) {
  if (WiFi.status() != WL_CONNECTED) return false;

  StaticJsonDocument<256> doc;
  doc["magnitude"] = magnitude;
  doc["timestamp"] = eventTimestamp;

  String body;
  serializeJson(doc, body);

  int code = 0;
  String resp;
  bool ok = sendHttp("POST", body, code, resp);

  Serial.printf("[POST event] code=%d\n", code);
  return ok && code >= 200 && code < 300;
}

void pollDownlink() {
  if (WiFi.status() != WL_CONNECTED) return;

  int code = 0;
  String resp;
  bool ok = sendHttp("GET", "", code, resp);
  if (!ok || code != 200) return;

  DynamicJsonDocument doc(4096);
  auto err = deserializeJson(doc, resp);
  if (err) return;

  JsonArray nums = doc["phone_numbers"].as<JsonArray>();
  if (!nums.isNull()) {
    registeredNumberCount = 0;
    for (JsonVariant numVar : nums) {
      if (registeredNumberCount >= MAX_REGISTERED_NUMBERS) break;

      const char* number = nullptr;
      if (numVar.is<const char*>()) number = numVar.as<const char*>();
      if (!number || strlen(number) == 0) number = numVar["phone_number"] | "";
      if (!number || strlen(number) == 0) number = numVar["number"] | "";
      if (!number || strlen(number) == 0) number = numVar["value"] | "";

      if (number && strlen(number) > 0) {
        registeredNumbers[registeredNumberCount++] = String(number);
      }
    }
  }

  ackCount = 0;
  JsonArray cmds = doc["pending_commands"].as<JsonArray>();
  if (cmds.isNull()) return;

  for (JsonObject cmd : cmds) {
    const char* id = cmd["id"] | "";
    const char* type = cmd["command_type"] | "";

    if (strcmp(type, "set_mode") == 0) {
      const char* value = cmd["payload"]["value"] | "magnitude";
      currentMode = (String(value) == "sta_lta") ? STA_LTA : MAGNITUDE;
    } else if (strcmp(type, "set_threshold") == 0) {
      float value = cmd["payload"]["value"] | 1.5;
      if (currentMode == MAGNITUDE) {
        if (value < 1.0) value = 1.0;
        if (value > 5.0) value = 5.0;
        magnitudeThreshold = value;
      } else {
        if (value < 1.0) value = 1.0;
        if (value > 5.0) value = 5.0;
        ratioThreshold = value;
      }
    }

    if (strlen(id) > 0 && ackCount < MAX_ACK_IDS) ackIds[ackCount++] = String(id);
  }
}

void acknowledgeCommands() {
  if (ackCount == 0 || WiFi.status() != WL_CONNECTED) return;

  StaticJsonDocument<512> doc;
  JsonArray arr = doc.createNestedArray("acknowledged_command_ids");
  for (int i = 0; i < ackCount; i++) arr.add(ackIds[i]);

  String body;
  serializeJson(doc, body);

  int code = 0;
  String resp;
  bool ok = sendHttp("PATCH", body, code, resp);

  if (ok && code == 200) ackCount = 0;
}
