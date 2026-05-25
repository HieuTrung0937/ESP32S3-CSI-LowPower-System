#include "EspNowTransmitter.hpp"

EspNowTransmitter *EspNowTransmitter::s_instance = nullptr;

// ── Send callback ─────────────────────────────────────────────────────────────
// Chạy trong WiFi driver context — giữ nhẹ, không block
void EspNowTransmitter::s_send_cb(const wifi_tx_info_t *info,
                                   esp_now_send_status_t status) {
    if (!s_instance) return;
    s_instance->send_count_++;

    if (status == ESP_NOW_SEND_SUCCESS) {
        // Log mỗi 100 gói để không spam UART
        if (s_instance->send_count_ % 100 == 0) {
            ESP_LOGI("TX_CB", "OK #%lu", (unsigned long)s_instance->send_count_);
        }
        // LED xanh lá nhẹ — báo ACK thành công
        if (s_instance->led_strip_) {
            led_strip_set_pixel(s_instance->led_strip_, 0, 0, 10, 0);
            led_strip_refresh(s_instance->led_strip_);
        }
    } else {
        ESP_LOGE("TX_CB", "FAIL #%lu", (unsigned long)s_instance->send_count_);
        // LED đỏ — báo fail
        if (s_instance->led_strip_) {
            led_strip_set_pixel(s_instance->led_strip_, 0, 10, 0, 0);
            led_strip_refresh(s_instance->led_strip_);
        }
    }
}

// ── Send loop task ────────────────────────────────────────────────────────────
void EspNowTransmitter::s_send_task(void *arg) {
    auto *self = static_cast<EspNowTransmitter *>(arg);
    
    // Điều chỉnh chu kỳ
    const TickType_t interval = pdMS_TO_TICKS(50);  // 50ms = 20Hz

    while (1) {
        self->tx_data_.packet_seq++;
        self->tx_data_.timestamp_ms = esp_timer_get_time() / 1000;
        self->tx_data_.telemetry_value += 0.01f;

        esp_err_t err = esp_now_send(
            MAC_BOARD_RX,
            reinterpret_cast<uint8_t *>(&self->tx_data_),
            sizeof(esp_now_payload_t)
        );

        if (err != ESP_OK) {
            ESP_LOGE("TX_TASK", "esp_now_send failed: %s", esp_err_to_name(err));
        }

        // LED nháy nhanh hơn
        vTaskDelay(pdMS_TO_TICKS(10));
        if (self->led_strip_) {
            led_strip_clear(self->led_strip_);
            led_strip_refresh(self->led_strip_);
        }

        // Chờ đến chu kỳ tiếp theo
        vTaskDelayUntil(&self->last_wake_time, interval);
    }
}

// ── Init methods ──────────────────────────────────────────────────────────────
void EspNowTransmitter::init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void EspNowTransmitter::init_wifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    // TX power vừa phải để CSI nhạy hơn
    esp_wifi_set_max_tx_power(40);

    // 11N bắt buộc để có CSI
    ESP_ERROR_CHECK(esp_wifi_set_protocol(
        WIFI_IF_STA,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N
    ));
}

void EspNowTransmitter::init_led() {
    led_strip_config_t strip_cfg = {
        .strip_gpio_num           = GPIO_NUM_45,
        .max_leds                 = 1,
        .led_model                = LED_MODEL_WS2812,      // led_model trước
        .color_component_format   = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags                    = { .invert_out = false }
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src          = RMT_CLK_SRC_DEFAULT,
        .resolution_hz    = 10 * 1000 * 1000,
        .mem_block_symbols = 0,                            // thêm field bị thiếu
        .flags            = { .with_dma = false }
    };

    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led_strip_) == ESP_OK) {
        // Test đỏ 200ms khi boot
        led_strip_set_pixel(led_strip_, 0, 10, 0, 0);
        led_strip_refresh(led_strip_);
        vTaskDelay(pdMS_TO_TICKS(200));
        led_strip_clear(led_strip_);
        led_strip_refresh(led_strip_);
        ESP_LOGI("TX_LED", "LED init OK");
    } else {
        ESP_LOGE("TX_LED", "LED init FAILED");
    }
}

void EspNowTransmitter::init_espnow() {
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(s_send_cb));

    // Đăng ký peer RX
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, MAC_BOARD_RX, 6);
    peer.channel = ESPNOW_WIFI_CHANNEL;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    // Tốc độ thấp để CSI ổn định
    esp_now_rate_config_t rate_cfg = {
        .phymode = WIFI_PHY_MODE_11G,
        .rate    = WIFI_PHY_RATE_2M_L,
        .ersu    = false,
        .dcm     = false    // thêm field này
    };
    esp_now_set_peer_rate_config(MAC_BOARD_RX, &rate_cfg);
}

// ── Entry point ───────────────────────────────────────────────────────────────
void EspNowTransmitter::begin() {
    s_instance = this;

    init_nvs();
    // init_led();       // LED trước để test boot
    init_wifi();
    init_espnow();
    last_wake_time = xTaskGetTickCount();
    ESP_LOGI("TX", "EspNowTransmitter started — 1 Hz, PHY 11G 2Mbps");

    xTaskCreatePinnedToCore(s_send_task, "tx_send", 4096, this, 5, nullptr, 1);
}