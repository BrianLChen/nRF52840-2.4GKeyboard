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
#include "bsp.h"
#include "custom_usbd_hid_kbd.h"
#include "nrf_drv_spi.h"
#include "nrfx_spi.h"
#include "nrfx_timer.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

// #include "Key.h"
#include "LED.h"

/* Gazell Config */
#define PIPE_NUMBER 1        /**< Pipe 0 is used in this example. */
#define TX_PAYLOAD_LENGTH 17 /**< 1-byte payload length is used when transmitting. */
#define RX_RECEIVE_LENGTH 1
#define MAX_TX_ATTEMPTS 20 /**< Maximum number of transmission attempts */
#define Channel_Table_Size 5
// #define SPI_LENGTH 2
// #define SPI_INSTANCE 1

#define Mode_Pin NRF_GPIO_PIN_MAP(1, 15)
#define PL_Pin NRF_GPIO_PIN_MAP(1, 11)

static uint8_t m_data_payload[TX_PAYLOAD_LENGTH]; /**< Payload to send to Host. */
static uint8_t m_ack_payload[RX_RECEIVE_LENGTH];  /**< Placeholder for received ACK payloads from Host. */
uint8_t Channel_Table[Channel_Table_Size] = {4, 25, 42, 63, 77};

const nrfx_timer_t TIMER_KBD = NRFX_TIMER_INSTANCE(1);

uint8_t KEYBOARD_Rep_Buff[keyboard_rep_byte];
uint8_t KEYBOARD_Rep_Buff_Prev[keyboard_rep_byte];
uint8_t CONSUMER_Rep_Buff[consumer_rep_byte];
uint8_t CONSUMER_Rep_Buff_Prev[consumer_rep_byte];
// define SPI instance
// static const nrfx_spi_t spi = NRFX_SPI_INSTANCE(1);
static const nrf_drv_spi_t scan_spi = NRF_DRV_SPI_INSTANCE(2);
// static volatile bool spi_xfer_done; /**< Flag used to indicate that SPI instance completed the transfer. */

static uint8_t m_tx_buff[2];
static uint8_t m_rx_buff[2];
static uint8_t scan_buff;

uint8_t test;

// static inline const nrfx_spim_xfer_desc_t spi_desc = {m_tx_buff, 2, m_rx_buff, 2};

// #define spi_desc NRFX_SPIM_SINGLE_XFER(m_tx_buff, 2, m_rx_buff, 2);

uint8_t KEY_TABLE[8] = {KEYBOARD_A, KEYBOARD_B, KEYBOARD_C, KEYBOARD_D,
    KEYBOARD_E, KEYBOARD_F, KEYBOARD_MENU, CONSUMER_MUTE};

/**
 * @brief Enable USB power detection
 */
#ifndef USBD_POWER_DETECTION
#define USBD_POWER_DETECTION true
#endif

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

// Get keyboard LED indicator status
static inline void kbd_status(void)
{
    if (app_usbd_hid_kbd_led_state_get(&m_app_hid_kbd, APP_USBD_HID_KBD_LED_NUM_LOCK))
    {
        LED_On(LED1);
    }
    else
    {
        LED_Off(LED1);
    }

    if (app_usbd_hid_kbd_led_state_get(&m_app_hid_kbd, APP_USBD_HID_KBD_LED_CAPS_LOCK))
    {
        LED_On(LED0);
    }
    else
    {
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
        LED_invert(LED2);
        kbd_status();
        break;
    case APP_USBD_HID_USER_EVT_IN_REPORT_DONE:
        LED_invert(LED2);
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
        // kbd_status(); /* Restore LED state - during SUSPEND all LEDS are turned off */
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
static inline uint32_t debouncefilter(uint16_t delay)
{
    // uint32_t i;
    // i = nrf_gpio_pin_read(Key1);
    // nrf_delay_us(delay);
    // if (i == nrf_gpio_pin_read(Key1))
    //{
    //     return i;
    // }
    // else
    //{
    //     return ~i;
    // }
    uint8_t scan_output;
    nrf_delay_us(delay);
    APP_ERROR_CHECK(nrf_drv_spi_transfer(&scan_spi, m_tx_buff, 2, m_rx_buff, 2));
    scan_output = scan_buff ^ m_rx_buff[0];
    scan_buff = scan_output;
}

inline void remap()
{
    uint8_t bitIndex, index;
    uint8_t remap_buff = ~scan_buff;
    for (uint8_t i = 0; i < 8; i++)
    {
        uint8_t temp = 0x01 << i;
        uint8_t temp2 = temp & remap_buff;
        if (remap_buff & (0x01 << i))
        {
            bitIndex = KEY_TABLE[i] % 8;
            index = KEY_TABLE[i] / 8;
            if (index < 17)
                KEYBOARD_Rep_Buff[index] = (0x01 << bitIndex) | KEYBOARD_Rep_Buff[index];
            else
                CONSUMER_Rep_Buff[1] = (0x01 << bitIndex) | CONSUMER_Rep_Buff[1];
        }
        else
        {
            continue;
        }
    }
}

// Function to scan the key state
static inline void scan_key(nrf_timer_event_t event_type, void *p_context)
{
    nrf_gpio_pin_set(PL_Pin);

    APP_ERROR_CHECK(nrf_drv_spi_transfer(&scan_spi, m_tx_buff, 2, m_rx_buff, 2));
    scan_buff = m_rx_buff[0];

    uint32_t a = debouncefilter(100);
    // int test = memcmp(&KEYBOARD_Rep_Buff_Prev[1], &KEYBOARD_Rep_Buff[1], keyboard_rep_byte - 1);
    // if (a == 0)
    // if (scan_buff == 0xfe)
    //{
    //    KEYBOARD_Rep_Buff[0] = 0x01;
    //    KEYBOARD_Rep_Buff[2] = 0x10;
    //}
    // else
    //{
    //    memset(&KEYBOARD_Rep_Buff[1], 0, keyboard_rep_byte - 1);
    //    // KEYBOARD_Rep_Buff[0] = 0x01;
    //}

    memset(&KEYBOARD_Rep_Buff[1], 0, keyboard_rep_byte - 1);
    memset(&CONSUMER_Rep_Buff[1], 0, consumer_rep_byte - 1);

    remap();
    // int test = memcmp(&KEYBOARD_Rep_Buff_Prev[1], &KEYBOARD_Rep_Buff[1], keyboard_rep_byte - 1);
    // if Keyboard report buff has changed
    if (memcmp(&KEYBOARD_Rep_Buff_Prev[1], &KEYBOARD_Rep_Buff[1], keyboard_rep_byte - 1))
    {
        //test = 1;
        KBD_Send(&m_app_hid_kbd, KEYBOARD_Rep_Buff);
    }
    // else if consumer report buffer has changed
    else if (memcmp(&CONSUMER_Rep_Buff_Prev[1], &CONSUMER_Rep_Buff[1], consumer_rep_byte - 1))
    {
        //test = 2;
        KBD_Send(&m_app_hid_kbd, CONSUMER_Rep_Buff);
    }
    // if none of the report buffers are change
    else
    {
        app_usbd_event_queue_process();
        nrf_gpio_pin_clear(PL_Pin);
        return;
    }

    app_usbd_event_queue_process();

    memcpy(&KEYBOARD_Rep_Buff_Prev[1], &KEYBOARD_Rep_Buff[1], keyboard_rep_byte - 1);
    memcpy(&CONSUMER_Rep_Buff_Prev[1], &CONSUMER_Rep_Buff[1], consumer_rep_byte - 1);

    nrf_gpio_pin_clear(PL_Pin);
    // kbd_status();
}

void TXD_blink()
{
    for (;;)
    {
        nrf_gpio_pin_clear(TX_PIN_NUMBER);
        nrf_delay_ms(1000);
        nrf_gpio_pin_set(TX_PIN_NUMBER);
        nrf_delay_ms(1000);
    }
}

void Pin_Cfg(void)
{
    nrf_gpio_cfg_output(TX_PIN_NUMBER);
    nrf_gpio_cfg_output(RX_PIN_NUMBER);
    nrf_gpio_cfg_output(LED0);
    nrf_gpio_cfg_output(LED1);

    nrf_gpio_cfg_input(Key1, NRF_GPIO_PIN_PULLUP); // Input_button
}

// Function to init Wire Mode Keyboard
void Wire_Mode()
{
    Pin_Cfg();
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

    uint32_t time_ms = 1; // Time(in miliseconds) between consecutive compare events.
    uint32_t time_ticks;
    uint32_t err_code = NRF_SUCCESS;
    nrfx_timer_config_t timer_cfg = NRFX_TIMER_DEFAULT_CONFIG;
    timer_cfg.interrupt_priority = 7;
    err_code = nrfx_timer_init(&TIMER_KBD, &timer_cfg, scan_key);
    APP_ERROR_CHECK(err_code);

    // time_ticks = nrfx_timer_ms_to_ticks(&TIMER_KBD, time_ms);
    time_ticks = nrfx_timer_us_to_ticks(&TIMER_KBD, 900);

    // Set compare, when counter value = time_ticks -> interrupt
    nrfx_timer_extended_compare(&TIMER_KBD, NRF_TIMER_CC_CHANNEL0, time_ticks, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);

    APP_ERROR_CHECK(nrf_drv_spi_transfer(&scan_spi, m_tx_buff, 2, m_rx_buff, 2));
    APP_ERROR_CHECK(nrf_drv_spi_transfer(&scan_spi, m_tx_buff, 2, m_rx_buff, 2));
    memset(m_rx_buff, 0, 2);

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

    // TXD_blink();
    while (true)
    {
        __WFE();
        // LED_On(TX_PIN_NUMBER);
        // nrf_delay_ms(1000);
        // LED_Off(TX_PIN_NUMBER);
        // nrf_delay_ms(1000);
        //  while (app_usbd_event_queue_process())
        //{
        //      /* Nothing to do */
        //  }
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
    // uint32_t a = debouncefilter(100);
    memset(array, 0, 17);

    // if (a != 0)
    //{
    //     // memset(array, 0, 17);
    //     array[0] = 0x01;
    // }
    // else
    //{
    //     array[0] = 0x01;
    //     array[2] = 0x10;
    // }
    nrf_gpio_pin_set(PL_Pin);

    APP_ERROR_CHECK(nrf_drv_spi_transfer(&scan_spi, m_tx_buff, 2, m_rx_buff, 2));
    scan_buff = m_rx_buff[0];

    uint32_t a = debouncefilter(100);
    // int test = memcmp(&KEYBOARD_Rep_Buff_Prev[1], &KEYBOARD_Rep_Buff[1], keyboard_rep_byte - 1);
    // if (a == 0)
    // if (scan_buff == 0xfe)
    //{
    //    KEYBOARD_Rep_Buff[0] = 0x01;
    //    KEYBOARD_Rep_Buff[2] = 0x10;
    //}
    // else
    //{
    //    memset(&KEYBOARD_Rep_Buff[1], 0, keyboard_rep_byte - 1);
    //    // KEYBOARD_Rep_Buff[0] = 0x01;
    //}
    memset(&KEYBOARD_Rep_Buff[1], 0, keyboard_rep_byte - 1);
    remap();
    // int test = memcmp(&KEYBOARD_Rep_Buff_Prev[1], &KEYBOARD_Rep_Buff[1], keyboard_rep_byte - 1);
    // if Keyboard report buff has changed
    if (memcmp(&KEYBOARD_Rep_Buff_Prev[1], &KEYBOARD_Rep_Buff[1], keyboard_rep_byte - 1))
    {
        memcpy(array, KEYBOARD_Rep_Buff, keyboard_rep_byte);
    }
    // else if consumer report buffer has changed
    else if (memcmp(&KEYBOARD_Rep_Buff_Prev[1], &KEYBOARD_Rep_Buff[1], consumer_rep_byte - 1))
    {
        memcpy(array, CONSUMER_Rep_Buff, consumer_rep_byte);
    }
    // if none of the report buffers are change
    else
    {
        nrf_gpio_pin_clear(PL_Pin);
        return;
    }

    memcpy(&KEYBOARD_Rep_Buff_Prev[1], &KEYBOARD_Rep_Buff[1], keyboard_rep_byte - 1);
    memcpy(&CONSUMER_Rep_Buff_Prev[1], &CONSUMER_Rep_Buff[1], consumer_rep_byte - 1);
    nrf_gpio_pin_clear(PL_Pin);
    // kbd_status();
}

/**
 * @brief Initialize the BSP modules.
 */
static void ui_init(void)
{
    uint32_t err_code;
    nrf_gpio_cfg_input(Key1, NRF_GPIO_PIN_PULLUP);

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

    // result_value = nrf_gzll_add_packet_to_tx_fifo(pipe, m_data_payload, TX_PAYLOAD_LENGTH);
    result_value = nrf_gzll_add_packet_to_tx_fifo(pipe, KEYBOARD_Rep_Buff, TX_PAYLOAD_LENGTH);

    if (!result_value)
    {
        NRF_LOG_ERROR("TX fifo error ");
    }
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
    nrf_gzll_set_timeslot_period(500);

    // set base address for pipe1
    result_value = nrf_gzll_set_base_address_1(0xbc010827);
    GAZELLE_ERROR_CODE_CHECK(result_value);
    result_value = nrf_gzll_set_address_prefix_byte(PIPE_NUMBER, 0x27);
    GAZELLE_ERROR_CODE_CHECK(result_value);

    // set frequency hopping
    nrf_gzll_set_channel_table(Channel_Table, Channel_Table_Size);
    nrf_gzll_set_timeslots_per_channel(4);
    nrf_gzll_set_timeslots_per_channel_when_device_out_of_sync(2);
    nrf_gzll_set_device_channel_selection_policy(NRF_GZLL_DEVICE_CHANNEL_SELECTION_POLICY_USE_CURRENT);
    nrf_gzll_set_sync_lifetime(8);

    // Attempt sending every packet up to MAX_TX_ATTEMPTS times.
    nrf_gzll_set_max_tx_attempts(MAX_TX_ATTEMPTS);

    input_get(m_data_payload);

    result_value = nrf_gzll_add_packet_to_tx_fifo(PIPE_NUMBER, m_data_payload, TX_PAYLOAD_LENGTH);

    APP_ERROR_CHECK(nrf_drv_spi_transfer(&scan_spi, m_tx_buff, 2, m_rx_buff, 2));
    APP_ERROR_CHECK(nrf_drv_spi_transfer(&scan_spi, m_tx_buff, 2, m_rx_buff, 2));
    memset(m_rx_buff, 0, 2);

    // Enable Gazell to start sending over the air.
    result_value = nrf_gzll_enable();
    GAZELLE_ERROR_CODE_CHECK(result_value);

    // NRF_LOG_INFO("Gzll ack payload device example started.");

    Pin_Cfg();
    // TXD_blink();

    while (true)
    {
        // NRF_LOG_FLUSH();
        __WFE();
    }
}

int main(void)
{
    KEYBOARD_Rep_Buff[0] = 0x01;
    KEYBOARD_Rep_Buff_Prev[0] = 0x01;
    CONSUMER_Rep_Buff[0] = 0x02;
    CONSUMER_Rep_Buff_Prev[0] = 0x02;

    m_tx_buff[0] = 0x00;
    m_tx_buff[1] = 0x00;

    nrf_gpio_cfg_input(Mode_Pin, NRF_GPIO_PIN_PULLUP);
    nrf_gpio_cfg_output(PL_Pin);

    nrf_drv_spi_config_t spi_config = NRF_DRV_SPI_DEFAULT_CONFIG;
    spi_config.ss_pin = SER_APP_SPIM0_SS_PIN;
    spi_config.miso_pin = SER_APP_SPIM0_MISO_PIN;
    spi_config.mosi_pin = NRFX_SPIM_PIN_NOT_USED;
    spi_config.sck_pin = SER_APP_SPIM0_SCK_PIN;
    spi_config.frequency = NRF_SPIM_FREQ_4M;

    APP_ERROR_CHECK(nrf_drv_spi_init(&scan_spi, &spi_config, NULL, NULL));

    uint32_t mode = nrf_gpio_pin_read(Mode_Pin);

    if (mode != 0)
        Wire_Mode();
    else
        Wireless_Mode();
}