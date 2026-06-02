#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <cstdio>
#include <CSIFallGuard.h>
// MAC thật của 2 board
uint8_t my_tx_mac[] = {0x80, 0xB5, 0x4E, 0xDE, 0xEE, 0x70};
uint8_t my_rx_mac[] = {0x80, 0xB5, 0x4E, 0xDE, 0xEC, 0x48};

extern "C" void app_main() {
    // Cấu hình MAC trước khi begin
    CSIFallGuard_set_tx_mac(my_tx_mac);
    CSIFallGuard_set_rx_mac(my_rx_mac);
    CSIFallGuard_set_channel(1);
    
    CSIFallGuard_begin(MODE_RX, 1);
    
    while (1) {
        CSIFallGuard_update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}