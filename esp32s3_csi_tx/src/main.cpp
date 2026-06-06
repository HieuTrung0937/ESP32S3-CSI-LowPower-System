#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <cstdio>
#include <CSIFallGuard.h>

uint8_t tx_mac[] = {0x80, 0xB5, 0x4E, 0xDE, 0xEE, 0x70};
uint8_t rx_mac[] = {0x80, 0xB5, 0x4E, 0xDE, 0xEC, 0x48};

extern "C" void app_main() {
    // Cấu hình bắt buộc (cả 3 dòng)
    CSIFallGuard_set_tx_mac(tx_mac);
    CSIFallGuard_set_rx_mac(rx_mac);
    CSIFallGuard_set_channel(1);          // dù mặc định là 1, vẫn nên set

    CSIFallGuard_begin(MODE_TX, 1);       // mode TX
    
    while (1) {
        CSIFallGuard_update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}