#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define ESPNOW_WIFI_CHANNEL 1

static const uint8_t MAC_BOARD_TX[6] = {0x80, 0xB5, 0x4E, 0xDE, 0xEE, 0x70};
static const uint8_t MAC_BOARD_RX[6] = {0x80, 0xB5, 0x4E, 0xDE, 0xEC, 0x48};

// Gói tin TX → RX qua ESP-NOW
typedef struct {
    uint32_t packet_seq;
    uint64_t timestamp_ms;   // uint64 — không cast về uint32 khi gán
    float    telemetry_value;
} __attribute__((packed)) esp_now_payload_t;

// Event từ promiscuous callback → queue xử lý (RX side)
typedef struct {
    uint32_t seq;
    int      rssi;
    int      sig_len;
} csi_event_t;

#endif