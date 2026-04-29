// Step1_I2C_Scanner.ino
// PURPOSE: Verify the MPU6050 sensor is connected and detected
// BOARD: ESP32-S3 DevKitC-1 (N16R8)
// WIRING NEEDED: Only MPU6050
//   MPU6050 VCC -> ESP32-S3 3.3V
//   MPU6050 GND -> ESP32-S3 GND
//   MPU6050 SDA -> ESP32-S3 GPIO21
//   MPU6050 SCL -> ESP32-S3 GPIO22
//
// EXPECTED OUTPUT: "Found device at 0x68"
// If nothing found: swap SDA and SCL wires

#include <Wire.h>

void setup() {
  // Wait for USB Serial to initialize (Specific for S3)
  Serial.begin(115200);
  while (!Serial) {
    delay(10); 
  }
  
  delay(1000); 

  // Initialize I2C on your chosen pins
  Wire.begin(8, 9);  // SDA=8, SCL=9
  
  Serial.println("\n--- ESP32-S3 I2C Scanner ---");
  Serial.println("Scanning pins: SDA=8, SCL=9");

  byte error, address;
  int nDevices = 0;

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      if (address == 0x68) Serial.print(" (MPU6050 detected!)");
      Serial.println();
      nDevices++;
    }
  }

  if (nDevices == 0) {
    Serial.println("No I2C devices found. Check your wiring and pull-ups!");
  } else {
    Serial.println("Scan complete.");
  }
}

void loop() {
  delay(5000); // Wait and scan again
}
