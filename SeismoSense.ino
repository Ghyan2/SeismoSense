#include <Wire.h>
#include "I2Cdev.h"
#include "MPU6050.h"

MPU6050 mpu;

const int buzzer = 23;
const float threshold = 1.5;
int16_t ax, ay, az;

const int vibration = 19;  
unsigned long alertStart = 0;
bool alertActive = false;
const unsigned long alertDuration = 15000; // 15 seconds
const unsigned long beepInterval = 400; // total beep cycle (ms)
const unsigned long beepOnTime = 200; // buzzer ON duration (ms)

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  pinMode(buzzer, OUTPUT);
  mpu.initialize();

  Serial.println("MPU6050 connected");
}

void loop() {
  mpu.getAcceleration(&ax, &ay, &az);

  float x = ax / 16384.0;
  float y = ay / 16384.0;
  float z = az / 16384.0 - 1.0;

  float magnitude = abs(sqrt((x * x) + (y * y) + (z * z)));

  bool vibrationDetected = digitalRead(vibration) == HIGH;

  Serial.println("X: "+ String(x) +" Y: "+ String(y) +" Z: "+ String(z) +" | M: "+ String(magnitude));
  delay(100);

  if ((magnitude > threshold && vibrationDetected) && !alertActive) {
    alertActive = true;
    alertStart = millis();
  }

  // Handle alert pattern
  if (alertActive) {
    unsigned long elapsed = millis() - alertStart;
    if (elapsed < alertDuration) {
      // pattern: 200ms ON, 200ms OFF
      if ((elapsed % beepInterval) < beepOnTime) {
        digitalWrite(buzzer, HIGH);
      } else {
        digitalWrite(buzzer, LOW);
      }
    } else {
      digitalWrite(buzzer, LOW);
      alertActive = false;
    }
  } else {
    digitalWrite(buzzer, LOW);
  }
}
