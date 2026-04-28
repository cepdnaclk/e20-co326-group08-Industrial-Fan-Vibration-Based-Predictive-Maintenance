# Fanalyzer — Complete Project Files
## ESP32-S3 DevKitC-1 (N16R8)

### File List

```
├── README.md                          <- This file
├── WIRING_REFERENCE.md                <- Every wire connection
│
├── Step1_I2C_Scanner/
│   └── Step1_I2C_Scanner.ino          <- Verify MPU6050 detected
│
├── Step2_MPU6050_Raw_Values/
│   └── Step2_MPU6050_Raw_Values.ino   <- Read raw sensor values
│
├── Step3_Hardware_Test/
│   └── Step3_Hardware_Test.ino        <- Test LEDs, buzzer, relays
│
├── Step4_Data_Collect/
│   └── Step4_Data_Collect.ino         <- Data collection firmware
│
├── collect_data.py                    <- Python data collector
├── Train.py                           <- Train 3 ML models
│
└── fanalyzer_neural_network/
    └── fanalyzer_neural_network.ino   <- Main firmware (put nn_model.h here)
```

### Step-by-Step Process

#### Phase 1: Verify Hardware
1. Wire MPU6050 only (4 wires — see WIRING_REFERENCE.md)
2. Arduino IDE: Board = "ESP32S3 Dev Module", USB CDC On Boot = Enabled
3. Upload Step1_I2C_Scanner.ino → should print "Found: 0x68"
4. Upload Step2_MPU6050_Raw_Values.ino → should print accelerometer numbers
5. Wire all remaining components (LEDs, buzzer, relays — NO 12V yet)
6. Upload Step3_Hardware_Test.ino → verify each component responds

#### Phase 2: Collect Data
7. Upload Step4_Data_Collect.ino
8. CLOSE Serial Monitor
9. Edit collect_data.py — change PORT to your COM port
10. Run: python collect_data.py
11. Collect 200 windows for each of 4 states (Normal, Blocked, Unbalanced, Off)
12. Output: fan_dataset.csv (800 rows)

#### Phase 3: Train Models
13. Copy fan_dataset.csv to same folder as Train.py
14. Run: python Train.py
15. Output: dt_model.h, rf_model.h, nn_model.h
16. Copy nn_model.h into fanalyzer_neural_network/ folder

#### Phase 4: Deploy
17. Edit fanalyzer_neural_network.ino — change WIFI_SSID and WIFI_PASSWORD
18. Upload fanalyzer_neural_network.ino
19. Open Serial Monitor at 115200
20. Send '1' to turn fan ON, '3' for pump ON
21. Open ESP32's IP address in browser for web control

#### Phase 5: Docker (for MQTT/Node-RED/InfluxDB/Grafana)
22. See docker-compose.yml (separate setup — next phase of project)

### Required Software
- Arduino IDE 2.x with ESP32 board support
- Arduino libraries: MPU6050 (Electronic Cats), arduinoFFT (v2.x)
- Python 3.11 with: pyserial, numpy, pandas, scikit-learn, tensorflow, micromlgen
- Docker Desktop (for server-side infrastructure)
