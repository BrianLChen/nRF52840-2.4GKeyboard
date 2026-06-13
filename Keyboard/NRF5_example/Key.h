#ifndef _KEY_H_
#define _KEY_H_


#include <nrf.h>
#include <nrf_gpio.h>

#define Key1 NRF_GPIO_PIN_MAP(0,11)

uint32_t Read_Pin_Value(uint32_t key_number);
bool Is_Key_Pressed(uint32_t key_number);

#endif