#include "esp_now_system.h" 

#define NEOPIXEL_PIN       GPIO_NUM_45
#define STRIP_LED_COUNT    1

static const char *TAG = "ESP_NOW_TX";
static led_strip_handle_t led_strip;

// Hàm phản hồi khi gói tin được bắn đi
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void now_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
#else
static void now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
#endif
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "Gui thanh cong");
        // Bật màu Xanh Lá độ sáng 10 vừa đủ nhìn, mát mạch
        led_strip_set_pixel(led_strip, 0, 10, 0, 0);
        led_strip_refresh(led_strip);
    } else {
        ESP_LOGE(TAG, "Gui THAT BAI");
        // Bật màu Đỏ báo lỗi kết nối
        led_strip_set_pixel(led_strip, 0, 0, 10, 0);
        led_strip_refresh(led_strip);
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

    // Hạ công suất xuống mức 40 để chip chạy mát 24/7 trên bàn làm việc
    esp_wifi_set_max_tx_power(40); 

    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
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
    ESP_ERROR_CHECK(esp_now_register_send_cb(now_send_cb));

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, MAC_BOARD_RX, 6);
    peer_info.channel = ESPNOW_WIFI_CHANNEL;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));

    esp_now_rate_config_t rate_config = {
        .phymode = WIFI_PHY_MODE_11B,
        .rate = WIFI_PHY_RATE_1M_L,
        .ersu = false
    };
    esp_now_set_peer_rate_config(MAC_BOARD_RX, &rate_config);

    ESP_LOGI(TAG, "TX San sang phat...");

    esp_now_payload_t tx_data;
    tx_data.packet_seq = 0;
    tx_data.telemetry_value = 11.7f; 

    while (1) {
        tx_data.packet_seq++;
        
        // ĐỒNG BỘ: Ép kiểu uint32_t chuẩn để gửi sang RX không bị lỗi bit âm
        tx_data.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
        tx_data.telemetry_value += 0.05f;
        
        esp_now_send(MAC_BOARD_RX, (uint8_t *)&tx_data, sizeof(tx_data));

        // Tắt đèn sau khi phát 30ms để tạo nhịp chớp nháy rõ ràng
        vTaskDelay(pdMS_TO_TICKS(30));
        led_strip_clear(led_strip);

        // Chờ nốt chu kỳ còn lại (Phát mỗi giây 1 gói)
        vTaskDelay(pdMS_TO_TICKS(970)); 
    }
}