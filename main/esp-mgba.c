#include "esp-mgba.h"

#include <esp_task.h>

#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "mobile.h"
#include "errno.h"

#define SPI_PORT VSPI_HOST
#define GPIO_MOSI 23
#define GPIO_MISO 22
#define GPIO_SCLK 21
#define GPIO_CS 5
#define GPIO_OE 4

static void init_spi()
{
    esp_err_t ret;

    static const spi_bus_config_t buscfg={
        .mosi_io_num=GPIO_MOSI,
        .miso_io_num=GPIO_MISO,
        .sclk_io_num=GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1,
        .flags = SPICOMMON_BUSFLAG_GPIO_PINS
    };

    /* Configuration for the SPI slave interface */
    static const spi_slave_interface_config_t slvcfg={
        .mode=3,
        .spics_io_num=GPIO_CS,
        .queue_size=1,
    };

    /* SPI Slave mode 3 parameters (in spi_ll.h)
     * hw->pin.ck_idle_edge = 1;
     * hw->user.ck_i_edge = 1;
     * hw->ctrl2.miso_delay_mode = 1;
     * hw->ctrl2.miso_delay_num = 0;
     * hw->ctrl2.mosi_delay_mode = 0;
     * hw->ctrl2.mosi_delay_num = 0;
     *
     * see: https://github.com/espressif/esp-idf/issues/7698
     */

    /* Used to toggle the CS line, might not be needed? */
    static const gpio_config_t io_conf_oe={
        .intr_type=GPIO_INTR_DISABLE,
        .mode=GPIO_MODE_OUTPUT,
        .pin_bit_mask=(1<<GPIO_OE),
    };
    gpio_config(&io_conf_oe);

    WRITE_PERI_REG(GPIO_OUT_W1TS_REG, (1<<GPIO_OE));

    gpio_set_pull_mode(GPIO_MISO, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(GPIO_MOSI, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(GPIO_SCLK, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(GPIO_CS, GPIO_FLOATING);
    gpio_set_pull_mode(GPIO_OE, GPIO_FLOATING);

    /* Initialize SPI slave interface */
    ret=spi_slave_initialize(SPI_PORT, &buscfg, &slvcfg, SPI_DMA_DISABLED);
    assert(ret==ESP_OK);
}


struct mobile_adapter adapter;
TaskHandle_t main_handle;

IRAM_ATTR void spi_task(void *parameter)
{
    esp_err_t ret;
    spi_slave_transaction_t t;
    spi_slave_transaction_t *ret_trans;
    WORD_ALIGNED_ATTR volatile uint32_t send;
    WORD_ALIGNED_ATTR volatile uint32_t recv;

    /* Red adapters send 0x4b first so might as well do that */
    send = 0x4b;

    while(1) {
        memset(&t, 0, sizeof(t));
        t.length = 8;
        t.tx_buffer = &send;
        t.rx_buffer = &recv;

        ret = spi_slave_queue_trans(SPI_PORT, &t, portMAX_DELAY);
        assert(ret==ESP_OK);

        /* For some reasons the value is never copied to W0
         * so we do it ourselves */
        WRITE_PERI_REG(SPI_W0_REG(3), send);

        WRITE_PERI_REG(GPIO_OUT_W1TC_REG, (1<<GPIO_OE));
        ret = spi_slave_get_trans_result(SPI_PORT, &ret_trans, portMAX_DELAY);
        assert(ret==ESP_OK);
        WRITE_PERI_REG(GPIO_OUT_W1TS_REG, (1<<GPIO_OE));

        send = (uint32_t)mobile_transfer(&adapter, recv);

        if(mobile_action_get(&adapter) != MOBILE_ACTION_NONE)
            xTaskNotifyGive(main_handle);

    }
}

void mobile_board_serial_disable(void *user) {
    //WRITE_PERI_REG(GPIO_OUT_W1TS_REG, (1<<GPIO_OE));
}

void mobile_board_serial_enable(void *user) {
    //WRITE_PERI_REG(GPIO_OUT_W1TC_REG, (1<<GPIO_OE));
}

IRAM_ATTR void mobile_board_debug_log(void *user, const char *line)
{
    ESP_LOGI("mgba", "%s", line);
}

void app_main(void)
{
    main_handle = xTaskGetCurrentTaskHandle();
    TaskHandle_t gbma_spi_task;

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    init_config();
    init_wifi();

    mobile_init(&adapter, NULL, NULL);
    init_spi();

    xTaskCreate(spi_task, "gbma_spi", 2048, NULL, 19, &gbma_spi_task);
    for(;;) {
        mobile_loop(&adapter);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
    };
}
