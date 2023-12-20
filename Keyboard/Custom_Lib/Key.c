#include "Key.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"

uint32_t Read_Pin_Value(uint32_t key_number)
{
    uint32_t i;
    i = nrf_gpio_pin_read(Key1);
    nrf_delay_ms(1);
    if (i == nrf_gpio_pin_read(Key1) && i == 0)
    {
        return i;
    }
    else
    {
        return ~i;
    }
}

bool Is_Key_Pressed(uint32_t key_number)
{
    uint32_t i;
    i = nrf_gpio_pin_read(Key1);
    nrf_delay_ms(1);
    if (i == nrf_gpio_pin_read(Key1) && i == 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}