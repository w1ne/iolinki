#!/usr/bin/env python3
import threading
import serial
import time
import sys

# Configuration
BOARD_A_PORT = '/dev/ttyACM1'  # Device (J-Link)
BOARD_B_PORT = '/dev/ttyACM0'  # Master (ST-Link)
BAUDRATE = 115200

def read_port(port_name, label):
    while True:
        try:
            ser = serial.Serial(port_name, BAUDRATE, timeout=1)
            print(f"[{label}] Connected to {port_name}")
            while True:
                try:
                    line = ser.readline()
                    if line:
                        decoded = line.decode('utf-8', errors='replace').strip()
                        if decoded:
                            print(f"[{label}] {decoded}")
                        if len(line) < 10 or not decoded.isprintable():
                            print(f"[{label}] RAW: {line.hex()}")
                except serial.SerialException:
                    print(f"[{label}] Connection lost, retrying...")
                    break
        except serial.SerialException as e:
            # Silence connection errors during retry
            time.sleep(0.5)

if __name__ == "__main__":
    print("=== IO-Link Nucleo Monitor ===")
    print(f"Board A (Device): {BOARD_A_PORT}")
    print(f"Board B (Master): {BOARD_B_PORT}")
    print("Press Ctrl+C to exit")
    print("")

    t1 = threading.Thread(target=read_port, args=(BOARD_A_PORT, "DEVICE"))
    t2 = threading.Thread(target=read_port, args=(BOARD_B_PORT, "MASTER"))

    t1.daemon = True
    t2.daemon = True

    t1.start()
    t2.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nExiting...")
