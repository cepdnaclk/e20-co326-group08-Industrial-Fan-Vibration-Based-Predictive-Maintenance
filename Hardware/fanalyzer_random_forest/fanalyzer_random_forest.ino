// fanalyzer_random_forest.ino
// Classes: 0=normal  1=blocked  2=unbalanced  3=off
//
// FOLDER SETUP:
//   fanalyzer_random_forest/
//   ├── fanalyzer_random_forest.ino   <- this file
//   └── rf_model.h                    <- copy from Python output

#include <Wire.h>
#include <MPU6050.h>
#include <arduinoFFT.h>
#include "rf_model.h"

#define N_SAMPLES       128
#define SAMPLE_RATE_HZ  500
#define SAMPLE_INTERVAL (1000000 / SAMPLE_RATE_HZ)

MPU6050 mpu;
double vReal[N_SAMPLES];
double vImag[N_SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, N_SAMPLES, SAMPLE_RATE_HZ);

const char* CLASS_LABELS[] = {"NORMAL", "BLOCKED", "UNBALANCED", "OFF"};

struct Features { float dom_freq, max_amp, mean_energy; };

// ── Feature extraction ─────────────────────────────────────────
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

// ── Print result ───────────────────────────────────────────────
void printResult(Features f, int pred) {
  Serial.println("\n--------------------------------");
  Serial.println("  MODEL: Random Forest");
  Serial.print("  Freq   : "); Serial.print(f.dom_freq, 2);    Serial.println(" Hz");
  Serial.print("  Amp    : "); Serial.println(f.max_amp, 2);
  Serial.print("  Energy : "); Serial.println(f.mean_energy, 2);
  Serial.println("--------------------------------");
  Serial.print("  >> "); Serial.println(CLASS_LABELS[pred]);
  Serial.println("--------------------------------");
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  delay(100);
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("ERROR: MPU6050 not found! Check wiring.");
    while (1);
  }
  Serial.println("\nFanalyzer - Random Forest");
  Serial.println("Classes: NORMAL / BLOCKED / UNBALANCED / OFF");
  Serial.println("Classifying every 2 seconds...\n");
}

void loop() {
  Features f = extractFeatures();

  // NOTE: Open rf_model.h and check what function/class it uses.
  //
  // If micromlgen generated the file, the function is inside a class:
  Eloquent::ML::Port::RandomForest clf;
  float feat[3] = {f.dom_freq, f.max_amp, f.mean_energy};
  int pred = clf.predict(feat);
  //
  // If the manual fallback was used, call directly:
  //int pred = rf_predict(f.dom_freq, f.max_amp, f.mean_energy);

  printResult(f, pred);
  delay(2000);
}
