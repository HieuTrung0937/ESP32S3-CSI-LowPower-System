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
        .seq     = s_instance->pkt_count_ + 1,
        .rssi    = pkt->rx_ctrl.rssi,
        .sig_len = (int)pkt->rx_ctrl.sig_len,
    };
    s_instance->pkt_count_ = s_instance->pkt_count_ + 1;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_instance->event_queue_, &ev, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

// ── Process task — lọc, baseline, phát hiện người, LED trực quan ───────────────
void CsiReceiver::s_process_task(void *arg) {
    auto *self = static_cast<CsiReceiver *>(arg);
    csi_event_t ev;
    
    // Bộ lọc trung bình động
    #define RSSI_WINDOW_SIZE 5
    int rssi_window[RSSI_WINDOW_SIZE] = {0};
    int window_idx = 0;
    bool window_filled = false;
    
    // Baseline
    int baseline_rssi = 0;
    bool baseline_ready = false;
    int baseline_count = 0;
    
    // Phát hiện người thông minh
    bool person_present = false;
    bool already_alerted = false;
    int64_t last_detection_time = 0;
    int stable_count = 0;
    uint32_t last_heartbeat = 0;
    uint32_t last_person_blink = 0;
        
    const int DEBOUNCE_THRESHOLD = 1;
    const int DETECTION_THRESHOLD = 5;   // 7 → 5
    const int EXIT_TIMEOUT_MS = 2000;    // 3000 → 2000
    
    while (1) {
        if (xQueueReceive(self->event_queue_, &ev, portMAX_DELAY) == pdTRUE) {
            
            // === LỌC TRUNG BÌNH ĐỘNG ===
            rssi_window[window_idx] = ev.rssi;
            window_idx = (window_idx + 1) % RSSI_WINDOW_SIZE;
            if (window_idx == 0) window_filled = true;
            
            int num_samples = window_filled ? RSSI_WINDOW_SIZE : window_idx;
            int filtered_rssi = 0;
            for (int i = 0; i < num_samples; i++) {
                filtered_rssi += rssi_window[i];
            }
            filtered_rssi /= num_samples;
            
            // === CAPTURE BASELINE (300 gói đầu, ~30 giây) ===
            if (!baseline_ready && ev.seq < 300) {
                if (baseline_count == 0) {
                    baseline_rssi = filtered_rssi;
                } else {
                    baseline_rssi = (baseline_rssi + filtered_rssi) / 2;
                }
                baseline_count++;
                
                if (ev.seq == 299) {
                    baseline_ready = true;
                    ESP_LOGI("CSI_RX", "========================================");
                    ESP_LOGI("CSI_RX", "BASELINE CAPTURED: RSSI = %d dBm", baseline_rssi);
                    ESP_LOGI("CSI_RX", "Threshold: %d dB", DETECTION_THRESHOLD);
                    ESP_LOGI("CSI_RX", "========================================");
                }
            }
            
            // === PHÁT HIỆN NGƯỜI ===
            int delta = 0;
            uint32_t now_ms = esp_timer_get_time() / 1000;
            
            if (baseline_ready) {
                delta = abs(filtered_rssi - baseline_rssi);
                
                // Phát hiện có người (debounce)
                if (delta > DETECTION_THRESHOLD) {
                    stable_count++;
                    if (stable_count >= DEBOUNCE_THRESHOLD && !person_present) {
                        person_present = true;
                        last_detection_time = now_ms;
                        ESP_LOGW("HUMAN", "!!! PERSON ENTERED !!! (delta=%d dB)", delta);
                        
                        // === LED BÁO NGƯỜI VÀO: NHÁY ĐỎ NHANH 5 LẦN ===
                        for (int i = 0; i < 5; i++) {
                            if (self->led_strip_) {
                                led_strip_set_pixel(self->led_strip_, 0, 30, 0, 0);
                                led_strip_refresh(self->led_strip_);
                                vTaskDelay(pdMS_TO_TICKS(80));
                                led_strip_clear(self->led_strip_);
                                led_strip_refresh(self->led_strip_);
                                vTaskDelay(pdMS_TO_TICKS(80));
                            }
                        }
                    }
                } else {
                    if (stable_count > 0) {
                        stable_count--;
                    }
                }
                
                // Kiểm tra timeout để reset (người đã rời)
                if (person_present) {
                    int64_t time_since_last = now_ms - last_detection_time;
                    if (time_since_last > EXIT_TIMEOUT_MS) {
                        if (delta < DETECTION_THRESHOLD / 2) {
                            person_present = false;
                            already_alerted = false;
                            stable_count = 0;
                            ESP_LOGI("HUMAN", "=== PERSON LEFT ===");
                            
                            // === LED BÁO NGƯỜI RA: NHÁY XANH CHẬM 3 LẦN ===
                            for (int i = 0; i < 3; i++) {
                                if (self->led_strip_) {
                                    led_strip_set_pixel(self->led_strip_, 0, 0, 20, 0);
                                    led_strip_refresh(self->led_strip_);
                                    vTaskDelay(pdMS_TO_TICKS(150));
                                    led_strip_clear(self->led_strip_);
                                    led_strip_refresh(self->led_strip_);
                                    vTaskDelay(pdMS_TO_TICKS(150));
                                }
                            }
                        } else {
                            last_detection_time = now_ms;
                        }
                    }
                }
                
                // === LED TRẠNG THÁI HIỆN TẠI ===
                if (person_present) {
                    // Có người: LED đỏ nhấp nháy chậm (1Hz)
                    if (now_ms - last_person_blink > 1000) {
                        if (self->led_strip_) {
                            led_strip_set_pixel(self->led_strip_, 0, 15, 0, 0);
                            led_strip_refresh(self->led_strip_);
                            vTaskDelay(pdMS_TO_TICKS(100));
                            led_strip_clear(self->led_strip_);
                            led_strip_refresh(self->led_strip_);
                        }
                        last_person_blink = now_ms;
                    }
                } else {
                    // Không có người: heartbeat LED xanh mỗi 2 giây
                    if (now_ms - last_heartbeat > 2000) {
                        if (self->led_strip_) {
                            led_strip_set_pixel(self->led_strip_, 0, 0, 8, 0);
                            led_strip_refresh(self->led_strip_);
                            vTaskDelay(pdMS_TO_TICKS(50));
                            led_strip_clear(self->led_strip_);
                            led_strip_refresh(self->led_strip_);
                        }
                        last_heartbeat = now_ms;
                    }
                }
                
                // In log
                printf("RSSI,%lu,%d,%d,%d,%d\n", 
                       (unsigned long)ev.seq, ev.rssi, filtered_rssi, delta, person_present ? 1 : 0);
            } else {
                // Chưa có baseline
                printf("RSSI,%lu,%d,%d,0,0\n", 
                       (unsigned long)ev.seq, ev.rssi, filtered_rssi);
            }
        }
    }
}

// ── Stats task — thống kê đơn giản ────────────────────────────────────────────
void CsiReceiver::s_stats_task(void *arg) {
    auto *self = static_cast<CsiReceiver *>(arg);
    uint32_t last = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));  // 1000 → 2000ms

        uint32_t now  = self->pkt_count_;
        uint32_t rate = now - last;
        last = now;

        ESP_LOGI("CSI_RX", "pkt/s: %lu  total: %lu  queue: %u",
                 (unsigned long)rate,
                 (unsigned long)now,
                 (unsigned)uxQueueMessagesWaiting(self->event_queue_));
        
        // Bỏ LED heartbeat ở stats task để tránh xung đột
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
        .led_model              = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags                  = { .invert_out = false }
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags             = { .with_dma = false }
    };

    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led_strip_) == ESP_OK) {
        // Test đỏ 500ms khi boot
        led_strip_set_pixel(led_strip_, 0, 20, 0, 0);
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
    pkt_count_   = 0;
    event_queue_ = xQueueCreate(512, sizeof(csi_event_t));  // Tăng queue

    init_nvs();
    init_led();
    init_wifi();

    ESP_LOGI("CSI_RX", "========================================");
    ESP_LOGI("CSI_RX", "CSI Human Detection System Started");
    ESP_LOGI("CSI_RX", "Capturing baseline in 30 seconds...");
    ESP_LOGI("CSI_RX", "Please leave the room!");
    ESP_LOGI("CSI_RX", "========================================");

    // Tăng priority và stack
    xTaskCreatePinnedToCore(s_process_task, "csi_proc", 12288, this, 10, nullptr, 0);
    xTaskCreatePinnedToCore(s_stats_task,   "csi_stat", 2048, this, 3, nullptr, 1);
}