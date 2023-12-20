#include "ws2812.h"
#include "nrf_delay.h"
#include "ws2812_effect.h"

// #include "nrfx_spim.h"

static nrfx_spim_xfer_desc_t spi_desc = {WS2812_BUFF, WS2812_BUFF_SIZE, unused_buffer, 3};

/* Initiate the SPI use for ws2812 control */
nrfx_err_t ws2812_init(ws2812_t ws2812_instance)
{
    nrfx_spim_config_t spi_config = NRFX_SPIM_DEFAULT_CONFIG;
    spi_config.mosi_pin = ws2812_instance.SPI_MOSI_PIN; /* Only Read */
    spi_config.sck_pin = ws2812_instance.SPI_SCK_PIN;
    spi_config.frequency = NRF_SPIM_FREQ_4M;
    spi_config.mode = NRF_SPIM_MODE_0;
    nrfx_err_t err_code = nrfx_spim_init(&ws2812_instance.spi_instance, &spi_config, NULL, NULL);
    return err_code;
}

void ws2812_rst()
{
    nrf_delay_us(60);
}

nrfx_err_t ws2812_set(ws2812_t ws2812_instance, colour *array)
{
    uint8_t i = 0;
    uint8_t led;
    uint8_t offset;
    uint8_t index;
    for (led = 0; led < RGB_NUMBER; led++)
    {
        // for (i = 0; i < 24; i++)
        //{
        //     uint8_t offset = 0x01 << (7 - (i % 8));
        //     uint8_t index = i / 8;
        //     if (array[index] & offset)
        //     // if(1)
        //     {
        //         WS2812_BUFF[i + 24 * led] = code1;
        //     }
        //     else
        //     {
        //         WS2812_BUFF[i + 24 * led] = code0;
        //     }
        // }
        /* set red colour */
        for (i = 0; i < 8; i++)
        {
            offset = 0x01 << (7 - (i % 8));
            index = i / 8;
            if (array[i].red & offset)
            {
                WS2812_BUFF[i + 24 * led] = code1;
            }
            else
            {
                WS2812_BUFF[i + 24 * led] = code0;
            }
        }
        /* set red colour */
        for (i = 0; i < 8; i++)
        {
            offset = 0x01 << (7 - (i % 8));
            index = i / 8;
            if (array[i].red & offset)
            {
                WS2812_BUFF[i + 8 + 24 * led] = code1;
            }
            else
            {
                WS2812_BUFF[i + 8 + 24 * led] = code0;
            }
        }
        /* set red colour */
        for (i = 0; i < 8; i++)
        {
            offset = 0x01 << (7 - (i % 8));
            index = i / 8;
            if (array[i].red & offset)
            {
                WS2812_BUFF[i + 16 + 24 * led] = code1;
            }
            else
            {
                WS2812_BUFF[i + 16 + 24 * led] = code0;
            }
        }
    }
    return nrfx_spim_xfer(&ws2812_instance.spi_instance, &spi_desc, NULL);
}

nrfx_err_t ws2812_off(ws2812_t ws2812_instance)
{
    uint8_t i;
    for (i = 0; i < RGB_NUMBER * 24; i++)
    {
        WS2812_BUFF[i] = code0;
    }
    return nrfx_spim_xfer(&ws2812_instance.spi_instance, &spi_desc, NULL);
}

nrfx_err_t ws2812_effect(ws2812_t ws2812_instance)
{
    // ws2812_set(ws2812_instance, led_code1);
    // ws2812_rst();
    // nrf_delay_ms(500);
    // ws2812_set(ws2812_instance, led_code2);
    // ws2812_rst();
    // nrf_delay_ms(500);
    // ws2812_set(ws2812_instance, led_code3);
    // ws2812_rst();
    // nrf_delay_ms(500);
    // ws2812_set(ws2812_instance, led_code4);
    // ws2812_rst();
    // nrf_delay_ms(500);
    for (int i = 0; i < TOTAL_FRAME; i++)
    {
        ws2812_set(ws2812_instance, breath[i]);
        nrf_delay_ms(50);
    }
}

void ws2812_uninit(ws2812_t ws2812_instance)
{
nrfx_spim_uninit(&ws2812_instance.spi_instance);
}