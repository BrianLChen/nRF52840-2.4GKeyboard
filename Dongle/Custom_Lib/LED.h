#ifndef __LED_H__
#define __LED_H__

#include <nrf.h>
#include <nrf_gpio.h>

#define LED0 NRF_GPIO_PIN_MAP(0,13)
#define LED1 NRF_GPIO_PIN_MAP(0,14)
#define LED2 NRF_GPIO_PIN_MAP(1,9)
#define LED3 NRF_GPIO_PIN_MAP(0,16)

#define Key1 NRF_GPIO_PIN_MAP(0,11)

void LED_On(uint32_t led_number);
void LED_Off(uint32_t led_number);
void LED_Write(uint32_t led_number, uint32_t state);
void LED_invert(uint32_t led_number);

#endif
