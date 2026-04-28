// fanalyzer_neural_network.ino
// COMPLETE FIRMWARE: Fan + Pump + LEDs + Buzzer + Serial + WiFi Control
//
// ═══════════════════════════════════════════════════════════════
//  COOLING TOWER LOGIC:
//    - Fan blows air across cooling tower
//    - Pump circulates water through the tower
//    - If fan is Blocked or Unbalanced → pump auto-shuts down
//      (no point pumping water with no airflow)
//    - When fan returns to Normal → pump auto-restarts
//      (if it was running before the fault)
// ═══════════════════════════════════════════════════════════════
//
// SERIAL COMMANDS:
//   '1' → Fan ON       '0' → Fan OFF
//   '3' → Pump ON      '2' → Pump OFF
//
// WIFI CONTROL:
//   Open the ESP32's IP address in a browser (shown on Serial Monitor)
//   Buttons on the web page to control fan and pump
//
// GPIO MAP:
//   GPIO21 = MPU6050 SDA (I2C)
//   GPIO22 = MPU6050 SCL (I2C)
//   GPIO25 = Green LED   (Normal state)
//   GPIO27 = Red LED 1   (Blocked state)
//   GPIO14 = Red LED 2   (Unbalanced state)
//   GPIO32 = Relay 1 IN  (Fan control, active-LOW)
//   GPIO15 = Relay 2 IN  (Pump control, active-LOW)
//   GPIO33 = Buzzer      (Alarm for fault states)

#include <Wire.h>
#include <MPU6050.h>
#include <arduinoFFT.h>
#include <WiFi.h>
#include <WebServer.h>
#include "nn_model.h"

// ── WiFi credentials ─────────────────────────────────────────
// CHANGE THESE to your WiFi network
const char* WIFI_SSID     = "Dasu";
const char* WIFI_PASSWORD = "Dasuni#2001";

// ── Sensor & FFT config ──────────────────────────────────────
#define N_SAMPLES       128
#define SAMPLE_RATE_HZ  500
#define SAMPLE_INTERVAL (1000000 / SAMPLE_RATE_HZ)

// ── Pin definitions ──────────────────────────────────────────
#define PIN_RELAY_FAN   32    // Relay 1: Fan (active-LOW)
#define PIN_RELAY_PUMP  15    // Relay 2: Pump (active-LOW)
#define PIN_LED_NORMAL  25    // Green LED
#define PIN_LED_BLOCKED 27    // Red LED 1
#define PIN_LED_UNBAL   14    // Red LED 2
#define PIN_BUZZER      33    // Active buzzer

// ── Relay helpers (hide active-LOW logic) ────────────────────
// Both relays are active-LOW: LOW = relay ON, HIGH = relay OFF
#define FAN_ON()    digitalWrite(PIN_RELAY_FAN, LOW)
#define FAN_OFF()   digitalWrite(PIN_RELAY_FAN, HIGH)
#define PUMP_ON()   digitalWrite(PIN_RELAY_PUMP, LOW)
#define PUMP_OFF()  digitalWrite(PIN_RELAY_PUMP, HIGH)

// ── Objects ──────────────────────────────────────────────────
MPU6050 mpu;
double vReal[N_SAMPLES];
double vImag[N_SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, N_SAMPLES, SAMPLE_RATE_HZ);
WebServer server(80);

const char* CLASS_LABELS[] = {"NORMAL", "BLOCKED", "UNBALANCED", "OFF"};

// ── State tracking ───────────────────────────────────────────
bool fanRunning      = false;   // Has user commanded fan ON?
bool pumpRunning     = false;   // Has user commanded pump ON?
bool pumpInterlocked = false;   // Is pump blocked by interlock?
bool pumpWasRunning  = false;   // Was pump on before interlock triggered?
int  lastPrediction  = -1;
int  currentPrediction = 3;     // Start as OFF

struct Features { float dom_freq, max_amp, mean_energy; };

// ══════════════════════════════════════════════════════════════
//  FEATURE EXTRACTION
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
//  STANDARD SCALER
// ══════════════════════════════════════════════════════════════
void scaleFeatures(float* input, float* output) {
  for (int i = 0; i < 3; i++) {
    output[i] = (input[i] - SCALER_MEAN[i]) / SCALER_STD[i];
  }
}

// ══════════════════════════════════════════════════════════════
//  SOFTMAX
// ══════════════════════════════════════════════════════════════
void softmax(float* x, int n) {
  float maxVal = x[0];
  for (int i = 1; i < n; i++) if (x[i] > maxVal) maxVal = x[i];
  float sum = 0;
  for (int i = 0; i < n; i++) { x[i] = exp(x[i] - maxVal); sum += x[i]; }
  for (int i = 0; i < n; i++) x[i] /= sum;
}

// ══════════════════════════════════════════════════════════════
//  NEURAL NETWORK FORWARD PASS
// ══════════════════════════════════════════════════════════════
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

// ══════════════════════════════════════════════════════════════
//  LED CONTROL
// ══════════════════════════════════════════════════════════════
void updateLEDs(int prediction) {
  digitalWrite(PIN_LED_NORMAL,  LOW);
  digitalWrite(PIN_LED_BLOCKED, LOW);
  digitalWrite(PIN_LED_UNBAL,   LOW);

  switch (prediction) {
    case 0: digitalWrite(PIN_LED_NORMAL,  HIGH); break;
    case 1: digitalWrite(PIN_LED_BLOCKED, HIGH); break;
    case 2: digitalWrite(PIN_LED_UNBAL,   HIGH); break;
    case 3: break;  // All off when fan is off
  }
}

// ══════════════════════════════════════════════════════════════
//  BUZZER CONTROL — double beep for fault states
// ══════════════════════════════════════════════════════════════
void updateBuzzer(int prediction) {
  if (prediction == 1 || prediction == 2) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(200);
    digitalWrite(PIN_BUZZER, LOW);
    delay(200);
    digitalWrite(PIN_BUZZER, HIGH);
    delay(200);
    digitalWrite(PIN_BUZZER, LOW);
  } else {
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// ══════════════════════════════════════════════════════════════
//  COOLING TOWER INTERLOCK LOGIC
//  If fan is blocked or unbalanced → pump must stop
//  When fan recovers → pump auto-restarts if it was running
// ══════════════════════════════════════════════════════════════
void updateInterlock(int prediction) {
  bool faultDetected = (prediction == 1 || prediction == 2);

  if (faultDetected && !pumpInterlocked) {
    // Fault just detected — engage interlock
    pumpWasRunning = pumpRunning;  // Remember if pump was on
    if (pumpRunning) {
      PUMP_OFF();
      pumpRunning = false;
      pumpInterlocked = true;
      Serial.println("  !! INTERLOCK: Pump shut down — fan fault detected !!");
    } else {
      pumpInterlocked = true;
    }
  }
  else if (!faultDetected && pumpInterlocked) {
    // Fault cleared — release interlock
    pumpInterlocked = false;
    if (pumpWasRunning) {
      PUMP_ON();
      pumpRunning = true;
      Serial.println("  >> INTERLOCK RELEASED: Pump restarted — fan normal <<");
    }
    pumpWasRunning = false;
  }
}

// ══════════════════════════════════════════════════════════════
//  FAN CONTROL FUNCTION (used by serial + WiFi)
// ══════════════════════════════════════════════════════════════
void setFan(bool on) {
  if (on) {
    FAN_ON();
    fanRunning = true;
    Serial.println("\n*** FAN ON ***");
  } else {
    FAN_OFF();
    fanRunning = false;
    Serial.println("\n*** FAN OFF ***");
  }
}

// ══════════════════════════════════════════════════════════════
//  PUMP CONTROL FUNCTION (used by serial + WiFi)
// ══════════════════════════════════════════════════════════════
void setPump(bool on) {
  if (pumpInterlocked && on) {
    Serial.println("\n*** PUMP BLOCKED — Fan fault active! Fix fan first. ***");
    return;
  }
  if (on) {
    PUMP_ON();
    pumpRunning = true;
    Serial.println("\n*** PUMP ON ***");
  } else {
    PUMP_OFF();
    pumpRunning = false;
    Serial.println("\n*** PUMP OFF ***");
  }
}

// ══════════════════════════════════════════════════════════════
//  SERIAL COMMAND HANDLER
//  '1' = Fan ON    '0' = Fan OFF
//  '3' = Pump ON   '2' = Pump OFF
// ══════════════════════════════════════════════════════════════
void handleSerialCommands() {
  while (Serial.available()) {
    char cmd = Serial.read();
    switch (cmd) {
      case '1': setFan(true);   break;
      case '0': setFan(false);  break;
      case '3': setPump(true);  break;
      case '2': setPump(false); break;
      default: break;  // Ignore newline/carriage return
    }
  }
}

// ══════════════════════════════════════════════════════════════
//  WiFi WEB SERVER — control page with buttons
// ══════════════════════════════════════════════════════════════
String buildWebPage() {
  String fanState    = fanRunning ? "ON" : "OFF";
  String pumpState   = pumpRunning ? "ON" : "OFF";
  String fanClass    = CLASS_LABELS[currentPrediction];
  String interlockState = pumpInterlocked ? "ACTIVE" : "Clear";

  String fanColor    = fanRunning ? "#27ae60" : "#e74c3c";
  String pumpColor   = pumpRunning ? "#27ae60" : "#e74c3c";
  String interlockColor = pumpInterlocked ? "#e74c3c" : "#27ae60";

  String stateColor;
  switch (currentPrediction) {
    case 0: stateColor = "#27ae60"; break;
    case 1: stateColor = "#e74c3c"; break;
    case 2: stateColor = "#e67e22"; break;
    case 3: stateColor = "#95a5a6"; break;
    default: stateColor = "#95a5a6";
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='3'>";
  html += "<title>Fanalyzer Control</title>";
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
  html += "<p style='text-align:center;color:#666;font-size:12px'>Cooling Tower Control System</p>";

  // Fan Health Status
  html += "<h2>Fan health</h2>";
  html += "<div class='card'>";
  html += "<div class='status'><span class='label'>Classification</span>";
  html += "<span class='value' style='background:" + stateColor + "'>" + fanClass + "</span></div>";
  html += "</div>";

  // Fan Control
  html += "<h2>Fan control</h2>";
  html += "<div class='card'>";
  html += "<div class='status'><span class='label'>Fan</span>";
  html += "<span class='value' style='background:" + fanColor + "'>" + fanState + "</span></div>";
  html += "<div class='btn-row'>";
  html += "<a class='btn btn-on' href='/fan/on'>Fan ON</a>";
  html += "<a class='btn btn-off' href='/fan/off'>Fan OFF</a>";
  html += "</div></div>";

  // Pump Control
  html += "<h2>Pump control</h2>";
  html += "<div class='card'>";
  html += "<div class='status'><span class='label'>Pump</span>";
  html += "<span class='value' style='background:" + pumpColor + "'>" + pumpState + "</span></div>";
  html += "<div class='status'><span class='label'>Interlock</span>";
  html += "<span class='value' style='background:" + interlockColor + "'>" + interlockState + "</span></div>";

  if (pumpInterlocked) {
    html += "<div class='warn'>Pump locked out — fan fault detected. Fix fan to release interlock.</div>";
  }

  html += "<div class='btn-row'>";
  html += "<a class='btn btn-on' href='/pump/on'>Pump ON</a>";
  html += "<a class='btn btn-off' href='/pump/off'>Pump OFF</a>";
  html += "</div></div>";

  html += "<p style='text-align:center;color:#444;font-size:11px;margin-top:20px'>Auto-refreshes every 3 seconds</p>";
  html += "</body></html>";
  return html;
}

void handleRoot()    { server.send(200, "text/html", buildWebPage()); }
void handleFanOn()   { setFan(true);   server.sendHeader("Location", "/"); server.send(302); }
void handleFanOff()  { setFan(false);  server.sendHeader("Location", "/"); server.send(302); }
void handlePumpOn()  { setPump(true);  server.sendHeader("Location", "/"); server.send(302); }
void handlePumpOff() { setPump(false); server.sendHeader("Location", "/"); server.send(302); }

// JSON status endpoint (useful for Node-RED / future integrations)
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

// ══════════════════════════════════════════════════════════════
//  PRINT RESULT
// ══════════════════════════════════════════════════════════════
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
  Serial.print("  WiFi:   http://");
  Serial.println(WiFi.localIP());
}

// ══════════════════════════════════════════════════════════════
//  WIFI SETUP
// ══════════════════════════════════════════════════════════════
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

    // Set up web server routes
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
    Serial.println("  WiFi not available — using Serial control only.");
    Serial.println("  Check SSID and password at the top of the sketch.");
  }
}

// ══════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  // I2C + Sensor
  Wire.begin(21, 22);
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

  // Startup message
  Serial.println("\n========================================");
  Serial.println("  Fanalyzer — Cooling Tower System");
  Serial.println("  Fan + Pump + Interlock + WiFi");
  Serial.println("========================================");
  Serial.println("  Classes: NORMAL / BLOCKED / UNBALANCED / OFF");
  Serial.println("  LEDs: Green=Normal, Red1=Blocked, Red2=Unbalanced");
  Serial.println("  Buzzer: Beeps on fault states");
  Serial.println("----------------------------------------");
  Serial.println("  SERIAL CONTROLS:");
  Serial.println("    '1' = Fan ON     '0' = Fan OFF");
  Serial.println("    '3' = Pump ON    '2' = Pump OFF");
  Serial.println("----------------------------------------");
  Serial.println("  INTERLOCK:");
  Serial.println("    Blocked/Unbalanced -> Pump auto-stops");
  Serial.println("    Fan recovers -> Pump auto-restarts");
  Serial.println("========================================");

  // WiFi
  setupWiFi();

  Serial.println("\n  System ready. Send '1' to start fan.\n");
}

// ══════════════════════════════════════════════════════════════
//  MAIN LOOP
// ══════════════════════════════════════════════════════════════
void loop() {
  // Handle WiFi client requests
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  // Handle serial commands
  handleSerialCommands();

  // Read sensor + classify
  Features f = extractFeatures();
  int pred = nn_predict(f.dom_freq, f.max_amp, f.mean_energy);
  currentPrediction = pred;

  // Update LEDs
  updateLEDs(pred);

  // Buzzer for fault states
  updateBuzzer(pred);

  // Cooling tower interlock logic
  updateInterlock(pred);

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
