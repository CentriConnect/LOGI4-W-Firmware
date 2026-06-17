#include "ResetCounter.h"

#include "esp_attr.h"

static RTC_DATA_ATTR uint16_t s_resetCounter = 0;
static bool s_initializedThisBoot = false;

void LogiResetCounter_Init()
{
    if (!s_initializedThisBoot) {
        s_resetCounter++;
        s_initializedThisBoot = true;
    }
}

uint16_t LogiResetCounter_Get()
{
    return s_resetCounter;
}
