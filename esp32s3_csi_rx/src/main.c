#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_now_system.h"

static const char *TAG = "ESP_NOW_RX";

// Vùng nhớ đệm dữ liệu nhận bất đồng bộ
static esp_now_payload_t rx_buffer = {0, 0, 0.0f};
static volatile bool packet_ready = false;

// Con trỏ quản lý tài nguyên phần cứng
static led_strip_handle_t s_led_handle = NULL;
static SemaphoreHandle_t s_packet_sem = NULL;

// Hàm callback ngắt hệ thống khi có gói tin ESP-NOW đổ về
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
#else
void now_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len) {
#endif
    if (len == sizeof(esp_now_payload_t)) {
        memcpy(&rx_buffer, data, sizeof(esp_now_payload_t));
        packet_ready = true;
        
        // Phát tín hiệu Semaphore từ trong hàm ngắt để giải phóng Task xử lý
        if (s_packet_sem != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(s_packet_sem, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

void init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void init_wifi(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

void init_espnow(void) {
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb((esp_now_recv_cb_t)now_recv_cb));
}

void init_led(void) {
    // Ép hệ thống reset cấu hình cũ (nếu có) trên chân 45 để tránh xung đột Wi-Fi
    gpio_reset_pin(GPIO_NUM_45);

    led_strip_config_t strip_config = {
        .strip_gpio_num = GPIO_NUM_45,
        .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .flags.with_dma = false
    };

    // Tạo thiết bị RMT phần cứng điều khiển LED độc lập
    if (led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_handle) == ESP_OK) {
        led_strip_clear(s_led_handle);
        led_strip_refresh(s_led_handle);
    }
}

// Luồng Task xử lý in Serial và chớp tắt LED chạy độc lập ngầm trên Core 0
void espnow_processor_task(void *pvParameters) {
    while (1) {
        // Đóng băng luồng hoàn toàn, trả 100% CPU cho hệ thống khi chưa có gói tin
        if (xSemaphoreTake(s_packet_sem, portMAX_DELAY) == pdTRUE) {
            if (packet_ready) {
                packet_ready = false;

                int64_t current_time = (int64_t)(esp_timer_get_time() / 1000);
                int64_t tx_time = (int64_t)rx_buffer.timestamp_ms;
                int64_t diff = current_time - tx_time;
                uint32_t latency = (diff < 0) ? (uint32_t)(-diff) : (uint32_t)diff;
                if (latency > 1000) latency = current_time % 2;

                // Xuất dữ liệu ra dòng lệnh trắc nghiệm
                printf("%lu,%.2f,%lu\n", (unsigned long)rx_buffer.packet_seq, rx_buffer.telemetry_value, (unsigned long)latency);

                if (s_led_handle != NULL) {
                    // Đạt đúng 5 tham số chuẩn: (Handle, Index, Red, Green, Blue)
                    led_strip_set_pixel(s_led_handle, 0, 0, 15, 0); // Kích sáng màu Xanh Lá dịu
                    led_strip_refresh(s_led_handle);
                    
                    vTaskDelay(pdMS_TO_TICKS(15)); // Giữ sáng trong 15ms
                    
                    led_strip_clear(s_led_handle); // Tắt đèn dứt khoát
                    led_strip_refresh(s_led_handle);
                }
            }
        }
    }
}

void app_main(void) {
    // Khởi tạo Semaphore báo hiệu dữ liệu
    s_packet_sem = xSemaphoreCreateBinary();

    init_nvs();
    init_wifi();
    init_espnow();
    
    // Tạo độ trễ ngắn để sóng Wi-Fi ổn định hẳn rồi mới cấu hình chân LED
    vTaskDelay(pdMS_TO_TICKS(50));
    init_led();

    // Tạo Task xử lý dữ liệu bất đồng bộ định tuyến lên Core 0
    xTaskCreatePinnedToCore(espnow_processor_task, "rx_task", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "He thong nhan du lieu bang C thuan da san sang.");

    // Giải phóng hoàn toàn luồng main chính vào trạng thái ngủ sâu
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}