#include "CsiReceiver.hpp"
#include <math.h>
#include <string.h>

CsiReceiver *CsiReceiver::s_instance = nullptr;

void compute_amplitude_c(const int16_t *csi_raw, float *amp_out) {
    for (int i = 0; i < 52; i++) {
        int16_t I = csi_raw[i * 2];
        int16_t Q = csi_raw[i * 2 + 1];
        amp_out[i] = sqrtf((float)(I * I + Q * Q));
    }
}

void IRAM_ATTR CsiReceiver::s_promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_instance) return;

    auto *pkt = static_cast<wifi_promiscuous_pkt_t *>(buf);
    auto *hdr = reinterpret_cast<wifi_80211_hdr_t *>(pkt->payload);

    if (memcmp(hdr->addr2, MAC_BOARD_TX, 6) != 0) return;

    csi_event_t ev;
    ev.seq = s_instance->pkt_count_ + 1;
    ev.rssi = pkt->rx_ctrl.rssi;
    ev.sig_len = (int)pkt->rx_ctrl.sig_len;
    
    if (ev.sig_len > 0) {
        memset(ev.csi_raw, 0, 104 * sizeof(int16_t));
    } else {
        memset(ev.csi_raw, 0, 104 * sizeof(int16_t));
    }
    
    s_instance->pkt_count_ = s_instance->pkt_count_ + 1;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_instance->event_queue_, &ev, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void CsiReceiver::s_process_task(void *arg) {
    auto *self = static_cast<CsiReceiver *>(arg);
    csi_event_t ev;
    
    #define RSSI_WINDOW_SIZE 8
    int rssi_window[RSSI_WINDOW_SIZE] = {0};
    int window_idx = 0;
    bool window_filled = false;
    
    int baseline_rssi = 0;
    bool baseline_ready = false;
    int baseline_count = 0;
    
    float baseline_amp[52] = {0};
    bool amp_baseline_ready = false;
    int amp_baseline_count = 0;
    
    bool person_present = false;
    int64_t last_detection_time = 0;
    int stable_count = 0;
    
    const int DEBOUNCE_THRESHOLD = 1;
    const int DETECTION_THRESHOLD = 5;
    const int EXIT_TIMEOUT_MS = 2000;
    
    float last_amp_change = 0;
    int amp_calc_counter = 0;
    int log_counter = 0;
    
    while (1) {
        if (xQueueReceive(self->event_queue_, &ev, portMAX_DELAY) == pdTRUE) {
            
            rssi_window[window_idx] = ev.rssi;
            window_idx = (window_idx + 1) & (RSSI_WINDOW_SIZE - 1);
            if (window_idx == 0) window_filled = true;
            
            int num_samples = window_filled ? RSSI_WINDOW_SIZE : window_idx;
            int sum = 0;
            for (int i = 0; i < num_samples; i++) {
                sum += rssi_window[i];
            }
            int filtered_rssi = sum >> 3;
            
            if (!baseline_ready && ev.seq < 256) {
                if (baseline_count == 0) {
                    baseline_rssi = filtered_rssi;
                } else {
                    baseline_rssi = (baseline_rssi + filtered_rssi) >> 1;
                }
                baseline_count++;
                if (ev.seq == 255) {
                    baseline_ready = true;
                    ESP_LOGI("CSI_RX", "BASELINE RSSI: %d dBm", baseline_rssi);
                }
            }
            
            if (!amp_baseline_ready && ev.seq < 256 && ev.sig_len > 0) {
                float current_amp[52];
                compute_amplitude_c(ev.csi_raw, current_amp);
                if (amp_baseline_count == 0) {
                    memcpy(baseline_amp, current_amp, sizeof(baseline_amp));
                } else {
                    for (int i = 0; i < 52; i++) {
                        baseline_amp[i] = (baseline_amp[i] + current_amp[i]) * 0.5f;
                    }
                }
                amp_baseline_count++;
                if (ev.seq == 255) {
                    amp_baseline_ready = true;
                    ESP_LOGI("CSI_RX", "AMP baseline ready");
                }
            }
            
            int delta = 0;
            uint32_t now_ms = esp_timer_get_time() / 1000;
            float amp_change = last_amp_change;
            
            if (baseline_ready) {
                delta = abs(filtered_rssi - baseline_rssi);
                
                if (amp_baseline_ready && ev.sig_len > 0) {
                    amp_calc_counter++;
                    if (amp_calc_counter >= 5) {
                        amp_calc_counter = 0;
                        float current_amp[52];
                        compute_amplitude_c(ev.csi_raw, current_amp);
                        float sum_change = 0;
                        for (int i = 0; i < 52; i++) {
                            float diff = current_amp[i] - baseline_amp[i];
                            sum_change += (diff < 0) ? -diff : diff;
                        }
                        last_amp_change = sum_change / 52;
                        amp_change = last_amp_change;
                    }
                }
                
                if (delta > DETECTION_THRESHOLD) {
                    stable_count++;
                    if (stable_count >= DEBOUNCE_THRESHOLD && !person_present) {
                        person_present = true;
                        last_detection_time = now_ms;
                        ESP_LOGW("HUMAN", "PERSON ENTERED (d=%d, amp=%.1f)", delta, amp_change);
                    }
                } else {
                    if (stable_count > 0) stable_count--;
                }
                
                if (person_present && (now_ms - last_detection_time) > EXIT_TIMEOUT_MS) {
                    if (delta < (DETECTION_THRESHOLD >> 1)) {
                        person_present = false;
                        stable_count = 0;
                        ESP_LOGI("HUMAN", "PERSON LEFT");
                    } else {
                        last_detection_time = now_ms;
                    }
                }
                
                log_counter++;
                if (log_counter >= 5) {
                    log_counter = 0;
                    printf("RSSI,%lu,%d,%d,%d,%d,%.1f\n", 
                           (unsigned long)ev.seq, ev.rssi, filtered_rssi, delta, 
                           person_present ? 1 : 0, amp_change);
                }
            }
        }
    }
}

void CsiReceiver::s_stats_task(void *arg) {
    auto *self = static_cast<CsiReceiver *>(arg);
    uint32_t last = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        uint32_t now = self->pkt_count_;
        uint32_t rate = now - last;
        last = now;
        ESP_LOGI("CSI_RX", "pkt/s: %lu  total: %lu  queue: %u",
                 (unsigned long)rate, (unsigned long)now,
                 (unsigned)uxQueueMessagesWaiting(self->event_queue_));
    }
}

CsiReceiver::~CsiReceiver() {
    if (event_queue_) {
        vQueueDelete(event_queue_);
    }
    if (led_strip_) {
        led_strip_del(led_strip_);
    }
}

void CsiReceiver::init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
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
    // BỎ TRỐNG - không dùng LED nữa
}

void CsiReceiver::begin() {
    s_instance   = this;
    pkt_count_   = 0;
    event_queue_ = xQueueCreate(1024, sizeof(csi_event_t));

    init_nvs();
    init_wifi();

    ESP_LOGI("CSI_RX", "========================================");
    ESP_LOGI("CSI_RX", "CSI Human Detection Started (LED disabled)");
    ESP_LOGI("CSI_RX", "Capturing baseline in 25 seconds...");
    ESP_LOGI("CSI_RX", "========================================");

    xTaskCreatePinnedToCore(s_process_task, "csi_proc", 16384, this, 15, nullptr, 0);
    xTaskCreatePinnedToCore(s_stats_task,   "csi_stat", 2048, this, 3, nullptr, 1);
}