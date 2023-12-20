#ifndef _KEY_H_
#define _KEY_H_

#include <nrf_gpio.h>

#define Key1 NRF_GPIO_PIN_MAP(0,11)

void Key_Event(uint32_t Key_number);
void Key_(uint32_t Key_number);

#endif