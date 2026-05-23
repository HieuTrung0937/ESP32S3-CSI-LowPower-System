#ifndef ESP_NOW_RECEIVER_HPP
#define ESP_NOW_RECEIVER_HPP

#include "esp_now_system.h"

class EspNowReceiver {
private:
    const char* TAG;
    led_strip_handle_t led_strip;
    
    static esp_now_payload_t rx_buffer;
    static volatile bool packet_ready;
    
    void init_nvs();
    void init_wifi();
    void init_espnow();
    void init_led();
    
    static void espnow_processor_task(void *pvParameters);
    
public:
    EspNowReceiver();
    ~EspNowReceiver();
    
    void begin();
    void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len);
};

#endif