#include <Wire.h>
#include <MPU6050.h>
#include <math.h>

MPU6050 mpu;

// Pins
#define SW420_PIN 19
#define BUZZER_PIN 23
#define LED_PIN 25

// Sampling
#define SAMPLE_RATE 100

// STA/LTA parameters
#define STA_WINDOW 30
#define LTA_WINDOW 300
#define RATIO_THRESHOLD 3.0
#define MIN_LTA 0.002
#define MIN_SIGNAL 0.0001

// Magnitude parameters
#define MAG_THRESHOLD 1.5

#define REQUIRED_TRIGGERS 1
#define COOLDOWN_TIME 10000
#define ALERT_DURATION 5000

// Detection options
bool useMagnitudeDetection = true; // true = magnitude-based, false = STA/LTA-based
bool useSW420 = true;              // true = consider SW420 input, false = ignore it

float staBuffer[STA_WINDOW];
float ltaBuffer[LTA_WINDOW];
float staSum = 0;
float ltaSum = 0;
int staIndex = 0;
int ltaIndex = 0;
int triggerCount = 0;

unsigned long lastSampleTime = 0;
unsigned long lastEventTime = 0;
unsigned long alertStart = 0;
bool alertActive = false;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  mpu.initialize();

  pinMode(SW420_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  for (int i = 0; i < STA_WINDOW; i++) staBuffer[i] = 0;
  for (int i = 0; i < LTA_WINDOW; i++) ltaBuffer[i] = 0;
}

void loop() {
  if (millis() - lastSampleTime >= (1000 / SAMPLE_RATE)) {
    lastSampleTime = millis();

    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);

    float accelX = ax / 16384.0;
    float accelY = ay / 16384.0;
    float accelZ = az / 16384.0;
    float magnitude = sqrt(accelX*accelX + accelY*accelY + accelZ*accelZ);

    int swState = digitalRead(SW420_PIN);

    // If SW420 is disabled, override the state to HIGH so it does not block detection
    if (!useSW420) swState = HIGH;

    if (useMagnitudeDetection) {
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
      detectSTAEvent(ratio, signal, swState, magnitude, sta, lta);
    }
  }

  handleAlert();
}

// ---------------- Magnitude-based detection ----------------
void detectMagnitudeEvent(float magnitude, int swState) {
  if (millis() - lastEventTime < COOLDOWN_TIME) return;

  if (magnitude > MAG_THRESHOLD && swState == HIGH) {
    triggerCount++;
    if (triggerCount >= REQUIRED_TRIGGERS) {
      startAlert(magnitude);
      triggerCount = 0;
    }
  } else {
    triggerCount = 0;
  }
}

// ---------------- STA/LTA detection ----------------
void updateSTA(float value) {
  staSum -= staBuffer[staIndex];
  staBuffer[staIndex] = value;
  staSum += value;
  staIndex++;
  if (staIndex >= STA_WINDOW) staIndex = 0;
}

void updateLTA(float value) {
  ltaSum -= ltaBuffer[ltaIndex];
  ltaBuffer[ltaIndex] = value;
  ltaSum += value;
  ltaIndex++;
  if (ltaIndex >= LTA_WINDOW) ltaIndex = 0;
}

void detectSTAEvent(float ratio, float signal, int swState,
                    float magnitude, float sta, float lta) {
  if (millis() - lastEventTime < COOLDOWN_TIME) return;

  if (ratio > RATIO_THRESHOLD && signal > MIN_SIGNAL && swState == HIGH) {
    triggerCount++;
    if (triggerCount >= REQUIRED_TRIGGERS) {
      startAlert(magnitude);
      triggerCount = 0;
    }
  } else {
    triggerCount = 0;
  }
}

// ---------------- Alert handling ----------------
void startAlert(float magnitude) {
  alertActive = true;
  alertStart = millis();
  lastEventTime = millis();

  Serial.println("EVENT DETECTED");
  Serial.print("Magnitude: "); Serial.println(magnitude, 3);
  Serial.print("Count: "); Serial.println(REQUIRED_TRIGGERS);
  Serial.print("Alert: "); Serial.println(alertActive);
  Serial.println("----------------------------");
}

void handleAlert() {
  if (!alertActive) return;

  digitalWrite(LED_PIN, HIGH);
  if ((millis() / 200) % 2 == 0) digitalWrite(BUZZER_PIN, HIGH);
  else digitalWrite(BUZZER_PIN, LOW);

  if (millis() - alertStart > ALERT_DURATION) {
    alertActive = false;
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
  }
}
