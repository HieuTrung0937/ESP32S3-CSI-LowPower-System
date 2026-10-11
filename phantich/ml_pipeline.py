#!/usr/bin/env python3
"""
Pipeline Machine Learning cho Fall Detection.
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
SAMPLE_RATE_HZ = 20       # <-- ĐÃ SỬA: 20 Hz thực tế
WINDOW_SAMPLES = int(WINDOW_SEC * SAMPLE_RATE_HZ)   # 40 mẫu
STRIDE_SAMPLES = int(STRIDE_SEC * SAMPLE_RATE_HZ)   # 20 mẫu

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
    amp_cols = [c for c in df.columns if c.startswith("amp_")]
    print(f"  ✓ {len(df)} gói tin, {len(amp_cols)} subcarriers")
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


def label_mode_filename(csv_path):
    print(f"\n[2/5] Gán nhãn từ tên file")
    filename = os.path.basename(csv_path).lower()
    label = -1
    if "dung" in filename or "stand" in filename: label = 0
    elif "di" in filename or "walk" in filename: label = 1
    elif "ngoi" in filename or "sit" in filename: label = 2
    elif "nam" in filename or "lie" in filename: label = 3
    elif "te" in filename or "fall" in filename: label = 4

    if label == -1:
        print("  ✗ Không tìm thấy nhãn trong tên file!")
        sys.exit(1)

    print(f"  ✓ Nhãn: {LABEL_MAP[label]}")
    return label


def label_mode_interactive(df):
    print(f"\n[2/5] Gán nhãn thủ công (Ctrl+C để dừng)")
    print(f"  Nhãn: 0=Dung, 1=Di, 2=Ngoi, 3=Nam, 4=Te")
    print(f"  Thời gian: 0 → {df['timestamp_ms'].max()/1000:.1f} giây\n")

    df['label'] = -1
    try:
        while True:
            try:
                start = float(input("  Start (giây): "))
                end = float(input("  End (giây): "))
                label = int(input("  Label (0-4): "))
                mask = (df['timestamp_ms'] >= start*1000) & (df['timestamp_ms'] <= end*1000)
                df.loc[mask, 'label'] = label
                print(f"    ✓ {start}-{end}s = {LABEL_MAP[label]}")
            except (ValueError, EOFError):
                break
    except KeyboardInterrupt:
        pass
    return df


def extract_features(window):
    """Trích xuất đặc trưng từ 1 window (samples x subcarriers)."""
    mean_val = np.mean(window)
    var_val = np.var(window)
    p2p = np.max(window) - np.min(window)
    diff = np.diff(window, axis=0)
    roc = np.mean(np.abs(diff))
    return [mean_val, var_val, p2p, roc]


def create_windows(df, amp_cols):
    print(f"\n[3/5] Chia Sliding Window ({WINDOW_SEC}s, stride {STRIDE_SEC}s)")
    print(f"  Window = {WINDOW_SAMPLES} mẫu, Stride = {STRIDE_SAMPLES} mẫu")

    data = df[amp_cols].values
    labels = df['label'].values

    features = []
    window_labels = []

    for i in range(0, len(data) - WINDOW_SAMPLES + 1, STRIDE_SAMPLES):
        window = data[i : i + WINDOW_SAMPLES]
        win_labels = labels[i : i + WINDOW_SAMPLES]

        valid_labels = win_labels[win_labels >= 0]
        if len(valid_labels) < WINDOW_SAMPLES * 0.7:
            continue

        dominant_label = int(np.bincount(valid_labels.astype(int)).argmax())
        feat = extract_features(window)
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
        n_estimators=100,
        max_depth=10,
        class_weight='balanced',
        random_state=42,
        n_jobs=-1
    )
    model.fit(X_train, y_train)

    y_pred = model.predict(X_test)
    acc = accuracy_score(y_test, y_pred)

    # <-- FIX: Tự động lấy nhãn từ dữ liệu thực tế
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

    # Lưu model
    os.makedirs(output_dir, exist_ok=True)
    model_path = os.path.join(output_dir, "fall_detection_model.pkl")
    joblib.dump(model, model_path)
    print(f"  ✓ Đã lưu model: {model_path}")

    # Lưu label_map thực tế (để realtime_csi.py đọc)
    actual_map = {int(l): LABEL_MAP.get(int(l), f"Class_{l}") for l in present_labels}
    joblib.dump(actual_map, os.path.join(output_dir, "label_map.pkl"))
    print(f"  ✓ Đã lưu label_map: {output_dir}/label_map.pkl")

    # Confusion Matrix
    cm = confusion_matrix(y_test, y_pred, labels=present_labels)
    plt.figure(figsize=(8, 6))
    sns.heatmap(cm, annot=True, fmt='d', cmap='Blues',
                xticklabels=target_names, yticklabels=target_names)
    plt.xlabel("Predicted")
    plt.ylabel("Actual")
    plt.title(f"Confusion Matrix (Acc: {acc*100:.1f}%)")
    cm_path = os.path.join(output_dir, "confusion_matrix.png")
    plt.savefig(cm_path, dpi=120, bbox_inches='tight')
    plt.close()
    print(f"  ✓ Đã lưu: {cm_path}")

    # Feature Importance
    feat_names = ['Mean', 'Variance', 'Peak-to-Peak', 'Rate of Change']
    importances = model.feature_importances_

    plt.figure(figsize=(8, 5))
    plt.barh(feat_names, importances, color='teal')
    plt.xlabel("Importance")
    plt.title("Feature Importance (Random Forest)")
    plt.tight_layout()
    fi_path = os.path.join(output_dir, "feature_importance.png")
    plt.savefig(fi_path, dpi=120, bbox_inches='tight')
    plt.close()
    print(f"  ✓ Đã lưu: {fi_path}")

    return model, acc


def main():
    if len(sys.argv) < 2:
        print("=" * 60)
        print("CACH SU DUNG:")
        print("  python3 ml_pipeline.py data.csv labels.csv")
        print("  python3 ml_pipeline.py data.csv --from-filename")
        print("  python3 ml_pipeline.py data.csv --interactive")
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
        elif mode == "--interactive":
            df = label_mode_interactive(df)
        else:
            df = label_mode_timeline(df, mode)
    else:
        df = label_mode_interactive(df)

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