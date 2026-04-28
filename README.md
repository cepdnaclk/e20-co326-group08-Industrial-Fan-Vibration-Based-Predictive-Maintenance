# Edge-to-Cloud Digital Twin for Cooling Tower Fan Predictive Maintenance

## Group Members
* M.L De Croos Rubin - E/20/054
* A.H.D. Kawya - E/20/197
* K.W.I.T. Kumarasinghe - E/20/211
* D.M.B. Edirisooriya - E/20/093

## Project Description
This project implements a 4-layer Cyber-Physical System (CPS) and Digital Twin to monitor the health of an industrial cooling tower fan. By capturing real-time vibration data using an MPU6050 accelerometer, the system performs Edge AI inference (Decision Tree) directly on an ESP32 microcontroller to classify the fan's state as Normal, Blocked, or Unbalanced. 

The data is securely transmitted via MQTT to a local containerized cloud stack. The system not only visualizes real-time telemetry but also calculates the Remaining Useful Life (RUL) of the fan based on accumulated mechanical stress (mean vibration energy) and allows for bidirectional control to remotely shut down the asset in critical conditions.

## System Architecture
Our solution strictly adheres to the 4-Layer Industrial IoT architecture:

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
* Connect the MPU6050 to the ESP32 via I2C (SDA to GPIO 21, SCL to GPIO 22). Power it using 3.3V.
* Connect the Relay module control pin to GPIO 4.
* Flash the ESP32 using the Arduino IDE with the provided `.ino` sketch and `dt_model.h` file.

### 2. Cloud Infrastructure Setup
Ensure Docker and Docker Compose are installed on your host machine.
1. Clone this repository: `https://github.com/cepdnaclk/e20-co326-group08-Industrial-Fan-Vibration-Based-Predictive-Maintenance.git`
2. Navigate to the project root: `cd e20-co326-group08-Industrial-Fan-Vibration-Based-Predictive-Maintenance`
3. Spin up the microservices: 
   ```bash
   docker-compose up -d
