#!/usr/bin/env python3
"""Thu thap du lieu CSI tu ESP32-S3 RX qua Serial."""

import serial
import serial.tools.list_ports
import csv
import time
import sys
import os
from datetime import datetime

SERIAL_PORT = "/dev/ttyACM0"
BAUD_RATE = 921600
OUTPUT_DIR = "csi_data"
DURATION_SEC = 36000
MIN_RSSI = -80


def find_esp32_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if any(kw in port.description.lower() for kw in ["cp210", "ch340", "usb serial", "espressif"]):
            return port.device
    print("Khong tu dong tim thay ESP32. Cac cong hien co:")
    for i, port in enumerate(ports):
        print(f"  [{i}] {port.device} - {port.description}")
    return None


def parse_csi_line(line):
    line = line.strip()
    if not line.startswith("CSI,"):
        return None
    parts = line.split(",")
    if len(parts) < 4:
        return None
    try:
        seq = int(parts[1])
        rssi = int(parts[2])
        num_subcarriers = int(parts[3])
        amplitudes = [float(x) for x in parts[4:4 + num_subcarriers]]
    except (ValueError, IndexError):
        return None
    if len(amplitudes) != num_subcarriers:
        return None
    return {"seq": seq, "rssi": rssi, "num_subcarriers": num_subcarriers, "amplitudes": amplitudes}


def main():
    port = SERIAL_PORT or find_esp32_port()
    if not port:
        print("Loi: Khong tim thay cong Serial. Hay gan SERIAL_PORT thu cong.")
        sys.exit(1)

    print(f"Dang ket noi toi {port} @ {BAUD_RATE} baud...")
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=1)
    except serial.SerialException as e:
        print(f"Loi mo cong Serial: {e}")
        sys.exit(1)

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path = os.path.join(OUTPUT_DIR, f"csi_{timestamp}.csv")

    csv_file = open(csv_path, "w", newline="")
    writer = csv.writer(csv_file)
    header_written = False

    print(f"Dang thu thap du lieu, luu vao: {csv_path}")
    print("Nhan Ctrl+C de dung.\n")

    start_time = time.time()
    packet_count = 0
    dropped_count = 0

    try:
        while True:
            if DURATION_SEC > 0 and (time.time() - start_time) >= DURATION_SEC:
                print(f"\nDa du {DURATION_SEC} giay, dung thu thap.")
                break
            try:
                raw_line = ser.readline().decode("utf-8", errors="ignore")
            except serial.SerialException:
                continue
            if not raw_line:
                continue
            pkt = parse_csi_line(raw_line)
            if pkt is None:
                continue
            if pkt["rssi"] < MIN_RSSI:
                dropped_count += 1
                continue
            if not header_written:
                num_sub = pkt["num_subcarriers"]
                header = ["seq", "rssi", "num_subcarriers", "timestamp_ms"]
                header += [f"amp_{i}" for i in range(num_sub)]
                writer.writerow(header)
                header_written = True
                print(f"Da phat hien {num_sub} subcarriers. Bat dau ghi...\n")
            elapsed_ms = int((time.time() - start_time) * 1000)
            row = [pkt["seq"], pkt["rssi"], pkt["num_subcarriers"], elapsed_ms]
            row += pkt["amplitudes"]
            writer.writerow(row)
            packet_count += 1
            if packet_count % 100 == 0:
                elapsed = time.time() - start_time
                rate = packet_count / elapsed if elapsed > 0 else 0
                print(f"  Da thu: {packet_count} goi | Toc do: {rate:.1f} pkt/s | Bo qua: {dropped_count}")
    except KeyboardInterrupt:
        print("\nNguoi dung dung thu thap.")
    finally:
        csv_file.close()
        ser.close()
        elapsed = time.time() - start_time
        rate = packet_count / elapsed if elapsed > 0 else 0
        print(f"\n===== KET QUA =====")
        print(f"File luu: {csv_path}")
        print(f"Tong goi thu duoc: {packet_count}")
        print(f"Goi bi bo qua: {dropped_count}")
        print(f"Thoi gian: {elapsed:.1f} giay")
        print(f"Toc do trung binh: {rate:.1f} pkt/s")


if __name__ == "__main__":
    main()
