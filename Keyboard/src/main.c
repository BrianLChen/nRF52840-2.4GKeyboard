/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/util.h>

#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>

#include <zephyr/logging/log.h>
#include <debounce.h>
#include <kbd_define.h>
#include <key_state.h>
#include <keymap.h>
#include <keyscan_spi.h>
LOG_MODULE_REGISTER(main, KBD_LOG_LEVEL);

static const uint8_t hid_report_desc[] = {
	0x05, 0x01,                    /* Usage Page (Generic Desktop) */
	0x09, 0x06,                    /* Usage (Keyboard) */
	0xa1, 0x01,                    /* Collection (Application) */
	0x85, KBD_HID_REPORT_ID_KEYBOARD,

	0x05, 0x07,                    /* Usage Page (Keyboard/Keypad) */
	0x75, 0x01,                    /* Report Size (1) */
	0x95, 0x08,                    /* Report Count (8) */
	0x19, 0xe0,                    /* Usage Minimum (Left Control) */
	0x29, 0xe7,                    /* Usage Maximum (Right GUI) */
	0x15, 0x00,                    /* Logical Minimum (0) */
	0x25, 0x01,                    /* Logical Maximum (1) */
	0x81, 0x02,                    /* Input (Data, Variable, Absolute) */

	0x95, KBD_HID_KEYBOARD_BITMAP_BITS,
	0x75, 0x01,                    /* Report Size (1) */
	0x15, 0x00,                    /* Logical Minimum (0) */
	0x25, 0x01,                    /* Logical Maximum (1) */
	0x05, 0x07,                    /* Usage Page (Keyboard/Keypad) */
	0x19, 0x00,                    /* Usage Minimum (Reserved) */
	0x29, KBD_HID_KEYBOARD_USAGE_MAX,
	0x81, 0x02,                    /* Input (Data, Variable, Absolute) */

	0x95, 0x05,                    /* Report Count (5) */
	0x75, 0x01,                    /* Report Size (1) */
	0x05, 0x08,                    /* Usage Page (LEDs) */
	0x19, 0x01,                    /* Usage Minimum (Num Lock) */
	0x29, 0x05,                    /* Usage Maximum (Kana) */
	0x91, 0x02,                    /* Output (Data, Variable, Absolute) */
	0x95, 0x01,                    /* Report Count (1) */
	0x75, 0x03,                    /* Report Size (3) */
	0x91, 0x03,                    /* Output (Constant, Variable, Absolute) */
	0xc0,                          /* End Collection */

	0x05, 0x0c,                    /* Usage Page (Consumer) */
	0x09, 0x01,                    /* Usage (Consumer Control) */
	0xa1, 0x01,                    /* Collection (Application) */
	0x85, KBD_HID_REPORT_ID_CONSUMER,
	0x05, 0x0c,                    /* Usage Page (Consumer) */
	0x15, 0x00,                    /* Logical Minimum (0) */
	0x25, 0x01,                    /* Logical Maximum (1) */
	0x75, 0x01,                    /* Report Size (1) */
	0x95, 0x08,                    /* Report Count (8) */
	0x09, 0xe9,                    /* Usage (Volume Increment) */
	0x09, 0xea,                    /* Usage (Volume Decrement) */
	0x09, 0xe2,                    /* Usage (Mute) */
	0x09, 0xcd,                    /* Usage (Play/Pause) */
	0x09, 0xb7,                    /* Usage (Stop) */
	0x09, 0xb5,                    /* Usage (Scan Next Track) */
	0x09, 0xb6,                    /* Usage (Scan Previous Track) */
	0x0a, 0x92, 0x01,              /* Usage (Calculator) */
	0x81, 0x02,                    /* Input (Data, Variable, Absolute) */
	0xc0,                          /* End Collection */
};

enum kb_leds_idx {
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

enum keyboard_mode {
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

struct kb_event {
	uint16_t code;
	int32_t value;
};

K_MSGQ_DEFINE(kb_msgq, sizeof(struct kb_event), 2, 1);
K_SEM_DEFINE(scan_sem, 0, 1);

UDC_STATIC_BUF_DEFINE(keyboard_report, KBD_HID_KEYBOARD_REPORT_BYTES);
UDC_STATIC_BUF_DEFINE(consumer_report, KBD_HID_CONSUMER_REPORT_BYTES);
static uint8_t keyboard_report_prev[KBD_HID_KEYBOARD_REPORT_BYTES];
static uint8_t consumer_report_prev[KBD_HID_CONSUMER_REPORT_BYTES];
static uint32_t kb_duration;
static bool kb_ready;
static uint8_t wireless_disconnect_counter;
static uint32_t wireless_keep_alive_fail_counter_ms;
static uint8_t keyboard_led_state;

static void scan_timer_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	k_sem_give(&scan_sem);
}

K_TIMER_DEFINE(scan_timer, scan_timer_expiry, NULL);

static void keyboard_report_init(void)
{
	keyboard_report[0] = KBD_HID_REPORT_ID_KEYBOARD;
	consumer_report[0] = KBD_HID_REPORT_ID_CONSUMER;
	keyboard_report_prev[0] = KBD_HID_REPORT_ID_KEYBOARD;
	consumer_report_prev[0] = KBD_HID_REPORT_ID_CONSUMER;
}

static void keyboard_leds_set_state(uint8_t led_state)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(kb_leds); i++) {
		bool enabled = (led_state & BIT(i)) != 0U;

		if (kb_leds[i].port == NULL) {
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
	if (len == 0U) {
		return 0;
	}

	if (id != 0U && id != KBD_HID_REPORT_ID_KEYBOARD) {
		LOG_WRN("Unsupported output report ID %u", id);
		return -ENOTSUP;
	}

	if (len > 1U && buf[0] == KBD_HID_REPORT_ID_KEYBOARD) {
		keyboard_led_state = buf[1];
	} else {
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

	if (usage >= 0xe0 && usage <= 0xe7) {
		mask = BIT(usage - 0xe0);
		if (pressed) {
			keyboard_report[1] |= mask;
		} else {
			keyboard_report[1] &= ~mask;
		}

		return;
	}

	if (usage > KBD_HID_KEYBOARD_USAGE_MAX) {
		LOG_WRN("Keyboard usage 0x%02x is outside NKRO bitmap", usage);
		return;
	}

	bit = usage + KBD_HID_KEYBOARD_BITMAP_BIT_OFFSET;
	mask = BIT(bit % 8U);

	if (pressed) {
		keyboard_report[bit / 8U] |= mask;
	} else {
		keyboard_report[bit / 8U] &= ~mask;
	}
}

static void keyboard_report_set_code(uint16_t code)
{
	uint8_t byte_index;
	uint8_t mask;

	if (code == KBD_KEY_NONE) {
		return;
	}

	if (code >= CONSUMER_VOLUME_INCREASE &&
	    code <= CONSUMER_AL_CALCULATOR) {
		consumer_report[1] |= BIT(code - CONSUMER_VOLUME_INCREASE);
		return;
	}

	byte_index = code / 8U;
	if (byte_index >= KBD_HID_KEYBOARD_REPORT_BYTES) {
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

	memset(&keyboard_report[1], 0, KBD_HID_KEYBOARD_REPORT_BYTES - 1);
	consumer_report[1] = 0;

	for (uint16_t key = 0; key < KBD_KEY_COUNT; key++) {
		if (!keys[key].press_status || keymap_is_fn_key(key)) {
			continue;
		}

		keyboard_report_set_code(keymap_get_code(active_layer, key));
	}
}

static bool keyboard_scan_once(void)
{
	int ret;

	LOG_DBG("Main thread: Scan");

	ret = keyscan_read();
	if (ret != 0) {
		LOG_ERR("Matrix scan failed, %d", ret);
		return false;
	}

	if (debounce_update()) {
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
	if (wireless_disconnect_counter < KBD_WIRELESS_DISCONNECT_COUNTER_THRESHOLD) {
		wireless_disconnect_counter++;
	}

	if (wireless_disconnect_counter >= KBD_WIRELESS_DISCONNECT_COUNTER_THRESHOLD) {
		keyboard_leds_clear();
	}
}

static void __maybe_unused wireless_disconnect_counter_add_keep_alive_fail(void)
{
	uint32_t now = k_uptime_get_32();

	if ((now - wireless_keep_alive_fail_counter_ms) <
	    KBD_WIRELESS_DISCONNECT_COUNTER_PERIOD_MS) {
		return;
	}

	wireless_keep_alive_fail_counter_ms = now;
	wireless_disconnect_counter_add_report_fail();
}

static void __maybe_unused wireless_sleep_prepare(void)
{
	keyboard_leds_clear();
}

static void keyboard_submit_report(const struct device *hid_dev,
				   struct usbd_context *sample_usbd,
				   const uint8_t *report,
				   size_t report_size,
				   bool wake_on_press)
{
	int ret;

	if (!kb_ready) {
		LOG_INF("USB HID device is not ready");
		return;
	}

	if (IS_ENABLED(CONFIG_SAMPLE_USBD_REMOTE_WAKEUP) &&
	    usbd_is_suspended(sample_usbd)) {
		if (wake_on_press) {
			ret = usbd_wakeup_request(sample_usbd);
			if (ret) {
				LOG_ERR("Remote wakeup error, %d", ret);
			}
		}
		return;
	}

	LOG_DBG("Main thread: USB report");

	ret = hid_device_submit_report(hid_dev, report_size, report);
	if (ret) {
		LOG_ERR("HID submit report error, %d", ret);
	}
}

static void keyboard_submit_keymap_reports(const struct device *hid_dev,
					   struct usbd_context *sample_usbd)
{
	keyboard_report_build_from_keymap();

	if (memcmp(keyboard_report_prev, keyboard_report,
		   sizeof(keyboard_report_prev)) != 0) {
		keyboard_submit_report(hid_dev, sample_usbd,
				       keyboard_report,
				       KBD_HID_KEYBOARD_REPORT_BYTES,
				       keyboard_report[1] != 0U);
		memcpy(keyboard_report_prev, keyboard_report,
		       sizeof(keyboard_report_prev));
	}

	if (memcmp(consumer_report_prev, consumer_report,
		   sizeof(consumer_report_prev)) != 0) {
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

	while (k_msgq_get(&kb_msgq, &kb_evt, K_NO_WAIT) == 0) {
		switch (kb_evt.code) {
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
	if (!gpio_is_ready_dt(gpio)) {
		LOG_ERR("Mode GPIO device %s is not ready", gpio->port->name);
		return -ENODEV;
	}

	return gpio_pin_configure_dt(gpio, GPIO_INPUT);
}

static int mode_gpio_init(void)
{
	int ret;

	ret = mode_gpio_init_one(&mode_wire);
	if (ret != 0) {
		return ret;
	}

	ret = mode_gpio_init_one(&mode_ble);
	if (ret != 0) {
		return ret;
	}

	return mode_gpio_init_one(&mode_wireless);
}

static enum keyboard_mode __maybe_unused keyboard_mode_detect(void)
{
	if (gpio_pin_get_dt(&mode_wire) > 0) {
		LOG_INF("Keyboard mode: wired");
		return KEYBOARD_MODE_WIRED;
	}

	if (gpio_pin_get_dt(&mode_ble) > 0) {
		LOG_INF("Keyboard mode: BLE");
		return KEYBOARD_MODE_BLE;
	}

	if (gpio_pin_get_dt(&mode_wireless) > 0) {
		LOG_INF("Keyboard mode: 2.4G");
		return KEYBOARD_MODE_WIRELESS_24G;
	}

	LOG_WRN("No mode switch active, defaulting to wired mode");
	return KEYBOARD_MODE_WIRED;
}

static int peripheral_init(void)
{
	int ret;

	keyboard_report_init();
	keymap_init();

	ret = keyscan_init();
	if (ret != 0) {
		return ret;
	}

	k_timer_start(&scan_timer,
		      K_USEC(KBD_SCAN_PERIOD_US),
		      K_USEC(KBD_SCAN_PERIOD_US));

	return 0;
}

static void wireless_24g_mode_dummy(void)
{
	LOG_INF("2.4G mode dummy init");

	/*
	 * Future Gazell transport rules:
	 * - TX success calls wireless_disconnect_counter_reset(), with or
	 *   without LED ACK.
	 * - Normal report TX failure calls
	 *   wireless_disconnect_counter_add_report_fail().
	 * - Keep-alive TX failure calls
	 *   wireless_disconnect_counter_add_keep_alive_fail(), which advances
	 *   the shared counter only on
	 *   KBD_WIRELESS_DISCONNECT_COUNTER_PERIOD_MS boundaries.
	 * - Disconnected state and wireless sleep must call keyboard_leds_clear()
	 *   so Caps/Num/Scroll indicators do not remain latched.
	 * - Keep-alive traffic must not reset the wireless sleep idle timer.
	 */
	while (true) {
		k_sem_take(&scan_sem, K_FOREVER);
		(void)keyboard_scan_once();
	}
}

static void ble_mode_dummy(void)
{
	LOG_INF("BLE mode dummy init");

	while (true) {
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
	if (k_msgq_put(&kb_msgq, &kb_evt, K_NO_WAIT) != 0) {
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
	if (type != HID_REPORT_TYPE_OUTPUT) {
		LOG_WRN("Unsupported Get Report type %u ID %u", type, id);
		return -ENOTSUP;
	}

	if (id != 0U && id != KBD_HID_REPORT_ID_KEYBOARD) {
		LOG_WRN("Unsupported output report ID %u", id);
		return -ENOTSUP;
	}

	if (len == 0U) {
		return -EINVAL;
	}

	buf[0] = keyboard_led_state;
	LOG_INF("Get keyboard LED state: 0x%02x", keyboard_led_state);

	return 1;
}

static int kb_set_report(const struct device *dev,
			 const uint8_t type, const uint8_t id, const uint16_t len,
			 const uint8_t *const buf)
{
	if (type != HID_REPORT_TYPE_OUTPUT) {
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
	.output_report = NULL,
};

/* doc device msg-cb start */
static void msg_cb(struct usbd_context *const usbd_ctx,
		   const struct usbd_msg *const msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (msg->type == USBD_MSG_CONFIGURATION) {
		LOG_INF("\tConfiguration value %d", msg->status);
	}

	if (usbd_can_detect_vbus(usbd_ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(usbd_ctx)) {
				LOG_ERR("Failed to enable device support");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(usbd_ctx)) {
				LOG_ERR("Failed to disable device support");
			}
		}
	}
}
/* doc device msg-cb end */

static int wired_mode_run(void)
{
	struct usbd_context *sample_usbd;
	const struct device *hid_dev;
	int ret;

	LOG_INF("Wired mode init");

	for (unsigned int i = 0; i < ARRAY_SIZE(kb_leds); i++) {
		if (kb_leds[i].port == NULL) {
			continue;
		}

		if (!gpio_is_ready_dt(&kb_leds[i])) {
			LOG_ERR("LED device %s is not ready", kb_leds[i].port->name);
			return -EIO;
		}

		ret = gpio_pin_configure_dt(&kb_leds[i], GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("Failed to configure the LED pin, %d", ret);
			return -EIO;
		}
	}

	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID Device is not ready");
		return -EIO;
	}

	ret = hid_device_register(hid_dev,
				  hid_report_desc, sizeof(hid_report_desc),
				  &kb_ops);
	if (ret != 0) {
		LOG_ERR("Failed to register HID Device, %d", ret);
		return ret;
	}

	if (IS_ENABLED(CONFIG_USBD_HID_SET_POLLING_PERIOD)) {
		ret = hid_device_set_in_polling(hid_dev, 1000);
		if (ret) {
			LOG_WRN("Failed to set IN report polling period, %d", ret);
		}

		ret = hid_device_set_out_polling(hid_dev, 1000);
		if (ret != 0 && ret != -ENOTSUP) {
			LOG_WRN("Failed to set OUT report polling period, %d", ret);
		}
	}

	sample_usbd = sample_usbd_init_device(msg_cb);
	if (sample_usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -ENODEV;
	}

	if (!usbd_can_detect_vbus(sample_usbd)) {
		/* doc device enable start */
		ret = usbd_enable(sample_usbd);
		if (ret) {
			LOG_ERR("Failed to enable device support");
			return ret;
		}
		/* doc device enable end */
	}

	LOG_INF("HID keyboard sample is initialized");

	while (true) {
		k_sem_take(&scan_sem, K_FOREVER);
		if (keyboard_scan_once()) {
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

	k_thread_priority_set(k_current_get(), KBD_MAIN_THREAD_PRIORITY);

	ret = mode_gpio_init();
	if (ret != 0) {
		return ret;
	}

	/* Force wired mode until the mode switch hardware is ready. */
	mode = KEYBOARD_MODE_WIRED;

	ret = peripheral_init();
	if (ret != 0) {
		return ret;
	}

	switch (mode) {
	case KEYBOARD_MODE_WIRELESS_24G:
		wireless_24g_mode_dummy();
		break;
	case KEYBOARD_MODE_BLE:
		ble_mode_dummy();
		break;
	case KEYBOARD_MODE_WIRED:
	default:
		return wired_mode_run();
	}

	return 0;
}
