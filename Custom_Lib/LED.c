#include "led.h"

void LED_On(uint32_t led_number)
{
 nrf_gpio_cfg_output(led_number);
 nrf_gpio_pin_clear(led_number);
}
void LED_Off(uint32_t led_number)
{
 nrf_gpio_cfg_output(led_number);
 nrf_gpio_pin_set(led_number);
}

void LED_Write(uint32_t led_number, uint32_t state)
{
  nrf_gpio_pin_write(led_number, state);
}

void LED_invert(uint32_t led_number)
{
    nrf_gpio_cfg_output(led_number);
    uint32_t state = nrf_gpio_pin_out_read(led_number);
    if (state == 0)
    {
        nrf_gpio_pin_set(led_number);
    }
    else
    {
        nrf_gpio_pin_clear(led_number);
    }
}