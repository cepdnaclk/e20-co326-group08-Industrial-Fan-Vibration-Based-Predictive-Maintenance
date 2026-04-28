# collect_data.py
# PURPOSE: Collect labeled vibration data from ESP32-S3 for ML training
#
# PREREQUISITES:
#   pip install pyserial
#
# BEFORE RUNNING:
#   1. Upload Step4_Data_Collect.ino to ESP32-S3
#   2. CLOSE the Arduino Serial Monitor (only one program can use COM port)
#   3. Find your COM port in Device Manager > Ports
#   4. Edit PORT below to match
#
# HOW TO RUN:
#   cd C:\fanalyzer\data_collection
#   python collect_data.py
#
# OUTPUT: fan_dataset.csv with 800 rows (200 per class)
#         129 columns (s0-s127 samples + label)

import serial
import time
import csv
import os
import sys
sys.stdout.reconfigure(encoding='utf-8')

# ─── CHANGE THIS TO YOUR COM PORT ─────────────────────────
PORT = "COM9"       # Windows: check Device Manager > Ports
                    # Mac/Linux: "/dev/ttyUSB0" or "/dev/ttyACM0"
BAUD = 115200
N_SAMPLES = 128
SAMPLES_PER_CLASS = 200  # 200 windows per fan state
# ──────────────────────────────────────────────────────────

CLASSES = {
    '0': 'normal',
    '1': 'blocked',
    '2': 'unbalanced',
    '4': 'off'
}

OUTPUT_FILE = "fan_dataset.csv"


def collect_one_window(ser):
    """Send 'C' command, read 128 samples, return as list."""
    ser.flushInput()          # clear any leftover data
    ser.write(b'C')
    time.sleep(0.05)          # small pause after sending command
    samples = []
    timeout_count = 0

    while len(samples) < N_SAMPLES:
        line = ser.readline().decode('utf-8', errors='ignore').strip()

        if line == "END":
            break
        if line == "" or line == "READY":
            timeout_count += 1
            if timeout_count > 20:   # give up after too many empty lines
                return None
            continue
        try:
            samples.append(float(line))
        except ValueError:
            pass  # skip any non-numeric lines

    return samples if len(samples) == N_SAMPLES else None


def main():
    print("=" * 50)
    print("  Fanalyzer - Data Collection Tool")
    print("  Collects vibration data for ML training")
    print("=" * 50)

    # Open serial port
    try:
        ser = serial.Serial(PORT, BAUD, timeout=5)
        time.sleep(2)  # Wait for ESP32-S3 to reset after serial open
        ser.flushInput()
        print(f"  Connected to {PORT}")
    except Exception as e:
        print(f"\n  ERROR: Could not open {PORT}: {e}")
        print(f"  Check:")
        print(f"    1. Is the ESP32-S3 plugged in?")
        print(f"    2. Is Arduino Serial Monitor closed?")
        print(f"    3. Is the COM port correct? Check Device Manager.")
        return

    # Prepare CSV file
    file_exists = os.path.exists(OUTPUT_FILE)
    csv_file = open(OUTPUT_FILE, 'a', newline='')
    writer = csv.writer(csv_file)

    if not file_exists:
        # Write header row
        header = [f"s{i}" for i in range(N_SAMPLES)] + ["label"]
        writer.writerow(header)
        print(f"  Created new file: {OUTPUT_FILE}")
    else:
        print(f"  Appending to existing: {OUTPUT_FILE}")

    try:
        while True:
            print("\n─── Select Fan Condition ───────────────────")
            print("  Press 0 → NORMAL      (fan running freely)")
            print("  Press 1 → BLOCKED     (obstruct the blades)")
            print("  Press 2 → UNBALANCED  (coin taped to blade)")
            print("  Press 4 → OFF         (fan powered off)")
            print("  Press Q → Quit and save")
            print("────────────────────────────────────────────")

            choice = input("  Your choice: ").strip().upper()

            if choice == 'Q':
                break

            if choice not in CLASSES:
                print("  Invalid choice. Try again.")
                continue

            label = int(choice)
            class_name = CLASSES[choice]

            print(f"\n  Set fan to: {class_name.upper()}")
            print(f"  Make sure the fan is in the correct state!")
            input(f"  Press ENTER when ready...")

            print(f"  Collecting {SAMPLES_PER_CLASS} windows for '{class_name}'...")
            collected = 0

            while collected < SAMPLES_PER_CLASS:
                samples = collect_one_window(ser)

                if samples is None:
                    print("  ⚠ Bad read, retrying...")
                    continue

                row = samples + [label]
                writer.writerow(row)
                csv_file.flush()
                collected += 1

                # Progress bar
                progress = int((collected / SAMPLES_PER_CLASS) * 20)
                bar = "█" * progress + "░" * (20 - progress)
                print(f"\r  [{bar}] {collected}/{SAMPLES_PER_CLASS}", end='', flush=True)

                time.sleep(0.1)

            print(f"\n  ✓ Collected {collected} windows for '{class_name}'!")

    finally:
        csv_file.close()
        ser.close()
        print(f"\n  Dataset saved to: {OUTPUT_FILE}")
        print(f"  You can now run Train.py!")


if __name__ == "__main__":
    main()
