/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>

#include <gzll_glue.h>
#include <nrf_gzll.h>

#include <zephyr/logging/log.h>
#include <hid_descriptor.h>
#include <kbd_define.h>

LOG_MODULE_REGISTER(main, KBD_LOG_LEVEL);

#define DONGLE_GZLL_REPORT_BYTES KBD_HID_KEYBOARD_REPORT_BYTES
#define DONGLE_GZLL_ACK_BYTES KBD_GZLL_ACK_PAYLOAD_BYTES

static const uint8_t dongle_gzll_channel_table[KBD_GZLL_CHANNEL_COUNT] =
	KBD_GZLL_CHANNEL_TABLE;

struct dongle_report {
	uint8_t buf[DONGLE_GZLL_REPORT_BYTES];
	uint8_t len;
};

struct dongle_gzll_rx_result {
	uint32_t pipe;
	nrf_gzll_host_rx_info_t info;
};

K_MSGQ_DEFINE(dongle_report_msgq, sizeof(struct dongle_report), 4, 1);
K_MSGQ_DEFINE(dongle_gzll_rx_msgq,
	      sizeof(struct dongle_gzll_rx_result),
	      4,
	      sizeof(uint32_t));

static uint32_t kb_duration;
static bool kb_ready;
static uint8_t keyboard_led_state;
static bool keyboard_led_state_dirty;
static uint8_t dongle_gzll_rx_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
static uint8_t dongle_gzll_ack_payload[DONGLE_GZLL_ACK_BYTES];
static struct k_work dongle_gzll_work;

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
	ARG_UNUSED(dev);

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
	uint8_t led_state;

	ARG_UNUSED(dev);

	if (type != HID_REPORT_TYPE_OUTPUT) {
		LOG_WRN("Unsupported report type");
		return -ENOTSUP;
	}

	if (len == 0U) {
		return 0;
	}

	if (id != 0U && id != KBD_HID_REPORT_ID_KEYBOARD) {
		LOG_WRN("Unsupported output report ID %u", id);
		return -ENOTSUP;
	}

	if (len > 1U && buf[0] == KBD_HID_REPORT_ID_KEYBOARD) {
		led_state = buf[1];
	} else {
		led_state = buf[0];
	}

	led_state &= BIT(0) | BIT(1) | BIT(2);

	if (keyboard_led_state != led_state) {
		keyboard_led_state = led_state;
		keyboard_led_state_dirty = true;
		LOG_INF("Keyboard LED state: 0x%02x", keyboard_led_state);
	}

	return 0;
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

struct hid_device_ops kb_ops = {
	.iface_ready = kb_iface_ready,
	.get_report = kb_get_report,
	.set_report = kb_set_report,
	.set_idle = kb_set_idle,
	.get_idle = kb_get_idle,
	.set_protocol = kb_set_protocol,
	.output_report = NULL,
};

static size_t dongle_usb_report_size(const uint8_t *buf, size_t len)
{
	if (len == 0U) {
		return 0;
	}

	switch (buf[0]) {
	case KBD_HID_REPORT_ID_KEYBOARD:
		return len >= KBD_HID_KEYBOARD_REPORT_BYTES ?
		       KBD_HID_KEYBOARD_REPORT_BYTES : 0;
	case KBD_HID_REPORT_ID_CONSUMER:
		return len >= KBD_HID_CONSUMER_REPORT_BYTES ?
		       KBD_HID_CONSUMER_REPORT_BYTES : 0;
	default:
		LOG_WRN("Unsupported dongle report ID %u", buf[0]);
		return 0;
	}
}

static void dongle_usb_submit_report(const struct device *hid_dev,
				     struct usbd_context *sample_usbd,
				     const uint8_t *buf,
				     size_t len)
{
	size_t report_size;
	int ret;

	report_size = dongle_usb_report_size(buf, len);
	if (report_size == 0U) {
		return;
	}

	if (!kb_ready) {
		LOG_INF("USB HID device is not ready");
		return;
	}

	if (IS_ENABLED(CONFIG_SAMPLE_USBD_REMOTE_WAKEUP) &&
	    usbd_is_suspended(sample_usbd)) {
		ret = usbd_wakeup_request(sample_usbd);
		if (ret) {
			LOG_ERR("Remote wakeup error, %d", ret);
		}
		return;
	}

	ret = hid_device_submit_report(hid_dev, report_size, buf);
	if (ret) {
		LOG_ERR("HID submit report error, %d", ret);
	}
}

static void dongle_gzll_receive_report_buffer(const uint8_t *buf, size_t len)
{
	struct dongle_report report = {0};

	if (len > sizeof(report.buf)) {
		LOG_WRN("Truncating dongle report from %u to %u bytes",
			(unsigned int)len, (unsigned int)sizeof(report.buf));
		len = sizeof(report.buf);
	}

	memcpy(report.buf, buf, len);
	report.len = len;

	if (k_msgq_put(&dongle_report_msgq, &report, K_NO_WAIT) != 0) {
		LOG_ERR("Failed to queue dongle report");
	}
}

static bool dongle_gzll_ack_payload_prepare(uint8_t *buf, size_t len)
{
	if (len < DONGLE_GZLL_ACK_BYTES) {
		return false;
	}

	buf[0] = keyboard_led_state;

	if (!keyboard_led_state_dirty) {
		return false;
	}

	keyboard_led_state_dirty = false;
	return true;
}

static void dongle_gzll_queue_ack_payload(uint32_t pipe)
{
	bool result_value;

	/*
	 * The ACK payload is the fast path back to the keyboard. It always
	 * carries the latest USB host LED byte; the dirty flag is only useful
	 * later if we want to avoid extra bookkeeping on unchanged state.
	 */
	(void)dongle_gzll_ack_payload_prepare(dongle_gzll_ack_payload,
					      sizeof(dongle_gzll_ack_payload));

	result_value = nrf_gzll_add_packet_to_tx_fifo(pipe,
						      dongle_gzll_ack_payload,
						      sizeof(dongle_gzll_ack_payload));
	if (!result_value) {
		LOG_ERR("Cannot add GZLL ACK payload");
	}
}

static void dongle_gzll_handle_rx_result(struct dongle_gzll_rx_result *rx_result)
{
	bool result_value;
	uint32_t payload_len = sizeof(dongle_gzll_rx_payload);

	ARG_UNUSED(rx_result->info);

	/*
	 * Host mode receives keyboard reports from the body-side device.
	 * Fetch the payload outside the Gazell callback and hand it to the
	 * normal USB thread through dongle_report_msgq.
	 */
	result_value = nrf_gzll_fetch_packet_from_rx_fifo(rx_result->pipe,
							 dongle_gzll_rx_payload,
							 &payload_len);
	if (!result_value) {
		LOG_ERR("GZLL RX FIFO fetch failed");
		dongle_gzll_queue_ack_payload(rx_result->pipe);
		return;
	}

	if (payload_len > 0U) {
		dongle_gzll_receive_report_buffer(dongle_gzll_rx_payload,
						  payload_len);
	}

	/* Queue the next ACK payload after each RX, as required by GZLL host. */
	dongle_gzll_queue_ack_payload(rx_result->pipe);
}

static void dongle_gzll_work_handler(struct k_work *work)
{
	struct dongle_gzll_rx_result rx_result;

	ARG_UNUSED(work);

	while (k_msgq_get(&dongle_gzll_rx_msgq, &rx_result, K_NO_WAIT) == 0) {
		dongle_gzll_handle_rx_result(&rx_result);
	}
}

void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info)
{
	struct dongle_gzll_rx_result rx_result = {
		.pipe = pipe,
		.info = rx_info,
	};

	/*
	 * Gazell callbacks can run in a constrained context. Only copy the RX
	 * metadata here; the work handler fetches FIFO data and queues USB work.
	 */
	if (k_msgq_put(&dongle_gzll_rx_msgq, &rx_result, K_NO_WAIT) == 0) {
		k_work_submit(&dongle_gzll_work);
	} else {
		LOG_ERR("Cannot queue GZLL RX result");
	}
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info)
{
	ARG_UNUSED(pipe);
	ARG_UNUSED(tx_info);
}

void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info)
{
	ARG_UNUSED(pipe);
	ARG_UNUSED(tx_info);
}

void nrf_gzll_disabled(void)
{
}

static int dongle_gzll_init(void)
{
	bool result_value;

	k_work_init(&dongle_gzll_work, dongle_gzll_work_handler);

	/* Initialize the NCS glue layer before touching the Gazell LL API. */
	result_value = gzll_glue_init();
	if (!result_value) {
		LOG_ERR("Cannot initialize GZLL glue");
		return -EIO;
	}

	/* The dongle is the always-listening Gazell host. */
	result_value = nrf_gzll_init(NRF_GZLL_MODE_HOST);
	if (!result_value) {
		LOG_ERR("Cannot initialize GZLL host");
		return -EIO;
	}

	/* Keep radio addressing identical to the keyboard body-side device. */
	result_value = nrf_gzll_set_base_address_1(KBD_GZLL_BASE_ADDRESS_1);
	if (!result_value) {
		LOG_ERR("Cannot set GZLL base address");
		return -EIO;
	}

	result_value = nrf_gzll_set_address_prefix_byte(KBD_GZLL_PIPE_NUMBER,
							KBD_GZLL_PIPE_PREFIX);
	if (!result_value) {
		LOG_ERR("Cannot set GZLL pipe prefix");
		return -EIO;
	}

	result_value = nrf_gzll_set_channel_table(dongle_gzll_channel_table,
						  KBD_GZLL_CHANNEL_COUNT);
	if (!result_value) {
		LOG_ERR("Cannot set GZLL channel table");
		return -EIO;
	}

	/*
	 * Use the shared timing knobs so host and device hop channels with the
	 * same cadence. Device-only supervision settings remain on the body side.
	 */
	nrf_gzll_set_timeslots_per_channel(KBD_GZLL_TIMESLOTS_PER_CHANNEL);

	/* Preload the first ACK payload; GZLL host consumes it on RX. */
	dongle_gzll_queue_ack_payload(KBD_GZLL_PIPE_NUMBER);

	result_value = nrf_gzll_enable();
	if (!result_value) {
		LOG_ERR("Cannot enable GZLL");
		return -EIO;
	}

	LOG_INF("GZLL host initialized");

	return 0;
}

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

int main(void)
{
	struct usbd_context *sample_usbd;
	const struct device *hid_dev;
	int ret;

	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID Device is not ready");
		return -EIO;
	}

	ret = hid_device_register(hid_dev,
				  hid_report_desc, hid_report_desc_size,
				  &kb_ops);
	if (ret != 0) {
		LOG_ERR("Failed to register HID Device, %d", ret);
		return ret;
	}

	if (IS_ENABLED(CONFIG_USBD_HID_SET_POLLING_PERIOD)) {
		ret = hid_device_set_in_polling(hid_dev,
						KBD_HID_REPORT_POLLING_PERIOD_US);
		if (ret) {
			LOG_WRN("Failed to set IN report polling period, %d", ret);
		}

		ret = hid_device_set_out_polling(hid_dev,
						 KBD_HID_REPORT_POLLING_PERIOD_US);
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

	ret = dongle_gzll_init();
	if (ret != 0) {
		return ret;
	}

	while (true) {
		struct dongle_report report;

		k_msgq_get(&dongle_report_msgq, &report, K_FOREVER);
		dongle_usb_submit_report(hid_dev, sample_usbd,
					 report.buf, report.len);
	}

	return 0;
}
