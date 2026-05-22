#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// Định nghĩa kênh truyền RF chung cho cả 2 board (từ 1 đến 14)
#define ESPNOW_WIFI_CHANNEL 1

// Địa chỉ MAC định danh của Board Phát (TX)
static const uint8_t MAC_BOARD_TX[6] = {0x80, 0xB5, 0x4E, 0xDE, 0xEE, 0x70};

// Địa chỉ MAC định danh của Board Nhận (RX)
static const uint8_t MAC_BOARD_RX[6] = {0x80, 0xB5, 0x4E, 0xDE, 0xEC, 0x48};

// Cấu trúc gói tin trao đổi dữ liệu kiểm thử giữa hai mạch
typedef struct {
    uint32_t packet_seq;     // Số thứ tự gói tin để kiểm tra tỉ lệ mất gói
    uint64_t timestamp_ms;   // Thời gian gửi (ms) để tính toán độ trễ (latency)
    float telemetry_value;   // Giá trị dữ liệu giả lập (hoặc cảm biến sau này)
} __attribute__((packed)) esp_now_payload_t;

#endif