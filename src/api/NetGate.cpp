#include "NetGate.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t s_gate = nullptr;

namespace NetGate {

void begin() {
    if (!s_gate) s_gate = xSemaphoreCreateMutex();
}

Lock::Lock() {
    if (s_gate) xSemaphoreTake(s_gate, portMAX_DELAY);
}

Lock::~Lock() {
    if (s_gate) xSemaphoreGive(s_gate);
}

} // namespace NetGate
