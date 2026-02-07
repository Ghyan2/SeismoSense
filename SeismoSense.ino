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

bool hasLastSample = false;
xyzFloat lastGVal = {0.0, 0.0, 0.0};

void setup() {
  Wire.begin();
  Serial.begin(115200);
  pinMode(SW420, INPUT);

  if (!mpu.init()) {
    Serial.println("MPU9250 not responding. Check wiring.");
    while (true) {
      delay(1000);
    }
  }

  // Ensure the sensor is awake and all accel axes are enabled
  mpu.sleep(false);
  mpu.enableCycle(false);
  mpu.enableAccAxes(MPU9250_ENABLE_XYZ);

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

    // DELTA-BASED MOTION (vector magnitude of change)
    float deltaMotion = 0.0;
    if (hasLastSample) {
      float dx = gVal.x - lastGVal.x;
      float dy = gVal.y - lastGVal.y;
      float dz = gVal.z - lastGVal.z;
      deltaMotion = sqrt((dx * dx) + (dy * dy) + (dz * dz));
    }
    lastGVal = gVal;
    hasLastSample = true;

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
