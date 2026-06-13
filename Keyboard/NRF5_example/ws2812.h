#ifndef WS2812_H
#define WS2812_H

#include "nrfx_spim.h"
//#include "ws2812_effect.h"

typedef struct 
{
    nrfx_spim_t spi_instance;
    uint32_t SPI_MOSI_PIN;
    uint32_t SPI_SCK_PIN;
} ws2812_t;

static uint8_t code0 = 0x40;
static uint8_t code1 = 0x70;

nrfx_err_t ws2812_init(ws2812_t ws2812_instance);

void ws2812_rst();

nrfx_err_t ws2812_set();

nrfx_err_t ws2812_off(ws2812_t ws2812_instance);


nrfx_err_t ws2812_effect(ws2812_t ws2812_instance);

void ws2812_uninit(ws2812_t ws2812_instance);


#endif // WS2812_H