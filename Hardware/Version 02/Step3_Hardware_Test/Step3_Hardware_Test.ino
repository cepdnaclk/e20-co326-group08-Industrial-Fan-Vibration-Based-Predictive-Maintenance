// Step3_Hardware_Test.ino
// PURPOSE: Test every hardware component one by one
// BOARD: ESP32-S3 DevKitC-1 (N16R8)
//
// TESTS (runs automatically in sequence):
//   1. MPU6050 sensor check
//   2. Green LED (GPIO4) blink
//   3. Red LED 1 (GPIO5) blink
//   4. Red LED 2 (GPIO6) blink
//   5. Buzzer (GPIO16) beep
//   6. Relay 1 - Fan (GPIO7) toggle
//   7. Relay 2 - Pump (GPIO15) toggle
//
// ALL WIRING MUST BE COMPLETE BEFORE RUNNING THIS
// (except 12V adapter - do NOT connect 12V yet)

#include <Wire.h>
#include <MPU6050.h>

#define PIN_RELAY_FAN   7
#define PIN_RELAY_PUMP  15
#define PIN_LED_NORMAL  4
#define PIN_LED_BLOCKED 5
#define PIN_LED_UNBAL   6
#define PIN_BUZZER      16

MPU6050 mpu;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize all pins
  pinMode(PIN_LED_NORMAL, OUTPUT);
  pinMode(PIN_LED_BLOCKED, OUTPUT);
  pinMode(PIN_LED_UNBAL, OUTPUT);
  pinMode(PIN_RELAY_FAN, OUTPUT);
  pinMode(PIN_RELAY_PUMP, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // Everything off
  digitalWrite(PIN_LED_NORMAL, LOW);
  digitalWrite(PIN_LED_BLOCKED, LOW);
  digitalWrite(PIN_LED_UNBAL, LOW);
  digitalWrite(PIN_RELAY_FAN, HIGH);   // Active-LOW: HIGH=OFF
  digitalWrite(PIN_RELAY_PUMP, HIGH);  // Active-LOW: HIGH=OFF
  digitalWrite(PIN_BUZZER, LOW);

  Wire.begin(21, 22);
  delay(100);

  Serial.println("\n========================================");
  Serial.println("  Fanalyzer Hardware Test");
  Serial.println("  ESP32-S3 DevKitC-1 N16R8");
  Serial.println("========================================");
  Serial.println("  Testing each component in sequence...");
  Serial.println("  Watch each component and verify.\n");
}

void loop() {
  // ── TEST 1: MPU6050 ──
  Serial.println("TEST 1: MPU6050 Sensor");
  mpu.initialize();
  if (mpu.testConnection()) {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    float mag = sqrt((float)ax*ax + (float)ay*ay + (float)az*az);
    Serial.print("  PASS - magnitude = ");
    Serial.println(mag, 2);
  } else {
    Serial.println("  FAIL - sensor not detected!");
  }
  delay(2000);

  // ── TEST 2: Green LED ──
  Serial.println("\nTEST 2: Green LED (GPIO4) - should blink 3 times");
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_NORMAL, HIGH);
    delay(500);
    digitalWrite(PIN_LED_NORMAL, LOW);
    delay(500);
  }
  Serial.println("  Did the green LED blink? (y/n in your head)");
  delay(1000);

  // ── TEST 3: Red LED 1 ──
  Serial.println("\nTEST 3: Red LED 1 (GPIO5) - should blink 3 times");
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_BLOCKED, HIGH);
    delay(500);
    digitalWrite(PIN_LED_BLOCKED, LOW);
    delay(500);
  }
  Serial.println("  Did red LED 1 blink?");
  delay(1000);

  // ── TEST 4: Red LED 2 ──
  Serial.println("\nTEST 4: Red LED 2 (GPIO6) - should blink 3 times");
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_UNBAL, HIGH);
    delay(500);
    digitalWrite(PIN_LED_UNBAL, LOW);
    delay(500);
  }
  Serial.println("  Did red LED 2 blink?");
  delay(1000);

  // ── TEST 5: Buzzer ──
  Serial.println("\nTEST 5: Buzzer (GPIO16) - should beep twice");
  digitalWrite(PIN_BUZZER, HIGH);
  delay(200);
  digitalWrite(PIN_BUZZER, LOW);
  delay(200);
  digitalWrite(PIN_BUZZER, HIGH);
  delay(200);
  digitalWrite(PIN_BUZZER, LOW);
  Serial.println("  Did the buzzer beep?");
  delay(2000);

  // ── TEST 6: Relay 1 ──
  Serial.println("\nTEST 6: Relay 1 - Fan (GPIO7) - should click on/off");
  Serial.println("  (Do NOT have 12V connected for this test)");
  digitalWrite(PIN_RELAY_FAN, LOW);   // ON
  Serial.println("  Relay 1 ON (should hear click)");
  delay(2000);
  digitalWrite(PIN_RELAY_FAN, HIGH);  // OFF
  Serial.println("  Relay 1 OFF (should hear click)");
  delay(2000);

  // ── TEST 7: Relay 2 ──
  Serial.println("\nTEST 7: Relay 2 - Pump (GPIO15) - should click on/off");
  digitalWrite(PIN_RELAY_PUMP, LOW);  // ON
  Serial.println("  Relay 2 ON (should hear click)");
  delay(2000);
  digitalWrite(PIN_RELAY_PUMP, HIGH); // OFF
  Serial.println("  Relay 2 OFF (should hear click)");
  delay(2000);

  // ── ALL DONE ──
  Serial.println("\n========================================");
  Serial.println("  ALL TESTS COMPLETE");
  Serial.println("  If all components responded correctly,");
  Serial.println("  proceed to data collection.");
  Serial.println("  If any failed, check wiring and re-run.");
  Serial.println("========================================");
  Serial.println("  Restarting tests in 10 seconds...\n");
  delay(10000);
}
