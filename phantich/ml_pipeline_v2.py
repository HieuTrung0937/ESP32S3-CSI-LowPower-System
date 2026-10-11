#!/usr/bin/env python3
"""
Pipeline Machine Learning v2 - Nâng cấp đặc trưng.
- Thêm đặc trưng RSSI
- Loại bỏ subcarrier chết (27-36)
- Đặc trưng per-subcarrier variance
"""

import pandas as pd
import numpy as np
import joblib
import sys
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, confusion_matrix, accuracy_score

# ==================== CẤU HÌNH ====================
WINDOW_SEC = 2.0
STRIDE_SEC = 1.0
SAMPLE_RATE_HZ = 20
WINDOW_SAMPLES = int(WINDOW_SEC * SAMPLE_RATE_HZ)
STRIDE_SAMPLES = int(STRIDE_SEC * SAMPLE_RATE_HZ)

# Loại bỏ subcarrier chết (index 27-36)
DEAD_SUBCARRIERS = list(range(27, 37))
ACTIVE_SUBCARRIERS = [i for i in range(64) if i not in DEAD_SUBCARRIERS]

LABEL_MAP = {
    0: "Dung",
    1: "Di",
    2: "Ngoi",
    3: "Nam",
    4: "Te"
}
# ==================================================


def load_csi_data(csv_path):
    print(f"\n[1/5] Đang đọc file: {csv_path}")
    df = pd.read_csv(csv_path)
    amp_cols = [f"amp_{i}" for i in ACTIVE_SUBCARRIERS]
    print(f"  ✓ {len(df)} gói tin")
    print(f"  ✓ Sử dụng {len(ACTIVE_SUBCARRIERS)} subcarrier hoạt động")
    print(f"  ✓ Thời gian: {df['timestamp_ms'].max()/1000:.1f} giây")
    return df, amp_cols


def label_mode_timeline(df, label_csv_path):
    print(f"\n[2/5] Gán nhãn từ file: {label_csv_path}")
    labels_df = pd.read_csv(label_csv_path)
    df['label'] = -1

    for _, row in labels_df.iterrows():
        mask = (df['timestamp_ms'] >= row['start_ms']) & \
               (df['timestamp_ms'] <= row['end_ms'])
        df.loc[mask, 'label'] = int(row['label'])

    unlabel_rate = (df['label'] == -1).mean() * 100
    print(f"  ✓ Đã gán nhãn. Phần chưa gán: {unlabel_rate:.1f}%")
    return df


def extract_features_v2(window_amp, window_rssi):
    """
    Trích xuất đặc trưng nâng cấp:
    - window_amp: shape (samples, 54 subcarriers)
    - window_rssi: shape (samples,)
    """
    features = []
    
    # 1. Đặc trưng từ Amplitude
    amp_mean = np.mean(window_amp)
    amp_var = np.var(window_amp)
    amp_p2p = np.max(window_amp) - np.min(window_amp)
    
    # Per-subcarrier variance
    subcarrier_vars = np.var(window_amp, axis=0)
    subcarrier_var_mean = np.mean(subcarrier_vars)
    subcarrier_var_var = np.var(subcarrier_vars)
    
    # Rate of Change
    diff_amp = np.diff(window_amp, axis=0)
    roc_amp = np.mean(np.abs(diff_amp))
    
    # 2. Đặc trưng từ RSSI
    rssi_mean = np.mean(window_rssi)
    rssi_var = np.var(window_rssi)
    rssi_min = np.min(window_rssi)
    rssi_max = np.max(window_rssi)
    rssi_p2p = rssi_max - rssi_min
    
    diff_rssi = np.diff(window_rssi)
    roc_rssi = np.mean(np.abs(diff_rssi))
    
    features.extend([
        amp_mean, amp_var, amp_p2p,
        subcarrier_var_mean, subcarrier_var_var,
        roc_amp,
        rssi_mean, rssi_var, rssi_min, rssi_max, rssi_p2p,
        roc_rssi
    ])
    
    return features


def create_windows(df, amp_cols):
    print(f"\n[3/5] Chia Sliding Window ({WINDOW_SEC}s, stride {STRIDE_SEC}s)")
    print(f"  Window = {WINDOW_SAMPLES} mẫu, Stride = {STRIDE_SAMPLES} mẫu")

    data_amp = df[amp_cols].values
    data_rssi = df['rssi'].values
    labels = df['label'].values

    features = []
    window_labels = []

    for i in range(0, len(data_amp) - WINDOW_SAMPLES + 1, STRIDE_SAMPLES):
        window_amp = data_amp[i : i + WINDOW_SAMPLES]
        window_rssi = data_rssi[i : i + WINDOW_SAMPLES]
        win_labels = labels[i : i + WINDOW_SAMPLES]

        valid_labels = win_labels[win_labels >= 0]
        if len(valid_labels) < WINDOW_SAMPLES * 0.7:
            continue

        dominant_label = int(np.bincount(valid_labels.astype(int)).argmax())
        feat = extract_features_v2(window_amp, window_rssi)
        features.append(feat)
        window_labels.append(dominant_label)

    X = np.array(features)
    y = np.array(window_labels)

    print(f"  ✓ Tạo được {len(X)} windows, {X.shape[1]} đặc trưng")

    unique, counts = np.unique(y, return_counts=True)
    print(f"\n  Phân bố nhãn:")
    for u, c in zip(unique, counts):
        name = LABEL_MAP.get(int(u), f"Unknown_{u}")
        print(f"    {name}: {c} windows ({c/len(y)*100:.1f}%)")

    return X, y


def train_model(X, y, output_dir):
    print(f"\n[4/5] Huấn luyện Random Forest...")

    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42, stratify=y
    )

    model = RandomForestClassifier(
        n_estimators=200,
        max_depth=15,
        class_weight='balanced',
        random_state=42,
        n_jobs=-1
    )
    model.fit(X_train, y_train)

    y_pred = model.predict(X_test)
    acc = accuracy_score(y_test, y_pred)

    present_labels = sorted(np.unique(y))
    target_names = [LABEL_MAP.get(int(l), f"Class_{l}") for l in present_labels]

    print(f"\n  Accuracy: {acc*100:.2f}%")
    print(f"\n  Classification Report:")
    report = classification_report(
        y_test, y_pred,
        labels=present_labels,
        target_names=target_names
    )
    print(report)

    os.makedirs(output_dir, exist_ok=True)
    model_path = os.path.join(output_dir, "fall_detection_model_v2.pkl")
    joblib.dump(model, model_path)
    print(f"  ✓ Đã lưu model: {model_path}")

    actual_map = {int(l): LABEL_MAP.get(int(l), f"Class_{l}") for l in present_labels}
    joblib.dump(actual_map, os.path.join(output_dir, "label_map_v2.pkl"))
    print(f"  ✓ Đã lưu label_map: {output_dir}/label_map_v2.pkl")

    cm = confusion_matrix(y_test, y_pred, labels=present_labels)
    plt.figure(figsize=(8, 6))
    sns.heatmap(cm, annot=True, fmt='d', cmap='Blues',
                xticklabels=target_names, yticklabels=target_names)
    plt.xlabel("Predicted")
    plt.ylabel("Actual")
    plt.title(f"Confusion Matrix v2 (Acc: {acc*100:.1f}%)")
    cm_path = os.path.join(output_dir, "confusion_matrix_v2.png")
    plt.savefig(cm_path, dpi=120, bbox_inches='tight')
    plt.close()
    print(f"  ✓ Đã lưu: {cm_path}")

    feat_names = [
        'Amp Mean', 'Amp Var', 'Amp P2P',
        'SubVar Mean', 'SubVar Var', 'Amp ROC',
        'RSSI Mean', 'RSSI Var', 'RSSI Min', 'RSSI Max', 'RSSI P2P', 'RSSI ROC'
    ]
    importances = model.feature_importances_

    plt.figure(figsize=(10, 6))
    plt.barh(feat_names, importances, color='teal')
    plt.xlabel("Importance")
    plt.title("Feature Importance v2 (Random Forest)")
    plt.tight_layout()
    fi_path = os.path.join(output_dir, "feature_importance_v2.png")
    plt.savefig(fi_path, dpi=120, bbox_inches='tight')
    plt.close()
    print(f"  ✓ Đã lưu: {fi_path}")

    return model, acc


def main():
    if len(sys.argv) < 2:
        print("=" * 60)
        print("CACH SU DUNG:")
        print("  python3 ml_pipeline_v2.py data.csv labels.csv")
        print("=" * 60)
        sys.exit(1)

    csv_path = sys.argv[1]
    output_dir = "ml_output"

    df, amp_cols = load_csi_data(csv_path)

    if len(sys.argv) >= 3:
        mode = sys.argv[2]
        if mode == "--from-filename":
            label = label_mode_filename(csv_path)
            df['label'] = label
        else:
            df = label_mode_timeline(df, mode)
    else:
        print("  ✗ Cần file labels.csv!")
        sys.exit(1)

    df = df[df['label'] >= 0].reset_index(drop=True)
    if len(df) == 0:
        print("\n✗ Không có dữ liệu nào được gán nhãn!")
        sys.exit(1)

    X, y = create_windows(df, amp_cols)

    if len(X) < 10:
        print("\n✗ Quá ít windows! Thu thập thêm dữ liệu.")
        sys.exit(1)

    model, acc = train_model(X, y, output_dir)

    print("\n" + "=" * 60)
    print(f"✅ HOÀN TẤT! Accuracy: {acc*100:.2f}%")
    print(f"📁 Output: {output_dir}/")
    print("=" * 60)


if __name__ == "__main__":
    main()
