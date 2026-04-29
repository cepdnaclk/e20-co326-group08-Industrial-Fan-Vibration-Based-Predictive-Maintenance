// Step2_MPU6050_Raw_Values.ino
// PURPOSE: Read and print raw accelerometer values to verify sensor works
// BOARD: ESP32-S3 DevKitC-1 (N16R8)
// WIRING: Same as Step 1 (MPU6050 only)
//
// EXPECTED OUTPUT: Three acceleration values (ax, ay, az) and
// a magnitude value, updating every 500ms.
// When fan is off: magnitude ~16000-17000 (gravity only)
// When fan is running: magnitude fluctuates with vibration

#include <Wire.h>
#include <MPU6050.h>
#include <math.h> // For the sqrt function

MPU6050 mpu;

void setup() {
  Serial.begin(115200);
  
  // CRITICAL for S3: Wait for Serial to initialize
  while (!Serial) {
    delay(10);
  }
  
  delay(1000);

  // Update to your working S3 pins
  Wire.begin(8, 9); 
  delay(100);

  Serial.println("Initializing MPU6050...");
  mpu.initialize();

  // Verify connection
  if (!mpu.testConnection()) {
    Serial.println("ERROR: MPU6050 connection failed!");
    Serial.println("Verify SDA is GPIO 8 and SCL is GPIO 9");
    while (1);
  }

  Serial.println("\n================================");
  Serial.println("  MPU6050 Raw Value Reader");
  Serial.println("================================");
}

void loop() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);

  // Calculate magnitude (Total acceleration vector)
  float mag = sqrt(pow(ax, 2) + pow(ay, 2) + pow(az, 2));

  Serial.print("ax="); Serial.print(ax);
  Serial.print("\tay="); Serial.print(ay);
  Serial.print("\taz="); Serial.print(az);
  Serial.print("\tmag="); Serial.println(mag, 2);

  delay(500);
}