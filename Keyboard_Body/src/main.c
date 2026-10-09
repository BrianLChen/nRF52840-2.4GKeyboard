/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/usb/usb_buf.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usbd.h>

#include <gzll_keyboard.h>
#include <zephyr/sys/poweroff.h>

#include <debounce.h>
#include <hid_descriptor.h>
#include <hid_report.h>
#include <ble_keyboard.h>
#include <radio_mode.h>
#include <kbd_define.h>
#include <kbd_macro.h>
#include <kbd_report_pipeline.h>
#include <key_state.h>
#include <keymap.h>
#include <keyscan_spi.h>
#include <knob.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, KBD_LOG_LEVEL);

BUILD_ASSERT(KBD_USB_SCAN_PERIOD_US > 0);
BUILD_ASSERT(KBD_GZLL_SCAN_PERIOD_US > 0);
BUILD_ASSERT(KBD_USB_REPORT_PERIOD_US > 0);
BUILD_ASSERT(KBD_GZLL_REPORT_PERIOD_US > 0);

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

/* Selected once by the switch work; changing modes reboots the device. */
static enum keyboard_mode active_mode;

#define KBD_USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec kbd_power =
    GPIO_DT_SPEC_GET(KBD_USER_NODE, kbd_power_gpios);
static const struct gpio_dt_spec kbd_wake =
    GPIO_DT_SPEC_GET(KBD_USER_NODE, kbd_wake_gpios);
static struct gpio_callback wake_callback;
static atomic_t wake_seen;

static void keyboard_wake_callback(const struct device *port,
                                   struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    /* Remember even a short press while the hardware changes to wake mode. */
    atomic_set(&wake_seen, 1);
}

static const struct gpio_dt_spec mode_wire =
    GPIO_DT_SPEC_GET(KBD_USER_NODE, mode_wire_gpios);
static const struct gpio_dt_spec mode_ble =
    GPIO_DT_SPEC_GET(KBD_USER_NODE, mode_ble_gpios);
static const struct gpio_dt_spec mode_wireless =
    GPIO_DT_SPEC_GET(KBD_USER_NODE, mode_wireless_gpios);

K_SEM_DEFINE(scan_sem, 0, 1);

UDC_STATIC_BUF_DEFINE(keyboard_report, KBD_HID_KEYBOARD_REPORT_BYTES);
UDC_STATIC_BUF_DEFINE(consumer_report, KBD_HID_CONSUMER_REPORT_BYTES);
/* Owned by USB until input_report_done(); scan reports remain writable. */
UDC_STATIC_BUF_DEFINE(usb_tx_report, KBD_HID_KEYBOARD_REPORT_BYTES);
static atomic_t usb_tx_busy;
static atomic_t usb_input_epoch;
static uint8_t keyboard_report_prev[KBD_HID_KEYBOARD_REPORT_BYTES];
static uint8_t consumer_report_prev[KBD_HID_CONSUMER_REPORT_BYTES];

static uint32_t kb_duration;
static atomic_t kb_ready;
/* USB callbacks cache host state; the scan thread owns the LED GPIOs. */
static atomic_t usb_led_state;
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

static uint32_t scan_period_us;
static bool scan_succeeded;

/* Only the scan thread changes timer/debounce units. Bluetooth callbacks
 * publish their requested period through kbd_ble_scan_period_us().
 */
static void keyboard_scan_set_period(uint32_t period_us)
{
    if (period_us == scan_period_us)
    {
        return;
    }
    k_timer_stop(&scan_timer);
    k_sem_reset(&scan_sem);
    debounce_set_scan_period_us(period_us);
    scan_period_us = period_us;
    k_timer_start(&scan_timer, K_USEC(period_us), K_USEC(period_us));
    LOG_INF("Matrix scan period: %u us", period_us);
}

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
    kbd_report_physical_reset(keyboard_report, consumer_report);
}

/* All transports share the same active-low status LED GPIOs. */
static int keyboard_leds_init(void)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(kb_leds); i++)
    {
        if (kb_leds[i].port == NULL)
        {
            continue;
        }
        if (!gpio_is_ready_dt(&kb_leds[i]))
        {
            LOG_ERR("LED device %s is not ready", kb_leds[i].port->name);
            return -ENODEV;
        }
        int ret = gpio_pin_configure_dt(&kb_leds[i], GPIO_OUTPUT_INACTIVE);
        if (ret != 0)
        {
            LOG_ERR("Failed to configure LED %u, %d", i, ret);
            return ret;
        }
    }
    return 0;
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
    uint8_t led_state;

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
        led_state = buf[1];
    }
    else
    {
        led_state = buf[0];
    }

    led_state &= BIT(KB_LED_NUMLOCK) |
                 BIT(KB_LED_CAPSLOCK) |
                 BIT(KB_LED_SCROLLLOCK);

    atomic_set(&usb_led_state, led_state);
    LOG_INF("Keyboard LED state: 0x%02x", led_state);

    return 0;
}

static void keyboard_report_load_physical(void)
{
    kbd_report_physical_get(keyboard_report, consumer_report);
}

/* Session changes discard old physical input, without moving report slots. */
static void keyboard_report_resync(void)
{
    uint8_t physical_keyboard[KBD_HID_KEYBOARD_REPORT_BYTES];
    uint8_t physical_consumer[KBD_HID_CONSUMER_REPORT_BYTES];

    hid_report_build_nkro(physical_keyboard, physical_consumer);
    kbd_report_physical_reset(physical_keyboard, physical_consumer);
}

static bool keyboard_report_slot_due(uint32_t period_us)
{
    return kbd_report_due(k_ticks_to_us_floor64(k_uptime_ticks()), period_us);
}

static void keyboard_handle_actions(uint8_t actions)
{
    /* Cancel/session actions win over starts in the same scan. */
    if (actions & (KBD_ACTION_MACRO_CANCEL | KBD_ACTION_PAIRING |
                   KBD_ACTION_DEVICE_SWITCH))
    {
        kbd_macro_cancel();
    }
    else
    {
        for (uint8_t id = 0; id < KBD_MACRO_SLOT_COUNT; id++)
        {
            if (actions & (KBD_ACTION_MACRO_0 << id))
            {
                bool started = kbd_macro_start(id, (uint64_t)k_uptime_get());
                LOG_INF("Macro %u %s", id, started ? "started" : "ignored");
                ARG_UNUSED(started);
                /* One start per scan, no queueing of subsequent triggers. */
                break;
            }
        }
    }
    /* No scanning occurs in BLE mode until kbd_ble_init() has succeeded. */
    if (active_mode == KEYBOARD_MODE_BLE)
    {
        kbd_ble_handle_actions(actions);
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
    scan_succeeded = ret == 0;
    if (ret != 0)
    {
        LOG_ERR("Matrix scan failed, %d", ret);
        return false;
    }

    if (debounce_update())
    {
        keyboard_handle_actions(keymap_update_actions());
        uint8_t physical_keyboard[KBD_HID_KEYBOARD_REPORT_BYTES];
        uint8_t physical_consumer[KBD_HID_CONSUMER_REPORT_BYTES];
        hid_report_build_nkro(physical_keyboard, physical_consumer);
        if (!kbd_report_capture(physical_keyboard, physical_consumer))
        {
            LOG_WRN("Physical report queue full; retaining latest state");
        }
        LOG_INF("Debounced key state changed");
        keyscan_log_changes();
        return true;
    }

    return false;
}

/* A report slot consumes one physical snapshot. Full transport queues leave
 * it pending for the next slot; macro/knob progression follows these slots. */
static void wireless_submit_keymap_reports(void)
{
    int ret = 0;

    keyboard_report_load_physical();
    kbd_macro_apply(keyboard_report, (uint64_t)k_uptime_get(),
                    kbd_gzll_tx_idle(), 0);
    consumer_report[1] = knob_report_apply(consumer_report[1]);

    if (memcmp(keyboard_report_prev, keyboard_report,
               sizeof(keyboard_report_prev)) != 0)
    {
        ret = kbd_gzll_submit(keyboard_report,
                                          KBD_HID_KEYBOARD_REPORT_BYTES);
        if (ret == 0)
        {
            memcpy(keyboard_report_prev, keyboard_report,
                   sizeof(keyboard_report_prev));
        }
    }

    if (ret == 0)
    {
        /* Includes an unchanged union, e.g. a physically held macro key. */
        kbd_macro_report_accepted();
    }

    if (memcmp(consumer_report_prev, consumer_report,
               sizeof(consumer_report_prev)) != 0)
    {
        ret = kbd_gzll_submit(consumer_report,
                                          KBD_HID_CONSUMER_REPORT_BYTES);
        if (ret == 0)
        {
            memcpy(consumer_report_prev, consumer_report,
                   sizeof(consumer_report_prev));
            knob_report_sent(consumer_report[1]);
        }
    }
    if (!memcmp(keyboard_report_prev, keyboard_report, sizeof(keyboard_report_prev)) &&
        !memcmp(consumer_report_prev, consumer_report, sizeof(consumer_report_prev)))
    {
        kbd_report_physical_accepted();
    }
}

static int keyboard_submit_report(const struct device *hid_dev,
                                   struct usbd_context *sample_usbd,
                                   const uint8_t *report,
                                   size_t report_size,
                                   bool wake_on_press)
{
    int ret;

    if (!atomic_get(&kb_ready))
    {
        return -EAGAIN;
    }

    /*
     * If the host suspended USB, only a key press should request remote wake.
     * Reports remain pending until USB resumes.
     */
    if (usbd_is_suspended(sample_usbd))
    {
        if (IS_ENABLED(CONFIG_SAMPLE_USBD_REMOTE_WAKEUP) && wake_on_press)
        {
            ret = usbd_wakeup_request(sample_usbd);
            if (ret)
            {
                LOG_ERR("Remote wakeup error, %d", ret);
            }
        }
        return -EAGAIN;
    }

    LOG_DBG("Main thread: USB report");

    if (atomic_get(&usb_tx_busy))
    {
        return -EAGAIN;
    }
    memcpy(usb_tx_report, report, report_size);
    /* Completion may run before submit returns. */
    atomic_set(&usb_tx_busy, 1);
    ret = hid_device_submit_report(hid_dev, report_size, usb_tx_report);
    if (ret)
    {
        atomic_clear(&usb_tx_busy);
        LOG_ERR("HID submit report error, %d", ret);
    }
    return ret;
}

static void keyboard_submit_keymap_reports(const struct device *hid_dev,
                                           struct usbd_context *sample_usbd)
{
    bool wake_on_press = false;
    int ret = 0;
    atomic_val_t epoch = atomic_get(&usb_input_epoch);

    keyboard_report_load_physical();
    kbd_macro_apply(keyboard_report, (uint64_t)k_uptime_get(),
                    !atomic_get(&usb_tx_busy), 0);
    consumer_report[1] = knob_report_apply(consumer_report[1]);

    for (size_t i = 1; i < sizeof(keyboard_report); i++)
    {
        wake_on_press |= keyboard_report[i] != 0U;
    }

    /*
     * Submit only changed reports. Keyboard and consumer reports are tracked
     * separately, so a media-key change does not resend the full NKRO report.
     * Advance the sent snapshot only on success; retry on subsequent slots.
     */
    if (memcmp(keyboard_report_prev, keyboard_report,
               sizeof(keyboard_report_prev)) != 0)
    {
        ret = keyboard_submit_report(hid_dev, sample_usbd,
                               keyboard_report,
                               KBD_HID_KEYBOARD_REPORT_BYTES,
                               wake_on_press);
        if (ret == 0)
        {
            memcpy(keyboard_report_prev, keyboard_report,
                   sizeof(keyboard_report_prev));
        }
    }

    if (ret == 0 && atomic_get(&kb_ready) && !usbd_is_suspended(sample_usbd) &&
        epoch == atomic_get(&usb_input_epoch))
    {
        kbd_macro_report_accepted();
    }

    if (memcmp(consumer_report_prev, consumer_report,
               sizeof(consumer_report_prev)) != 0)
    {
        ret = keyboard_submit_report(hid_dev, sample_usbd,
                               consumer_report,
                               KBD_HID_CONSUMER_REPORT_BYTES,
                               consumer_report[1] != 0U);
        if (ret == 0)
        {
            memcpy(consumer_report_prev, consumer_report,
                   sizeof(consumer_report_prev));
            knob_report_sent(consumer_report[1]);
        }
    }
    if (atomic_get(&kb_ready) && !usbd_is_suspended(sample_usbd) &&
        epoch == atomic_get(&usb_input_epoch) &&
        !memcmp(keyboard_report_prev, keyboard_report, sizeof(keyboard_report_prev)) &&
        !memcmp(consumer_report_prev, consumer_report, sizeof(consumer_report_prev)))
    {
        kbd_report_physical_accepted();
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

#define MODE_SWITCH_DEBOUNCE_MS KBD_MODE_SWITCH_DEBOUNCE_MS

static bool mode_selected;
static uint32_t mode_last_edge_ms;
K_SEM_DEFINE(mode_selected_sem, 0, 1);

static const struct gpio_dt_spec *const mode_inputs[] = {
    &mode_wire, &mode_ble, &mode_wireless,
};
static struct gpio_callback mode_callbacks[ARRAY_SIZE(mode_inputs)];

/* gpio_pin_get_dt() returns logical 1 for the active-low selected contact. */
static int keyboard_mode_detect(void)
{
    int wired = gpio_pin_get_dt(&mode_wire);
    int ble = gpio_pin_get_dt(&mode_ble);
    int wireless = gpio_pin_get_dt(&mode_wireless);

    if (wired < 0 || ble < 0 || wireless < 0)
    {
        return wired < 0 ? wired : (ble < 0 ? ble : wireless);
    }

    /* Ignore open contacts and overlapping contacts while the switch moves. */
    if (wired + ble + wireless != 1)
    {
        return -EAGAIN;
    }

    return wired ? KEYBOARD_MODE_WIRED :
           (ble ? KEYBOARD_MODE_BLE : KEYBOARD_MODE_WIRELESS_24G);
}

static void mode_switch_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(mode_switch_work, mode_switch_work_handler);

static void mode_switch_gpio_callback(const struct device *port,
                                     struct gpio_callback *cb,
                                     gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    mode_last_edge_ms = k_uptime_get_32();
    /* No wait or reboot in interrupt context; every edge restarts debounce. */
    k_work_reschedule(&mode_switch_work, K_MSEC(MODE_SWITCH_DEBOUNCE_MS));
}

static void mode_switch_work_handler(struct k_work *work)
{
    unsigned int key;
    uint32_t elapsed;
    int mode;

    ARG_UNUSED(work);

    /* A queued handler may run after a newer edge. Recheck its age and read
     * the three on-chip GPIOs without a callback changing the timestamp.
     */
    key = irq_lock();
    elapsed = (uint32_t)(k_uptime_get_32() - mode_last_edge_ms);
    if (elapsed < MODE_SWITCH_DEBOUNCE_MS)
    {
        k_work_reschedule(&mode_switch_work,
                          K_MSEC(MODE_SWITCH_DEBOUNCE_MS - elapsed));
        irq_unlock(key);
        return;
    }
    mode = keyboard_mode_detect();
    irq_unlock(key);

    if (mode < 0)
    {
        if (mode != -EAGAIN)
        {
            LOG_ERR("Failed to read mode switch, %d", mode);
        }
        /* No valid selection: keep the current mode and await another edge. */
        return;
    }

    if (!mode_selected)
    {
        active_mode = (enum keyboard_mode)mode;
        mode_selected = true;
        k_sem_give(&mode_selected_sem);
    }
    else if (mode != (int)active_mode)
    {
        /* Reset releases the previous USB/radio resources before mode init. */
        LOG_INF("Mode switch %u -> %u, rebooting",
                (unsigned int)active_mode, (unsigned int)mode);
        sys_reboot(SYS_REBOOT_COLD);
        return;
    }

}

static int mode_switch_start(void)
{
    int ret;
    size_t i;

    for (i = 0; i < ARRAY_SIZE(mode_inputs); i++)
    {
        gpio_init_callback(&mode_callbacks[i], mode_switch_gpio_callback,
                           BIT(mode_inputs[i]->pin));
        ret = gpio_add_callback(mode_inputs[i]->port, &mode_callbacks[i]);
        if (ret != 0)
        {
            break;
        }
        ret = gpio_pin_interrupt_configure_dt(mode_inputs[i], GPIO_INT_EDGE_BOTH);
        if (ret != 0)
        {
            gpio_remove_callback(mode_inputs[i]->port, &mode_callbacks[i]);
            break;
        }
    }

    if (i != ARRAY_SIZE(mode_inputs))
    {
        while (i > 0)
        {
            i--;
            gpio_pin_interrupt_configure_dt(mode_inputs[i], GPIO_INT_DISABLE);
            gpio_remove_callback(mode_inputs[i]->port, &mode_callbacks[i]);
        }
        k_work_cancel_delayable(&mode_switch_work);
        return ret;
    }

    /* Initial evaluation also works when the switch never produces an edge. */
    unsigned int key = irq_lock();
    mode_last_edge_ms = k_uptime_get_32();
    k_work_reschedule(&mode_switch_work, K_MSEC(MODE_SWITCH_DEBOUNCE_MS));
    irq_unlock(key);
    return 0;
}

static int peripheral_init(void)
{
    int ret;

    ret = keyboard_leds_init();
    if (ret != 0)
    {
        return ret;
    }

    /* P1.05 high turns Q1 on and grounds the key common for normal scanning. */
    if (!gpio_is_ready_dt(&kbd_power))
    {
        LOG_ERR("Keyboard power GPIO is not ready");
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&kbd_power, GPIO_OUTPUT_ACTIVE);
    if (ret != 0)
    {
        LOG_ERR("Failed to enable keyboard power, %d", ret);
        return ret;
    }
    k_msleep(100);

    /*
     * Shared initialization for wired, BLE, and 2.4G modes. Mode-specific USB
     * or radio setup happens later, but all modes need reports, keymap, SPI
     * matrix scan, debounce state, and the periodic scan timer.
     */
    keyboard_report_init();
    keymap_init();
    kbd_macro_set_enabled(false);

    if (active_mode != KEYBOARD_MODE_WIRED)
    {
        if (!gpio_is_ready_dt(&kbd_wake))
        {
            return -ENODEV;
        }
        ret = gpio_pin_configure_dt(&kbd_wake, GPIO_INPUT);
        if (ret != 0)
        {
            return ret;
        }
        ret = gpio_pin_interrupt_configure_dt(&kbd_wake, GPIO_INT_DISABLE);
        if (ret != 0)
        {
            return ret;
        }
        gpio_init_callback(&wake_callback, keyboard_wake_callback, BIT(kbd_wake.pin));
        ret = gpio_add_callback(kbd_wake.port, &wake_callback);
        if (ret != 0)
        {
            return ret;
        }
    }
    ret = knob_init();
    if (ret != 0)
    {
        LOG_ERR("Knob device is not ready, %d", ret);
        return ret;
    }

    ret = keyscan_init();
    if (ret != 0)
    {
        return ret;
    }

    keyboard_scan_set_period(active_mode == KEYBOARD_MODE_BLE ?
                             kbd_ble_scan_period_us() :
                             (active_mode == KEYBOARD_MODE_WIRED ?
                              KBD_USB_SCAN_PERIOD_US : KBD_GZLL_SCAN_PERIOD_US));

    return 0;
}

static bool keyboard_any_key_down(void)
{
    struct key_state *keys = key_state_buffer_get();
    for (size_t i = 0; i < KBD_KEY_COUNT; ++i)
    {
        /* Include raw state so a key still being debounced prevents sleep. */
        if (keys[i].press_status || keys[i].scan_status)
        {
            return true;
        }
    }
    return false;
}

static void wireless_sleep_prepare(void)
{
    bool ble = active_mode == KEYBOARD_MODE_BLE;
    bool interrupts_locked = false;
    unsigned int irq_key = 0;
    uint32_t knob_epoch = knob_activity_epoch();
    int ret = gpio_pin_get_dt(&kbd_wake);
    if (ret != 0)
    {
        LOG_WRN("Deferring sleep: wake input active or unreadable (%d)", ret);
        return;
    }
    if (keyboard_any_key_down())
    {
        return;
    }

    /* Shutdown runs in the scan thread, never an IRQ or system work item. */
    ret = ble ? kbd_ble_stop() : kbd_gzll_stop();
    if (ble && ret == -EBUSY)
    {
        /* Pairing/slot state changed since the idle check; scanning continues. */
        return;
    }
    if (ret != 0)
    {
        goto reboot;
    }
    k_timer_stop(&scan_timer);
    /* A disconnect may take time. Recheck physical input before removing SPI. */
    ret = keyscan_read();
    if (ret != 0)
    {
        goto reboot;
    }
    if (keyboard_any_key_down() || knob_activity_epoch() != knob_epoch)
    {
        ret = -EAGAIN;
        goto reboot;
    }
    keyboard_leds_clear();
    ret = knob_suspend();
    if (ret == 0)
    {
        ret = keyscan_suspend();
    }
    if (ret == 0)
    {
        atomic_clear(&wake_seen);
        ret = gpio_pin_interrupt_configure_dt(&kbd_wake, GPIO_INT_EDGE_TO_ACTIVE);
    }
    if (ret == 0)
    {
        /* Preserve the original PCB sleep state: external R1 pulls Q1 off.
         * R3 is populated as 100 kohm, not the schematic 0 ohm. This
         * releases the key common rail; it does not switch VCC33 off.
         */
        ret = gpio_pin_configure(kbd_power.port, kbd_power.pin, GPIO_INPUT);
    }
    if (ret != 0)
    {
        goto reboot;
    }
    k_msleep(5);
    ret = gpio_pin_get_dt(&kbd_wake);
    if (ret != 0 || atomic_get(&wake_seen))
    {
        ret = ret < 0 ? ret : -EAGAIN;
        goto reboot;
    }

    /* Preserve mode-switch wake from OFF, including BLE <-> 2.4G.
     * The selected (already low) contact must not immediately wake us. */
    struct k_work_sync sync;
    const struct gpio_dt_spec *selected_mode = ble ? &mode_ble : &mode_wireless;
    for (size_t i = 0; i < ARRAY_SIZE(mode_inputs); ++i)
    {
        gpio_pin_interrupt_configure_dt(mode_inputs[i], GPIO_INT_DISABLE);
        gpio_remove_callback(mode_inputs[i]->port, &mode_callbacks[i]);
    }
    k_work_cancel_delayable_sync(&mode_switch_work, &sync);
    LOG_INF("[power] %s entering System OFF; press a key or move the mode switch\n",
           ble ? "BLE" : "Gazell");
    (void)hwinfo_clear_reset_cause();
    /* nRF GPIO level interrupts retrigger while active. Keep this final,
     * nonblocking GPIO/SENSE-to-OFF sequence atomic, including mode contacts.
     * A later level assertion wakes System OFF through hardware DETECT.
     */
    irq_key = irq_lock();
    interrupts_locked = true;
    for (size_t i = 0; i < ARRAY_SIZE(mode_inputs); ++i)
    {
        ret = gpio_pin_interrupt_configure_dt(mode_inputs[i],
                    mode_inputs[i] == selected_mode ?
                    GPIO_INT_LEVEL_INACTIVE : GPIO_INT_LEVEL_ACTIVE);
        if (ret != 0)
        {
            goto reboot;
        }
        ret = gpio_pin_get_dt(mode_inputs[i]);
        if (ret < 0 || ret != (mode_inputs[i] == selected_mode))
        {
            ret = ret < 0 ? ret : -EAGAIN;
            goto reboot;
        }
    }
    ret = gpio_pin_interrupt_configure_dt(&kbd_wake, GPIO_INT_LEVEL_ACTIVE);
    if (ret != 0)
    {
        goto reboot;
    }
    ret = gpio_pin_get_dt(&kbd_wake);
    if (ret != 0 || atomic_get(&wake_seen))
    {
        ret = ret < 0 ? ret : -EAGAIN;
        goto reboot;
    }
    sys_poweroff(); /* nRF52840 wake starts a fresh boot. */
    return;

reboot:
    if (interrupts_locked)
    {
        /* Avoid an asserted level IRQ starving the reset/error path. */
        (void)gpio_pin_interrupt_configure_dt(&kbd_wake, GPIO_INT_DISABLE);
        for (size_t i = 0; i < ARRAY_SIZE(mode_inputs); ++i)
        {
            (void)gpio_pin_interrupt_configure_dt(mode_inputs[i], GPIO_INT_DISABLE);
        }
        irq_unlock(irq_key);
    }
    /* Once the radio is stopped there is no in-place resume. A fresh boot
     * restores GPIOs, SPI, encoder, timers, and the selected radio together.
     */
    LOG_INF("[power] Sleep aborted (%d); rebooting to restore input\n", ret);
    sys_reboot(SYS_REBOOT_COLD);
}

static int wireless_24g_mode_run(void)
{
    int ret;

    LOG_INF("2.4G mode init");

    ret = kbd_gzll_init();
    if (ret != 0)
    {
        return ret;
    }

    uint32_t last_activity_ms = k_uptime_get_32();
    uint32_t knob_epoch = knob_activity_epoch();
    bool previous_connected = false;

    while (true)
    {
        k_sem_take(&scan_sem, K_FOREVER);
        kbd_gzll_process();
        bool connected = kbd_gzll_connected();
        if (!connected && previous_connected && kbd_macro_active())
        {
            /* Old composite snapshots must not replay a macro on reconnect. */
            kbd_gzll_discard_pending();
            memset(keyboard_report_prev, 0xff, sizeof(keyboard_report_prev));
            memset(consumer_report_prev, 0xff, sizeof(consumer_report_prev));
        }
        kbd_macro_set_enabled(connected);
        if (connected != previous_connected)
        {
            LOG_INF("Gazell %s", connected ? "connected" : "disconnected");
            previous_connected = connected;
        }
        bool changed = keyboard_scan_once();
        uint32_t current_knob_epoch = knob_activity_epoch();
        if (!scan_succeeded || changed || keyboard_any_key_down() || current_knob_epoch != knob_epoch ||
            kbd_macro_active())
        {
            last_activity_ms = k_uptime_get_32();
        }
        knob_epoch = current_knob_epoch;

        if (keyboard_report_slot_due(KBD_GZLL_REPORT_PERIOD_US))
        {
            wireless_submit_keymap_reports();
        }
        kbd_gzll_process();
        uint8_t led_state = kbd_gzll_led_state();
        if (led_state != keyboard_led_state)
        {
            keyboard_led_state = led_state;
            keyboard_leds_set_state(led_state);
        }
        /* Radio traffic must never extend the user-input idle deadline. */
        if ((uint32_t)(k_uptime_get_32() - last_activity_ms) >=
            KBD_WIRELESS_SLEEP_TIMEOUT_MS)
        {
            wireless_sleep_prepare();
            /* An asserted wake line defers sleep; try again after idle time. */
            last_activity_ms = k_uptime_get_32();
        }
    }

    return 0;
}

static int ble_mode_run(void)
{
    uint8_t six_kro[KBD_HID_6KRO_REPORT_BYTES];
    bool wait_for_release = true;
    int ret = kbd_ble_init();

    if (ret != 0)
    {
        return ret;
    }
    uint32_t input_epoch = kbd_ble_input_epoch();
    uint32_t last_activity_ms = k_uptime_get_32();
    uint32_t knob_epoch = knob_activity_epoch();
    LOG_INF("[ble] Idle System OFF timeout: %u ms (pairing/held keys postpone sleep)\n",
           CONFIG_KBD_BLE_SLEEP_TIMEOUT_MS);
    LOG_INF("BLE mode initialized");

    /* Same scan/keymap pipeline as USB and Gazell; only BLE uses 6-KRO.
     * The transport copies reports into its queue and sends in work context.
     */
    while (true)
    {
        keyboard_scan_set_period(kbd_ble_scan_period_us());
        k_sem_take(&scan_sem, K_FOREVER);
        uint32_t epoch = kbd_ble_input_epoch();
        if (epoch != input_epoch)
        {
            input_epoch = epoch;
            wait_for_release = true;
            knob_reset();
            kbd_macro_set_enabled(false);
            keyboard_report_resync();
        }
        bool keyboard_ready = kbd_ble_keyboard_ready();
        kbd_macro_set_enabled(keyboard_ready && !wait_for_release);
        bool changed = keyboard_scan_once();

        uint32_t current_knob_epoch = knob_activity_epoch();
        uint32_t now = k_uptime_get_32();
        if (!scan_succeeded || changed || keyboard_any_key_down() || current_knob_epoch != knob_epoch ||
            !kbd_ble_can_sleep() || kbd_ble_input_epoch() != input_epoch ||
            kbd_macro_active())
        {
            last_activity_ms = now;
        }
        knob_epoch = current_knob_epoch;
        /* This check also runs while disconnected or waiting for HID CCC. */
        if ((uint32_t)(now - last_activity_ms) >= CONFIG_KBD_BLE_SLEEP_TIMEOUT_MS)
        {
            wireless_sleep_prepare();
            last_activity_ms = k_uptime_get_32();
            continue;
        }

        uint8_t led_state = kbd_ble_led_state();
        if (led_state != keyboard_led_state)
        {
            keyboard_led_state = led_state;
            keyboard_leds_set_state(led_state);
        }

        epoch = kbd_ble_input_epoch();
        if (epoch != input_epoch)
        {
            input_epoch = epoch;
            wait_for_release = true;
            knob_reset();
            kbd_macro_set_enabled(false);
            keyboard_report_resync();
        }
        if (!kbd_ble_keyboard_ready())
        {
            /* Do not replay disconnected typing or knob motion on reconnect. */
            wait_for_release = true;
            knob_reset();
            kbd_macro_set_enabled(false);
            keyboard_report_resync();
            continue;
        }

        if (wait_for_release)
        {
            /* Sample only current held state during reconnect admission. */
            keyboard_report_resync();
            /* Release admission follows scan cadence, so a fresh short press
             * need not wait for a slow BLE report slot to become eligible.
             */
            wait_for_release = keyboard_any_key_down();
        }
        if (!keyboard_report_slot_due(kbd_ble_report_period_us()))
        {
            continue;
        }
        keyboard_report_load_physical();
        if (wait_for_release)
        {
            /* Include consumed Fn/macro keys: holding a trigger across a
             * reconnect must not start playback until a fresh physical press.
             */
            bool held = keyboard_any_key_down();
            for (size_t i = 1; i < sizeof(keyboard_report); i++)
            {
                held |= keyboard_report[i] != 0;
            }
            wait_for_release = held;
            memset(&keyboard_report[1], 0, sizeof(keyboard_report) - 1);
            consumer_report[1] = 0;
            knob_reset();
            kbd_macro_set_enabled(false);
        }
        else
        {
            kbd_macro_apply(keyboard_report, (uint64_t)k_uptime_get(),
                            kbd_ble_tx_idle(input_epoch), KBD_HID_6KRO_KEY_COUNT);
        }

        hid_report_nkro_to_6kro(keyboard_report, six_kro);
        ret = kbd_ble_submit_keyboard(six_kro, input_epoch);
        bool keyboard_accepted = ret == 0;
        if (ret == 0 && input_epoch == kbd_ble_input_epoch())
        {
            kbd_macro_report_accepted();
        }

        bool consumer_accepted = true;
        if (kbd_ble_consumer_ready())
        {
            if (!wait_for_release)
            {
                consumer_report[1] = knob_report_apply(consumer_report[1]);
            }
            ret = kbd_ble_submit_consumer(consumer_report[1], input_epoch);
            consumer_accepted = ret == 0;
            if (ret == 0)
            {
                knob_report_sent(consumer_report[1]);
            }
        }
        else
        {
            /* Boot Protocol has no consumer report. */
            knob_reset();
        }
        if (keyboard_accepted && consumer_accepted && input_epoch == kbd_ble_input_epoch())
        {
            kbd_report_physical_accepted();
        }
    }

    return 0;
}

static void kb_iface_ready(const struct device *dev, const bool ready)
{
    LOG_INF("HID device %s interface is %s",
            dev->name, ready ? "ready" : "not ready");
    atomic_set(&kb_ready, ready);
    if (!ready)
    {
        atomic_inc(&usb_input_epoch);
    }
}

static void kb_input_report_done(const struct device *dev, const uint8_t *const report)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(report);
    atomic_clear(&usb_tx_busy);
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
    buf[0] = (uint8_t)atomic_get(&usb_led_state);
    LOG_INF("Get keyboard LED state: 0x%02x", buf[0]);

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
    .input_report_done = kb_input_report_done,
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

    if (msg->type == USBD_MSG_SUSPEND || msg->type == USBD_MSG_RESET ||
        msg->type == USBD_MSG_VBUS_REMOVED)
    {
        atomic_inc(&usb_input_epoch);
        /* Discard old LEDs on suspend as well as reset/disconnect.
         * Resume leaves them off until the host sends a new SET_REPORT.
         */
        atomic_clear(&usb_led_state);
    }

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
     *   3. merge knob pulses with matrix consumer state
     *   4. on report slots, submit ordered states and retry failed submissions
     */
    atomic_val_t input_epoch = -1;
    while (true)
    {
        k_sem_take(&scan_sem, K_FOREVER);
        atomic_val_t epoch = atomic_get(&usb_input_epoch);
        if (epoch != input_epoch)
        {
            input_epoch = epoch;
            kbd_macro_set_enabled(false);
            keyboard_report_resync();
            memset(keyboard_report_prev, 0xff, sizeof(keyboard_report_prev));
            memset(consumer_report_prev, 0xff, sizeof(consumer_report_prev));
        }
        bool usb_active = atomic_get(&kb_ready) && !usbd_is_suspended(sample_usbd);
        /* Keep indicators off while USB is inactive. The suspend callback
         * clears the cached report; resume does not restore the old LEDs.
         * Only this thread writes LED GPIOs.
         */
        uint8_t led_state = usb_active ? (uint8_t)atomic_get(&usb_led_state) : 0U;
        if (led_state != keyboard_led_state)
        {
            keyboard_led_state = led_state;
            keyboard_leds_set_state(led_state);
        }
        kbd_macro_set_enabled(usb_active);
        (void)keyboard_scan_once();
        if (keyboard_report_slot_due(KBD_USB_REPORT_PERIOD_US))
        {
            keyboard_submit_keymap_reports(hid_dev, sample_usbd);
        }
    }

    return 0;
}

int main(void)
{
    enum keyboard_mode mode;
    int ret;

    uint32_t reset_cause;
    if (hwinfo_get_reset_cause(&reset_cause) == 0)
    {
        LOG_INF("[power] Boot reset cause: 0x%08x%s\n", reset_cause,
               reset_cause & RESET_LOW_POWER_WAKE ? " (woke from System OFF)" : "");
        (void)hwinfo_clear_reset_cause();
    }

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

    ret = mode_switch_start();
    if (ret != 0)
    {
        LOG_ERR("Failed to enable mode switch interrupts, %d", ret);
        return ret;
    }

    /* Sleep until a unique contact has been stable for 50 ms. */
    k_sem_take(&mode_selected_sem, K_FOREVER);
    mode = active_mode;
    LOG_INF("Selected mode: %s", mode == KEYBOARD_MODE_WIRED ? "wired" :
            (mode == KEYBOARD_MODE_BLE ? "BLE" : "2.4G"));

    if (mode == KEYBOARD_MODE_WIRELESS_24G)
    {
        /* Gazell needs exclusive radio ownership. USB must keep MPSL alive:
         * CONFIG_CLOCK_CONTROL_MPSL routes its HFXO requests through MPSL,
         * even though wired mode never calls bt_enable().
         */
        ret = kbd_radio_prepare_non_ble();
        if (ret != 0)
        {
            LOG_ERR("Cannot release BLE radio resources, %d", ret);
            return ret;
        }
    }

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
        return ble_mode_run();
    case KEYBOARD_MODE_WIRED:
    default:
        return wired_mode_run();
    }

    return 0;
}
