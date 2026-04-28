# # collect_data.py
# # Run this script to collect labeled vibration data from your fan

# import serial
# import time
# import csv
# import os
# import sys
# sys.stdout.reconfigure(encoding='utf-8')

# # ─── CONFIGURE THIS ───────────────────────────────────────────────
# PORT = "COM7"       # Windows: "COM3", "COM4" etc.
#                     # Mac/Linux: "/dev/ttyUSB0" or "/dev/ttyACM0"
# BAUD = 115200
# N_SAMPLES = 128
# SAMPLES_PER_CLASS = 200  # How many windows to collect per class
# # ──────────────────────────────────────────────────────────────────

# CLASSES = {
#     '0': 'normal',
#     '1': 'blocked',
#     '2': 'unbalanced',
#     '4': 'off'
# }

# OUTPUT_FILE = "fan_dataset.csv"


# def collect_one_window(ser):
#     ser.flushInput()          # clear any leftover data first
#     ser.write(b'C')
#     time.sleep(0.2)          # small pause after sending command
#     samples = []
#     timeout_count = 0

#     while len(samples) < N_SAMPLES:
#         line = ser.readline().decode('utf-8', errors='ignore').strip()
#         if line == "END":
#             break
#         if line == "" or line == "READY":
#             timeout_count += 1
#             if timeout_count > 20:   # give up after too many empty lines
#                 return None
#             continue
#         try:
#             samples.append(float(line))
#         except ValueError:
#             pass

#     return samples if len(samples) == N_SAMPLES else None


# def main():
#     print("=" * 50)
#     print("  Fanalyzer - Data Collection Tool")
#     print("=" * 50)

#     # Open serial port
#     try:
#         ser = serial.Serial(PORT, BAUD, timeout=5)
#         time.sleep(2)  # Wait for ESP32 to reset
#         ser.flushInput()
#         print(f"Connected to {PORT}")
#     except Exception as e:
#         print(f"Could not open {PORT}: {e}")
#         print("Check your COM port in Device Manager / Arduino IDE")
#         return

#     # Prepare CSV file
#     file_exists = os.path.exists(OUTPUT_FILE)
#     csv_file = open(OUTPUT_FILE, 'a', newline='')
#     writer = csv.writer(csv_file)

#     if not file_exists:
#         # Write header
#         header = [f"s{i}" for i in range(N_SAMPLES)] + ["label"]
#         writer.writerow(header)
#         print(f"Created new dataset file: {OUTPUT_FILE}")
#     else:
#         print(f"Appending to existing: {OUTPUT_FILE}")

#     try:
#         while True:
#             print("\n─── Select Fan Condition ───────────────────")
#             for key, name in CLASSES.items():
#                 print(f"  Press {key} → {name.upper()}")
#             print("  Press Q → Quit")
#             print("────────────────────────────────────────────")

#             choice = input("Your choice: ").strip().upper()

#             if choice == 'Q':
#                 break

#             if choice not in CLASSES:
#                 print("Invalid choice. Try again.")
#                 continue

#             label = int(choice)
#             class_name = CLASSES[choice]

#             print(f"\nSet fan to: {class_name.upper()}")
#             input(f"Press ENTER when fan is running in '{class_name}' condition...")

#             print(f"Collecting {SAMPLES_PER_CLASS} windows for '{class_name}'...")
#             collected = 0

#             while collected < SAMPLES_PER_CLASS:
#                 samples = collect_one_window(ser)

#                 if samples is None:
#                     print("  ⚠ Bad read, retrying...")
#                     continue

#                 row = samples + [label]
#                 writer.writerow(row)
#                 csv_file.flush()
#                 collected += 1

#                 # Progress indicator
#                 progress = int((collected / SAMPLES_PER_CLASS) * 20)
#                 bar = "█" * progress + "░" * (20 - progress)
#                 print(f"\r  [{bar}] {collected}/{SAMPLES_PER_CLASS}", end='', flush=True)

#                 time.sleep(0.1)

#             print(f"\nCollected {collected} windows for '{class_name}'!")

#     finally:
#         csv_file.close()
#         ser.close()
#         print(f"\nDataset saved to: {OUTPUT_FILE}")
#         print("You can now run the training scripts!")


# if __name__ == "__main__":
#     main()

# collect_data.py
# Run this script to collect labeled vibration data from your fan

import serial
import time
import csv
import os
import sys
sys.stdout.reconfigure(encoding='utf-8')

# ─── CONFIGURE THIS ───────────────────────────────────────────────
PORT = "COM7"       # Change if needed
BAUD = 115200
N_SAMPLES = 128
SAMPLES_PER_CLASS = 200
# ──────────────────────────────────────────────────────────────────

CLASSES = {
    '0': 'normal',
    '1': 'blocked',
    '2': 'unbalanced',
    '4': 'off'
}

OUTPUT_FILE = "fan_dataset.csv"


def collect_one_window(ser):
    ser.reset_input_buffer()   # clear junk data

    ser.write(b'C')
    time.sleep(0.2)

    samples = []
    started = False   # ensures we only collect real numeric data

    while True:
        line = ser.readline().decode('utf-8', errors='ignore').strip()

        # DEBUG (optional)
        # print("RAW:", line)

        try:
            value = float(line)
            started = True
            samples.append(value)
        except:
            if line == "END" and started:
                break
            continue

        if len(samples) >= N_SAMPLES:
            break

    return samples if len(samples) == N_SAMPLES else None


def main():
    print("=" * 50)
    print("  Fanalyzer - Data Collection Tool")
    print("=" * 50)

    # Open serial port
    try:
        ser = serial.Serial(PORT, BAUD, timeout=5)

        # 🔥 Increased delay to handle ESP32 reset
        time.sleep(4)

        ser.reset_input_buffer()
        print(f"Connected to {PORT}")
    except Exception as e:
        print(f"Could not open {PORT}: {e}")
        print("Check your COM port in Device Manager / Arduino IDE")
        return

    # Prepare CSV file
    file_exists = os.path.exists(OUTPUT_FILE)
    csv_file = open(OUTPUT_FILE, 'a', newline='')
    writer = csv.writer(csv_file)

    if not file_exists:
        header = [f"s{i}" for i in range(N_SAMPLES)] + ["label"]
        writer.writerow(header)
        print(f"Created new dataset file: {OUTPUT_FILE}")
    else:
        print(f"Appending to existing: {OUTPUT_FILE}")

    try:
        while True:
            print("\n─── Select Fan Condition ───────────────────")
            for key, name in CLASSES.items():
                print(f"  Press {key} → {name.upper()}")
            print("  Press Q → Quit")
            print("────────────────────────────────────────────")

            choice = input("Your choice: ").strip().upper()

            if choice == 'Q':
                break

            if choice not in CLASSES:
                print("Invalid choice. Try again.")
                continue

            label = int(choice)
            class_name = CLASSES[choice]

            print(f"\nSet fan to: {class_name.upper()}")
            input(f"Press ENTER when fan is running in '{class_name}' condition...")

            print(f"Collecting {SAMPLES_PER_CLASS} windows for '{class_name}'...")
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

            print(f"\nCollected {collected} windows for '{class_name}'!")

    finally:
        csv_file.close()
        ser.close()
        print(f"\nDataset saved to: {OUTPUT_FILE}")
        print("You can now run the training scripts!")


if __name__ == "__main__":
    main()