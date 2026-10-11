#!/usr/bin/env python3
"""
Theo doi CSI thoi gian thuc + AI Prediction (Random Forest v2)
"""

import serial
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import sys
import threading
import joblib
from collections import deque

# ==================== CẤU HÌNH ====================
SERIAL_PORT = "/dev/ttyACM0"
BAUD_RATE = 921600
WINDOW_SIZE = 200              # Số gói hiển thị trên biểu đồ
NUM_SUBCARRIERS = 64

# Cấu hình AI (KHỚP VỚI ML_PIPELINE_V2)
SAMPLE_RATE_HZ = 20
WINDOW_SEC = 2.0
WINDOW_SAMPLES = int(WINDOW_SEC * SAMPLE_RATE_HZ) # 40 mẫu
DEAD_SUBCARRIERS = list(range(27, 37))
ACTIVE_SUBCARRIERS = [i for i in range(64) if i not in DEAD_SUBCARRIERS]

MODEL_PATH = "ml_output/fall_detection_model_v2.pkl"
LABEL_MAP_PATH = "ml_output/label_map_v2.pkl"
# ==================================================

class CsiStream:
    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baud, timeout=1)
        self.rssi_buf = deque(maxlen=WINDOW_SIZE)
        self.mean_amp_buf = deque(maxlen=WINDOW_SIZE)
        self.amp_matrix = deque(maxlen=WINDOW_SIZE)
        
        # Buffer riêng cho AI Sliding Window
        self.ai_amp_buf = deque(maxlen=WINDOW_SAMPLES)
        self.ai_rssi_buf = deque(maxlen=WINDOW_SAMPLES)
        
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
                
                # Cập nhật buffer cho AI
                active_amps = amps[ACTIVE_SUBCARRIERS]
                self.ai_amp_buf.append(active_amps)
                self.ai_rssi_buf.append(rssi)

    def snapshot(self):
        with self.lock:
            return (
                list(self.rssi_buf),
                list(self.mean_amp_buf),
                np.array(self.amp_matrix) if self.amp_matrix else np.zeros((0, NUM_SUBCARRIERS)),
                list(self.ai_amp_buf),
                list(self.ai_rssi_buf),
                self.packet_count,
            )

    def stop(self):
        self.running = False
        self.thread.join(timeout=2)
        self.ser.close()

def extract_features_for_ai(amp_list, rssi_list):
    """Trích xuất 12 đặc trưng giống hệt ml_pipeline_v2.py"""
    window_amp = np.array(amp_list)   # shape: (40, 54)
    window_rssi = np.array(rssi_list) # shape: (40,)
    
    amp_mean = np.mean(window_amp)
    amp_var = np.var(window_amp)
    amp_p2p = np.max(window_amp) - np.min(window_amp)
    
    subcarrier_vars = np.var(window_amp, axis=0)
    subcarrier_var_mean = np.mean(subcarrier_vars)
    subcarrier_var_var = np.var(subcarrier_vars)
    
    diff_amp = np.diff(window_amp, axis=0)
    roc_amp = np.mean(np.abs(diff_amp))
    
    rssi_mean = np.mean(window_rssi)
    rssi_var = np.var(window_rssi)
    rssi_min = np.min(window_rssi)
    rssi_max = np.max(window_rssi)
    rssi_p2p = rssi_max - rssi_min
    
    diff_rssi = np.diff(window_rssi)
    roc_rssi = np.mean(np.abs(diff_rssi))
    
    return [[
        amp_mean, amp_var, amp_p2p,
        subcarrier_var_mean, subcarrier_var_var,
        roc_amp,
        rssi_mean, rssi_var, rssi_min, rssi_max, rssi_p2p,
        roc_rssi
    ]]

def main():
    print(f"Đang load Model AI từ: {MODEL_PATH}")
    try:
        model = joblib.load(MODEL_PATH)
        label_map = joblib.load(LABEL_MAP_PATH)
        print("✅ Load model thành công!")
    except FileNotFoundError:
        print("❌ Lỗi: Không tìm thấy file model. Hãy chạy ml_pipeline_v2.py trước.")
        sys.exit(1)

    print(f"Đang kết nối tới {SERIAL_PORT} @ {BAUD_RATE} baud...")
    try:
        stream = CsiStream(SERIAL_PORT, BAUD_RATE)
    except serial.SerialException as e:
        print(f"Lỗi mở cổng Serial: {e}")
        sys.exit(1)

    # ---- Setup figure ----
    fig, axes = plt.subplots(3, 1, figsize=(12, 10))
    fig.suptitle("ESP32-S3 CSI Real-time + AI Fall Detection (Phase 1)", fontsize=14, fontweight='bold')

    # Plot 1: RSSI
    ax_rssi = axes[0]
    line_rssi, = ax_rssi.plot([], [], color="tab:blue", linewidth=1)
    ax_rssi.set_xlim(0, WINDOW_SIZE)
    ax_rssi.set_ylim(-90, -30)
    ax_rssi.set_ylabel("RSSI (dBm)")
    ax_rssi.grid(True, alpha=0.3)

    # Plot 2: Mean amplitude
    ax_amp = axes[1]
    line_amp, = ax_amp.plot([], [], color="tab:green", linewidth=1)
    ax_amp.set_xlim(0, WINDOW_SIZE)
    ax_amp.set_ylim(0, 60)
    ax_amp.set_ylabel("Amplitude TB")
    ax_amp.grid(True, alpha=0.3)

    # Plot 3: Heatmap
    ax_heat = axes[2]
    heat_data = np.zeros((NUM_SUBCARRIERS, WINDOW_SIZE))
    im = ax_heat.imshow(heat_data, aspect="auto", cmap="viridis",
                        vmin=0, vmax=40,
                        extent=[0, WINDOW_SIZE, 0, NUM_SUBCARRIERS],
                        origin="lower")
    ax_heat.set_ylabel("Subcarrier")
    ax_heat.set_xlabel("Gói tin (thời gian)")
    plt.colorbar(im, ax=ax_heat, label="Amplitude", fraction=0.03)

    # ---- KHUNG HIỂN THỊ AI PREDICTION ----
    pred_text = axes[0].text(0.98, 0.95, "Đang khởi tạo AI...", 
                             transform=ax_rssi.transAxes, ha='right', va='top',
                             fontsize=12, fontweight='bold', color='gray',
                             bbox=dict(boxstyle="round,pad=0.5", fc="white", ec="gray", lw=2))

    plt.tight_layout()

    # ---- Animation update ----
    def update(frame):
        rssi, mean_amp, matrix, ai_amp, ai_rssi, count = stream.snapshot()
        n = len(rssi)
        if n == 0:
            return line_rssi, line_amp, im, pred_text

        x = np.arange(n)
        line_rssi.set_data(x, rssi)
        line_amp.set_data(x, mean_amp)

        # Heatmap
        if matrix.shape[0] > 0:
            heat = matrix.T
            if heat.shape[1] < WINDOW_SIZE:
                pad = np.zeros((NUM_SUBCARRIERS, WINDOW_SIZE - heat.shape[1]))
                heat = np.concatenate([heat, pad], axis=1)
            else:
                heat = heat[:, -WINDOW_SIZE:]
            im.set_data(heat)
            im.set_clim(vmin=0, vmax=max(40, heat.max()))

        # ---- AI PREDICTION LOGIC (ĐÃ FIX NGƯỠNG) ----
        if len(ai_amp) >= WINDOW_SAMPLES:
            # Lấy đúng 40 mẫu gần nhất
            window_amp = list(ai_amp)[-WINDOW_SAMPLES:]
            window_rssi = list(ai_rssi)[-WINDOW_SAMPLES:]
            
            features = extract_features_for_ai(window_amp, window_rssi)
            prediction = model.predict(features)[0]
            probabilities = model.predict_proba(features)[0]
            confidence = max(probabilities) * 100
            
            # === FIX QUAN TRỌNG: NGƯỠNG TIN CẬY ===
            if confidence < 60.0:
                label_name = "Dang quan sat..."
                color = "gray"
            else:
                label_name = label_map.get(int(prediction), "Unknown")
                # Màu xanh cho trạng thái an toàn, Cam cho chuyển động, Đỏ cho Té (nếu có)
                if label_name in ["Dung", "Ngoi", "Nam"]:
                    color = "green"
                elif label_name == "Di":
                    color = "orange"
                else:
                    color = "red" # Dự phòng nếu sau này có lớp Té
            
            pred_text.set_text(f"AI: {label_name.upper()}\nDo tin cay: {confidence:.1f}%")
            pred_text.set_color(color)
            pred_text.set_bbox(dict(boxstyle="round,pad=0.5", fc="white", ec=color, lw=2))

        return line_rssi, line_amp, im, pred_text

    ani = animation.FuncAnimation(fig, update, interval=100, blit=False, cache_frame_data=False)

    try:
        plt.show()
    except KeyboardInterrupt:
        pass
    finally:
        stream.stop()
        print(f"\nTổng gói đã thu: {stream.packet_count}")

if __name__ == "__main__":
    main()
