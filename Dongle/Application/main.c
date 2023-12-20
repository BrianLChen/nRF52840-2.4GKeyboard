/**
 * Copyright (c) 2012 - 2021, Nordic Semiconductor ASA
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
/**
 * This project requires that a device that runs the
 * @ref gzll_device_m_ack_payload_example is used as a counterpart for
 * receiving the data. This can be on either an nRF5x device or an nRF24Lxx device
 * running the \b gzll_device_m_ack_payload example in the nRFgo SDK.
 *
 * This example listens for a packet and sends an ACK
 * when a packet is received. The contents of the first payload byte of
 * the received packet is output on the GPIO Port BUTTONS.
 * The contents of GPIO Port LEDS are sent in the first payload byte (byte 0)
 * of the ACK packet.
 */

#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_drv_clock.h"
#include "nrf_drv_power.h"
#include "nrf_drv_usbd.h"
#include "nrf_gpio.h"

#include "app_error.h"
#include "app_timer.h"
#include "app_usbd.h"
#include "app_usbd_core.h"
// #include "bsp.h"
#include "nrf_gzll.h"
#include "nrf_gzll_error.h"
#include "nrfx_timer.h"

#include "LED.h"
#include "custom_usbd_hid_kbd.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

/**
 * @brief Enable USB power detection
 */
#ifndef USBD_POWER_DETECTION
#define USBD_POWER_DETECTION true
#endif

/*****************************************************************************/
/** @name Configuration  */
/*****************************************************************************/
#define PIPE_NUMBER 1       /**< Pipe 0 is used in this example. */
#define TX_PAYLOAD_LENGTH 1 /**< 1-byte payload length is used when transmitting. */
#define RX_RECEIVE_LENGTH 17
#define Channel_Table_Size 5

static uint8_t m_data_payload[RX_RECEIVE_LENGTH]; /**< Placeholder for data payload received from host. */
static uint8_t m_ack_payload[TX_PAYLOAD_LENGTH];  /**< Payload to attach to ACK sent to device. */

uint8_t Channel_Table[Channel_Table_Size] = {4, 25, 42, 63, 77};

#define LED_R NRF_GPIO_PIN_MAP(0, 8)
#define LED_G NRF_GPIO_PIN_MAP(1, 9)
#define LED_B NRF_GPIO_PIN_MAP(0, 12)
#define LED NRF_GPIO_PIN_MAP(0, 6)

const nrfx_timer_t TIMER_KBD = NRFX_TIMER_INSTANCE(1);

/**
 * @brief USB composite interfaces
 */
#define APP_USBD_INTERFACE_KBD 0

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

/*lint -restore*/

static void kbd_status(void)
{
    if (app_usbd_hid_kbd_led_state_get(&m_app_hid_kbd, APP_USBD_HID_KBD_LED_NUM_LOCK))
    {
        // bsp_board_led_on(LED_NUMLOCK);
        // LED_On(BSP_LED_1);
    }
    else
    {
        // bsp_board_led_off(LED_NUMLOCK);
        // LED_Off(BSP_LED_1);
    }

    if (app_usbd_hid_kbd_led_state_get(&m_app_hid_kbd, APP_USBD_HID_KBD_LED_CAPS_LOCK))
    {
        // bsp_board_led_on(LED_CAPSLOCK);
        // LED_On(BSP_LED_0);
    }
    else
    {
        // bsp_board_led_off(LED_CAPSLOCK);
        // LED_Off(BSP_LED_0);
    }
}

void Pin_Cfg()
{
    // nrf_gpio_cfg_output(LED0);
    // nrf_gpio_cfg_output(LED1);
    // nrf_gpio_cfg_output(LED2);
    // nrf_gpio_cfg_output(LED3);
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
        // bsp_board_led_invert(LED_HID_REP);
        // LED_invert(BSP_LED_2);
        // kbd_status();
        break;
    case APP_USBD_HID_USER_EVT_IN_REPORT_DONE:
        // bsp_board_led_invert(LED_HID_REP);
        // LED_invert(BSP_LED_2);
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
        custom_LED_state_off(&m_app_hid_kbd);
        m_ack_payload[0] = 0;
        // nrfx_clock_hfclk_start();
        break;
    case APP_USBD_EVT_DRV_RESUME:
        // kbd_status(); /* Restore LED state - during SUSPEND all LEDS are turned off */
        break;
    case APP_USBD_EVT_STARTED:
        // LED_On(BSP_LED_3);
        break;
    case APP_USBD_EVT_STOPPED:
        app_usbd_disable();
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

/**
 * @brief Function to read the button state.
 *
 * @return Returns states of the buttons.
 */
static uint8_t input_get(void)
{
}

/**
 * @brief Function to control the LED outputs.
 *
 * @param[in] val Desirable Received data.
 */
static inline void output_present(uint8_t *val)
{
    if (val[0] != 0)
    {
        KBD_Send(&m_app_hid_kbd, val);
        return;
    }
    else
    {
        return;
    }
}

/**
 * @brief Initialize the BSP modules.
 */
static void ui_init(void)
{
    uint32_t err_code;

    // Set up logger.
    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();

    // NRF_LOG_INFO("Gazell ACK payload example. Host mode.");
    // NRF_LOG_FLUSH();

    // bsp_board_init(BSP_INIT_LEDS);
}

/*****************************************************************************/
/** @name Gazell callback function definitions.  */
/*****************************************************************************/
/**
 * @brief RX data ready callback.
 *
 * @details
 */
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info)
{
    uint32_t data_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
    // Pop packet and write first byte of the payload to the GPIO port.
    CRITICAL_REGION_ENTER();
    bool result_value = nrf_gzll_fetch_packet_from_rx_fifo(pipe,
        m_data_payload,
        &data_payload_length);
    if (!result_value)
    {
        NRF_LOG_ERROR("RX fifo error ");
    }
    if (data_payload_length > 0)
    {
        output_present(m_data_payload);
    }
    CRITICAL_REGION_EXIT();

    // Read buttons and load ACK payload into TX queue.
    m_ack_payload[0] = custom_LED_state_get(&m_app_hid_kbd); // Button logic is inverted.

    result_value = nrf_gzll_add_packet_to_tx_fifo(pipe, m_ack_payload, TX_PAYLOAD_LENGTH);
    if (!result_value)
    {
        NRF_LOG_ERROR("TX fifo error ");
    }
}

/**
 * @brief Gazelle callback.
 * @warning Required for successful Gazell initialization.
 */
void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info)
{
}

/**
 * @brief Gazelle callback.
 * @warning Required for successful Gazell initialization.
 */
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info)
{
}

/**
 * @brief Gazelle callback.
 * @warning Required for successful Gazell initialization.
 */
void nrf_gzll_disabled()
{
}

void usb_init()
{
    ret_code_t ret;
    static const app_usbd_config_t usbd_config = {
        .ev_state_proc = usbd_user_ev_handler,
    };

    ret = nrf_drv_clock_init();
    APP_ERROR_CHECK(ret);

    // nrf_drv_clock_lfclk_request(NULL);
    // while (!nrf_drv_clock_lfclk_is_running())
    //{
    //     /* Just waiting */
    // }

    ret = app_usbd_init(&usbd_config);
    APP_ERROR_CHECK(ret);

    app_usbd_class_inst_t const *class_inst_kbd;

    class_inst_kbd = app_usbd_hid_kbd_class_inst_get(&m_app_hid_kbd);
    ret = app_usbd_class_append(class_inst_kbd);

    APP_ERROR_CHECK(ret);

    // NRF_LOG_INFO("USBD HID composite example started.");

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
}

/*****************************************************************************/
/**
 * @brief Main function.
 * @return ANSI required int return type.
 */
/*****************************************************************************/
int main()
{
    Pin_Cfg();

    // Set up the user interface.
    ui_init();
    usb_init();
    nrf_drv_clock_hfclk_request(NULL);
    // Initialize Gazell.
    bool result_value = nrf_gzll_init(NRF_GZLL_MODE_HOST);
    GAZELLE_ERROR_CODE_CHECK(result_value);
    // set Time Slot Period
    nrf_gzll_set_timeslot_period(600);

    result_value = nrf_gzll_set_base_address_1(0xbc010827);
    GAZELLE_ERROR_CODE_CHECK(result_value);
    result_value = nrf_gzll_set_address_prefix_byte(PIPE_NUMBER, 0x27);
    GAZELLE_ERROR_CODE_CHECK(result_value);

    // set frequency hopping
    nrf_gzll_set_channel_table(Channel_Table, Channel_Table_Size);
    nrf_gzll_set_timeslots_per_channel(3);
    nrf_gzll_set_timeslots_per_channel_when_device_out_of_sync(20);
    nrf_gzll_set_device_channel_selection_policy(NRF_GZLL_DEVICE_CHANNEL_SELECTION_POLICY_USE_CURRENT);
    nrf_gzll_set_sync_lifetime(45);

    // Load data into TX queue.
    m_ack_payload[0] = custom_LED_state_get(&m_app_hid_kbd);

    result_value = nrf_gzll_add_packet_to_tx_fifo(PIPE_NUMBER, m_data_payload, TX_PAYLOAD_LENGTH);
    if (!result_value)
    {
        // NRF_LOG_ERROR("TX fifo error ");
        NRF_LOG_FLUSH();
    }

    // Enable Gazell to start sending over the air.
    result_value = nrf_gzll_enable();
    GAZELLE_ERROR_CODE_CHECK(result_value);

    // NRF_LOG_INFO("Gzll ack payload host example started.");
    while (true)
    {
        app_usbd_event_queue_process();
        //  NRF_LOG_FLUSH();
        //__WFE();
    }
}