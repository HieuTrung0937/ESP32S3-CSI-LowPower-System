#include "esp_now_system.h"

#define NEOPIXEL_PIN       GPIO_NUM_45
#define STRIP_LED_COUNT    1

static const char *TAG = "ESP_NOW_RX";
static led_strip_handle_t led_strip;
static volatile uint32_t csi_count = 0;

// Promiscuous callback - nhận tất cả gói Wi-Fi
static void IRAM_ATTR promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    wifi_pkt_rx_ctrl_t *ctrl = (wifi_pkt_rx_ctrl_t *)&pkt->rx_ctrl;
    
    // Chỉ xử lý gói từ TX (theo MAC)
    uint8_t *mac = pkt->payload + 10; // Địa chỉ MAC nguồn trong frame 802.11
    if (memcmp(mac, MAC_BOARD_TX, 6) == 0) {
        // Đây là gói từ TX, có CSI
        csi_count++;
        if (csi_count % 100 == 0) {
            ESP_LOGI(TAG, "Got packet from TX #%lu, RSSI=%d", csi_count, ctrl->rssi);
        }
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
    
    led_strip_set_pixel(led_strip, 0, 15, 0, 0);
    led_strip_refresh(led_strip);
    vTaskDelay(pdMS_TO_TICKS(500));
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
    ESP_LOGI(TAG, "LED test done");
}

static void init_wifi_promiscuous(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Bật promiscuous
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(promiscuous_cb));
    
    ESP_LOGI(TAG, "Promiscuous mode enabled");
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
    init_wifi_promiscuous();

    ESP_LOGI(TAG, "RX ready in promiscuous mode");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // In thống kê mỗi giây
        static uint32_t last_csi = 0;
        ESP_LOGI(TAG, "CSI packets/sec: %lu", csi_count - last_csi);
        last_csi = csi_count;
    }
}