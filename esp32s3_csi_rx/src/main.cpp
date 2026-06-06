#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <CSIFallGuard.h>

uint8_t tx_mac[] = {0x80, 0xB5, 0x4E, 0xDE, 0xEE, 0x70};
uint8_t rx_mac[] = {0x80, 0xB5, 0x4E, 0xDE, 0xEC, 0x48};

extern "C" void app_main() {
    // Cấu hình cơ bản
    CSIFallGuard_set_tx_mac(tx_mac);
    CSIFallGuard_set_rx_mac(rx_mac);
    CSIFallGuard_set_channel(1);
    
    // Cấu hình chế độ in (đã có macro)
    CSIFallGuard_set_print_mode(PRINT_MODE_RSSI | PRINT_MODE_STATE);
    
    // Cấu hình phát hiện nằm
    CSIFallGuard_set_lying_params(12, 3000, 2000);
    
    // Khởi tạo RX
    CSIFallGuard_begin(MODE_RX, 1);
    
    while (1) {
        CSIFallGuard_update();
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Kiểm tra trạng thái nằm
        if (CSIFallGuard_is_lying()) {
            // Xử lý khi phát hiện nằm
        }
    }
}