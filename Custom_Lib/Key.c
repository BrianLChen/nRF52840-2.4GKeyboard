#include "Key.h"

void Key_Event(uint32_t Key_number)
{
}

void Key_Noting(uint32_t Key_number)
{
}

bool Is_Key_Pressed(uint32_t key_number)
{
    nrf_gpio_cfg_input(key_number, NRF_GPIO_PIN_PULLUP);

    if (nrf_gpio_pin_read(key_number) == 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}