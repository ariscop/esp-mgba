#include "esp-mgba.h"

#include "driver/spi_common.h"
#include "driver/spi_common_internal.h"
#include "driver/gpio.h"
#include "hal/spi_ll.h"

static spi_dev_t *hw = SPI_LL_GET_HW(SPI_PORT);

intr_handle_t spi_intr_handle;

#define clear_cs()  WRITE_PERI_REG(GPIO_OUT_W1TS_REG, (1<<GPIO_OE));
#define set_cs()    WRITE_PERI_REG(GPIO_OUT_W1TC_REG, (1<<GPIO_OE));


static void IRAM_ATTR spi_intr(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    clear_cs();

    hw->data_buf[0] = mobile_transfer((struct mobile_adapter*)arg, (uint8_t)hw->data_buf[0]);

    if(mobile_action_get(&adapter) != MOBILE_ACTION_NONE)
        xTaskNotifyFromISR(main_handle, 0, eNoAction, &xHigherPriorityTaskWoken);

    spi_ll_slave_reset(hw);
    spi_ll_clear_int_stat(hw);
    spi_ll_slave_user_start(hw);

    set_cs();

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


void init_spi(void)
{
    static const spi_bus_config_t buscfg={
        .mosi_io_num=GPIO_MOSI,
        .miso_io_num=GPIO_MISO,
        .sclk_io_num=GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1,
        .flags = SPICOMMON_BUSFLAG_SLAVE|SPICOMMON_BUSFLAG_GPIO_PINS,
        .intr_flags = 0 //ESP_INTR_FLAG_IRAM
    };

    /* Used to toggle the CS line, might not be needed?
     * useful with a logic analyzer when debugging */
    static const gpio_config_t io_conf_oe={
        .intr_type=GPIO_INTR_DISABLE,
        .mode=GPIO_MODE_OUTPUT,
        .pin_bit_mask=(1<<GPIO_OE),
    };
    gpio_config(&io_conf_oe);

    clear_cs();

    gpio_set_pull_mode(GPIO_MISO, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(GPIO_MOSI, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(GPIO_SCLK, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(GPIO_CS, GPIO_FLOATING);
    gpio_set_pull_mode(GPIO_OE, GPIO_FLOATING);

    spi_bus_initialize(SPI_PORT, &buscfg, SPI_DMA_DISABLED);
    spicommon_cs_initialize(SPI_PORT, GPIO_CS, 0 /*cs_num*/, true /*force_gpio_matrix*/);

    spi_ll_slave_init(hw);
    spi_ll_disable_int(hw);
    spi_ll_clear_int_stat(hw);
    spi_ll_set_rx_lsbfirst(hw, false);
    spi_ll_set_tx_lsbfirst(hw, false);
    spi_ll_slave_set_mode(hw, 3, false);

    /* ESP-IDF modifications needed
     * SPI Slave mode 3 parameters (in spi_ll.h)
     * hw->pin.ck_idle_edge = 1;
     * hw->user.ck_i_edge = 1;
     * hw->ctrl2.miso_delay_mode = 1;
     * hw->ctrl2.miso_delay_num = 0;
     * hw->ctrl2.mosi_delay_mode = 0;
     * hw->ctrl2.mosi_delay_num = 0;
     *
     * see: https://github.com/espressif/esp-idf/issues/7698
     */

    esp_intr_alloc(spicommon_irqsource_for_host(SPI_PORT), buscfg.intr_flags, spi_intr, &adapter, &spi_intr_handle);
}

void IRAM_ATTR mobile_board_serial_disable(void *user)
{
    spi_ll_disable_int(hw);
    clear_cs();
    spi_ll_slave_reset(hw);
    spi_ll_clear_int_stat(hw);
}

void IRAM_ATTR mobile_board_serial_enable(void *user) { //, bool mode_32bit) {
    //spi_ll_slave_set_rx_bitlen(hw, mode_32bit ? 32 : 8);
    spi_ll_slave_set_rx_bitlen(hw, 8);
    //spi_ll_slave_set_tx_bitlen(hw, mode_32bit ? 32 : 8);
    spi_ll_slave_set_tx_bitlen(hw, 8);
    spi_ll_enable_mosi(hw, true);
    spi_ll_enable_miso(hw, true);
    spi_ll_enable_int(hw);
    spi_ll_slave_user_start(hw);
    set_cs();
}

