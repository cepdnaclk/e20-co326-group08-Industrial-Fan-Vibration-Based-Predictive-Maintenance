#include <Wire.h>
#include <MPU6050.h>

MPU6050 mpu;

#define N_SAMPLES 128
#define SAMPLE_INTERVAL_US 2000  // 2ms = 500Hz sampling rate

float raw_samples[N_SAMPLES];

void collectSamples() {
  for (int i = 0; i < N_SAMPLES; i++) {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);

    // Compute vibration magnitude (removes gravity effect direction)
    float mag = sqrt((float)ax * ax + (float)ay * ay + (float)az * az);
    raw_samples[i] = mag;

    delayMicroseconds(SAMPLE_INTERVAL_US);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  delay(100);
  mpu.initialize();

  if (!mpu.testConnection()) {
    Serial.println("ERROR: MPU6050 not found!");
    while (1);
  }

  Serial.println("READY");
}

void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();

    if (cmd == 'C') {
      collectSamples();

      // Send all samples to PC
      for (int i = 0; i < N_SAMPLES; i++) {
        Serial.println(raw_samples[i], 4);
      }
      Serial.println("END");
    }
  }
}