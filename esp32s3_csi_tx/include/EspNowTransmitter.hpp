#ifndef ESP_NOW_TRANSMITTER_HPP
#define ESP_NOW_TRANSMITTER_HPP

#include "esp_now_system.h"
class EspNowTransmitter {
public:
    void begin();

private:
    led_strip_handle_t led_strip_  = nullptr;
    uint32_t           send_count_ = 0;
    esp_now_payload_t  tx_data_    = {0, 0, 11.7f};
// Trong EspNowTransmitter.hpp, thêm vào private:
    TickType_t last_wake_time; 
    void init_nvs();
    void init_wifi();
    void init_led();
    void init_espnow();
    void check_temperature();
    // Callback gửi — static bắt buộc cho ESP-NOW C API
    static void s_send_cb(const wifi_tx_info_t *info,
                          esp_now_send_status_t status);

    // Task vòng lặp gửi liên tục
    static void s_send_task(void *arg);

    static EspNowTransmitter *s_instance;
};

#endif