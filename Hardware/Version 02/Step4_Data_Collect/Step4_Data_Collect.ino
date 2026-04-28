#include <Wire.h>
#include <MPU6050.h>

#define N_SAMPLES 128
#define SAMPLE_INTERVAL 2000  // microseconds = 500Hz

MPU6050 mpu;

void setup() {
  Serial.begin(115200);
  
  // CRITICAL: Wait for S3 Native USB Serial to be ready
  while (!Serial) {
    delay(10);
  }

  // Update to your working S3 pins
  Wire.begin(8, 9); 
  delay(100);

  mpu.initialize();

  // If connection fails, Python will see "MPU6050 ERROR"
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 ERROR");
    while (1);
  }

  // Python looks for this string to confirm connection
  Serial.println("READY");
}

void loop() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();

    if (cmd == 'C') {
      // Collect one window of 128 samples
      for (int i = 0; i < N_SAMPLES; i++) {
        int16_t ax, ay, az;
        mpu.getAcceleration(&ax, &ay, &az);
        
        // Use pow() or (float) casting for magnitude
        float mag = sqrt(pow(ax, 2) + pow(ay, 2) + pow(az, 2));
        
        Serial.println(mag, 4);
        delayMicroseconds(SAMPLE_INTERVAL);
      }
      Serial.println("END");
    }
  }
}