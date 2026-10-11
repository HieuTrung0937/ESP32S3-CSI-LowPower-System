#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp32s3-csi-har.h"

uint8_t tx_mac[] = {0x80, 0xB5, 0x4E, 0xDE, 0xEE, 0x70};
uint8_t rx_mac[] = {0x80, 0xB5, 0x4E, 0xDE, 0xEC, 0x48};

extern "C" void app_main() {
    // 1. Cấu hình MAC
    csi_har_set_mac(tx_mac, rx_mac);

    // 2. Khởi tạo TX ở Channel 1 (HT20 MCS0)
    csi_har_begin(CSI_MODE_TX, 1);

    // 3. Sleep nhường CPU cho FreeRTOS Task gửi gói
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}