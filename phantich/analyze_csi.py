#!/usr/bin/env python3
"""Phan tich du lieu CSI da thu thap."""

import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import sys
import os


def load_csi_csv(path):
    df = pd.read_csv(path)
    amp_cols = [c for c in df.columns if c.startswith("amp_")]
    print(f"Da doc {len(df)} goi, {len(amp_cols)} subcarriers.")
    print(f"RSSI: min={df['rssi'].min()}, max={df['rssi'].max()}, mean={df['rssi'].mean():.1f}")
    return df, amp_cols


def plot_amplitude_heatmap(df, amp_cols, out_path):
    amp_matrix = df[amp_cols].values.T
    plt.figure(figsize=(14, 6))
    plt.imshow(amp_matrix, aspect="auto", cmap="viridis",
               extent=[0, len(df), 0, len(amp_cols)])
    plt.colorbar(label="Amplitude")
    plt.xlabel("Goi (thoi gian)")
    plt.ylabel("Subcarrier index")
    plt.title("CSI Amplitude theo thoi gian va subcarrier")
    plt.tight_layout()
    plt.savefig(out_path, dpi=120)
    plt.close()
    print(f"Da luu heatmap: {out_path}")


def plot_rssi(df, out_path):
    plt.figure(figsize=(12, 4))
    plt.plot(df["timestamp_ms"] / 1000.0, df["rssi"], linewidth=0.8)
    plt.xlabel("Thoi gian (giay)")
    plt.ylabel("RSSI (dBm)")
    plt.title("RSSI theo thoi gian")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(out_path, dpi=120)
    plt.close()
    print(f"Da luu RSSI: {out_path}")


def plot_mean_amplitude(df, amp_cols, out_path):
    mean_amp = df[amp_cols].mean(axis=1)
    plt.figure(figsize=(12, 4))
    plt.plot(df["timestamp_ms"] / 1000.0, mean_amp, linewidth=0.8)
    plt.xlabel("Thoi gian (giay)")
    plt.ylabel("Amplitude trung binh")
    plt.title("Amplitude trung binh theo thoi gian")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(out_path, dpi=120)
    plt.close()
    print(f"Da luu mean amplitude: {out_path}")


def print_statistics(df, amp_cols):
    print("\n===== THONG KE =====")
    print(f"Tong so goi: {len(df)}")
    duration = df["timestamp_ms"].max() / 1000
    print(f"Thoi gian: {duration:.1f} giay")
    print(f"Toc do: {len(df) / duration:.1f} pkt/s")
    amp_matrix = df[amp_cols].values
    print(f"\nAmplitude toan cuc:")
    print(f"  Min: {amp_matrix.min():.2f}")
    print(f"  Max: {amp_matrix.max():.2f}")
    print(f"  Mean: {amp_matrix.mean():.2f}")
    print(f"  Std: {amp_matrix.std():.2f}")
    var_per_sub = amp_matrix.var(axis=0)
    top5 = np.argsort(var_per_sub)[-5:][::-1]
    print(f"\nTop 5 subcarrier bien dong manh nhat:")
    for idx in top5:
        print(f"  Subcarrier {idx}: var={var_per_sub[idx]:.2f}")


def main():
    if len(sys.argv) < 2:
        print("Cach dung: python analyze_csi.py <duong_dan_file.csv>")
        sys.exit(1)
    path = sys.argv[1]
    if not os.path.exists(path):
        print(f"File khong ton tai: {path}")
        sys.exit(1)
    df, amp_cols = load_csi_csv(path)
    print_statistics(df, amp_cols)
    base = os.path.splitext(path)[0]
    plot_rssi(df, base + "_rssi.png")
    plot_mean_amplitude(df, amp_cols, base + "_mean_amp.png")
    plot_amplitude_heatmap(df, amp_cols, base + "_heatmap.png")


if __name__ == "__main__":
    main()
