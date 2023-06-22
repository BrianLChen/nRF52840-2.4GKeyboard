/**
 * Copyright (c) 2017 - 2021, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_drv_clock.h"
#include "nrf_drv_power.h"
#include "nrf_drv_usbd.h"
#include "nrf_gpio.h"

#include "nrf_gzll.h"
#include "nrf_gzll_error.h"

#include "app_error.h"
#include "app_timer.h"
#include "app_usbd.h"
#include "app_usbd_core.h"
#include "custom_usbd_hid_kbd.h"
// #include "Custom_Lib/app_usbd_hid_generic.h"
#include "bsp.h"
//#include "bsp_cli.h"
//#include "nrf_cli.h"
//#include "nrf_cli_uart.h"
#include "nrfx_timer.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

// #include "Key.h"
#include "LED.h"

/* Gazell Config */
#define PIPE_NUMBER 1        /**< Pipe 0 is used in this example. */
#define TX_PAYLOAD_LENGTH 16 /**< 1-byte payload length is used when transmitting. */
#define RX_Receive_Length
#define MAX_TX_ATTEMPTS 20 /**< Maximum number of transmission attempts */

#define Mode_Pin NRF_GPIO_PIN_MAP(0, 26)

static uint8_t m_data_payload[TX_PAYLOAD_LENGTH];                /**< Payload to send to Host. */
static uint8_t m_ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH]; /**< Placeholder for received ACK payloads from Host. */

const nrfx_timer_t TIMER_KBD = NRFX_TIMER_INSTANCE(1);

uint8_t KEYBOARD_Rep_Buff[17];
uint8_t CONSUMER_Rep_Buff[2];

/**
 * @brief Enable USB power detection
 */
#ifndef USBD_POWER_DETECTION
#define USBD_POWER_DETECTION true
#endif

///**
// * @brief Enable HID keyboard class
// */
// #define CONFIG_HAS_KBD      1

/**
 * @brief Letter to be sent on LETTER button
 *
 * @sa BTN_KBD
 */
#define CONFIG_KBD_LETTER CUSTOM_USBD_HID_KBD_A

#define LED_CAPSLOCK (BSP_BOARD_LED_0)  /**< CAPSLOCK LED0 */
#define LED_NUMLOCK (BSP_BOARD_LED_1)   /**< NUMLOCK LED1 */
#define LED_HID_REP (BSP_BOARD_LED_2)   /**< Changes its state if any HID report was received or transmitted LED 2*/
#define LED_USB_START (BSP_BOARD_LED_3) /**< The USBD library has been started and the bus is not in SUSPEND state LED3 */

// #define BTN_KBD_SHIFT 1
#define BTN_KBD 0
/**
 * @brief Additional key release events
 *
 * This example needs to process release events of used buttons
 */
enum
{
    BSP_USER_EVENT_RELEASE_0 = BSP_EVENT_KEY_LAST + 1, /**< Button 0 released */
    BSP_USER_EVENT_RELEASE_1,                          /**< Button 1 released */
    BSP_USER_EVENT_RELEASE_2,                          /**< Button 2 released */
    BSP_USER_EVENT_RELEASE_3,                          /**< Button 3 released */
    BSP_USER_EVENT_RELEASE_4,                          /**< Button 4 released */
    BSP_USER_EVENT_RELEASE_5,                          /**< Button 5 released */
    BSP_USER_EVENT_RELEASE_6,                          /**< Button 6 released */
    BSP_USER_EVENT_RELEASE_7,                          /**< Button 7 released */
};

/**
 * @brief USB composite interfaces
 */
#define APP_USBD_INTERFACE_KBD 0
#define CUSTOM_USBD_INTERFACE_CONSUMER 1
/**
 * @brief User event handler, HID keyboard
 */
static void hid_kbd_user_ev_handler(app_usbd_class_inst_t const *p_inst,
    app_usbd_hid_user_event_t event);

/*lint -save -e26 -e64 -e123 -e505 -e651*/

/**
 * @brief Global HID keyboard instance
 */
APP_USBD_HID_KBD_GLOBAL_DEF(m_app_hid_kbd,
    APP_USBD_INTERFACE_KBD,
    NRF_DRV_USBD_EPIN1,
    hid_kbd_user_ev_handler,
    APP_USBD_HID_SUBCLASS_BOOT);

// APP_USBD_HID_KBD_GLOBAL_DEF(consumer_hid,
//     CUSTOM_USBD_INTERFACE_CONSUMER,
//     NRF_DRV_USBD_EPIN1,
//     hid_kbd_user_ev_handler,
//     APP_USBD_HID_SUBCLASS_BOOT);

/*lint -restore*/

static void kbd_status(void)
{
    if (app_usbd_hid_kbd_led_state_get(&m_app_hid_kbd, APP_USBD_HID_KBD_LED_NUM_LOCK))
    {
        // bsp_board_led_on(LED_NUMLOCK);
        LED_On(LED1);
    }
    else
    {
        // bsp_board_led_off(LED_NUMLOCK);
        LED_Off(LED1);
    }

    if (app_usbd_hid_kbd_led_state_get(&m_app_hid_kbd, APP_USBD_HID_KBD_LED_CAPS_LOCK))
    {
        // bsp_board_led_on(LED_CAPSLOCK);
        LED_On(LED0);
    }
    else
    {
        // bsp_board_led_off(LED_CAPSLOCK);
        LED_Off(LED0);
    }
}

/**
 * @brief Class specific event handler.
 *
 * @param p_inst    Class instance.
 * @param event     Class specific event.
 * */
static void hid_kbd_user_ev_handler(app_usbd_class_inst_t const *p_inst,
    app_usbd_hid_user_event_t event)
{
    UNUSED_PARAMETER(p_inst);
    switch (event)
    {
    case APP_USBD_HID_USER_EVT_OUT_REPORT_READY:
        /* Only one output report IS defined for HID keyboard class. Update LEDs state. */
        // LED_invert(LED2);
        kbd_status();
        break;
    case APP_USBD_HID_USER_EVT_IN_REPORT_DONE:
        // LED_invert(LED2);
        break;
    case APP_USBD_HID_USER_EVT_SET_BOOT_PROTO:
        UNUSED_RETURN_VALUE(hid_kbd_clear_buffer(p_inst));
        break;
    default:
        break;
    }
}

/**
 * @brief USBD library specific event handler.
 *
 * @param event     USBD library event.
 * */
static void usbd_user_ev_handler(app_usbd_event_type_t event)
{
    switch (event)
    {
    case APP_USBD_EVT_DRV_SOF:
        break;
    case APP_USBD_EVT_DRV_SUSPEND:
        app_usbd_suspend_req(); // Allow the library to put the peripheral into sleep mode
        // bsp_board_leds_off();
        LED_Off(LED0);
        LED_Off(LED1);
        LED_Off(LED2);
        LED_Off(LED3);
        break;
    case APP_USBD_EVT_DRV_RESUME:
        // bsp_board_led_on(LED_USB_START);
        LED_On(LED3);
        kbd_status(); /* Restore LED state - during SUSPEND all LEDS are turned off */
        break;
    case APP_USBD_EVT_STARTED:
        // bsp_board_led_on(LED_USB_START);
        LED_On(LED3);
        break;
    case APP_USBD_EVT_STOPPED:
        app_usbd_disable();
        // bsp_board_leds_off();
        LED_Off(LED0);
        LED_Off(LED1);
        LED_Off(LED2);
        LED_Off(LED3);
        break;
    case APP_USBD_EVT_POWER_DETECTED:
        NRF_LOG_INFO("USB power detected");

        if (!nrf_drv_usbd_is_enabled())
        {
            app_usbd_enable();
        }
        break;
    case APP_USBD_EVT_POWER_REMOVED:
        NRF_LOG_INFO("USB power removed");
        app_usbd_stop();
        break;
    case APP_USBD_EVT_POWER_READY:
        NRF_LOG_INFO("USB ready");
        app_usbd_start();
        break;
    default:
        break;
    }
}

// A function to appley debounce filter
uint32_t debouncefilter(uint8_t delay)
{
    uint32_t i;
    i = nrf_gpio_pin_read(Key1);
    nrf_delay_us(delay);
    if (i == nrf_gpio_pin_read(Key1))
    {
        return i;
    }
    else
    {
        return ~i;
    }
}

// Function to scan the key state
static void scan_key(nrf_timer_event_t event_type, void *p_context)
{
    uint32_t a = debouncefilter(100);

    if (a != 0)
    {
        memset(KEYBOARD_Rep_Buff, 0, 17);
        KEYBOARD_Rep_Buff[0] = 0x01;
    }
    else
    {
        KEYBOARD_Rep_Buff[0] = 0x01;
        KEYBOARD_Rep_Buff[2] = 0x10;
    }
    KBD_Send(&m_app_hid_kbd, KEYBOARD_Rep_Buff);
    app_usbd_event_queue_process();

    // kbd_status();
}

/**
 * @brief Auxiliary internal macro
 *
 * Macro used only in @ref init_bsp to simplify the configuration
 */
//#define INIT_BSP_ASSIGN_RELEASE_ACTION(btn) \
//    APP_ERROR_CHECK(                        \
//        bsp_event_to_button_action_assign(  \
//            btn,                            \
//            BSP_BUTTON_ACTION_RELEASE,      \
//            (bsp_event_t)CONCAT_2(BSP_USER_EVENT_RELEASE_, btn)))

// static void init_bsp(void)
//{
//     ret_code_t ret;
//     ret = bsp_init(BSP_INIT_BUTTONS, bsp_event_callback);
//     APP_ERROR_CHECK(ret);

//    INIT_BSP_ASSIGN_RELEASE_ACTION(BTN_KBD);

//    /* Configure LEDs */
//    bsp_board_init(BSP_INIT_LEDS);
//}

// Function to init Wire Mode Keyboard
void Wire_Mode()
{
    ret_code_t ret;
    static const app_usbd_config_t usbd_config = {
        .ev_state_proc = usbd_user_ev_handler,
    };

    // ret = NRF_LOG_INIT(NULL);
    // APP_ERROR_CHECK(ret);

    // 禁止后蜂鸣器会一直响，且LED状态不正常
    ret = nrf_drv_clock_init();
    APP_ERROR_CHECK(ret);

    // 禁止后无法使用按键 KEY1
    // nrf_drv_clock_lfclk_request(NULL);
    // while (!nrf_drv_clock_lfclk_is_running())
    //{
    //    /* Just waiting */
    //}

    // ret = app_timer_init();
    // APP_ERROR_CHECK(ret);

    // init_bsp();

    ret = app_usbd_init(&usbd_config);
    APP_ERROR_CHECK(ret);

    app_usbd_class_inst_t const *class_inst_kbd;

    class_inst_kbd = app_usbd_hid_kbd_class_inst_get(&m_app_hid_kbd);
    ret = app_usbd_class_append(class_inst_kbd);

    APP_ERROR_CHECK(ret);

    // NRF_LOG_INFO("USBD HID composite example started.");

    nrf_gpio_cfg_input(Key1, NRF_GPIO_PIN_PULLUP);
    uint32_t time_ms = 1; // Time(in miliseconds) between consecutive compare events.
    uint32_t time_ticks;
    uint32_t err_code = NRF_SUCCESS;
    nrfx_timer_config_t timer_cfg = NRFX_TIMER_DEFAULT_CONFIG;
    err_code = nrfx_timer_init(&TIMER_KBD, &timer_cfg, scan_key);
    APP_ERROR_CHECK(err_code);

    time_ticks = nrfx_timer_ms_to_ticks(&TIMER_KBD, time_ms);

    // Set compare, when counter value = time_ticks -> interrupt
    nrfx_timer_extended_compare(&TIMER_KBD, NRF_TIMER_CC_CHANNEL0, time_ticks, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);
    // nrfx_timer_compare_int_enable(&TIMER_KBD, NRF_TIMER_INT_COMPARE0);
    nrfx_timer_compare(&TIMER_KBD, NRF_TIMER_CC_CHANNEL0, time_ticks, true);

    if (USBD_POWER_DETECTION)
    {
        ret = app_usbd_power_events_enable();
        APP_ERROR_CHECK(ret);
    }
    else
    {
        // NRF_LOG_INFO("No USB power detection enabled\r\nStarting USB now");

        app_usbd_enable();
        app_usbd_start();
    }

    for (uint32_t i = 0; i < 200000; i++)
    {
        app_usbd_event_queue_process();
    }

    nrfx_timer_enable(&TIMER_KBD);

    nrf_gpio_cfg_output(TX_PIN_NUMBER);
    nrf_gpio_cfg_output(RX_PIN_NUMBER);
    while (true)
    {
        LED_On(TX_PIN_NUMBER);
        nrf_delay_ms(1000);
        LED_Off(TX_PIN_NUMBER);
        nrf_delay_ms(1000);
        // while (app_usbd_event_queue_process())
        //{
        //     /* Nothing to do */
        // }
    }
}


/**
 * @brief Function to control the LED outputs.
 *
 * @param[in] val Desirable state of the LEDs.
 */
static void output_present(uint8_t val)
{
    if ((val & 0x02) != 0)
    {
        LED_On(LED0);
    }
    else
    {
        LED_Off(LED0);
    }

    if ((val & 0x01) != 0)
    {
        LED_On(LED1);
    }
    else
    {
        LED_Off(LED1);
    }

    if ((val & 0x04) != 0)
    {
        LED_On(LED2);
    }
    else
    {
        LED_Off(LED2);
    }
}

/**
 * @brief Function to read the button states.
 *
 * @return Returns states of buttons.
 */
void input_get(uint8_t *array)
{
    uint32_t a = debouncefilter(100);
    memset(array, 0, 17);

    if (a != 0)
    {
        // memset(array, 0, 17);
        array[0] = 0x01;
    }
    else
    {
        array[0] = 0x01;
        array[2] = 0x10;
    }
}

/**
 * @brief Initialize the BSP modules.
 */
static void ui_init(void)
{
    uint32_t err_code;
    nrf_gpio_cfg_input(Key1, NRF_GPIO_PIN_PULLUP);
    // Initialize application timer.
    // err_code = app_timer_init();
    // APP_ERROR_CHECK(err_code);

    //// BSP initialization.
    // err_code = bsp_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS, NULL);
    // APP_ERROR_CHECK(err_code);

    // Set up logger
    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();

    // NRF_LOG_INFO("Gazell ACK payload example. Device mode.");
    NRF_LOG_FLUSH();

    // bsp_board_init(BSP_INIT_LEDS);
}

/*****************************************************************************/
/** @name Gazell callback function definitions  */
/*****************************************************************************/
/**
 * @brief TX success callback.
 *
 * @details If an ACK was received, another packet is sent.
 */
void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info)
{
    bool result_value = false;
    uint32_t m_ack_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;

    if (tx_info.payload_received_in_ack)
    {
        // Pop packet and write first byte of the payload to the GPIO port.
        result_value =
            nrf_gzll_fetch_packet_from_rx_fifo(pipe, m_ack_payload, &m_ack_payload_length);
        if (!result_value)
        {
            NRF_LOG_ERROR("RX fifo error ");
        }

        if (m_ack_payload_length > 0)
        {
            output_present(m_ack_payload[0]);
        }
    }

    // Load data payload into the TX queue.
    input_get(m_data_payload);

    result_value = nrf_gzll_add_packet_to_tx_fifo(pipe, m_data_payload, TX_PAYLOAD_LENGTH);
    if (!result_value)
    {
        NRF_LOG_ERROR("TX fifo error ");
    }

#if GZLL_PA_LNA_CONTROL
    m_rssi_sum += tx_info.rssi;
    m_packets_cnt++;
#endif
}

/**
 * @brief TX failed callback.
 *
 * @details If the transmission failed, send a new packet.
 *
 * @warning This callback does not occur by default since NRF_GZLL_DEFAULT_MAX_TX_ATTEMPTS
 * is 0 (inifinite retransmits).
 */
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info)
{
    NRF_LOG_ERROR("Gazell transmission failed");

    // Load data into TX queue.
    input_get(m_data_payload);

    bool result_value = nrf_gzll_add_packet_to_tx_fifo(pipe, m_data_payload, TX_PAYLOAD_LENGTH);
    if (!result_value)
    {
        NRF_LOG_ERROR("TX fifo error ");
    }
}

/**
 * @brief Gazelle callback.
 * @warning Required for successful Gazell initialization.
 */
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info)
{
}

/**
 * @brief Gazelle callback.
 * @warning Required for successful Gazell initialization.
 */
void nrf_gzll_disabled()
{
}

// Function to init 2.4G Wireless Mode Keyboard
void Wireless_Mode()
{
    // Set up the user interface (buttons and LEDs).
    ui_init();

    // Initialize Gazell.
    bool result_value = nrf_gzll_init(NRF_GZLL_MODE_DEVICE);
    GAZELLE_ERROR_CODE_CHECK(result_value);

    // set base address for pipe1
    result_value = nrf_gzll_set_base_address_1(0xbc010827);
    GAZELLE_ERROR_CODE_CHECK(result_value);
    result_value = nrf_gzll_set_address_prefix_byte(PIPE_NUMBER, 0x27);
    GAZELLE_ERROR_CODE_CHECK(result_value);

    // Attempt sending every packet up to MAX_TX_ATTEMPTS times.
    nrf_gzll_set_max_tx_attempts(MAX_TX_ATTEMPTS);

    input_get(m_data_payload);

    result_value = nrf_gzll_add_packet_to_tx_fifo(PIPE_NUMBER, m_data_payload, TX_PAYLOAD_LENGTH);

    // Enable Gazell to start sending over the air.
    result_value = nrf_gzll_enable();
    GAZELLE_ERROR_CODE_CHECK(result_value);

    // NRF_LOG_INFO("Gzll ack payload device example started.");

    while (true)
    {
        // NRF_LOG_FLUSH();
        __WFE();
    }
}

int main(void)
{
    //if(1)
    //Wire_Mode();
    //else
    //Wireless_Mode();

        nrf_gpio_cfg_input(Mode_Pin, NRF_GPIO_PIN_PULLUP);
    uint32_t mode = nrf_gpio_pin_read(Mode_Pin);

     if(mode != 0)
    Wire_Mode();
     else
     Wireless_Mode();
}