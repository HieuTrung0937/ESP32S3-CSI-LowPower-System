#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define ESPNOW_WIFI_CHANNEL 1

// Địa chỉ MAC của 2 board
static const uint8_t MAC_BOARD_TX[6] = {0x80, 0xB5, 0x4E, 0xDE, 0xEE, 0x70};
static const uint8_t MAC_BOARD_RX[6] = {0x80, 0xB5, 0x4E, 0xDE, 0xEC, 0x48};

// Cấu trúc gói tin ESP-NOW
typedef struct {
    uint32_t packet_seq;
    uint32_t timestamp_ms;
    float telemetry_value;
} __attribute__((packed)) esp_now_payload_t;

#endif