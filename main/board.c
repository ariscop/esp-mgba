#include "esp-mgba.h"

_Atomic int64_t mobile_clock_latch[MOBILE_MAX_TIMERS];

IRAM_ATTR void mobile_board_time_latch(void *user, enum mobile_timers timer)
{
    mobile_clock_latch[timer] = esp_timer_get_time();
}

IRAM_ATTR bool mobile_board_time_check_ms(void *user, enum mobile_timers timer, unsigned ms)
{
    return (esp_timer_get_time() - mobile_clock_latch[timer]) > (ms * 1000);
};

IRAM_ATTR void mobile_board_debug_log(void *user, const char *line)
{
    ESP_LOGI("mgba", "%s", line);
}
