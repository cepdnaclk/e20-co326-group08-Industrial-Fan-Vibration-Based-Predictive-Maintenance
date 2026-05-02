// fanalyzer_neural_network.ino
// COMPLETE FIRMWARE: Fan + Pump + LEDs + Buzzer + Serial + WiFi Dashboard + MQTT
// BOARD: ESP32-S3 DevKitC-1 (N16R8)
//
// ═══════════════════════════════════════════════════════════
//  COOLING TOWER SYSTEM:
//    Fan blows air across cooling tower
//    Pump circulates water through the tower
//    If fan is NOT NORMAL -> pump auto-shuts down (interlock)
//    When fan recovers to NORMAL -> pump auto-restarts
//    Pump start is DENIED unless fan classification is NORMAL
// ═══════════════════════════════════════════════════════════
//
// REQUIRES: nn_model.h in the same folder
//
// SERIAL COMMANDS (115200 baud):
//   '1' = Fan ON     '0' = Fan OFF
//   '3' = Pump ON    '2' = Pump OFF
//
// WiFi: Open ESP32's IP in browser (shown in Serial Monitor)
//       Endpoints: /  /fan/on  /fan/off  /pump/on  /pump/off  /status
//
// MQTT Topics:
//   sensors/group08/coolingTower/data/fan01     (publish telemetry)
//   sensors/group08/coolingTower/cmd/fan01      (subscribe commands)
//   sensors/group08/coolingTower/cmd/motor01    (subscribe commands)
//
// GPIO MAP (ESP32-S3 N16R8):
//   GPIO8  = MPU6050 SDA (I2C)
//   GPIO9  = MPU6050 SCL (I2C)
//   GPIO4  = Green LED   (Normal)
//   GPIO5  = Red LED 1   (Blocked)
//   GPIO6  = Red LED 2   (Unbalanced)
//   GPIO7  = Relay 1 IN  (Fan, active-LOW)
//   GPIO15 = Relay 2 IN  (Pump, active-LOW)
//   GPIO16 = Buzzer

#include <Wire.h>
#include <MPU6050.h>
#include <arduinoFFT.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include "nn_model.h"

// ── WiFi & MQTT credentials ──────────────────────────────
const char* WIFI_SSID     = "Dasu";
const char* WIFI_PASSWORD = "Dasuni#2001";

const char* mqtt_server   = "10.61.231.19"; // <--- CHANGE THIS to the IP of the PC running Docker
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
#define FAN_ON()    digitalWrite(PIN_RELAY_FAN, HIGH)
#define FAN_OFF()   digitalWrite(PIN_RELAY_FAN, LOW)
#define PUMP_ON()   digitalWrite(PIN_RELAY_PUMP, LOW)
#define PUMP_OFF()  digitalWrite(PIN_RELAY_PUMP, HIGH)

// ── Objects ──────────────────────────────────────────────
MPU6050 mpu;
double vReal[N_SAMPLES];
double vImag[N_SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, N_SAMPLES, SAMPLE_RATE_HZ);

WiFiClient    espClient;
PubSubClient  mqttClient(espClient);
WebServer     server(80);

const char* CLASS_LABELS[] = {"NORMAL", "BLOCKED", "UNBALANCED", "EMPTY", "OFF"};

// ── State tracking ───────────────────────────────────────
bool fanRunning      = false;
bool pumpRunning     = false;
bool pumpInterlocked = false;
bool pumpWasRunning  = false;
int  lastPrediction  = -1;
int  currentPrediction = 3;  // Start as EMPTY/OFF-ish

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
//  SOFTMAX (with max-subtraction for numerical stability)
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
//  BUZZER — double beep for fault states
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
//  COOLING TOWER INTERLOCK
//  Only NORMAL (class 0) is considered safe.
// ══════════════════════════════════════════════════════════
void updateInterlock(int prediction) {
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
//  FAN & PUMP CONTROL (used by serial + WiFi + MQTT)
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
//  SERIAL COMMANDS: '1'=fan on, '0'=fan off,
//                   '3'=pump on, '2'=pump off
// ══════════════════════════════════════════════════════════
void handleSerialCommands() {
  while (Serial.available()) {
    char cmd = Serial.read();
    switch (cmd) {
      case '1': setFan(true);   break;
      case '0': setFan(false);  break;
      case '3': setPump(true);  break;
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
  for (unsigned int i = 0; i < length; i++) {
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
  // Handle Motor (Pump) Commands
  else if (String(topic) == topic_cmd_motor) {
    if (message == "SHUTDOWN" || message == "2" || message == "OFF") setPump(false);
    else if (message == "ON" || message == "3" || message == "4") setPump(true);
  }
}

// ══════════════════════════════════════════════════════════
//  WiFi WEB SERVER — Dashboard
// ══════════════════════════════════════════════════════════
String buildWebPage() {
  String fanState       = fanRunning ? "ON" : "OFF";
  String pumpState      = pumpRunning ? "ON" : "OFF";
  String fanClass       = CLASS_LABELS[currentPrediction];
  String interlockState = pumpInterlocked ? "ACTIVE" : "Clear";

  String fanColor       = fanRunning ? "#27ae60" : "#e74c3c";
  String pumpColor      = pumpRunning ? "#27ae60" : "#e74c3c";
  String interlockColor = pumpInterlocked ? "#e74c3c" : "#27ae60";

  String stateColor;
  switch (currentPrediction) {
    case 0: stateColor = "#27ae60"; break;
    case 1: stateColor = "#e74c3c"; break;
    case 2: stateColor = "#e67e22"; break;
    case 3: stateColor = "#7f8c8d"; break;
    case 4: stateColor = "#95a5a6"; break;
    default: stateColor = "#95a5a6";
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='3'>";
  html += "<title>Cooling Tower Fanalyzer</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;max-width:500px;margin:20px auto;padding:0 15px;background:#1a1a2e;color:#eee}";
  html += "h1{text-align:center;color:#00d2ff;font-size:22px}";
  html += "h2{font-size:16px;color:#aaa;margin:20px 0 8px;border-bottom:1px solid #333;padding-bottom:4px}";
  html += ".card{background:#16213e;border-radius:10px;padding:15px;margin:10px 0}";
  html += ".status{display:flex;justify-content:space-between;align-items:center;padding:8px 0}";
  html += ".label{color:#aaa;font-size:14px}";
  html += ".value{font-weight:bold;font-size:16px;padding:4px 12px;border-radius:6px;color:#fff}";
  html += ".btn-row{display:flex;gap:10px;margin:8px 0}";
  html += ".btn{flex:1;padding:14px;border:none;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer;color:#fff;text-decoration:none;text-align:center;display:block}";
  html += ".btn-on{background:#27ae60}.btn-off{background:#e74c3c}";
  html += ".btn:active{opacity:0.7}";
  html += ".warn{background:#e74c3c22;border:1px solid #e74c3c;border-radius:8px;padding:10px;margin:8px 0;text-align:center;color:#e74c3c;font-size:14px}";
  html += "</style></head><body>";

  html += "<h1>Fanalyzer</h1>";
  html += "<p style='text-align:center;color:#666;font-size:12px'>Cooling Tower Control — ESP32-S3 + MQTT</p>";

  // Fan Health
  html += "<h2>Fan health</h2><div class='card'>";
  html += "<div class='status'><span class='label'>Classification</span>";
  html += "<span class='value' style='background:" + stateColor + "'>" + fanClass + "</span></div></div>";

  // Fan Control
  html += "<h2>Fan control</h2><div class='card'>";
  html += "<div class='status'><span class='label'>Fan</span>";
  html += "<span class='value' style='background:" + fanColor + "'>" + fanState + "</span></div>";
  html += "<div class='btn-row'>";
  html += "<a class='btn btn-on' href='/fan/on'>Fan ON</a>";
  html += "<a class='btn btn-off' href='/fan/off'>Fan OFF</a></div></div>";

  // Pump Control
  html += "<h2>Pump control</h2><div class='card'>";
  html += "<div class='status'><span class='label'>Pump</span>";
  html += "<span class='value' style='background:" + pumpColor + "'>" + pumpState + "</span></div>";
  html += "<div class='status'><span class='label'>Interlock</span>";
  html += "<span class='value' style='background:" + interlockColor + "'>" + interlockState + "</span></div>";
  if (pumpInterlocked) {
    html += "<div class='warn'>Pump locked - fan is not NORMAL. Fix fan to release.</div>";
  }
  html += "<div class='btn-row'>";
  html += "<a class='btn btn-on' href='/pump/on'>Pump ON</a>";
  html += "<a class='btn btn-off' href='/pump/off'>Pump OFF</a></div></div>";

  html += "<p style='text-align:center;color:#444;font-size:11px;margin-top:20px'>Auto-refreshes every 3s</p>";
  html += "</body></html>";
  return html;
}

void handleRoot()    { server.send(200, "text/html", buildWebPage()); }
void handleFanOn()   { setFan(true);   server.sendHeader("Location", "/"); server.send(302); }
void handleFanOff()  { setFan(false);  server.sendHeader("Location", "/"); server.send(302); }
void handlePumpOn()  { setPump(true);  server.sendHeader("Location", "/"); server.send(302); }
void handlePumpOff() { setPump(false); server.sendHeader("Location", "/"); server.send(302); }

void handleStatus() {
  String json = "{";
  json += "\"fan_state\":\"" + String(CLASS_LABELS[currentPrediction]) + "\",";
  json += "\"fan_running\":" + String(fanRunning ? "true" : "false") + ",";
  json += "\"pump_running\":" + String(pumpRunning ? "true" : "false") + ",";
  json += "\"pump_interlocked\":" + String(pumpInterlocked ? "true" : "false") + ",";
  json += "\"class\":" + String(currentPrediction);
  json += "}";
  server.send(200, "application/json", json);
}

// ══════════════════════════════════════════════════════════
//  WiFi SETUP
// ══════════════════════════════════════════════════════════
void setupWiFi() {
  Serial.print("  Connecting to WiFi: ");
  Serial.print(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected!");
    Serial.print("  IP Address: http://");
    Serial.println(WiFi.localIP());

    server.on("/",         handleRoot);
    server.on("/fan/on",   handleFanOn);
    server.on("/fan/off",  handleFanOff);
    server.on("/pump/on",  handlePumpOn);
    server.on("/pump/off", handlePumpOff);
    server.on("/status",   handleStatus);
    server.begin();
    Serial.println("  Web server started!");
  } else {
    Serial.println(" FAILED!");
    Serial.println("  Using Serial / MQTT control only.");
    Serial.println("  Check WIFI_SSID and WIFI_PASSWORD.");
  }
}

// ══════════════════════════════════════════════════════════
//  MQTT RECONNECT
// ══════════════════════════════════════════════════════════
void reconnectMQTT() {
  // Try once per loop iteration — do NOT block forever, otherwise
  // the web server stops responding when the broker is offline.
  if (mqttClient.connected()) return;

  Serial.print("Attempting MQTT connection...");
  if (mqttClient.connect("ESP32_CoolingTower08")) {
    Serial.println("connected");
    mqttClient.subscribe(topic_cmd_fan);
    mqttClient.subscribe(topic_cmd_motor);
  } else {
    Serial.print("failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" — will retry next loop");
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
  Serial.println();
  Serial.println("================================");

  if (pred == 1) Serial.println("  !! WARNING: Fan BLOCKED !!");
  if (pred == 2) Serial.println("  !! WARNING: Fan UNBALANCED !!");

  Serial.println("  Serial: '1'=Fan ON  '0'=Fan OFF");
  Serial.println("          '3'=Pump ON '2'=Pump OFF");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("  WiFi:   http://");
    Serial.println(WiFi.localIP());
  }
  Serial.print("  MQTT:   ");
  Serial.println(mqttClient.connected() ? "connected" : "disconnected");
}

// ══════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);  // Wait for S3 native USB

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

  // Startup banner
  Serial.println("\n========================================");
  Serial.println("  Fanalyzer — Cooling Tower System");
  Serial.println("  Board: ESP32-S3 DevKitC-1 N16R8");
  Serial.println("========================================");
  Serial.println("  Classes: NORMAL / BLOCKED / UNBALANCED / EMPTY / OFF");
  Serial.println("  LEDs: Green=Normal Red1=Blocked Red2=Unbalanced");
  Serial.println("  Buzzer: Double-beep on fault states");
  Serial.println("----------------------------------------");
  Serial.println("  SERIAL: '1'=Fan ON  '0'=Fan OFF");
  Serial.println("          '3'=Pump ON '2'=Pump OFF");
  Serial.println("  INTERLOCK: Non-NORMAL  -> pump stops");
  Serial.println("             NORMAL recovery -> pump restarts");
  Serial.println("========================================");

  // WiFi + Web Server
  setupWiFi();

  // MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  Serial.println("\n  System ready. Send '1' to start fan.\n");
}

// ══════════════════════════════════════════════════════════
//  MAIN LOOP
// ══════════════════════════════════════════════════════════
void loop() {
  // Keep WiFi up
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
  }

  // Handle web dashboard requests
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  // Keep MQTT up (non-blocking retry)
  if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Serial control
  handleSerialCommands();

  // Sensing + classification
  Features f = extractFeatures();
  int pred = nn_predict(f.dom_freq, f.max_amp, f.mean_energy);
  currentPrediction = pred;

  updateLEDs(pred);
  updateBuzzer(pred);
  updateInterlock(pred);
  printResult(f, pred);

  if (pred != lastPrediction) {
    Serial.print("  >> State changed: ");
    if (lastPrediction >= 0) Serial.print(CLASS_LABELS[lastPrediction]);
    else Serial.print("(startup)");
    Serial.print(" -> ");
    Serial.println(CLASS_LABELS[pred]);
    lastPrediction = pred;
  }

  // Publish telemetry via MQTT
  if (mqttClient.connected()) {
    String json = "{";
    json += "\"fan_state\":\"" + String(CLASS_LABELS[pred]) + "\",";
    json += "\"max_amplitude\":" + String(f.max_amp) + ",";
    json += "\"mean_energy\":" + String(f.mean_energy) + ",";
    json += "\"dom_freq\":" + String(f.dom_freq) + ",";
    json += "\"fan_running\":" + String(fanRunning ? "true" : "false") + ",";
    json += "\"pump_running\":" + String(pumpRunning ? "true" : "false") + ",";
    json += "\"pump_interlocked\":" + String(pumpInterlocked ? "true" : "false") + ",";
    json += "\"class\":" + String(pred);
    json += "}";
    mqttClient.publish(topic_data_out, json.c_str());
  }

  delay(2000);
}