#include <stdio.h>
#include <string.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_event.h>
#include "mobile.h"

#define SPI_PORT VSPI_HOST
#define GPIO_MOSI 23
#define GPIO_MISO 22
#define GPIO_SCLK 21
#define GPIO_CS 5
#define GPIO_OE 4

extern TaskHandle_t main_handle;
extern struct mobile_adapter adapter;

void init_config(void);
void init_wifi(void);
void init_spi(void);
