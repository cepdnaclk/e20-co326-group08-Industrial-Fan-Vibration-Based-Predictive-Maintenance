// fanalyzer_neural_network.ino
// COMPLETE FIRMWARE: Fan + Pump + LEDs + Buzzer + Serial + WiFi + MQTT
// BOARD: ESP32-S3 DevKitC-1 (N16R8)

#include <Wire.h>
#include <MPU6050.h>
#include <arduinoFFT.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "nn_model.h"

// ── WiFi & MQTT credentials ──────────────────────────────
const char* WIFI_SSID     = "Dasu";
const char* WIFI_PASSWORD = "Dasuni#2001";

const char* mqtt_server   = "YOUR_LAPTOP_IP"; // <--- CHANGE THIS to the IP of the PC running Docker
const int   mqtt_port     = 1883;

// ── MQTT Topics ──────────────────────────────────────────
const char* topic_data_out  = "sensors/group08/coolingTower/data/fan01";
const char* topic_cmd_fan   = "sensors/group08/coolingTower/cmd/fan01";
const char* topic_cmd_motor = "sensors/group08/coolingTower/cmd/motor01";

// ── Sensor & FFT config ──────────────────────────────────
#define N_SAMPLES       128
#define SAMPLE_RATE_HZ  500
#define SAMPLE_INTERVAL (1000000 / SAMPLE_RATE_HZ)

// ── Pin definitions (ESP32-S3 N16R8) ─────────────────────
#define PIN_RELAY_FAN   7     // Relay 1: Fan (active-LOW)
#define PIN_RELAY_PUMP  15    // Relay 2: Pump (active-LOW)
#define PIN_LED_NORMAL  4     // Green LED
#define PIN_LED_BLOCKED 5     // Red LED 1
#define PIN_LED_UNBAL   6     // Red LED 2
#define PIN_BUZZER      16    // Active buzzer

// ── Relay helpers (hide active-LOW logic) ────────────────
#define FAN_ON()    digitalWrite(PIN_RELAY_FAN, LOW)
#define FAN_OFF()   digitalWrite(PIN_RELAY_FAN, HIGH)
#define PUMP_ON()   digitalWrite(PIN_RELAY_PUMP, LOW)
#define PUMP_OFF()  digitalWrite(PIN_RELAY_PUMP, HIGH)

// ── Objects ──────────────────────────────────────────────
MPU6050 mpu;
double vReal[N_SAMPLES];
double vImag[N_SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, N_SAMPLES, SAMPLE_RATE_HZ);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

const char* CLASS_LABELS[] = {"NORMAL", "BLOCKED", "UNBALANCED", "EMPTY", "OFF"};

// ── State tracking ───────────────────────────────────────
bool fanRunning      = false;
bool pumpRunning     = false;
bool pumpInterlocked = false;
bool pumpWasRunning  = false;
int  lastPrediction  = -1;
int  currentPrediction = 3;  // Start as OFF

struct Features { float dom_freq, max_amp, mean_energy; };

// ══════════════════════════════════════════════════════════
//  FEATURE EXTRACTION
// ══════════════════════════════════════════════════════════
Features extractFeatures() {
  for (int i = 0; i < N_SAMPLES; i++) {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    float mag = sqrt(pow((float)ax, 2) + pow((float)ay, 2) + pow((float)az, 2));
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

// ══════════════════════════════════════════════════════════
//  STANDARD SCALER
// ══════════════════════════════════════════════════════════
void scaleFeatures(float* input, float* output) {
  for (int i = 0; i < 3; i++) {
    output[i] = (input[i] - SCALER_MEAN[i]) / SCALER_STD[i];
  }
}

// ══════════════════════════════════════════════════════════
//  SOFTMAX
// ══════════════════════════════════════════════════════════
void softmax(float* x, int n) {
  float maxVal = x[0];
  for (int i = 1; i < n; i++) if (x[i] > maxVal) maxVal = x[i];
  float sum = 0;
  for (int i = 0; i < n; i++) { x[i] = exp(x[i] - maxVal); sum += x[i]; }
  for (int i = 0; i < n; i++) x[i] /= sum;
}

// ══════════════════════════════════════════════════════════
//  NEURAL NETWORK FORWARD PASS (3->16->8->4)
// ══════════════════════════════════════════════════════════
int nn_predict(float dom_freq, float max_amp, float mean_energy) {
  float raw[3] = {dom_freq, max_amp, mean_energy};
  float inp[3];
  scaleFeatures(raw, inp);

  float h1[NN_LAYER_1_SIZE];
  for (int j = 0; j < NN_LAYER_1_SIZE; j++) {
    float s = b1[j];
    for (int i = 0; i < NN_LAYER_0_SIZE; i++) s += inp[i] * W1[i][j];
    h1[j] = relu(s);
  }

  float h2[NN_LAYER_2_SIZE];
  for (int j = 0; j < NN_LAYER_2_SIZE; j++) {
    float s = b2[j];
    for (int i = 0; i < NN_LAYER_1_SIZE; i++) s += h1[i] * W2[i][j];
    h2[j] = relu(s);
  }

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

// ══════════════════════════════════════════════════════════
//  LED CONTROL
// ══════════════════════════════════════════════════════════
void updateLEDs(int prediction) {
  digitalWrite(PIN_LED_NORMAL,  LOW);
  digitalWrite(PIN_LED_BLOCKED, LOW);
  digitalWrite(PIN_LED_UNBAL,   LOW);

  switch (prediction) {
    case 0: digitalWrite(PIN_LED_NORMAL,  HIGH); break;
    case 1: digitalWrite(PIN_LED_BLOCKED, HIGH); break;
    case 2: digitalWrite(PIN_LED_UNBAL,   HIGH); break;
    case 4: break;  // All off when fan is off
  }
}

// ══════════════════════════════════════════════════════════
//  BUZZER
// ══════════════════════════════════════════════════════════
void updateBuzzer(int prediction) {
  if (prediction == 1 || prediction == 2) {
    digitalWrite(PIN_BUZZER, HIGH); delay(200);
    digitalWrite(PIN_BUZZER, LOW);  delay(200);
    digitalWrite(PIN_BUZZER, HIGH); delay(200);
    digitalWrite(PIN_BUZZER, LOW);
  } else {
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// ══════════════════════════════════════════════════════════
//  FAN & PUMP CONTROL
// ══════════════════════════════════════════════════════════
void setFan(bool on) {
  if (on) { FAN_ON();  fanRunning = true;  Serial.println("\n*** FAN ON ***"); }
  else    { FAN_OFF(); fanRunning = false; Serial.println("\n*** FAN OFF ***"); }
}

void setPump(bool on) {
  if (on) {
    // STRICT RULE: Pump can ONLY turn on if Fan is NORMAL (Class 0)
    if (currentPrediction != 0) {
      Serial.println("\n*** PUMP START DENIED: Fan is not NORMAL! Fix fan first. ***");
      return;
    }
    PUMP_ON();  pumpRunning = true;  Serial.println("\n*** PUMP ON ***"); 
  }
  else { 
    PUMP_OFF(); pumpRunning = false; Serial.println("\n*** PUMP OFF ***");
    pumpWasRunning = false; // Clear auto-restart memory if manually stopped
  }
}

// ══════════════════════════════════════════════════════════
//  COOLING TOWER INTERLOCK
// ══════════════════════════════════════════════════════════
void updateInterlock(int prediction) {
  // It is ONLY safe if the prediction is 0 (NORMAL)
  bool isSafe = (prediction == 0);

  if (!isSafe && !pumpInterlocked) {
    pumpWasRunning = pumpRunning; // Remember if it was running before the fault
    if (pumpRunning) {
      PUMP_OFF();
      pumpRunning = false;
      Serial.println("  !! INTERLOCK: Pump shut down — fan is no longer NORMAL !!");
    }
    pumpInterlocked = true;
  }
  else if (isSafe && pumpInterlocked) {
    pumpInterlocked = false;
    if (pumpWasRunning) {
      PUMP_ON();
      pumpRunning = true;
      Serial.println("  >> INTERLOCK RELEASED: Pump auto-restarted <<");
    }
    pumpWasRunning = false;
  }
}

// ══════════════════════════════════════════════════════════
//  SERIAL COMMANDS
// ══════════════════════════════════════════════════════════
void handleSerialCommands() {
  while (Serial.available()) {
    char cmd = Serial.read();
    switch (cmd) {
      case '1': setFan(true);   break;
      case '0': setFan(false);  break;
      case '4': setPump(true);  break;
      case '2': setPump(false); break;
      default: break;
    }
  }
}

// ══════════════════════════════════════════════════════════
//  MQTT CALLBACK (Incoming Commands)
// ══════════════════════════════════════════════════════════
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();
  
  Serial.print("MQTT Command Received on [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  // Handle Fan Commands
  if (String(topic) == topic_cmd_fan) {
    if (message == "SHUTDOWN" || message == "0" || message == "OFF") setFan(false);
    else if (message == "ON" || message == "1") setFan(true);
  }
  // Handle Motor Commands
  else if (String(topic) == topic_cmd_motor) {
    if (message == "SHUTDOWN" || message == "2" || message == "OFF") setPump(false);
    else if (message == "ON" || message == "4") setPump(true);
  }
}

// ══════════════════════════════════════════════════════════
//  WiFi & MQTT SETUP
// ══════════════════════════════════════════════════════════
void setupWiFi() {
  Serial.print("  Connecting to WiFi: ");
  Serial.print(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
  Serial.print("  IP Address: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect("ESP32_CoolingTower08")) {
      Serial.println("connected");
      // Subscribe to command topics
      mqttClient.subscribe(topic_cmd_fan);
      mqttClient.subscribe(topic_cmd_motor);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// ══════════════════════════════════════════════════════════
//  PRINT RESULT
// ══════════════════════════════════════════════════════════
void printResult(Features f, int pred) {
  Serial.println("\n================================");
  Serial.println("  Fanalyzer — Cooling Tower");
  Serial.print("  Freq   : "); Serial.print(f.dom_freq, 2);    Serial.println(" Hz");
  Serial.print("  Amp    : "); Serial.println(f.max_amp, 2);
  Serial.print("  Energy : "); Serial.println(f.mean_energy, 2);
  Serial.println("--------------------------------");
  Serial.print("  STATE  : "); Serial.println(CLASS_LABELS[pred]);
  Serial.print("  FAN    : "); Serial.println(fanRunning ? "ON" : "OFF");
  Serial.print("  PUMP   : "); Serial.print(pumpRunning ? "ON" : "OFF");
  if (pumpInterlocked) Serial.print(" [INTERLOCKED]");
  Serial.println("\n================================\n");
}

// ══════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);

  // I2C + Sensor
  Wire.begin(8, 9);
  delay(100);
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("ERROR: MPU6050 not found! Check wiring.");
    while (1);
  }

  // Output pins
  pinMode(PIN_RELAY_FAN,   OUTPUT);
  pinMode(PIN_RELAY_PUMP,  OUTPUT);
  pinMode(PIN_LED_NORMAL,  OUTPUT);
  pinMode(PIN_LED_BLOCKED, OUTPUT);
  pinMode(PIN_LED_UNBAL,   OUTPUT);
  pinMode(PIN_BUZZER,      OUTPUT);

  // Initial state: everything OFF
  FAN_OFF();
  PUMP_OFF();
  digitalWrite(PIN_LED_NORMAL,  LOW);
  digitalWrite(PIN_LED_BLOCKED, LOW);
  digitalWrite(PIN_LED_UNBAL,   LOW);
  digitalWrite(PIN_BUZZER,      LOW);

  // WiFi & MQTT Initialization
  setupWiFi();
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  Serial.println("\n  System ready.\n");
}

// ══════════════════════════════════════════════════════════
//  MAIN LOOP
// ══════════════════════════════════════════════════════════
void loop() {
  // Ensure WiFi & MQTT stay connected
  if (WiFi.status() != WL_CONNECTED) setupWiFi();
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();

  handleSerialCommands();

  Features f = extractFeatures();
  int pred = nn_predict(f.dom_freq, f.max_amp, f.mean_energy);
  currentPrediction = pred;

  updateLEDs(pred);
  updateBuzzer(pred);
  updateInterlock(pred);
  printResult(f, pred);

  if (pred != lastPrediction) {
    lastPrediction = pred;
  }

  // Publish Data via MQTT
  String json = "{";
  json += "\"fan_state\":\"" + String(CLASS_LABELS[pred]) + "\",";
  json += "\"max_amplitude\":" + String(f.max_amp) + ",";
  json += "\"mean_energy\":" + String(f.mean_energy) + ",";
  json += "\"fan_running\":" + String(fanRunning ? "true" : "false") + ",";
  json += "\"pump_running\":" + String(pumpRunning ? "true" : "false") + ",";
  json += "\"pump_interlocked\":" + String(pumpInterlocked ? "true" : "false") + ",";
  json += "\"class\":" + String(pred);
  json += "}";
  
  mqttClient.publish(topic_data_out, json.c_str());

  delay(2000);
}