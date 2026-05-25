#include "CsiReceiver.hpp"

CsiReceiver *CsiReceiver::s_instance = nullptr;

// ── Promiscuous callback — interrupt context ──────────────────────────────────
void IRAM_ATTR CsiReceiver::s_promiscuous_cb(
        void *buf, wifi_promiscuous_pkt_type_t type) {

    if (!s_instance) return;

    auto *pkt = static_cast<wifi_promiscuous_pkt_t *>(buf);
    auto *hdr = reinterpret_cast<wifi_80211_hdr_t *>(pkt->payload);

    if (memcmp(hdr->addr2, MAC_BOARD_TX, 6) != 0) return;

    csi_event_t ev = {
        .seq     = ++s_instance->pkt_count_,
        .rssi    = pkt->rx_ctrl.rssi,
        .sig_len = (int)pkt->rx_ctrl.sig_len,
    };

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_instance->event_queue_, &ev, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

// ── Process task — chỉ printf, không LED, không delay ────────────────────────
void CsiReceiver::s_process_task(void *arg) {
    auto *self = static_cast<CsiReceiver *>(arg);
    csi_event_t ev;

    while (1) {
        if (xQueueReceive(self->event_queue_, &ev, portMAX_DELAY) == pdTRUE) {
            if (ev.sig_len > 0) {
                printf("CSI_RAW,%d,%d\n", ev.rssi, ev.sig_len);
            } else {
                printf("RSSI_DATA,%lu,%d\n", (unsigned long)ev.seq, ev.rssi);
            }
        }
    }
}

// ── Stats task — heartbeat LED + log mỗi 1 giây ──────────────────────────────
void CsiReceiver::s_stats_task(void *arg) {
    auto *self = static_cast<CsiReceiver *>(arg);
    uint32_t last = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t now  = self->pkt_count_;
        uint32_t rate = now - last;
        last = now;

        ESP_LOGI("CSI_RX", "pkt/s: %lu  total: %lu  queue: %u",
                 (unsigned long)rate,
                 (unsigned long)now,
                 (unsigned)uxQueueMessagesWaiting(self->event_queue_));

        if (self->led_strip_) {
            led_strip_set_pixel(self->led_strip_, 0, 0, 8, 0);
            led_strip_refresh(self->led_strip_);
            vTaskDelay(pdMS_TO_TICKS(30));
            led_strip_clear(self->led_strip_);
            led_strip_refresh(self->led_strip_);
        }
    }
}

// ── Init methods ──────────────────────────────────────────────────────────────
void CsiReceiver::init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void CsiReceiver::init_wifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(s_promiscuous_cb));

    ESP_LOGI("CSI_RX", "Promiscuous mode enabled");
}

void CsiReceiver::init_led() {
    gpio_reset_pin(GPIO_NUM_45);

    led_strip_config_t strip_cfg = {
        .strip_gpio_num         = GPIO_NUM_45,
        .max_leds               = 1,
        .led_model              = LED_MODEL_WS2812,           // đúng thứ tự
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags                  = { .invert_out = false }
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 0,                               // tránh warning
        .flags             = { .with_dma = false }
    };

    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led_strip_) == ESP_OK) {
        // Test đỏ 500ms khi boot
        led_strip_set_pixel(led_strip_, 0, 15, 0, 0);
        led_strip_refresh(led_strip_);
        vTaskDelay(pdMS_TO_TICKS(500));
        led_strip_clear(led_strip_);
        led_strip_refresh(led_strip_);
        ESP_LOGI("CSI_RX", "LED init OK");
    } else {
        ESP_LOGE("CSI_RX", "LED init FAILED");
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────
void CsiReceiver::begin() {
    s_instance   = this;
    event_queue_ = xQueueCreate(128, sizeof(csi_event_t));

    init_nvs();
    init_led();
    init_wifi();

    ESP_LOGI("CSI_RX", "CsiReceiver started");

    // process_task Core 0 — ưu tiên cao để không bỏ lỡ gói
    // stats_task   Core 1 — ưu tiên thấp hơn, chỉ log + LED
    xTaskCreatePinnedToCore(s_process_task, "csi_proc", 4096, this, 5, nullptr, 0);
    xTaskCreatePinnedToCore(s_stats_task,   "csi_stat", 2048, this, 3, nullptr, 1);
}