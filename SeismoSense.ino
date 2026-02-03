#include <Wire.h>
#include <MPU9250_WE.h>

#define SW420 23

MPU9250_WE mpu(0x68);

// CONFIGURATION
float threshold = 0.25;              // delta-motion threshold (g)
unsigned long interval = 20;         // 50 Hz sampling
unsigned long debounceTime = 800;    // SW420 debounce

unsigned long lastRead = 0;
unsigned long lastTrigger = 0;

float lastResultant = 1.0;

void setup() {
  Wire.begin();
  Serial.begin(115200);
  pinMode(SW420, INPUT);

  Serial.println("Calibrating...");
  delay(1000);
  mpu.autoOffsets();

  // === KEY SETTINGS FOR VIBRATION ===
  mpu.setAccRange(MPU9250_ACC_RANGE_2G);
  mpu.enableAccDLPF(true);
  mpu.setAccDLPF(MPU9250_DLPF_1);     // LOW DLPF → vibration allowed
  mpu.setSampleRateDivider(0);       // maximum sample rate

  Serial.println("System ready.");
}

void loop() {
  if (millis() - lastRead >= interval) {
    lastRead = millis();

    // Get acceleration in g
    xyzFloat gVal = mpu.getGValues();

    // Resultant acceleration
    float resultantG = mpu.getResultantG(gVal);

    // DELTA-BASED MOTION (key fix)
    float deltaMotion = fabs(resultantG - lastResultant);
    lastResultant = resultantG;

    // Earthquake detection
    if (digitalRead(SW420) == HIGH && deltaMotion > threshold) {
      if (millis() - lastTrigger > debounceTime) {
        Serial.println("EQ OCCURRED");
        lastTrigger = millis();
      }
    }

    // Debug output
    Serial.print("X: "); Serial.print(gVal.x, 3);
    Serial.print(" | Y: "); Serial.print(gVal.y, 3);
    Serial.print(" | Z: "); Serial.print(gVal.z, 3);
    Serial.print(" | ΔMotion(g): ");
    Serial.println(deltaMotion, 3);
  }
}
