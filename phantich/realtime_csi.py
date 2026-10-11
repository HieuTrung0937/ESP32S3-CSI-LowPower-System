#!/usr/bin/env python3
"""
Theo doi CSI theo thoi gian thuc tu ESP32-S3 RX.
Hien thi 3 bieu do cap nhat lien tuc:
  1. RSSI theo thoi gian
  2. Amplitude trung binh 64 subcarrier
  3. Heatmap 64 subcarrier x 200 goi gan nhat
"""

import serial
import serial.tools.list_ports
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import time
import sys
import threading
from collections import deque

# ==================== CAU HINH ====================
SERIAL_PORT = "/dev/ttyACM0"   # Cong Serial cua ESP32-S3 RX
BAUD_RATE = 921600
WINDOW_SIZE = 200              # So goi hien thi tren bieu do cuon
NUM_SUBCARRIERS = 64
# ==================================================


class CsiStream:
    """Doc CSI tu Serial trong thread rieng, luu vao deque."""

    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baud, timeout=1)
        self.rssi_buf = deque(maxlen=WINDOW_SIZE)
        self.mean_amp_buf = deque(maxlen=WINDOW_SIZE)
        self.amp_matrix = deque(maxlen=WINDOW_SIZE)
        self.packet_count = 0
        self.running = True
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._read_loop, daemon=True)
        self.thread.start()

    def _read_loop(self):
        while self.running:
            try:
                line = self.ser.readline().decode("utf-8", errors="ignore").strip()
            except serial.SerialException:
                continue
            if not line.startswith("CSI,"):
                continue
            parts = line.split(",")
            if len(parts) < 4:
                continue
            try:
                rssi = int(parts[2])
                num_sc = int(parts[3])
                amps = np.array([float(x) for x in parts[4:4 + num_sc]])
            except (ValueError, IndexError):
                continue
            if len(amps) != num_sc or num_sc != NUM_SUBCARRIERS:
                continue
            with self.lock:
                self.rssi_buf.append(rssi)
                self.mean_amp_buf.append(amps.mean())
                self.amp_matrix.append(amps)
                self.packet_count += 1

    def snapshot(self):
        with self.lock:
            return (
                list(self.rssi_buf),
                list(self.mean_amp_buf),
                np.array(self.amp_matrix) if self.amp_matrix else np.zeros((0, NUM_SUBCARRIERS)),
                self.packet_count,
            )

    def stop(self):
        self.running = False
        self.thread.join(timeout=2)
        self.ser.close()


def main():
    print(f"Dang ket noi toi {SERIAL_PORT} @ {BAUD_RATE} baud...")
    try:
        stream = CsiStream(SERIAL_PORT, BAUD_RATE)
    except serial.SerialException as e:
        print(f"Loi mo cong Serial: {e}")
        sys.exit(1)

    print("Dang thu thap. Dong cua so bieu do de thoat.\n")

    # ---- Setup figure ----
    fig, axes = plt.subplots(3, 1, figsize=(12, 9))
    fig.suptitle("ESP32-S3 CSI Real-time Monitor", fontsize=14)

    # Plot 1: RSSI
    ax_rssi = axes[0]
    line_rssi, = ax_rssi.plot([], [], color="tab:blue", linewidth=1)
    ax_rssi.set_xlim(0, WINDOW_SIZE)
    ax_rssi.set_ylim(-90, -30)
    ax_rssi.set_ylabel("RSSI (dBm)")
    ax_rssi.set_title("RSSI theo thoi gian")
    ax_rssi.grid(True, alpha=0.3)
    text_rssi = ax_rssi.text(0.02, 0.95, "", transform=ax_rssi.transAxes,
                             va="top", fontsize=9, family="monospace")

    # Plot 2: Mean amplitude
    ax_amp = axes[1]
    line_amp, = ax_amp.plot([], [], color="tab:green", linewidth=1)
    ax_amp.set_xlim(0, WINDOW_SIZE)
    ax_amp.set_ylim(0, 60)
    ax_amp.set_ylabel("Amplitude TB")
    ax_amp.set_title("Amplitude trung binh 64 subcarrier")
    ax_amp.grid(True, alpha=0.3)
    text_amp = ax_amp.text(0.02, 0.95, "", transform=ax_amp.transAxes,
                           va="top", fontsize=9, family="monospace")

    # Plot 3: Heatmap
    ax_heat = axes[2]
    heat_data = np.zeros((NUM_SUBCARRIERS, WINDOW_SIZE))
    im = ax_heat.imshow(heat_data, aspect="auto", cmap="viridis",
                        vmin=0, vmax=40,
                        extent=[0, WINDOW_SIZE, 0, NUM_SUBCARRIERS],
                        origin="lower")
    ax_heat.set_ylabel("Subcarrier")
    ax_heat.set_xlabel("Goi (thoi gian)")
    ax_heat.set_title("Heatmap 64 subcarrier")
    plt.colorbar(im, ax=ax_heat, label="Amplitude", fraction=0.03)

    plt.tight_layout()

    # ---- Animation update ----
    def update(frame):
        rssi, mean_amp, matrix, count = stream.snapshot()
        n = len(rssi)
        if n == 0:
            return line_rssi, line_amp, im, text_rssi, text_amp

        x = np.arange(n)
        line_rssi.set_data(x, rssi)
        line_amp.set_data(x, mean_amp)

        # Heatmap: dua ma tran vao, pad neu thieu
        if matrix.shape[0] > 0:
            # Chuyen vi de shape = (64, n)
            heat = matrix.T
            # Pad ben phai neu chua du WINDOW_SIZE
            if heat.shape[1] < WINDOW_SIZE:
                pad = np.zeros((NUM_SUBCARRIERS, WINDOW_SIZE - heat.shape[1]))
                heat = np.concatenate([heat, pad], axis=1)
            else:
                heat = heat[:, -WINDOW_SIZE:]
            im.set_data(heat)
            im.set_clim(vmin=0, vmax=max(40, heat.max()))

        # Cap nhat text thong ke
        text_rssi.set_text(
            f"RSSI: cur={rssi[-1]:>4} dBm  "
            f"min={min(rssi):>4}  max={max(rssi):>4}  "
            f"mean={np.mean(rssi):>6.1f}"
        )
        text_amp.set_text(
            f"Mean amp: cur={mean_amp[-1]:>5.2f}  "
            f"min={min(mean_amp):>5.2f}  max={max(mean_amp):>5.2f}  "
            f"std={np.std(mean_amp):>5.2f}"
        )
        ax_rssi.set_title(f"RSSI theo thoi gian  —  Tong goi: {count}")
        return line_rssi, line_amp, im, text_rssi, text_amp

    ani = animation.FuncAnimation(
        fig, update, interval=100, blit=False, cache_frame_data=False
    )

    try:
        plt.show()
    except KeyboardInterrupt:
        pass
    finally:
        stream.stop()
        print(f"\nTong goi da thu: {stream.packet_count}")


if __name__ == "__main__":
    main()
