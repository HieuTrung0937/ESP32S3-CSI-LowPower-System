#include "CsiReceiver.hpp"

extern "C" void app_main() {
    static CsiReceiver rx;
    rx.begin();
}