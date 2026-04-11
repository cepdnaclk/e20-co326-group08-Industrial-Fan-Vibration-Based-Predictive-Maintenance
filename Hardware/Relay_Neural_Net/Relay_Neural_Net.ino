// fanalyzer_neural_network.ino
// UPDATED: Relay control + LEDs + Buzzer + Serial commands
//
// Classes: 0=normal  1=blocked  2=unbalanced  3=off
//
// FOLDER SETUP:
//   fanalyzer_neural_network/
//   ├── fanalyzer_neural_network.ino   <- this file
//   └── nn_model.h                    <- copy from Python output
//
// SERIAL COMMANDS:
//   Send '1' -> Turn fan ON  (relay activates)
//   Send '0' -> Turn fan OFF (relay deactivates)
//
// GPIO MAP:
//   GPIO21 = MPU6050 SDA (I2C)
//   GPIO22 = MPU6050 SCL (I2C)
//   GPIO25 = Green LED   (Normal state)
//   GPIO26 = Relay IN    (Fan on/off, active-LOW)
//   GPIO27 = Red LED 1   (Blocked state)
//   GPIO14 = Red LED 2   (Unbalanced state)
//   GPIO33 = Buzzer      (Alarm for problem states)

#include <Wire.h>
#include <MPU6050.h>
#include <arduinoFFT.h>
#include "nn_model.h"

// ── Sensor & FFT config ──────────────────────────────────────
#define N_SAMPLES       128
#define SAMPLE_RATE_HZ  500
#define SAMPLE_INTERVAL (1000000 / SAMPLE_RATE_HZ)

// ── Pin definitions ──────────────────────────────────────────
#define PIN_RELAY       32   // Relay IN (active-LOW: LOW=fan ON, HIGH=fan OFF)
#define PIN_LED_NORMAL  25    // Green LED
#define PIN_LED_BLOCKED 27    // Red LED 1
#define PIN_LED_UNBAL   14    // Red LED 2
#define PIN_BUZZER      33    // Active buzzer

// ── Relay helpers (hides the active-LOW logic) ───────────────
// Your relay module is active-LOW:
//   IN pin LOW  = coil energised = COM-NO connected = fan ON
//   IN pin HIGH = coil off       = COM-NO open      = fan OFF
// These functions let you think in terms of "fan on" / "fan off"
// without worrying about the inverted logic.
#define FAN_ON()   digitalWrite(PIN_RELAY, LOW)
#define FAN_OFF()  digitalWrite(PIN_RELAY, HIGH)

// ── Objects ──────────────────────────────────────────────────
MPU6050 mpu;
double vReal[N_SAMPLES];
double vImag[N_SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, N_SAMPLES, SAMPLE_RATE_HZ);

const char* CLASS_LABELS[] = {"NORMAL", "BLOCKED", "UNBALANCED", "OFF"};

// ── State tracking ───────────────────────────────────────────
bool fanRunning = false;       // Track whether we've commanded the fan on
int lastPrediction = -1;       // Track last classification for change detection
int consecutiveAlerts = 0;     // Count consecutive problem readings

struct Features { float dom_freq, max_amp, mean_energy; };

// ══════════════════════════════════════════════════════════════
//  FEATURE EXTRACTION (unchanged from your original)
// ══════════════════════════════════════════════════════════════
Features extractFeatures() {
  for (int i = 0; i < N_SAMPLES; i++) {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    float mag = sqrt((float)ax*ax + (float)ay*ay + (float)az*az);
    vReal[i] = (double)mag;
    vImag[i] = 0.0;
    delayMicroseconds(SAMPLE_INTERVAL);
  }

  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();

  int half = N_SAMPLES / 2;
  float maxAmp = 0; int domBin = 1; float sumE = 0;
  for (int i = 1; i < half; i++) {
    float m = (float)vReal[i];
    sumE += m;
    if (m > maxAmp) { maxAmp = m; domBin = i; }
  }

  return {
    domBin * ((float)SAMPLE_RATE_HZ / N_SAMPLES),
    maxAmp,
    sumE / (half - 1)
  };
}

// ══════════════════════════════════════════════════════════════
//  STANDARD SCALER (unchanged)
// ══════════════════════════════════════════════════════════════
void scaleFeatures(float* input, float* output) {
  for (int i = 0; i < 3; i++) {
    output[i] = (input[i] - SCALER_MEAN[i]) / SCALER_STD[i];
  }
}

// ══════════════════════════════════════════════════════════════
//  SOFTMAX (unchanged)
// ══════════════════════════════════════════════════════════════
void softmax(float* x, int n) {
  float maxVal = x[0];
  for (int i = 1; i < n; i++) if (x[i] > maxVal) maxVal = x[i];
  float sum = 0;
  for (int i = 0; i < n; i++) { x[i] = exp(x[i] - maxVal); sum += x[i]; }
  for (int i = 0; i < n; i++) x[i] /= sum;
}

// ══════════════════════════════════════════════════════════════
//  NEURAL NETWORK FORWARD PASS (unchanged)
// ══════════════════════════════════════════════════════════════
int nn_predict(float dom_freq, float max_amp, float mean_energy) {
  float raw[3] = {dom_freq, max_amp, mean_energy};
  float inp[3];
  scaleFeatures(raw, inp);

  // Layer 1: 3 -> 16, ReLU
  float h1[NN_LAYER_1_SIZE];
  for (int j = 0; j < NN_LAYER_1_SIZE; j++) {
    float s = b1[j];
    for (int i = 0; i < NN_LAYER_0_SIZE; i++) s += inp[i] * W1[i][j];
    h1[j] = relu(s);
  }

  // Layer 2: 16 -> 8, ReLU
  float h2[NN_LAYER_2_SIZE];
  for (int j = 0; j < NN_LAYER_2_SIZE; j++) {
    float s = b2[j];
    for (int i = 0; i < NN_LAYER_1_SIZE; i++) s += h1[i] * W2[i][j];
    h2[j] = relu(s);
  }

  // Output: 8 -> 4, Softmax
  float out[NN_LAYER_3_SIZE];
  for (int j = 0; j < NN_LAYER_3_SIZE; j++) {
    float s = b3[j];
    for (int i = 0; i < NN_LAYER_2_SIZE; i++) s += h2[i] * W3[i][j];
    out[j] = s;
  }
  softmax(out, NN_LAYER_3_SIZE);

  int best = 0;
  for (int i = 1; i < NN_LAYER_3_SIZE; i++) if (out[i] > out[best]) best = i;
  return best;
}

// ══════════════════════════════════════════════════════════════
//  LED CONTROL — lights the correct LED based on classification
// ══════════════════════════════════════════════════════════════
void updateLEDs(int prediction) {
  // Turn all LEDs off first
  digitalWrite(PIN_LED_NORMAL,  LOW);
  digitalWrite(PIN_LED_BLOCKED, LOW);
  digitalWrite(PIN_LED_UNBAL,   LOW);

  // Light the correct one
  switch (prediction) {
    case 0:  // Normal
      digitalWrite(PIN_LED_NORMAL, HIGH);
      break;
    case 1:  // Blocked
      digitalWrite(PIN_LED_BLOCKED, HIGH);
      break;
    case 2:  // Unbalanced
      digitalWrite(PIN_LED_UNBAL, HIGH);
      break;
    case 3:  // Off — all LEDs stay off
      break;
  }
}

// ══════════════════════════════════════════════════════════════
//  BUZZER CONTROL — beeps for problem states
// ══════════════════════════════════════════════════════════════
void updateBuzzer(int prediction) {
  if (prediction == 1 || prediction == 2) {
    // Problem detected: Blocked or Unbalanced
    // Short beep pattern: 200ms on, 200ms off, 200ms on
    digitalWrite(PIN_BUZZER, HIGH);
    delay(200);
    digitalWrite(PIN_BUZZER, LOW);
    delay(200);
    digitalWrite(PIN_BUZZER, HIGH);
    delay(200);
    digitalWrite(PIN_BUZZER, LOW);
  } else {
    // Normal or Off — buzzer silent
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// ══════════════════════════════════════════════════════════════
//  SERIAL COMMAND HANDLER — fan control via Serial Monitor
// ══════════════════════════════════════════════════════════════
void handleSerialCommands() {
  while (Serial.available()) {
    char cmd = Serial.read();

    if (cmd == '1') {
      // Turn fan ON
      FAN_ON();
      fanRunning = true;
      Serial.println("\n*** FAN ON — Relay activated ***");
    }
    else if (cmd == '0') {
      // Turn fan OFF
      FAN_OFF();
      fanRunning = false;
      Serial.println("\n*** FAN OFF — Relay deactivated ***");
    }
    // Ignore newline/carriage return characters
  }
}

// ══════════════════════════════════════════════════════════════
//  PRINT RESULT (updated with more info)
// ══════════════════════════════════════════════════════════════
void printResult(Features f, int pred) {
  Serial.println("\n================================");
  Serial.println("  Fanalyzer — Neural Network");
  Serial.print("  Freq   : "); Serial.print(f.dom_freq, 2);    Serial.println(" Hz");
  Serial.print("  Amp    : "); Serial.println(f.max_amp, 2);
  Serial.print("  Energy : "); Serial.println(f.mean_energy, 2);
  Serial.println("--------------------------------");
  Serial.print("  STATE  : "); Serial.println(CLASS_LABELS[pred]);
  Serial.print("  FAN    : "); Serial.println(fanRunning ? "ON (relay active)" : "OFF (relay inactive)");
  Serial.println("================================");

  // Show warning for problem states
  if (pred == 1) {
    Serial.println("  !! WARNING: Fan BLOCKED !!");
  } else if (pred == 2) {
    Serial.println("  !! WARNING: Fan UNBALANCED !!");
  }

  Serial.println("  Send '1' = Fan ON, '0' = Fan OFF");
}

// ══════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  // ── I2C + Sensor ──
  Wire.begin(21, 22);
  delay(100);
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("ERROR: MPU6050 not found! Check wiring.");
    while (1);
  }

  // ── Output pins ──
  pinMode(PIN_RELAY,       OUTPUT);
  pinMode(PIN_LED_NORMAL,  OUTPUT);
  pinMode(PIN_LED_BLOCKED, OUTPUT);
  pinMode(PIN_LED_UNBAL,   OUTPUT);
  pinMode(PIN_BUZZER,      OUTPUT);

  // ── Initial state: fan OFF, all LEDs off, buzzer off ──
  FAN_OFF();
  digitalWrite(PIN_LED_NORMAL,  LOW);
  digitalWrite(PIN_LED_BLOCKED, LOW);
  digitalWrite(PIN_LED_UNBAL,   LOW);
  digitalWrite(PIN_BUZZER,      LOW);

  // ── Startup message ──
  Serial.println("\n========================================");
  Serial.println("  Fanalyzer — Neural Network + Relay");
  Serial.println("  Classes: NORMAL / BLOCKED / UNBALANCED / OFF");
  Serial.println("  LEDs: Green=Normal, Red1=Blocked, Red2=Unbalanced");
  Serial.println("  Buzzer: Beeps on Blocked or Unbalanced");
  Serial.println("----------------------------------------");
  Serial.println("  CONTROLS:");
  Serial.println("    Send '1' -> Turn fan ON");
  Serial.println("    Send '0' -> Turn fan OFF");
  Serial.println("========================================");
  Serial.println("  Fan starts OFF. Send '1' to start.\n");
}

// ══════════════════════════════════════════════════════════════
//  MAIN LOOP
// ══════════════════════════════════════════════════════════════
void loop() {
  // Check for serial commands (fan on/off)
  handleSerialCommands();

  // Read sensor + classify
  Features f = extractFeatures();
  int pred = nn_predict(f.dom_freq, f.max_amp, f.mean_energy);

  // Update LEDs based on classification
  updateLEDs(pred);

  // Buzzer alert for problem states
  updateBuzzer(pred);

  // Print to serial
  printResult(f, pred);

  // Track state changes
  if (pred != lastPrediction) {
    Serial.print("  >> State changed: ");
    if (lastPrediction >= 0) Serial.print(CLASS_LABELS[lastPrediction]);
    else Serial.print("(startup)");
    Serial.print(" -> ");
    Serial.println(CLASS_LABELS[pred]);
    lastPrediction = pred;
  }

  delay(2000);  // Classify every 2 seconds
}
