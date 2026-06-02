#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <cstdio>
#include <CSIFallGuard.h>

// MAC của board TX (đã xác định trước)
uint8_t tx_mac[] = {0x80, 0xB5, 0x4E, 0xDE, 0xEE, 0x70};
uint8_t rx_mac[] = {0x80, 0xB5, 0x4E, 0xDE, 0xEC, 0x48};

extern "C" void app_main() {
    // Set MAC trước khi begin (quan trọng!)
    CSIFallGuard_set_tx_mac(tx_mac);
    CSIFallGuard_set_rx_mac(rx_mac);
    
    // Khởi tạo TX mode
    CSIFallGuard_begin(MODE_TX, 1);
    
    while (1) {
        CSIFallGuard_update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}