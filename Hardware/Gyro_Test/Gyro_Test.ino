#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);  // SDA = 21, SCL = 22

  Serial.println("🔍 Scanning for I2C devices...");
}

void loop() {
  byte count = 0;

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("✅ Found device at address: 0x");
      Serial.println(address, HEX);
      count++;
    }
  }

  if (count == 0) {
    Serial.println("❌ No I2C devices found");
  } else {
    Serial.println("✔ Scan complete");
  }

  delay(3000); // repeat every 3 seconds
}