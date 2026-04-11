// fanalyzer_neural_network.ino
// Classes: 0=normal  1=blocked  2=unbalanced  3=off
//
// FOLDER SETUP:
//   fanalyzer_neural_network/
//   ├── fanalyzer_neural_network.ino   <- this file
//   └── nn_model.h                     <- copy from Python output

#include <Wire.h>
#include <MPU6050.h>
#include <arduinoFFT.h>
#include "nn_model.h"

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

// ── StandardScaler ─────────────────────────────────────────────
void scaleFeatures(float* input, float* output) {
  for (int i = 0; i < 3; i++) {
    output[i] = (input[i] - SCALER_MEAN[i]) / SCALER_STD[i];
  }
}

// ── Softmax ────────────────────────────────────────────────────
void softmax(float* x, int n) {
  float maxVal = x[0];
  for (int i = 1; i < n; i++) if (x[i] > maxVal) maxVal = x[i];
  float sum = 0;
  for (int i = 0; i < n; i++) { x[i] = exp(x[i] - maxVal); sum += x[i]; }
  for (int i = 0; i < n; i++) x[i] /= sum;
}

// ── Neural Network forward pass ────────────────────────────────
int nn_predict(float dom_freq, float max_amp, float mean_energy) {
  // Scale input
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

  // Return class with highest probability
  int best = 0;
  for (int i = 1; i < NN_LAYER_3_SIZE; i++) if (out[i] > out[best]) best = i;
  return best;
}

// ── Print result ───────────────────────────────────────────────
void printResult(Features f, int pred) {
  Serial.println("\n--------------------------------");
  Serial.println("  MODEL: Neural Network 3->16->8->4");
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
  Serial.println("\nFanalyzer - Neural Network");
  Serial.println("Classes: NORMAL / BLOCKED / UNBALANCED / OFF");
  Serial.println("Classifying every 2 seconds...\n");
}

void loop() {
  Features f   = extractFeatures();
  int pred     = nn_predict(f.dom_freq, f.max_amp, f.mean_energy);
  printResult(f, pred);
  delay(2000);
}
