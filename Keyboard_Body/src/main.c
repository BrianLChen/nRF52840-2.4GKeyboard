/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usbd.h>

#include <gzll_glue.h>
#include <nrf_gzll.h>

#include <debounce.h>
#include <hid_descriptor.h>
#include <kbd_define.h>
#include <key_state.h>
#include <keymap.h>
#include <keyscan_spi.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, KBD_LOG_LEVEL);

enum kb_leds_idx
{
    /*
     * These indexes intentionally match the HID LED output report bit order:
     * bit0 NumLock, bit1 CapsLock, bit2 ScrollLock.
     */
    KB_LED_NUMLOCK = 0,
    KB_LED_CAPSLOCK,
    KB_LED_SCROLLLOCK,
    KB_LED_COUNT,
};

static const struct gpio_dt_spec kb_leds[KB_LED_COUNT] = {
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(kbd_led_numlock), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(kbd_led_capslock), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(kbd_led_scrolllock), gpios, {0}),
};

enum keyboard_mode
{
    KEYBOARD_MODE_WIRED,
    KEYBOARD_MODE_BLE,
    KEYBOARD_MODE_WIRELESS_24G,
};

#define KBD_USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec mode_wire =
    GPIO_DT_SPEC_GET(KBD_USER_NODE, mode_wire_gpios);
static const struct gpio_dt_spec mode_ble =
    GPIO_DT_SPEC_GET(KBD_USER_NODE, mode_ble_gpios);
static const struct gpio_dt_spec mode_wireless =
    GPIO_DT_SPEC_GET(KBD_USER_NODE, mode_wireless_gpios);

struct kb_event
{
    uint16_t code;
    int32_t value;
};

/*
 * Gazell callbacks should stay small. They run from the radio/Gazell context,
 * so the callback only copies the TX result into a queue and wakes normal
 * Zephyr work to do RX FIFO reads and LED state updates.
 */
struct gzll_tx_result
{
    bool success;
    uint32_t pipe;
    nrf_gzll_device_tx_info_t info;
};

K_MSGQ_DEFINE(kb_msgq, sizeof(struct kb_event), 2, 1);
K_SEM_DEFINE(scan_sem, 0, 1);
K_MSGQ_DEFINE(gzll_tx_msgq, sizeof(struct gzll_tx_result), 2, sizeof(uint32_t));

UDC_STATIC_BUF_DEFINE(keyboard_report, KBD_HID_KEYBOARD_REPORT_BYTES);
UDC_STATIC_BUF_DEFINE(consumer_report, KBD_HID_CONSUMER_REPORT_BYTES);
static uint8_t keyboard_report_prev[KBD_HID_KEYBOARD_REPORT_BYTES];
static uint8_t consumer_report_prev[KBD_HID_CONSUMER_REPORT_BYTES];

/*
 * 2.4G TX payload is intentionally fixed at the full keyboard report length.
 * Smaller reports, such as consumer control, are zero padded before enqueueing
 * so the dongle can remain a transparent USB forwarder.
 */
static uint8_t wireless_payload[KBD_GZLL_TX_PAYLOAD_BYTES];

/*
 * ACK payload carries host-to-keyboard status. For now only byte 0 is used for
 * keyboard LED state returned by the dongle after the host updates Num/Caps/
 * Scroll Lock.
 */
static uint8_t wireless_ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];

/*
 * nrf_gzll_set_channel_table() takes a mutable pointer, so this table cannot
 * be const even though the project treats the configured channels as fixed.
 */
static uint8_t wireless_gzll_channel_table[KBD_GZLL_CHANNEL_COUNT] =
    KBD_GZLL_CHANNEL_TABLE;
static struct k_work gzll_work;
static uint32_t kb_duration;
static bool kb_ready;
static uint8_t wireless_disconnect_counter;
static uint32_t wireless_keep_alive_fail_counter_ms;
static uint8_t keyboard_led_state;

/*
 * Timer ISR only releases the scan loop. SPI scan, debounce, keymap, and USB/
 * Gazell report work stay in thread context.
 */
static void scan_timer_expiry(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    k_sem_give(&scan_sem);
}

K_TIMER_DEFINE(scan_timer, scan_timer_expiry, NULL);

static void keyboard_report_init(void)
{
    /*
     * Reports keep their report ID in byte 0 permanently. Builders only clear
     * the payload bytes so report identity is never lost between submissions.
     */
    keyboard_report[0] = KBD_HID_REPORT_ID_KEYBOARD;
    consumer_report[0] = KBD_HID_REPORT_ID_CONSUMER;
    keyboard_report_prev[0] = KBD_HID_REPORT_ID_KEYBOARD;
    consumer_report_prev[0] = KBD_HID_REPORT_ID_CONSUMER;
}

static void keyboard_leds_set_state(uint8_t led_state)
{
    /*
     * Missing LED aliases are allowed during board bring-up. A zeroed
     * gpio_dt_spec is skipped so the firmware can run before final LED pins
     * are assigned in devicetree.
     */
    for (unsigned int i = 0; i < ARRAY_SIZE(kb_leds); i++)
    {
        bool enabled = (led_state & BIT(i)) != 0U;

        if (kb_leds[i].port == NULL)
        {
            continue;
        }

        (void)gpio_pin_set_dt(&kb_leds[i], enabled ? 1 : 0);
    }
}

static void keyboard_leds_clear(void)
{
    keyboard_leds_set_state(0);
}

static int keyboard_leds_set_report(const uint8_t id, const uint16_t len,
                                    const uint8_t *const buf)
{
    if (len == 0U)
    {
        return 0;
    }

    if (id != 0U && id != KBD_HID_REPORT_ID_KEYBOARD)
    {
        LOG_WRN("Unsupported output report ID %u", id);
        return -ENOTSUP;
    }

    /*
     * Control endpoint SET_REPORT can arrive either as [led_state] or as
     * [report_id, led_state]. Accept both forms because different hosts and
     * Zephyr paths may expose the output report with or without the ID byte.
     */
    if (len > 1U && buf[0] == KBD_HID_REPORT_ID_KEYBOARD)
    {
        keyboard_led_state = buf[1];
    }
    else
    {
        keyboard_led_state = buf[0];
    }

    keyboard_led_state &= BIT(KB_LED_NUMLOCK) |
                          BIT(KB_LED_CAPSLOCK) |
                          BIT(KB_LED_SCROLLLOCK);

    LOG_INF("Keyboard LED state: 0x%02x", keyboard_led_state);
    keyboard_leds_set_state(keyboard_led_state);

    return 0;
}

static void keyboard_report_set_usage(uint16_t usage, bool pressed)
{
    uint16_t bit;
    uint8_t mask;

    /*
     * HID modifier usages live in byte 1. Normal keyboard usages are stored in
     * the NKRO bitmap after KBD_HID_KEYBOARD_BITMAP_BIT_OFFSET.
     */
    if (usage >= 0xe0 && usage <= 0xe7)
    {
        mask = BIT(usage - 0xe0);
        if (pressed)
        {
            keyboard_report[1] |= mask;
        }
        else
        {
            keyboard_report[1] &= ~mask;
        }

        return;
    }

    if (usage > KBD_HID_KEYBOARD_USAGE_MAX)
    {
        LOG_WRN("Keyboard usage 0x%02x is outside NKRO bitmap", usage);
        return;
    }

    bit = usage + KBD_HID_KEYBOARD_BITMAP_BIT_OFFSET;
    mask = BIT(bit % 8U);

    if (pressed)
    {
        keyboard_report[bit / 8U] |= mask;
    }
    else
    {
        keyboard_report[bit / 8U] &= ~mask;
    }
}

static void keyboard_report_set_code(uint16_t code)
{
    uint8_t byte_index;
    uint8_t mask;

    if (code == KBD_KEY_NONE)
    {
        return;
    }

    /*
     * Keymap codes are already encoded as report bit positions for keyboard
     * usages, while consumer controls are kept in a separate report.
     */
    if (code >= CONSUMER_VOLUME_INCREASE &&
        code <= CONSUMER_AL_CALCULATOR)
    {
        consumer_report[1] |= BIT(code - CONSUMER_VOLUME_INCREASE);
        return;
    }

    byte_index = code / 8U;
    if (byte_index >= KBD_HID_KEYBOARD_REPORT_BYTES)
    {
        LOG_WRN("Keymap code %u is outside keyboard report", code);
        return;
    }

    mask = BIT(code % 8U);
    keyboard_report[byte_index] |= mask;
}

static void keyboard_report_build_from_keymap(void)
{
    uint8_t active_layer = keymap_get_active_layer();
    struct key_state *keys = key_state_buffer_get();

    /*
     * FN keys select the active layer but do not emit a HID usage by
     * themselves. The active layer is computed from debounced key state.
     */
    memset(&keyboard_report[1], 0, KBD_HID_KEYBOARD_REPORT_BYTES - 1);
    consumer_report[1] = 0;

    for (uint16_t key = 0; key < KBD_KEY_COUNT; key++)
    {
        if (!keys[key].press_status || keymap_is_fn_key(key))
        {
            continue;
        }

        keyboard_report_set_code(keymap_get_code(active_layer, key));
    }
}

static bool keyboard_scan_once(void)
{
    int ret;

    LOG_DBG("Main thread: Scan");

    /*
     * keyscan_read() writes raw scan bits into key_state. debounce_update()
     * then converts those raw bits into stable press_status values.
     */
    ret = keyscan_read();
    if (ret != 0)
    {
        LOG_ERR("Matrix scan failed, %d", ret);
        return false;
    }

    if (debounce_update())
    {
        LOG_INF("Debounced key state changed");
        keyscan_log_changes();
        return true;
    }

    return false;
}

static void __maybe_unused wireless_disconnect_counter_reset(void)
{
    wireless_disconnect_counter = 0;
    wireless_keep_alive_fail_counter_ms = k_uptime_get_32();
}

static void __maybe_unused wireless_disconnect_counter_add_report_fail(void)
{
    if (wireless_disconnect_counter < KBD_WIRELESS_DISCONNECT_COUNTER_THRESHOLD)
    {
        wireless_disconnect_counter++;
    }

    if (wireless_disconnect_counter >= KBD_WIRELESS_DISCONNECT_COUNTER_THRESHOLD)
    {
        keyboard_leds_clear();
    }
}

/*
 * Keep-alive failures are rate-limited before touching the shared disconnect
 * counter. This prevents background keep-alive traffic from making the device
 * enter disconnected behavior faster than real failed key reports.
 */
static void __maybe_unused wireless_disconnect_counter_add_keep_alive_fail(void)
{
    uint32_t now = k_uptime_get_32();

    if ((now - wireless_keep_alive_fail_counter_ms) <
        KBD_WIRELESS_DISCONNECT_COUNTER_PERIOD_MS)
    {
        return;
    }

    wireless_keep_alive_fail_counter_ms = now;
    wireless_disconnect_counter_add_report_fail();
}

static void __maybe_unused wireless_sleep_prepare(void)
{
    keyboard_leds_clear();
}

/*
 * Queue one Gazell packet. The caller passes the logical HID report length;
 * this function pads it to KBD_GZLL_TX_PAYLOAD_BYTES for the radio payload.
 */
static int wireless_gzll_queue_payload(const uint8_t *report, size_t report_size)
{
    bool result_value;

    if (report_size > sizeof(wireless_payload))
    {
        LOG_ERR("Wireless report too large: %u", (unsigned int)report_size);
        return -EINVAL;
    }

    memset(wireless_payload, 0, sizeof(wireless_payload));
    memcpy(wireless_payload, report, report_size);

    result_value = nrf_gzll_add_packet_to_tx_fifo(KBD_GZLL_PIPE_NUMBER,
                                                  wireless_payload,
                                                  sizeof(wireless_payload));
    if (!result_value)
    {
        LOG_WRN("GZLL TX FIFO add failed");
        return -EIO;
    }

    return 0;
}

/*
 * Process one completed Gazell TX attempt in workqueue context.
 *
 * TX success is the connection signal because the host does not continuously
 * send LED output reports. If the ACK contains data, byte 0 is applied to the
 * local keyboard LED indicators.
 */
static void wireless_gzll_handle_tx_result(struct gzll_tx_result *tx_result)
{
    bool result_value;
    uint32_t ack_payload_length = sizeof(wireless_ack_payload);

    if (tx_result->success)
    {
        wireless_disconnect_counter_reset();

        if (tx_result->info.payload_received_in_ack)
        {
            result_value = nrf_gzll_fetch_packet_from_rx_fifo(
                tx_result->pipe,
                wireless_ack_payload,
                &ack_payload_length);
            if (!result_value)
            {
                LOG_ERR("GZLL RX FIFO fetch failed");
                return;
            }

            if (ack_payload_length >= KBD_GZLL_ACK_PAYLOAD_BYTES)
            {
                keyboard_led_state = wireless_ack_payload[0] &
                                     (BIT(KB_LED_NUMLOCK) |
                                      BIT(KB_LED_CAPSLOCK) |
                                      BIT(KB_LED_SCROLLLOCK));
                keyboard_leds_set_state(keyboard_led_state);
            }
        }

        return;
    }

    wireless_disconnect_counter_add_report_fail();
}

/* Drain all queued radio completion events after a callback wakes this work. */
static void wireless_gzll_work_handler(struct k_work *work)
{
    struct gzll_tx_result tx_result;

    ARG_UNUSED(work);

    while (k_msgq_get(&gzll_tx_msgq, &tx_result, K_NO_WAIT) == 0)
    {
        wireless_gzll_handle_tx_result(&tx_result);
    }
}

/* Common callback helper: copy Gazell TX metadata into Zephyr-owned context. */
static void wireless_gzll_report_tx(bool success,
                                    uint32_t pipe,
                                    nrf_gzll_device_tx_info_t *tx_info)
{
    struct gzll_tx_result tx_result = {
        .success = success,
        .pipe = pipe,
        .info = *tx_info,
    };

    if (k_msgq_put(&gzll_tx_msgq, &tx_result, K_NO_WAIT) == 0)
    {
        k_work_submit(&gzll_work);
    }
}

/* Gazell device callback: packet was ACKed by the dongle. */
void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info)
{
    wireless_gzll_report_tx(true, pipe, &tx_info);
}

/* Gazell device callback: packet retry budget expired without an ACK. */
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info)
{
    wireless_gzll_report_tx(false, pipe, &tx_info);
}

/* Required Gazell callback. No dynamic shutdown handling is needed yet. */
void nrf_gzll_disabled(void)
{
}

/*
 * Host RX is not used by the keyboard body. The body is a Gazell device and
 * receives dongle data through ACK payloads after TX success.
 */
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info)
{
    ARG_UNUSED(pipe);
    ARG_UNUSED(rx_info);
}

/*
 * Configure the keyboard body as a Gazell device.
 *
 * Address, prefix, channel table, payload size, and ACK payload size must stay
 * aligned with the dongle side shared configuration.
 */
static int wireless_gzll_init(void)
{
    bool result_value;

    k_work_init(&gzll_work, wireless_gzll_work_handler);

    result_value = gzll_glue_init();
    if (!result_value)
    {
        LOG_ERR("Cannot initialize GZLL glue");
        return -EIO;
    }

    result_value = nrf_gzll_init(NRF_GZLL_MODE_DEVICE);
    if (!result_value)
    {
        LOG_ERR("Cannot initialize GZLL device");
        return -EIO;
    }

    result_value = nrf_gzll_set_base_address_1(KBD_GZLL_BASE_ADDRESS_1);
    if (!result_value)
    {
        LOG_ERR("Cannot set GZLL base address");
        return -EIO;
    }

    result_value = nrf_gzll_set_address_prefix_byte(KBD_GZLL_PIPE_NUMBER,
                                                    KBD_GZLL_PIPE_PREFIX);
    if (!result_value)
    {
        LOG_ERR("Cannot set GZLL pipe prefix");
        return -EIO;
    }

    result_value = nrf_gzll_set_channel_table(wireless_gzll_channel_table,
                                              KBD_GZLL_CHANNEL_COUNT);
    if (!result_value)
    {
        LOG_ERR("Cannot set GZLL channel table");
        return -EIO;
    }

    nrf_gzll_set_timeslots_per_channel(KBD_GZLL_TIMESLOTS_PER_CHANNEL);
    nrf_gzll_set_timeslots_per_channel_when_device_out_of_sync(
        KBD_GZLL_OUT_OF_SYNC_TIMESLOTS_PER_CHANNEL);
    nrf_gzll_set_device_channel_selection_policy(
        NRF_GZLL_DEVICE_CHANNEL_SELECTION_POLICY_USE_CURRENT);
    nrf_gzll_set_sync_lifetime(KBD_GZLL_SYNC_LIFETIME);
    nrf_gzll_set_max_tx_attempts(KBD_GZLL_MAX_TX_ATTEMPTS);

    /*
     * Prime the FIFO before enabling Gazell. This mirrors Nordic's example
     * flow and gives the radio an initial packet immediately after enable.
     */
    result_value = nrf_gzll_add_packet_to_tx_fifo(KBD_GZLL_PIPE_NUMBER,
                                                  keyboard_report,
                                                  KBD_GZLL_TX_PAYLOAD_BYTES);
    if (!result_value)
    {
        LOG_ERR("Cannot add initial GZLL TX packet");
        return -EIO;
    }

    result_value = nrf_gzll_enable();
    if (!result_value)
    {
        LOG_ERR("Cannot enable GZLL");
        return -EIO;
    }

    LOG_INF("GZLL device initialized");

    return 0;
}

/*
 * Convert debounced key state into radio reports.
 *
 * Previous-report tracking is updated only after the packet enters the Gazell
 * FIFO. If the FIFO is full, the same report remains pending and will be
 * retried by the next scan tick.
 */
static void wireless_submit_keymap_reports(void)
{
    int ret;

    keyboard_report_build_from_keymap();

    if (memcmp(keyboard_report_prev, keyboard_report,
               sizeof(keyboard_report_prev)) != 0)
    {
        ret = wireless_gzll_queue_payload(keyboard_report,
                                          KBD_HID_KEYBOARD_REPORT_BYTES);
        if (ret == 0)
        {
            memcpy(keyboard_report_prev, keyboard_report,
                   sizeof(keyboard_report_prev));
        }
    }

    if (memcmp(consumer_report_prev, consumer_report,
               sizeof(consumer_report_prev)) != 0)
    {
        ret = wireless_gzll_queue_payload(consumer_report,
                                          KBD_HID_CONSUMER_REPORT_BYTES);
        if (ret == 0)
        {
            memcpy(consumer_report_prev, consumer_report,
                   sizeof(consumer_report_prev));
        }
    }
}

static void keyboard_submit_report(const struct device *hid_dev,
                                   struct usbd_context *sample_usbd,
                                   const uint8_t *report,
                                   size_t report_size,
                                   bool wake_on_press)
{
    int ret;

    if (!kb_ready)
    {
        LOG_INF("USB HID device is not ready");
        return;
    }

    /*
     * If the host suspended USB, only a key press should request remote wake.
     * Release-only reports are dropped while suspended.
     */
    if (IS_ENABLED(CONFIG_SAMPLE_USBD_REMOTE_WAKEUP) &&
        usbd_is_suspended(sample_usbd))
    {
        if (wake_on_press)
        {
            ret = usbd_wakeup_request(sample_usbd);
            if (ret)
            {
                LOG_ERR("Remote wakeup error, %d", ret);
            }
        }
        return;
    }

    LOG_DBG("Main thread: USB report");

    ret = hid_device_submit_report(hid_dev, report_size, report);
    if (ret)
    {
        LOG_ERR("HID submit report error, %d", ret);
    }
}

static void keyboard_submit_keymap_reports(const struct device *hid_dev,
                                           struct usbd_context *sample_usbd)
{
    keyboard_report_build_from_keymap();

    /*
     * Submit only changed reports. Keyboard and consumer reports are tracked
     * separately, so a media-key change does not resend the full NKRO report.
     */
    if (memcmp(keyboard_report_prev, keyboard_report,
               sizeof(keyboard_report_prev)) != 0)
    {
        keyboard_submit_report(hid_dev, sample_usbd,
                               keyboard_report,
                               KBD_HID_KEYBOARD_REPORT_BYTES,
                               keyboard_report[1] != 0U);
        memcpy(keyboard_report_prev, keyboard_report,
               sizeof(keyboard_report_prev));
    }

    if (memcmp(consumer_report_prev, consumer_report,
               sizeof(consumer_report_prev)) != 0)
    {
        keyboard_submit_report(hid_dev, sample_usbd,
                               consumer_report,
                               KBD_HID_CONSUMER_REPORT_BYTES,
                               consumer_report[1] != 0U);
        memcpy(consumer_report_prev, consumer_report,
               sizeof(consumer_report_prev));
    }
}

static void keyboard_process_input_events(const struct device *hid_dev,
                                          struct usbd_context *sample_usbd)
{
    struct kb_event kb_evt;

    /*
     * This path is left from Zephyr's input sample behavior. Matrix-scanned
     * keys use keymap/debounce above; input events can still be useful during
     * bring-up for board buttons or temporary test keys.
     */
    while (k_msgq_get(&kb_msgq, &kb_evt, K_NO_WAIT) == 0)
    {
        switch (kb_evt.code)
        {
        case INPUT_KEY_0:
            keyboard_report_set_usage(HID_KEY_NUMLOCK, kb_evt.value);
            break;
        case INPUT_KEY_1:
            keyboard_report_set_usage(HID_KEY_CAPSLOCK, kb_evt.value);
            break;
        case INPUT_KEY_2:
            keyboard_report_set_usage(HID_KEY_SCROLLLOCK, kb_evt.value);
            break;
        case INPUT_KEY_3:
            keyboard_report_set_usage(0xe6, kb_evt.value);
            keyboard_report_set_usage(HID_KEY_1, kb_evt.value);
            keyboard_report_set_usage(HID_KEY_2, kb_evt.value);
            keyboard_report_set_usage(HID_KEY_3, kb_evt.value);
            break;
        default:
            LOG_INF("Unrecognized input code %u value %d",
                    kb_evt.code, kb_evt.value);
            continue;
        }

        keyboard_submit_report(hid_dev, sample_usbd,
                               keyboard_report,
                               KBD_HID_KEYBOARD_REPORT_BYTES,
                               kb_evt.value);
    }
}

static int mode_gpio_init_one(const struct gpio_dt_spec *gpio)
{
    if (!gpio_is_ready_dt(gpio))
    {
        LOG_ERR("Mode GPIO device %s is not ready", gpio->port->name);
        return -ENODEV;
    }

    return gpio_pin_configure_dt(gpio, GPIO_INPUT);
}

static int mode_gpio_init(void)
{
    int ret;

    ret = mode_gpio_init_one(&mode_wire);
    if (ret != 0)
    {
        return ret;
    }

    ret = mode_gpio_init_one(&mode_ble);
    if (ret != 0)
    {
        return ret;
    }

    return mode_gpio_init_one(&mode_wireless);
}

/*
 * Read the external mode switch.
 *
 * This is currently not used by main() because wired mode is forced during
 * bring-up. It will become the mode selection point once the switch hardware
 * mapping is finalized.
 */
static enum keyboard_mode __maybe_unused keyboard_mode_detect(void)
{
    if (gpio_pin_get_dt(&mode_wire) > 0)
    {
        LOG_INF("Keyboard mode: wired");
        return KEYBOARD_MODE_WIRED;
    }

    if (gpio_pin_get_dt(&mode_ble) > 0)
    {
        LOG_INF("Keyboard mode: BLE");
        return KEYBOARD_MODE_BLE;
    }

    if (gpio_pin_get_dt(&mode_wireless) > 0)
    {
        LOG_INF("Keyboard mode: 2.4G");
        return KEYBOARD_MODE_WIRELESS_24G;
    }

    LOG_WRN("No mode switch active, defaulting to wired mode");
    return KEYBOARD_MODE_WIRED;
}

static int peripheral_init(void)
{
    int ret;

    /*
     * Shared initialization for wired, BLE, and 2.4G modes. Mode-specific USB
     * or radio setup happens later, but all modes need reports, keymap, SPI
     * matrix scan, debounce state, and the periodic scan timer.
     */
    keyboard_report_init();
    keymap_init();

    ret = keyscan_init();
    if (ret != 0)
    {
        return ret;
    }

    k_timer_start(&scan_timer,
                  K_USEC(KBD_SCAN_PERIOD_US),
                  K_USEC(KBD_SCAN_PERIOD_US));

    return 0;
}

static int wireless_24g_mode_run(void)
{
    int ret;

    LOG_INF("2.4G mode init");

    ret = wireless_gzll_init();
    if (ret != 0)
    {
        return ret;
    }

    /*
     * The scan timer drives 2.4G reports the same way it drives wired USB
     * reports. Gazell callbacks only report TX status and ACK payloads.
     */
    while (true)
    {
        k_sem_take(&scan_sem, K_FOREVER);
        (void)keyboard_scan_once();
        wireless_submit_keymap_reports();
    }

    return 0;
}

static void ble_mode_dummy(void)
{
    LOG_INF("BLE mode dummy init");

    /*
     * BLE transport is intentionally empty for now. Keep scan/debounce running
     * here so the future BLE path can reuse the same key state pipeline.
     */
    while (true)
    {
        k_sem_take(&scan_sem, K_FOREVER);
        (void)keyboard_scan_once();
    }
}

static void input_cb(struct input_event *evt, void *user_data)
{
    struct kb_event kb_evt;

    ARG_UNUSED(user_data);

    kb_evt.code = evt->code;
    kb_evt.value = evt->value;
    if (k_msgq_put(&kb_msgq, &kb_evt, K_NO_WAIT) != 0)
    {
        LOG_ERR("Failed to put new input event");
    }
}

INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL);

static void kb_iface_ready(const struct device *dev, const bool ready)
{
    LOG_INF("HID device %s interface is %s",
            dev->name, ready ? "ready" : "not ready");
    kb_ready = ready;
}

static int kb_get_report(const struct device *dev,
                         const uint8_t type, const uint8_t id, const uint16_t len,
                         uint8_t *const buf)
{
    if (type != HID_REPORT_TYPE_OUTPUT)
    {
        LOG_WRN("Unsupported Get Report type %u ID %u", type, id);
        return -ENOTSUP;
    }

    if (id != 0U && id != KBD_HID_REPORT_ID_KEYBOARD)
    {
        LOG_WRN("Unsupported output report ID %u", id);
        return -ENOTSUP;
    }

    if (len == 0U)
    {
        return -EINVAL;
    }

    /*
     * Return the cached LED byte for control endpoint GET_REPORT. This mirrors
     * the last accepted SET_REPORT state.
     */
    buf[0] = keyboard_led_state;
    LOG_INF("Get keyboard LED state: 0x%02x", keyboard_led_state);

    return 1;
}

static int kb_set_report(const struct device *dev,
                         const uint8_t type, const uint8_t id, const uint16_t len,
                         const uint8_t *const buf)
{
    if (type != HID_REPORT_TYPE_OUTPUT)
    {
        LOG_WRN("Unsupported report type");
        return -ENOTSUP;
    }

    return keyboard_leds_set_report(id, len, buf);
}

/* Idle duration is stored but not used to calculate idle reports. */
static void kb_set_idle(const struct device *dev,
                        const uint8_t id, const uint32_t duration)
{
    LOG_INF("Set Idle %u to %u", id, duration);
    kb_duration = duration;
}

static uint32_t kb_get_idle(const struct device *dev, const uint8_t id)
{
    LOG_INF("Get Idle %u to %u", id, kb_duration);
    return kb_duration;
}

static void kb_set_protocol(const struct device *dev, const uint8_t proto)
{
    LOG_INF("Protocol changed to %s",
            proto == 0U ? "Boot Protocol" : "Report Protocol");
}

static void __maybe_unused kb_output_report(const struct device *dev,
                                            const uint16_t len,
                                            const uint8_t *const buf)
{
    LOG_HEXDUMP_INF(buf, len, "Output report");
    /*
     * This callback may receive either [led_state] or
     * [report_id, led_state]. Do not pass buf[0] as the report ID here:
     * for [led_state], CapsLock is 0x02 and Num+Caps is 0x03.
     */
    kb_set_report(dev, HID_REPORT_TYPE_OUTPUT, 0U, len, buf);
}

struct hid_device_ops kb_ops = {
    .iface_ready = kb_iface_ready,
    .get_report = kb_get_report,
    .set_report = kb_set_report,
    .set_idle = kb_set_idle,
    .get_idle = kb_get_idle,
    .set_protocol = kb_set_protocol,
    /*
     * Keep interrupt OUT disabled for now. LED output reports are handled
     * through control endpoint SET_REPORT to avoid the old delayed LED path.
     */
    .output_report = NULL,
};

/*
 * USB device message callback.
 *
 * On boards with VBUS detection, USB is enabled/disabled with cable state.
 * On boards without VBUS detection, wired_mode_run() enables USB directly.
 */
static void msg_cb(struct usbd_context *const usbd_ctx,
                   const struct usbd_msg *const msg)
{
    LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

    if (msg->type == USBD_MSG_CONFIGURATION)
    {
        LOG_INF("\tConfiguration value %d", msg->status);
    }

    if (usbd_can_detect_vbus(usbd_ctx))
    {
        if (msg->type == USBD_MSG_VBUS_READY)
        {
            if (usbd_enable(usbd_ctx))
            {
                LOG_ERR("Failed to enable device support");
            }
        }

        if (msg->type == USBD_MSG_VBUS_REMOVED)
        {
            if (usbd_disable(usbd_ctx))
            {
                LOG_ERR("Failed to disable device support");
            }
        }
    }
}

static int wired_mode_run(void)
{
    struct usbd_context *sample_usbd;
    const struct device *hid_dev;
    int ret;

    LOG_INF("Wired mode init");

    /*
     * LED GPIOs are initialized only in wired mode at the moment because USB
     * control endpoint LED reports are wired-mode behavior. The same helpers
     * are also used by wireless disconnect/sleep handling.
     */
    for (unsigned int i = 0; i < ARRAY_SIZE(kb_leds); i++)
    {
        if (kb_leds[i].port == NULL)
        {
            continue;
        }

        if (!gpio_is_ready_dt(&kb_leds[i]))
        {
            LOG_ERR("LED device %s is not ready", kb_leds[i].port->name);
            return -EIO;
        }

        ret = gpio_pin_configure_dt(&kb_leds[i], GPIO_OUTPUT_INACTIVE);
        if (ret != 0)
        {
            LOG_ERR("Failed to configure the LED pin, %d", ret);
            return -EIO;
        }
    }

    /*
     * Register one HID device that exposes both keyboard and consumer-control
     * reports through report IDs.
     */
    hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
    if (!device_is_ready(hid_dev))
    {
        LOG_ERR("HID Device is not ready");
        return -EIO;
    }

    ret = hid_device_register(hid_dev,
                              hid_report_desc, hid_report_desc_size,
                              &kb_ops);
    if (ret != 0)
    {
        LOG_ERR("Failed to register HID Device, %d", ret);
        return ret;
    }

    /*
     * Polling period is configured from kbd_define.h so product variants can
     * select 250/500/1000 Hz without touching USB setup code.
     */
    if (IS_ENABLED(CONFIG_USBD_HID_SET_POLLING_PERIOD))
    {
        ret = hid_device_set_in_polling(hid_dev,
                                        KBD_HID_REPORT_POLLING_PERIOD_US);
        if (ret)
        {
            LOG_WRN("Failed to set IN report polling period, %d", ret);
        }

        ret = hid_device_set_out_polling(hid_dev,
                                         KBD_HID_REPORT_POLLING_PERIOD_US);
        if (ret != 0 && ret != -ENOTSUP)
        {
            LOG_WRN("Failed to set OUT report polling period, %d", ret);
        }
    }

    sample_usbd = sample_usbd_init_device(msg_cb);
    if (sample_usbd == NULL)
    {
        LOG_ERR("Failed to initialize USB device");
        return -ENODEV;
    }

    if (!usbd_can_detect_vbus(sample_usbd))
    {
        ret = usbd_enable(sample_usbd);
        if (ret)
        {
            LOG_ERR("Failed to enable device support");
            return ret;
        }
    }

    LOG_INF("HID keyboard sample is initialized");

    /*
     * Wired main loop:
     *   1. wait for the scan timer
     *   2. scan SPI matrix and debounce
     *   3. submit changed keymap reports to USB
     *   4. process any temporary Zephyr input events
     */
    while (true)
    {
        k_sem_take(&scan_sem, K_FOREVER);
        if (keyboard_scan_once())
        {
            keyboard_submit_keymap_reports(hid_dev, sample_usbd);
        }
        keyboard_process_input_events(hid_dev, sample_usbd);
    }

    return 0;
}

int main(void)
{
    enum keyboard_mode mode;
    int ret;

    /*
     * Main thread owns scan/debounce/report work, so it runs above the RGB
     * thread. The scan timer only wakes this thread; it does not do SPI work.
     */
    k_thread_priority_set(k_current_get(), KBD_MAIN_THREAD_PRIORITY);

    ret = mode_gpio_init();
    if (ret != 0)
    {
        return ret;
    }

    /* Force wired mode until the mode switch hardware is ready. */
    mode = KEYBOARD_MODE_WIRED;

    ret = peripheral_init();
    if (ret != 0)
    {
        return ret;
    }

    /*
     * Common peripherals are ready before entering the selected transport
     * loop. Each mode function owns its transport-specific initialization and
     * then stays in its own forever loop.
     */
    switch (mode)
    {
    case KEYBOARD_MODE_WIRELESS_24G:
        return wireless_24g_mode_run();
    case KEYBOARD_MODE_BLE:
        ble_mode_dummy();
        break;
    case KEYBOARD_MODE_WIRED:
    default:
        return wired_mode_run();
    }

    return 0;
}
