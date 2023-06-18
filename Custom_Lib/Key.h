#ifndef _KEY_H_
#define _KEY_H_


#include <nrf.h>
#include <nrf_gpio.h>

void Key_Event(uint32_t Key_number);
void Key_(uint32_t Key_number);
bool Is_Key_Pressed(uint32_t key_number);

#endif