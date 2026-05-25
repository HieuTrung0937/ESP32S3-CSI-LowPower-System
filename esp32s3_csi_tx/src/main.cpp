#include "EspNowTransmitter.hpp"

extern "C" void app_main() {
    static EspNowTransmitter tx;
    tx.begin();
    
}