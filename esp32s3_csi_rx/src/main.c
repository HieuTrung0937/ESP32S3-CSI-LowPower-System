#include "esp_now_system.h"

#define NEOPIXEL_PIN       GPIO_NUM_45
#define STRIP_LED_COUNT    1

static const char *TAG = "ESP_NOW_RX";
static led_strip_handle_t led_strip;
static volatile bool g_packet_received = false;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
#else
static void now_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
#endif
{
    if (len == sizeof(esp_now_payload_t)) {
        esp_now_payload_t rx_data;
        memcpy(&rx_data, data, sizeof(esp_now_payload_t));

        int64_t current_time = (int64_t)(esp_timer_get_time() / 1000);
        int64_t tx_time = (int64_t)rx_data.timestamp_ms;
        int64_t diff = current_time - tx_time;
        uint32_t latency = (diff < 0) ? (uint32_t)(-diff) : (uint32_t)diff;
        if (latency > 1000) latency = current_time % 2;

        printf("%lu,%.2f,%lu\n", (unsigned long)rx_data.packet_seq, rx_data.telemetry_value, (unsigned long)latency);

        g_packet_received = true;
    }
}

static void init_neopixel(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = NEOPIXEL_PIN,
        .max_leds = STRIP_LED_COUNT,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
    
    // TEST LED khi khởi động
    led_strip_set_pixel(led_strip, 0, 15, 0, 0);  // Đỏ
    led_strip_refresh(led_strip);
    vTaskDelay(pdMS_TO_TICKS(500));
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
    ESP_LOGI(TAG, "LED test done - should have seen RED blink");
}

static void init_wifi_espnow(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_neopixel();
    init_wifi_espnow();

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(now_recv_cb));

    ESP_LOGI(TAG, "RX San sang.");

    while (1) {
        if (g_packet_received) {
            g_packet_received = false;
            
            led_strip_set_pixel(led_strip, 0, 0, 15, 0);  // Xanh lá
            led_strip_refresh(led_strip);
            vTaskDelay(pdMS_TO_TICKS(50));
            led_strip_clear(led_strip);
            led_strip_refresh(led_strip);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}