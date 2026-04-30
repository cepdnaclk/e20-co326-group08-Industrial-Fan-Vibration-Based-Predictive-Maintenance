# Edge-to-Cloud Digital Twin for Cooling Tower Fan Predictive Maintenance

## Group Members
* M.L De Croos Rubin - E/20/054
* A.H.D. Kawya - E/20/197
* K.W.I.T. Kumarasinghe - E/20/211
* D.M.B. Edirisooriya - E/20/093

## Project Description
This project implements a 4-layer Cyber-Physical System (CPS) and Digital Twin to monitor the health of an industrial cooling tower fan. By capturing real-time vibration data using an MPU6050 accelerometer, the system performs Edge AI inference (Decision Tree) directly on an ESP32 microcontroller to classify the fan's state as Normal, Blocked, or Unbalanced. 

<img width="1600" height="900" alt="image" align="center" src="https://github.com/user-attachments/assets/4fd58b7f-a38a-4398-858e-ffee46812b32" />

The data is securely transmitted via MQTT to a local containerized cloud stack. The system not only visualizes real-time telemetry but also calculates the Remaining Useful Life (RUL) of the fan based on accumulated mechanical stress (mean vibration energy) and allows for bidirectional control to remotely shut down the asset in critical conditions.

## System Architecture
Our solution strictly adheres to the 4-Layer Industrial IoT architecture:
<img width="1600" height="1142" alt="image" align="center" src="https://github.com/user-attachments/assets/225b4f5e-57fe-4a58-86f8-120caace0017" />

1. **Perception & Edge Layer (ESP32-S3):** * **Sensor:** MPU6050 Accelerometer for vibration data acquisition.
   * **Actuator:** 5V Relay module for bidirectional fan control.
   * **Edge AI:** C++ implementation of a Decision Tree model using Fast Fourier Transform (FFT) feature extraction (Dominant Frequency, Max Amplitude, Mean Energy).
2. **Transport Layer (Eclipse Mosquitto):** * MQTT Broker handling messaging using the Sparkplug B Unified Namespace (UNS) topic structure. Features Last Will and Testament (LWT) for reliability.
3. **Edge-Logic Layer (Node-RED):** * Subscribes to MQTT topics, orchestrates flow logic, calculates Remaining Useful Life (RUL) using linear degradation thresholds, and formats data for storage.
4. **Application Layer (InfluxDB & Grafana):**
   * **Historian:** InfluxDB v2.7 stores all time-series telemetry.
   * **Digital Twin:** Grafana visualizes the live state, RUL trajectory, and provides a UI for sending remote shutdown commands back to the ESP32.

## How to Run

### 1. Hardware Setup

#### Hardware Required

| Component | Qty |
|-----------|-----|
| ESP32-S3 DevKitC-1 (N16R8) | 1 |
| MPU6050 accelerometer | 1 |
| 5V Relay Module (JQC3F) | 2 |
| 12V DC Fan (PC fan) | 1 |
| 12V DC Water Pump | 1 |
| 12V DC Adapter for fan | 1 |
| 12V DC Adapter for pump | 1 |
| Green LED + 220Ω resistor | 1 |
| Red LED + 220Ω resistor | 2 |
| Active Buzzer | 1 |
| Breadboard + jumper wires | 1 set |

#### Wiring

#### ESP32-S3 Connections

| ESP32-S3 Pin | Connects To |
|-------------|-------------|
| 3.3V | MPU6050 VCC |
| VIN (5V) | Relay 1 VCC, Relay 2 VCC |
| GND | All components (shared GND rail) |
| GPIO8 | MPU6050 SDA |
| GPIO9 | MPU6050 SCL |
| GPIO4 | Green LED (+) — Normal |
| GPIO5 | Red LED 1 (+) — Blocked |
| GPIO6 | Red LED 2 (+) — Unbalanced |
| GPIO7 | Relay 1 IN — Fan |
| GPIO15 | Relay 2 IN — Pump |
| GPIO6 | Buzzer (+) |

#### Relay Switching Side

| Terminal | Relay 1 (Fan) | Relay 2 (Pump) |
|----------|--------------|----------------|
| COM (middle) | Fan adapter (+) | Pump adapter (+) |
| NO (left) | Fan (+) wire | Pump (+) wire |
| NC (right) | Empty | Empty |

Fan (-) → Fan adapter (-) directly. Pump (-) → Pump adapter (-) directly.

#### LEDs

Each: `GPIO → LED long leg (+) → LED short leg (-) → 220Ω resistor → GND`

#### Buzzer

(+) → GPIO16, (-) → GND

#### Ground Bus

Connect all component grounds to one shared breadboard GND rail.

### How to Run

#### 1. Arduino IDE Setup

- Board: **ESP32S3 Dev Module**
- USB CDC On Boot: **Enabled**
- Flash Size: **16MB**
- PSRAM: **OPI PSRAM**
- Install libraries: **MPU6050** (Electronic Cats), **arduinoFFT** (v2.x)

#### 2. Upload Firmware

1. Place `nn_model.h` in the same folder as `fanalyzer_neural_network.ino`.
2. Edit the WiFi credentials at the top of the `.ino` file:
   ```cpp
   const char* WIFI_SSID     = "YOUR_WIFI_NAME";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   ```
3. Upload to ESP32-S3 via the **USB** port (not UART).
4. Open Serial Monitor at **115200** baud.

#### 3. Control the System

**Serial commands:**

| Send | Action |
|------|--------|
| `1` | Fan ON |
| `0` | Fan OFF |
| `3` | Pump ON |
| `2` | Pump OFF |

**WiFi:** Open the IP address shown in Serial Monitor in any browser. Control page with buttons auto-refreshes every 3 seconds.

#### 4. Interlock Behavior

| Fan State | Pump |
|-----------|------|
| Normal | Runs normally |
| Blocked | Auto-stops |
| Unbalanced | Auto-stops |
| Returns to Normal | Auto-restarts (if it was on before) |

Manually starting the pump during a fault is refused.

### Relay Notes

The JQC3F module is **transistor-driven, active-HIGH**. VCC connects to **VIN (5V)**, not 3.3V. The code handles this with `FAN_ON() = HIGH` and `FAN_OFF() = LOW`.
### 2. Cloud Infrastructure Setup
Ensure Docker and Docker Compose are installed on your host machine.
1. Clone this repository: `https://github.com/cepdnaclk/e20-co326-group08-Industrial-Fan-Vibration-Based-Predictive-Maintenance.git`
2. Navigate to the project root: `cd e20-co326-group08-Industrial-Fan-Vibration-Based-Predictive-Maintenance`
3. Spin up the microservices: 
   ```bash
   docker-compose up -d
