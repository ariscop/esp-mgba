#include "esp-mgba.h"

struct mobile_adapter adapter;
TaskHandle_t main_handle;

void IRAM_ATTR main_loop(void) {
    for(;;) {
        mobile_loop(&adapter);
        xTaskNotifyWait(0, 0, NULL, pdMS_TO_TICKS(250));
    };
}

void app_main(void)
{
    main_handle = xTaskGetCurrentTaskHandle();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    init_config();
    init_wifi();
    init_spi();

    mobile_init(&adapter, NULL, NULL);
    main_loop();
}
