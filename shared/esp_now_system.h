#ifndef ESP_NOW_SYSTEM_H
#define ESP_NOW_SYSTEM_H

// 1. Thư viện tiêu chuẩn C
#include <stdio.h>
#include <string.h>

// 2. Thư viện hệ điều hành FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 3. Thư viện lưu trữ và hệ thống ESP32
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_timer.h"

// 4. Thư viện Wi-Fi và mạng ESP-NOW
#include "esp_wifi.h"
#include "esp_now.h"

// 5. Driver ngoại vi (LED NeoPixel)
#include "led_strip.h"
#include "driver/gpio.h"

// 6. Cấu hình định tuyến và gói tin của dự án
#include "config.h"

#endif // ESP_NOW_SYSTEM_H