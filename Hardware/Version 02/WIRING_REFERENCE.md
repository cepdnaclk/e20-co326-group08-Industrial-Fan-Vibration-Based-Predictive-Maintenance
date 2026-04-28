# Fanalyzer Complete Wiring Reference — ESP32-S3 DevKitC-1 (N16R8)

## IMPORTANT: GPIO26-37 are unavailable on this board (used by flash/PSRAM)

---

## MASTER GPIO PIN MAP

| ESP32-S3 Pin | Function       | Connects To                          | Direction     |
|-------------|----------------|--------------------------------------|---------------|
| 3.3V        | Power out       | MPU6050 VCC, Relay 1 VCC, Relay 2 VCC| Power         |
| GND         | Ground          | All 8 components (shared GND rail)   | Ground        |
| GPIO21      | I2C SDA         | MPU6050 SDA                          | Bidirectional |
| GPIO22      | I2C SCL         | MPU6050 SCL                          | Output        |
| GPIO4       | Digital out     | Green LED long leg (+) [Normal]      | Output        |
| GPIO5       | Digital out     | Red LED 1 long leg (+) [Blocked]     | Output        |
| GPIO6       | Digital out     | Red LED 2 long leg (+) [Unbalanced]  | Output        |
| GPIO7       | Digital out     | Relay 1 IN [Fan control]             | Output        |
| GPIO15      | Digital out     | Relay 2 IN [Pump control]            | Output        |
| GPIO16      | Digital out     | Buzzer (+) pin                       | Output        |

**Total: 10 pins used (3.3V, GND, 8 GPIOs), 25 wire connections**

---

## COMPONENT 1: MPU6050 Accelerometer (4 wires)

Mount firmly on fan motor housing with double-sided tape.

| Step | MPU6050 Pin | ESP32-S3 Pin | Notes                |
|------|-------------|--------------|----------------------|
| 1    | VCC         | 3.3V         | NOT 5V — will damage |
| 2    | GND         | GND          | To GND rail          |
| 3    | SDA         | GPIO21       | I2C data             |
| 4    | SCL         | GPIO22       | I2C clock            |

---

## COMPONENT 2: Relay 1 — Fan Control (5 connections)

**Control side (header pins):**

| Step | Relay Pin | ESP32-S3 Pin | Notes                              |
|------|-----------|--------------|------------------------------------|
| 1    | VCC       | 3.3V         | MUST be 3.3V, NOT VIN/5V           |
| 2    | GND       | GND          | Shared ground                      |
| 3    | IN        | GPIO7        | Active-LOW: LOW=ON, HIGH=OFF       |

**Switching side (screw terminals):**

| Step | Terminal      | Connects To                  |
|------|---------------|------------------------------|
| 4    | COM (middle)  | 12V adapter (+) positive     |
| 5    | NO (left)     | Fan (+) positive wire        |

Fan (-) goes DIRECTLY to 12V adapter (-). NC terminal empty.

---

## COMPONENT 3: Relay 2 — Pump Control (5 connections)

**Control side (header pins):**

| Step | Relay Pin | ESP32-S3 Pin | Notes                        |
|------|-----------|--------------|-----------------------------|
| 1    | VCC       | 3.3V         | Same 3.3V as Relay 1        |
| 2    | GND       | GND          | Shared ground               |
| 3    | IN        | GPIO15       | Active-LOW                  |

**Switching side (screw terminals):**

| Step | Terminal      | Connects To                        |
|------|---------------|------------------------------------|
| 4    | COM (middle)  | 12V adapter (+) (same as fan)      |
| 5    | NO (left)     | Pump (+) positive wire             |

Pump (-) goes DIRECTLY to 12V adapter (-). NC terminal empty.

---

## 12V POWER DISTRIBUTION

```
12V Adapter (+) ──┬── Relay 1 COM ── Relay 1 NO ── Fan (+)
                  │
                  └── Relay 2 COM ── Relay 2 NO ── Pump (+)

12V Adapter (-) ──┬── Fan (-)
                  │
                  └── Pump (-)
```

**The 12V adapter NEVER connects to the ESP32. ESP32 is powered by USB only.**

---

## COMPONENT 4: Green LED — Normal (3 connections)

| From              | To                    |
|-------------------|-----------------------|
| ESP32-S3 GPIO4    | LED long leg (+)      |
| LED short leg (-) | 220Ω resistor         |
| Resistor end      | GND rail              |

---

## COMPONENT 5: Red LED 1 — Blocked (3 connections)

| From              | To                    |
|-------------------|-----------------------|
| ESP32-S3 GPIO5    | LED long leg (+)      |
| LED short leg (-) | 220Ω resistor         |
| Resistor end      | GND rail              |

---

## COMPONENT 6: Red LED 2 — Unbalanced (3 connections)

| From              | To                    |
|-------------------|-----------------------|
| ESP32-S3 GPIO6    | LED long leg (+)      |
| LED short leg (-) | 220Ω resistor         |
| Resistor end      | GND rail              |

---

## COMPONENT 7: Active Buzzer (2 connections)

| Buzzer Pin | ESP32-S3 Pin |
|------------|--------------|
| (+)        | GPIO16       |
| (-)        | GND          |

---

## GROUND BUS (8 connections to breadboard GND rail)

| # | Component              |
|---|------------------------|
| 1 | ESP32-S3 GND pin       |
| 2 | MPU6050 GND            |
| 3 | Relay 1 GND            |
| 4 | Relay 2 GND            |
| 5 | Green LED resistor     |
| 6 | Red LED 1 resistor     |
| 7 | Red LED 2 resistor     |
| 8 | Buzzer (-)             |

---

## TESTING ORDER

| Order | Component      | Test                     | Expected             |
|-------|---------------|--------------------------|----------------------|
| 1     | MPU6050       | Step1_I2C_Scanner.ino    | Found: 0x68          |
| 2     | MPU6050       | Step2_MPU6050_Raw.ino    | Numbers printing     |
| 3     | All hardware  | Step3_Hardware_Test.ino  | Each component works |
| 4     | Data collect  | Step4_Data_Collect.ino   | 128 numbers + END    |
| 5     | Python        | python collect_data.py   | CSV file created     |
| 6     | Training      | python Train.py          | .h files generated   |
| 7     | Full system   | Upload firmware          | Classification works |

---

## SERIAL COMMANDS (115200 baud)

| Send | Action   |
|------|----------|
| 1    | Fan ON   |
| 0    | Fan OFF  |
| 3    | Pump ON  |
| 2    | Pump OFF |

## WIFI ENDPOINTS

| URL Path   | Function                            |
|------------|-------------------------------------|
| /          | Control page with buttons           |
| /fan/on    | Turn fan on                         |
| /fan/off   | Turn fan off                        |
| /pump/on   | Turn pump on                        |
| /pump/off  | Turn pump off                       |
| /status    | JSON status (for Node-RED)          |

---

## ARDUINO IDE SETTINGS (ESP32-S3)

| Setting           | Value              |
|-------------------|--------------------|
| Board             | ESP32S3 Dev Module |
| USB CDC On Boot   | Enabled            |
| Flash Size        | 16MB               |
| PSRAM             | OPI PSRAM          |
| Port              | (auto-detected)    |

---

## KEY LESSONS

1. **Relay VCC must be 3.3V** (not 5V/VIN) — 5V causes relay to stay permanently on
2. **Test jumper wires** — faulty wires waste hours
3. **Test GPIO pins** with LED blink before committing
4. **GPIO26-37 unavailable** on N16R8 variant (used by flash/PSRAM)
5. **Close Serial Monitor** before running Python scripts
6. **nn_model.h must be in same folder** as .ino file
7. **Change WiFi credentials** before uploading firmware
8. **Use USB port** (not UART port) on ESP32-S3 DevKitC-1
