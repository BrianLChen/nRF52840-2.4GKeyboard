/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/usb/usb_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/logging/log.h>
#include <gzll_glue.h>
#include <nrf_gzll.h>
#include <hid_descriptor.h>
#include <kbd_define.h>

LOG_MODULE_REGISTER(main, KBD_LOG_LEVEL);

enum { KEYBOARD, CONSUMER, REPORT_COUNT };
enum { EVENT_RESYNC, EVENT_SUSPEND, EVENT_VBUS_CHANGED, EVENT_VBUS_LOST };

struct dongle_report {
	uint8_t data[KBD_GZLL_TX_PAYLOAD_BYTES];
	uint8_t len;
};

static uint8_t channels[] = KBD_GZLL_CHANNEL_TABLE;
BUILD_ASSERT(ARRAY_SIZE(channels) == KBD_GZLL_CHANNEL_COUNT);
BUILD_ASSERT(KBD_GZLL_TX_PAYLOAD_BYTES <= NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH);
BUILD_ASSERT(KBD_GZLL_TX_PAYLOAD_BYTES >= KBD_HID_KEYBOARD_REPORT_BYTES);
BUILD_ASSERT(KBD_GZLL_TX_PAYLOAD_BYTES >= KBD_HID_CONSUMER_REPORT_BYTES);
BUILD_ASSERT(KBD_GZLL_TX_PAYLOAD_BYTES >= KBD_HID_6KRO_REPORT_BYTES);
BUILD_ASSERT(KBD_GZLL_ACK_PAYLOAD_BYTES >= 1);
BUILD_ASSERT(KBD_GZLL_ACK_PAYLOAD_BYTES <= NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH);
BUILD_ASSERT(KBD_HID_KEYBOARD_USAGE_MAX + KBD_HID_KEYBOARD_BITMAP_BIT_OFFSET <
	     KBD_HID_KEYBOARD_REPORT_BYTES * 8);
BUILD_ASSERT(CONFIG_DONGLE_LINK_TIMEOUT_MS > 2 * KBD_WIRELESS_KEEP_ALIVE_PERIOD_MS);
BUILD_ASSERT(CONFIG_DONGLE_REPORT_QUEUE_SIZE >= 4);

/* Main owns the queue, link supervision and all Gazell FIFO accesses.
 * USB callbacks publish atomic state only. GET_REPORT reads a short snapshot
 * under state_lock; no USB or Gazell API is called while holding that lock.
 */
static struct dongle_report pending[CONFIG_DONGLE_REPORT_QUEUE_SIZE];
static size_t head, count;
static struct dongle_report latest[REPORT_COUNT] = {
	[KEYBOARD] = { .data = { KBD_HID_REPORT_ID_KEYBOARD },
		       .len = KBD_HID_KEYBOARD_REPORT_BYTES },
	[CONSUMER] = { .data = { KBD_HID_REPORT_ID_CONSUMER },
		       .len = KBD_HID_CONSUMER_REPORT_BYTES },
};
static struct k_spinlock state_lock;
static atomic_t usb_ready, usb_suspended, usb_busy, events;
static atomic_t vbus_present;
static atomic_t protocol = HID_PROTOCOL_REPORT;
static atomic_t led_state;
static atomic_t idle_ms[REPORT_COUNT];
static uint32_t last_sent_ms[REPORT_COUNT];
static uint32_t last_rx_ms, suspend_ms, wake_attempt_ms;
static bool linked, wake_pending, wake_attempted;

/* This buffer belongs to the controller until input_report_done(), including
 * cancellation on reset/unplug. Never recycle it merely on iface_ready(false).
 */
UDC_STATIC_BUF_DEFINE(usb_report, KBD_GZLL_TX_PAYLOAD_BYTES);
K_SEM_DEFINE(dongle_event, 0, 1);

static unsigned int report_index(const struct dongle_report *report)
{
	return report->data[0] == KBD_HID_REPORT_ID_CONSUMER ? CONSUMER : KEYBOARD;
}

static struct dongle_report report_snapshot(unsigned int index)
{
	k_spinlock_key_t key = k_spin_lock(&state_lock);
	struct dongle_report report = latest[index];

	k_spin_unlock(&state_lock, key);
	return report;
}

static size_t encode_usb_report(const struct dongle_report *report,
			      uint8_t proto, uint8_t *buf)
{
	if (proto == HID_PROTOCOL_REPORT) {
		memcpy(buf, report->data, report->len);
		return report->len;
	}
	if (report_index(report) != KEYBOARD) {
		return 0; /* Boot keyboard protocol has no consumer report. */
	}

	memset(buf, 0, KBD_HID_6KRO_REPORT_BYTES);
	buf[0] = report->data[1];
	unsigned int keys = 0;

	for (unsigned int usage = 1; usage <= KBD_HID_KEYBOARD_USAGE_MAX; usage++) {
		unsigned int bit = usage + KBD_HID_KEYBOARD_BITMAP_BIT_OFFSET;

		if (!(report->data[bit / 8] & BIT(bit % 8))) {
			continue;
		}
		if (keys == KBD_HID_6KRO_KEY_COUNT) {
			/* HID ErrorRollOver in every array slot; retain modifiers. */
			memset(&buf[2], 0x01, KBD_HID_6KRO_KEY_COUNT);
			break;
		}
		buf[2 + keys++] = usage;
	}
	return KBD_HID_6KRO_REPORT_BYTES;
}

static void request_resync(void)
{
	atomic_set_bit(&events, EVENT_RESYNC);
	k_sem_give(&dongle_event);
}

static void kb_iface_ready(const struct device *dev, const bool ready)
{
	ARG_UNUSED(dev);
	/* The NCS HID class resets its protocol when enabling a configuration. */
	atomic_set(&protocol, HID_PROTOCOL_REPORT);
	/* A new USB configuration must obtain fresh LED state from the host. */
	atomic_clear(&led_state);
	for (unsigned int i = 0; i < REPORT_COUNT; i++) {
		atomic_clear(&idle_ms[i]);
	}
	atomic_set(&usb_ready, ready);
	atomic_clear(&usb_suspended);
	request_resync();
}

static int kb_get_report(const struct device *dev, const uint8_t type,
			const uint8_t id, const uint16_t len, uint8_t *const buf)
{
	uint8_t data[KBD_GZLL_TX_PAYLOAD_BYTES];
	uint8_t proto = atomic_get(&protocol);
	size_t size;

	ARG_UNUSED(dev);
	if (len == 0) {
		return -EINVAL;
	}
	if (type == HID_REPORT_TYPE_OUTPUT) {
		if (id != KBD_HID_REPORT_ID_KEYBOARD && id != 0) {
			return -ENOTSUP;
		}
		/* Preserve the existing control-EP LED readback contract. */
		data[0] = atomic_get(&led_state);
		size = 1;
	} else if (type == HID_REPORT_TYPE_INPUT) {
		unsigned int index;

		if (id == KBD_HID_REPORT_ID_KEYBOARD ||
		    (proto == HID_PROTOCOL_BOOT && id == 0)) {
			index = KEYBOARD;
		} else if (proto == HID_PROTOCOL_REPORT && id == KBD_HID_REPORT_ID_CONSUMER) {
			index = CONSUMER;
		} else {
			return -ENOTSUP;
		}
		struct dongle_report report = report_snapshot(index);

		size = encode_usb_report(&report, proto, data);
	} else {
		return -ENOTSUP;
	}
	size = MIN(size, len);
	memcpy(buf, data, size);
	return size;
}

static int kb_set_report(const struct device *dev, const uint8_t type,
			const uint8_t id, const uint16_t len, const uint8_t *const buf)
{
	uint8_t value;

	ARG_UNUSED(dev);
	if (type != HID_REPORT_TYPE_OUTPUT) {
		return -ENOTSUP;
	}
	if (len == 0U) {
		return 0;
	}
	if (id != 0U && id != KBD_HID_REPORT_ID_KEYBOARD) {
		return -ENOTSUP;
	}
	/* LED state comes only from control EP SET_REPORT. Preserve support for
	 * both [led_state] and [report_id, led_state], as on the current body.
	 */
	value = len > 1U && buf[0] == KBD_HID_REPORT_ID_KEYBOARD ? buf[1] : buf[0];
	atomic_set(&led_state, value & (BIT(0) | BIT(1) | BIT(2)));
	k_sem_give(&dongle_event);
	return 0;
}

static void kb_set_idle(const struct device *dev, const uint8_t id, const uint32_t duration)
{
	ARG_UNUSED(dev);
	/* NCS has already converted USB's 4 ms units to milliseconds. */
	if (id == 0 || id == KBD_HID_REPORT_ID_KEYBOARD) {
		atomic_set(&idle_ms[KEYBOARD], duration);
	}
	if (id == 0 || id == KBD_HID_REPORT_ID_CONSUMER) {
		atomic_set(&idle_ms[CONSUMER], duration);
	}
	k_sem_give(&dongle_event);
}

static uint32_t kb_get_idle(const struct device *dev, const uint8_t id)
{
	ARG_UNUSED(dev);
	if (id == KBD_HID_REPORT_ID_CONSUMER) {
		return atomic_get(&idle_ms[CONSUMER]);
	}
	return (id == 0 || id == KBD_HID_REPORT_ID_KEYBOARD) ?
		atomic_get(&idle_ms[KEYBOARD]) : 0;
}

static void kb_set_protocol(const struct device *dev, const uint8_t proto)
{
	ARG_UNUSED(dev);
	atomic_set(&protocol, proto);
	request_resync();
}

static void kb_input_report_done(const struct device *dev, const uint8_t *const report)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(report);
	atomic_clear(&usb_busy);
	k_sem_give(&dongle_event);
}

static const struct hid_device_ops kb_ops = {
	.iface_ready = kb_iface_ready,
	.get_report = kb_get_report,
	.set_report = kb_set_report,
	.set_idle = kb_set_idle,
	.get_idle = kb_get_idle,
	.set_protocol = kb_set_protocol,
	.input_report_done = kb_input_report_done,
	/* No interrupt OUT endpoint: host LED updates arrive through EP0. */
	.output_report = NULL,
};

/* Main thread only. Callers reserve enough room before appending. */
static void queue_append(const struct dongle_report *report)
{
	pending[(head + count) % ARRAY_SIZE(pending)] = *report;
	count++;
}

static void queue_resync(bool release_first)
{
	head = count = 0;
	if (release_first) {
		for (unsigned int i = 0; i < REPORT_COUNT; i++) {
			struct dongle_report release = report_snapshot(i);

			memset(&release.data[1], 0, sizeof(release.data) - 1);
			queue_append(&release);
		}
	}
	for (unsigned int i = 0; i < REPORT_COUNT; i++) {
		struct dongle_report report = report_snapshot(i);

		queue_append(&report);
	}
}

static void receive_report(const uint8_t *data, size_t len, uint32_t now)
{
	struct dongle_report report = {0};
	unsigned int index;

	if ((len == KBD_HID_KEYBOARD_REPORT_BYTES || len == KBD_GZLL_TX_PAYLOAD_BYTES) &&
	    data[0] == KBD_HID_REPORT_ID_KEYBOARD) {
		index = KEYBOARD;
		report.len = KBD_HID_KEYBOARD_REPORT_BYTES;
	} else if ((len == KBD_HID_CONSUMER_REPORT_BYTES || len == KBD_GZLL_TX_PAYLOAD_BYTES) &&
		   data[0] == KBD_HID_REPORT_ID_CONSUMER) {
		index = CONSUMER;
		report.len = KBD_HID_CONSUMER_REPORT_BYTES;
	} else {
		/* In particular, an old all-zero heartbeat is not a release report. */
		LOG_WRN("Invalid radio report, length %u", (unsigned int)len);
		return;
	}
	memcpy(report.data, data, report.len);
	last_rx_ms = now;
	linked = true;

	struct dongle_report previous = report_snapshot(index);
	if (memcmp(previous.data, report.data, report.len) == 0) {
		return; /* Body keep-alive: supervise link, but do not flood USB. */
	}
	bool pressed = false;
	for (size_t i = 1; i < report.len; i++) {
		pressed |= (report.data[i] & ~previous.data[i]) != 0;
	}
	k_spinlock_key_t key = k_spin_lock(&state_lock);

	latest[index] = report;
	k_spin_unlock(&state_lock, key);
	if (atomic_get(&usb_suspended) && pressed) {
		wake_pending = true;
	}
	if (!atomic_get(&usb_ready)) {
		return; /* Keep current state for the next configuration, not old typing. */
	}
	if (count == ARRAY_SIZE(pending)) {
		/* A radio ACK cannot be revoked when USB is stalled. Bound memory and
		 * recover both report IDs, including releases, instead of sticking keys.
		 * This recovery may lose intermediate input edges.
		 */
		LOG_WRN("USB report queue full; releasing and resynchronizing");
		queue_resync(true);
	} else {
		queue_append(&report);
	}
}

static bool queue_ack(void)
{
	/* Keep at most one outstanding LED snapshot. Never flush TX while enabled.
	 * Gazell can retain that snapshot until a later RX acknowledges it, so a
	 * changed LED byte is delivered over subsequent Body keep-alive packets.
	 */
	if (nrf_gzll_get_tx_fifo_packet_count(KBD_GZLL_PIPE_NUMBER) != 0) {
		return true;
	}
	uint8_t payload[KBD_GZLL_ACK_PAYLOAD_BYTES] = {0};

	/* Keep lock LEDs off while USB is suspended or unconfigured. The cache
	 * is cleared on session loss; only a fresh SET_REPORT may relight LEDs.
	 * A previously queued ACK drains first through Body keep-alive traffic.
	 */
	if (atomic_get(&usb_ready) && !atomic_get(&usb_suspended)) {
		payload[0] = atomic_get(&led_state);
	}
	return nrf_gzll_add_packet_to_tx_fifo(KBD_GZLL_PIPE_NUMBER, payload, sizeof(payload));
}

static void process_radio(uint32_t now)
{
	/* FIFO is the source of truth; coalesced notifications never lose packets.
	 * Bound a pass so continuous traffic cannot starve USB or supervision.
	 */
	for (unsigned int i = 0; i < NRF_GZLL_CONST_MAX_TOTAL_PACKETS; i++) {
		uint8_t payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
		uint32_t len = sizeof(payload);

		if (nrf_gzll_get_rx_fifo_packet_count(KBD_GZLL_PIPE_NUMBER) <= 0) {
			break;
		}
		if (!nrf_gzll_fetch_packet_from_rx_fifo(KBD_GZLL_PIPE_NUMBER, payload, &len)) {
			LOG_ERR("GZLL RX FIFO fetch failed");
			break;
		}
		receive_report(payload, len, now);
	}
	(void)queue_ack(); /* Retry on the next pass if Gazell's packet pool is full. */

	if (linked && now - last_rx_ms >= CONFIG_DONGLE_LINK_TIMEOUT_MS) {
		linked = false;
		wake_pending = false;
		k_spinlock_key_t key = k_spin_lock(&state_lock);

		for (unsigned int i = 0; i < REPORT_COUNT; i++) {
			memset(&latest[i].data[1], 0, sizeof(latest[i].data) - 1);
		}
		k_spin_unlock(&state_lock, key);
		queue_resync(false);
		LOG_INF("Radio link timed out; releasing all keys");
	}
}

static void process_usb_events(struct usbd_context *usbd, uint32_t now)
{
	atomic_val_t flags = atomic_set(&events, 0);

	if ((flags & BIT(EVENT_VBUS_CHANGED)) && usbd_can_detect_vbus(usbd)) {
		int ret = 0;

		/* A quick unplug/replug can coalesce both notifications. Always
		 * tear down the old configuration before enabling the new session.
		 */
		if (flags & BIT(EVENT_VBUS_LOST)) {
			ret = usbd_disable(usbd);
		}
		if (ret != 0 && ret != -EALREADY) {
			LOG_ERR("USB disable failed: %d", ret);
		}
		if (atomic_get(&vbus_present)) {
			ret = usbd_enable(usbd);
		}

		if (ret != 0 && ret != -EALREADY) {
			LOG_ERR("USB power transition failed: %d", ret);
		}
	}
	if (flags & BIT(EVENT_RESYNC)) {
		queue_resync(false);
		wake_pending = wake_attempted = false;
	}
	if (flags & BIT(EVENT_SUSPEND)) {
		/* Discard pre-suspend backlog. New press/release pairs received while
		 * suspended remain queued through resume, including the wake-up tap.
		 */
		queue_resync(false);
		suspend_ms = now;
		wake_pending = wake_attempted = false;
	}
}

static int submit_usb_report(const struct device *hid_dev,
			     const struct dongle_report *report, uint8_t proto, uint32_t now)
{
	size_t size = encode_usb_report(report, proto, usb_report);

	if (size == 0) {
		return 0; /* Consumer input is intentionally muted in Boot mode. */
	}
	/* Set before submit: completion may run before the API returns. */
	atomic_set(&usb_busy, 1);
	int ret = hid_device_submit_report(hid_dev, size, usb_report);

	if (ret != 0) {
		atomic_clear(&usb_busy);
	} else {
		last_sent_ms[report_index(report)] = now;
	}
	return ret;
}

static void process_usb(const struct device *hid_dev, struct usbd_context *usbd, uint32_t now)
{
	if (!atomic_get(&usb_ready) || atomic_get(&events)) {
		/* Handle configuration/protocol events before submitting queued data. */
		return;
	}
	if (atomic_get(&usb_suspended) || usbd_is_suspended(usbd)) {
		/* Unchanged keep-alives and releases must not wake the computer.
		 * Leave at least 5 ms after suspend; the stack enforces host permission.
		 */
		if (IS_ENABLED(CONFIG_SAMPLE_USBD_REMOTE_WAKEUP) && wake_pending &&
		    now - suspend_ms >= 5 &&
		    (!wake_attempted || now - wake_attempt_ms >= 100)) {
			int ret = usbd_wakeup_request(usbd);

			wake_attempted = true;
			wake_attempt_ms = now;
			if (ret == 0 || ret == -EACCES || ret == -ENOTSUP) {
				wake_pending = false;
			}
		}
		return;
	}
	wake_pending = wake_attempted = false;
	if (atomic_get(&usb_busy)) {
		return;
	}

	uint8_t proto = atomic_get(&protocol);
	for (unsigned int i = 0; i < REPORT_COUNT; i++) {
		uint32_t duration = atomic_get(&idle_ms[i]);
		bool queued = false;

		if ((proto == HID_PROTOCOL_BOOT && i == CONSUMER) ||
		    duration == 0 || now - last_sent_ms[i] < duration) {
			continue;
		}
		for (size_t j = 0; j < count; j++) {
			if (report_index(&pending[(head + j) % ARRAY_SIZE(pending)]) == i) {
				queued = true;
				break;
			}
		}
		if (!queued) {
			/* Refresh one ID even during traffic on the other ID, but never
			 * send a newer snapshot ahead of that ID's queued input edges.
			 */
			struct dongle_report report = report_snapshot(i);

			(void)submit_usb_report(hid_dev, &report, proto, now);
			return;
		}
	}
	while (count) {
		struct dongle_report *report = &pending[head];
		bool muted = proto == HID_PROTOCOL_BOOT && report_index(report) == CONSUMER;
		int ret = submit_usb_report(hid_dev, report, proto, now);

		if (ret != 0) {
			return; /* Retain queue head on busy/no-buffer/not-ready errors. */
		}
		head = (head + 1) % ARRAY_SIZE(pending);
		count--;
		if (!muted) {
			return;
		}
	}
}

void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t info)
{
	ARG_UNUSED(info);
	if (pipe == KBD_GZLL_PIPE_NUMBER) {
		k_sem_give(&dongle_event);
	}
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t info)
{
	ARG_UNUSED(pipe);
	ARG_UNUSED(info);
}

void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t info)
{
	ARG_UNUSED(pipe);
	ARG_UNUSED(info);
}

void nrf_gzll_disabled(void)
{
}

static int dongle_gzll_init(void)
{
	if (!gzll_glue_init() || !nrf_gzll_init(NRF_GZLL_MODE_HOST)) {
		LOG_ERR("Cannot initialize GZLL host");
		return -EIO;
	}
	if (!nrf_gzll_set_base_address_1(KBD_GZLL_BASE_ADDRESS_1) ||
	    !nrf_gzll_set_address_prefix_byte(KBD_GZLL_PIPE_NUMBER, KBD_GZLL_PIPE_PREFIX) ||
	    !nrf_gzll_set_rx_pipes_enabled(BIT(KBD_GZLL_PIPE_NUMBER)) ||
	    !nrf_gzll_set_channel_table(channels, ARRAY_SIZE(channels)) ||
	    !nrf_gzll_set_timeslot_period(KBD_GZLL_TIMESLOT_PERIOD_US) ||
	    !nrf_gzll_set_tx_power(NRF_GZLL_TX_POWER_0_DBM) ||
	    !nrf_gzll_set_timeslots_per_channel(KBD_GZLL_TIMESLOTS_PER_CHANNEL)) {
		LOG_ERR("Cannot configure GZLL host");
		return -EIO;
	}
	if (!queue_ack() || !nrf_gzll_enable()) {
		LOG_ERR("Cannot start GZLL host");
		return -EIO;
	}
	return 0;
}

static void msg_cb(struct usbd_context *const usbd, const struct usbd_msg *const msg)
{
	ARG_UNUSED(usbd);
	/* Runs in stack context (USBD_MSG_DEFERRED_MODE=n). Publish only state;
	 * enable/disable belongs to main, after USB initialization has completed.
	 */
	switch (msg->type) {
	case USBD_MSG_SUSPEND:
		atomic_set(&usb_suspended, 1);
		atomic_clear(&led_state);
		atomic_set_bit(&events, EVENT_SUSPEND);
		break;
	case USBD_MSG_RESUME:
		/* Preserve wake-up reports; LEDs wait for a fresh host SET_REPORT. */
		atomic_clear(&usb_suspended);
		break;
	case USBD_MSG_RESET:
		atomic_clear(&usb_ready);
		atomic_clear(&led_state);
		atomic_clear(&usb_suspended);
		atomic_set(&protocol, HID_PROTOCOL_REPORT);
		request_resync();
		break;
	case USBD_MSG_VBUS_READY:
		atomic_set(&vbus_present, 1);
		atomic_set_bit(&events, EVENT_VBUS_CHANGED);
		break;
	case USBD_MSG_VBUS_REMOVED:
		atomic_clear(&vbus_present);
		atomic_clear(&led_state);
		atomic_set_bit(&events, EVENT_VBUS_LOST);
		atomic_set_bit(&events, EVENT_VBUS_CHANGED);
		atomic_clear(&usb_ready);
		atomic_clear(&usb_suspended);
		request_resync();
		break;
	default:
		break;
	}
	k_sem_give(&dongle_event);
}

int main(void)
{
	const struct device *hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);

	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID device is not ready");
		return -ENODEV;
	}
	int ret = hid_device_register(hid_dev, hid_report_desc, hid_report_desc_size, &kb_ops);

	if (ret) {
		return ret;
	}
	if (IS_ENABLED(CONFIG_USBD_HID_SET_POLLING_PERIOD)) {
		ret = hid_device_set_in_polling(hid_dev, KBD_HID_REPORT_POLLING_PERIOD_US);
		if (ret) {
			LOG_WRN("Failed to set IN polling period: %d", ret);
		}
	}
	struct usbd_context *usbd = sample_usbd_init_device(msg_cb);

	if (usbd == NULL) {
		return -ENODEV;
	}
	if (!usbd_can_detect_vbus(usbd)) {
		ret = usbd_enable(usbd);
		if (ret) {
			return ret;
		}
	}
	ret = dongle_gzll_init();
	if (ret) {
		return ret;
	}
	LOG_INF("2.4G keyboard receiver initialized");
	while (true) {
		uint32_t now = k_uptime_get_32();

		process_usb_events(usbd, now);
		process_radio(now);
		process_usb(hid_dev, usbd, now);
		/* Also poll on timeout: recover coalesced events, retry USB/ACK, and
		 * service HID idle and link loss even when no new packet arrives.
		 */
		k_sem_take(&dongle_event, K_MSEC(1));
	}
	return 0;
}
