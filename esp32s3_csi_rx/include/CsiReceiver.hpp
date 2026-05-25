#ifndef CSI_RECEIVER_HPP
#define CSI_RECEIVER_HPP

#include "esp_now_system.h"
#include "freertos/queue.h"

typedef struct {
    uint16_t frame_ctrl;
    uint16_t duration;
    uint8_t  addr1[6];
    uint8_t  addr2[6];   // transmitter MAC
    uint8_t  addr3[6];
} __attribute__((packed)) wifi_80211_hdr_t;

class CsiReceiver {
public:
    void begin();

private:
    led_strip_handle_t led_strip_   = nullptr;
    QueueHandle_t      event_queue_ = nullptr;
    volatile uint32_t  pkt_count_  = 0;

    void init_nvs();
    void init_wifi();
    void init_led();

    static void IRAM_ATTR s_promiscuous_cb(void *buf,
                                           wifi_promiscuous_pkt_type_t type);
    static void s_process_task(void *arg);
    static void s_stats_task(void *arg);

    static CsiReceiver *s_instance;
};

#endif