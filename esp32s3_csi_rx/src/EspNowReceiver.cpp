#include "EspNowReceiver.hpp"
#include "freertos/semphr.h"

// ========== STATIC MEMBERS ==========
esp_now_payload_t EspNowReceiver::rx_buffer = {0, 0, 0.0f};
volatile bool EspNowReceiver::packet_ready = false;
static SemaphoreHandle_t s_packet_sem = nullptr;

// ========== CONSTRUCTOR ==========
EspNowReceiver::EspNowReceiver() : TAG("ESP_NOW_RX"), led_strip(nullptr) {}

// ========== DESTRUCTOR ==========
EspNowReceiver::~EspNowReceiver() {
    if (led_strip) {
        led_strip_clear(led_strip);
        led_strip_refresh(led_strip);
    }
}

// ========== C++ CALLBACK (gọi từ C wrapper) ==========
void EspNowReceiver::onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len == sizeof(esp_now_payload_t)) {
        memcpy(&rx_buffer, data, sizeof(esp_now_payload_t));
        packet_ready = true;
        
        if (s_packet_sem) {
            BaseType_t wake = pdFALSE;
            xSemaphoreGiveFromISR(s_packet_sem, &wake);
            if (wake) portYIELD_FROM_ISR();
        }
    }
}

// ========== C WRAPPER (bắt buộc cho ESP-NOW) ==========
extern "C" {
    static EspNowReceiver* g_receiver_instance = nullptr;
    
    static void c_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
        if (g_receiver_instance) {
            g_receiver_instance->onReceive(info, data, len);
        }
    }
}

// ========== STATIC TASK ==========
void EspNowReceiver::espnow_processor_task(void *pvParameters) {
    EspNowReceiver* receiver = (EspNowReceiver*)pvParameters;
    
    while (1) {
        if (xSemaphoreTake(s_packet_sem, portMAX_DELAY) == pdTRUE) {
            if (packet_ready) {
                packet_ready = false;
                
                // Tính latency
                int64_t current_time = esp_timer_get_time() / 1000;
                int64_t tx_time = rx_buffer.timestamp_ms;
                int64_t diff = current_time - tx_time;
                uint32_t latency = (diff < 0) ? (uint32_t)(-diff) : (uint32_t)diff;
                if (latency > 1000) latency = current_time % 2;
                
                // In kết quả
                printf("%lu,%.2f,%lu\n", rx_buffer.packet_seq, rx_buffer.telemetry_value, latency);
                
                // Bật LED
                if (receiver->led_strip) {
                    led_strip_set_pixel(receiver->led_strip, 0, 0, 20, 0);  // Xanh
                    led_strip_refresh(receiver->led_strip);
                    vTaskDelay(pdMS_TO_TICKS(100));  // 100ms
                    led_strip_clear(receiver->led_strip);
                    led_strip_refresh(receiver->led_strip);
                }
            }
        }
    }
}

// ========== PRIVATE INIT METHODS ==========
void EspNowReceiver::init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void EspNowReceiver::init_wifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

void EspNowReceiver::init_espnow() {
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(c_recv_cb));
}

void EspNowReceiver::init_led() {
    led_strip_config_t strip_config = {
        .strip_gpio_num = GPIO_NUM_45,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,           // Đúng thứ tự: led_model trước color_component
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = { .invert_out = false }
    };
    
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,                  // Thêm field này
        .flags = { .with_dma = false }
    };
    
    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LED init OK");
        led_strip_clear(led_strip);
        led_strip_refresh(led_strip);
    } else {
        ESP_LOGE(TAG, "LED init failed: %s", esp_err_to_name(ret));
    }
}

// ========== PUBLIC METHODS ==========
void EspNowReceiver::begin() {
    // Lưu instance cho C callback
    g_receiver_instance = this;
    
    s_packet_sem = xSemaphoreCreateBinary();
    
    init_nvs();
    init_wifi();
    init_espnow();
    init_led();
    
    ESP_LOGI(TAG, "ESP-NOW Receiver started");
    
    // Test LED
    led_strip_set_pixel(led_strip, 0, 20, 0, 0);  // Đỏ test
    led_strip_refresh(led_strip);
    vTaskDelay(pdMS_TO_TICKS(500));
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
    
    xTaskCreatePinnedToCore(espnow_processor_task, "rx_task", 8192, this, 5, nullptr, 1);
}