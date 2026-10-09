/*
 * Copyright (c) 2018-2026 Nordic Semiconductor ASA
 *
 * BLE transport adapted from Bluetooth_example's Nordic HIDS keyboard sample.
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <kbd_define.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_keyboard, KBD_LOG_LEVEL);

#include "ble_keyboard.h"
#include "pairing.h"
#include <errno.h>
#include <string.h>
#include <hid_descriptor.h>
#include <keymap.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/services/hids.h>

enum { KEYBOARD_IDX, CONSUMER_IDX };
enum { KEYBOARD_NOTIFY, CONSUMER_NOTIFY, BOOT_NOTIFY, BOOT_MODE };
enum { LED_REPORT_MASK = 0x07, LED_HOST_SUSPENDED = BIT(7) };
BT_HIDS_DEF(hids, 1, KBD_HID_6KRO_REPORT_BYTES, 1);
BUILD_ASSERT(CONFIG_BT_MAX_CONN == 1);
BUILD_ASSERT(CONFIG_BT_HIDS_INPUT_REP_MAX >= 2);
BUILD_ASSERT(CONFIG_KBD_BLE_SCAN_MIN_US <= CONFIG_KBD_BLE_SCAN_MAX_US);
BUILD_ASSERT(CONFIG_KBD_BLE_REPORT_MIN_US <= CONFIG_KBD_BLE_REPORT_MAX_US);

/* The outer mutex serializes HIDS lifetime and TX. HIDS callbacks invoked
 * under the SDK's own context lock must only touch atomics (no lock inversion).
 * Notification sends run in the system workqueue: the SDK uses nonblocking
 * allocation there, so TX cannot wait on a Bluetooth callback holding this lock.
 */
K_MUTEX_DEFINE(transport_mutex);
static struct bt_conn *active_conn;
static bool initialized;
static atomic_t flags;
static atomic_t epoch;
/* One atomic word prevents a racing output report from undoing Suspend. */
static atomic_t leds;
static atomic_t connection_interval_us;
static uint32_t queue_epoch;
static bool previous_valid[2];
static uint8_t previous_keyboard[KBD_HID_6KRO_REPORT_BYTES];
static uint8_t previous_consumer;
static int last_tx_error;

struct report_packet {
	uint8_t index;
	uint8_t data[KBD_HID_6KRO_REPORT_BYTES];
};
K_MSGQ_DEFINE(tx_queue, sizeof(struct report_packet), 16, 1);
static void tx_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(tx_work, tx_work_handler);

static void clear_queue(void)
{
	k_msgq_purge(&tx_queue);
	memset(previous_valid, 0, sizeof(previous_valid));
	queue_epoch = (uint32_t)atomic_get(&epoch);
}

static void sync_epoch(void)
{
	if (queue_epoch != (uint32_t)atomic_get(&epoch)) {
		clear_queue();
	}
}

/* Called with transport_mutex held. CCC readiness is per report. */
static bool report_ready(uint8_t index)
{
	if (!initialized || !active_conn || !kbd_pairing_can_send(active_conn)) {
		return false;
	}
	if (atomic_test_bit(&flags, BOOT_MODE)) {
		return index == KEYBOARD_IDX && bt_gatt_is_subscribed(active_conn,
			&hids.gp.svc.attrs[hids.boot_kb_inp_rep.att_ind], BT_GATT_CCC_NOTIFY);
	}
	/* Query the actual per-connection CCC, including restored bond settings. */
	return bt_gatt_is_subscribed(active_conn,
		&hids.gp.svc.attrs[hids.inp_rep_group.reports[index].att_ind], BT_GATT_CCC_NOTIFY);
}

static int send_packet(const struct report_packet *packet)
{
	if (packet->index == KEYBOARD_IDX && atomic_test_bit(&flags, BOOT_MODE)) {
		return bt_hids_boot_kb_inp_rep_send(&hids, active_conn, packet->data,
						  KBD_HID_6KRO_REPORT_BYTES, NULL);
	}
	return bt_hids_inp_rep_send(&hids, active_conn, packet->index, packet->data,
				   packet->index == KEYBOARD_IDX ?
				   KBD_HID_6KRO_REPORT_BYTES : 1, NULL);
}

static void tx_work_handler(struct k_work *work)
{
	struct report_packet packet;
	bool retry = false;
	ARG_UNUSED(work);

	k_mutex_lock(&transport_mutex, K_FOREVER);
	sync_epoch();
	/* Bound each invocation so pairing and switch work can run promptly. */
	for (unsigned int n = 0; n < 4 && !k_msgq_peek(&tx_queue, &packet); n++) {
		if (queue_epoch != (uint32_t)atomic_get(&epoch)) {
			clear_queue();
			break;
		}
		if (!report_ready(packet.index)) {
			clear_queue();
			break;
		}
		int err = send_packet(&packet);
		if (err) {
			/* Keep press/release ordering when the ATT queue is full. */
			if (err != last_tx_error && err != -ENOMEM && err != -EAGAIN &&
			    err != -ENOBUFS) {
				LOG_INF("[ble] Report %u send failed: %d; retrying\n", packet.index, err);
			}
			last_tx_error = err;
			retry = true;
			break;
		}
		last_tx_error = 0;
		(void)k_msgq_get(&tx_queue, &packet, K_NO_WAIT);
	}
	retry |= k_msgq_num_used_get(&tx_queue) != 0;
	k_mutex_unlock(&transport_mutex);
	if (retry) {
		k_work_schedule(&tx_work, K_MSEC(5));
	}
}

static int submit(uint8_t index, const uint8_t *data, size_t size, uint32_t input_epoch)
{
	struct report_packet packet = { .index = index };
	int err = k_mutex_lock(&transport_mutex, K_NO_WAIT);
	if (err) {
		return -EAGAIN;
	}
	sync_epoch();
	if (input_epoch != queue_epoch) {
		err = -ESTALE;
		goto out;
	}
	if (!report_ready(index)) {
		err = -EACCES;
		goto out;
	}
	uint8_t *previous = index == KEYBOARD_IDX ? previous_keyboard : &previous_consumer;
	if (previous_valid[index] && !memcmp(previous, data, size)) {
		err = 0;
		goto out;
	}
	memcpy(packet.data, data, size);
	err = k_msgq_put(&tx_queue, &packet, K_NO_WAIT);
	if (!err) {
		memcpy(previous, data, size);
		previous_valid[index] = true;
		k_work_schedule(&tx_work, K_NO_WAIT);
	}
out:
	k_mutex_unlock(&transport_mutex);
	return err;
}

int kbd_ble_submit_keyboard(const uint8_t report[KBD_HID_6KRO_REPORT_BYTES],
			    uint32_t input_epoch)
{
	return submit(KEYBOARD_IDX, report, KBD_HID_6KRO_REPORT_BYTES, input_epoch);
}

int kbd_ble_submit_consumer(uint8_t bits, uint32_t input_epoch)
{
	return submit(CONSUMER_IDX, &bits, sizeof(bits), input_epoch);
}

bool kbd_ble_tx_idle(uint32_t input_epoch)
{
	if (k_mutex_lock(&transport_mutex, K_NO_WAIT) != 0) {
		return false;
	}
	sync_epoch();
	bool idle = input_epoch == queue_epoch && report_ready(KEYBOARD_IDX) &&
		k_msgq_num_used_get(&tx_queue) == 0;
	k_mutex_unlock(&transport_mutex);
	return idle;
}

static bool ready(uint8_t index)
{
	k_mutex_lock(&transport_mutex, K_FOREVER);
	bool result = report_ready(index);
	k_mutex_unlock(&transport_mutex);
	return result;
}

bool kbd_ble_keyboard_ready(void) { return ready(KEYBOARD_IDX); }
bool kbd_ble_consumer_ready(void) { return ready(CONSUMER_IDX); }
uint32_t kbd_ble_input_epoch(void) { return (uint32_t)atomic_get(&epoch); }
uint8_t kbd_ble_led_state(void) { return (uint8_t)(atomic_get(&leds) & LED_REPORT_MASK); }

bool kbd_ble_can_sleep(void)
{
	k_mutex_lock(&transport_mutex, K_FOREVER);
	bool result = initialized && k_msgq_num_used_get(&tx_queue) == 0;
	k_mutex_unlock(&transport_mutex);
	return result && kbd_pairing_can_poweroff();
}

int kbd_ble_stop(void)
{
	if (!kbd_ble_can_sleep()) {
		return -EBUSY;
	}
	/* Never hold transport_mutex while waiting for work or RX callbacks:
	 * both need it to release the old host and connection references.
	 */
	int err = kbd_pairing_prepare_poweroff();
	if (err) {
		return err;
	}
	k_mutex_lock(&transport_mutex, K_FOREVER);
	initialized = false;
	atomic_inc(&epoch);
	clear_queue();
	k_mutex_unlock(&transport_mutex);
	struct k_work_sync sync;
	(void)k_work_cancel_delayable_sync(&tx_work, &sync);
	/* The SDC driver also releases MPSL with the configured disable option.
	 * No unpair, identity deletion, or settings erasure belongs here.
	 */
	err = bt_disable();
	LOG_INF("[ble] Power-off: Bluetooth stopped, err=%d\n", err);
	/* Reserve -EBUSY for admission refusal before anything was stopped. */
	return err == -EBUSY ? -EIO : err;
}

static uint32_t connection_period_us(void)
{
	uint32_t interval_us = (uint32_t)atomic_get(&connection_interval_us);

	if (!interval_us) {
#if defined(CONFIG_BT_PERIPHERAL_PREF_MIN_INT) && \
	CONFIG_BT_PERIPHERAL_PREF_MIN_INT >= 6 && CONFIG_BT_PERIPHERAL_PREF_MIN_INT <= 3200
		interval_us = CONFIG_BT_PERIPHERAL_PREF_MIN_INT * 1250U;
#else
		/* Preferred parameters disabled or set to "no specific value". */
		/* Original default fallback: 8 ms * 4, independent of rate settings. */
		interval_us = 32000U;
#endif
	}
	return interval_us;
}

static uint32_t dynamic_period_us(void)
{
	return CLAMP(DIV_ROUND_UP(connection_period_us(), CONFIG_KBD_BLE_SCANS_PER_INTERVAL),
		     CONFIG_KBD_BLE_SCAN_MIN_US, CONFIG_KBD_BLE_SCAN_MAX_US);
}

uint32_t kbd_ble_scan_period_us(void)
{
	if (CONFIG_KBD_BLE_FIXED_SCAN_PERIOD_US) {
		return CONFIG_KBD_BLE_FIXED_SCAN_PERIOD_US;
	}
	return dynamic_period_us();
}

uint32_t kbd_ble_report_period_us(void)
{
	if (CONFIG_KBD_BLE_REPORT_PERIOD_US) {
		return CONFIG_KBD_BLE_REPORT_PERIOD_US;
	}
	return CLAMP(DIV_ROUND_UP(connection_period_us(), CONFIG_KBD_BLE_REPORTS_PER_INTERVAL),
		     CONFIG_KBD_BLE_REPORT_MIN_US, CONFIG_KBD_BLE_REPORT_MAX_US);
}

static void record_connection_interval(uint32_t interval_us, uint16_t latency,
				       uint16_t timeout)
{
	if (!interval_us) {
		return;
	}
	atomic_set(&connection_interval_us, interval_us);
	uint32_t event_rate_millihz = (uint32_t)(1000000000ULL / interval_us);
	LOG_INF("[ble] Link interval=%u us, event rate=%u.%03u Hz, latency=%u, "
	       "timeout=%u ms, matrix scan=%u us, report update=%u us\n", interval_us,
	       event_rate_millihz / 1000U, event_rate_millihz % 1000U,
	       latency, timeout * 10U, kbd_ble_scan_period_us(), kbd_ble_report_period_us());
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
			     uint16_t latency, uint16_t timeout)
{
	ARG_UNUSED(conn);
	/* Standard connection interval units are 1.25 ms. No scan timer work
	 * here: only publish the value; the scan thread applies it safely.
	 */
	record_connection_interval(interval * 1250U, latency, timeout);
}

static void notification_changed(unsigned int bit, enum bt_hids_notify_evt evt)
{
	atomic_set_bit_to(&flags, bit, evt == BT_HIDS_CCCD_EVT_NOTIFY_ENABLED);
	atomic_inc(&epoch);
}

static void keyboard_notify(enum bt_hids_notify_evt evt)
{
	notification_changed(KEYBOARD_NOTIFY, evt);
}

static void consumer_notify(enum bt_hids_notify_evt evt)
{
	notification_changed(CONSUMER_NOTIFY, evt);
}

static void boot_notify(enum bt_hids_notify_evt evt)
{
	notification_changed(BOOT_NOTIFY, evt);
}

static void output_report(struct bt_hids_rep *rep, struct bt_conn *conn, bool write)
{
	/* Initial lock state may arrive before the pairing commit work executes.
	 * The single encrypted HIDS connection is already the source of this write.
	 */
	if (!write || !rep->size || bt_conn_get_security(conn) < BT_SECURITY_L2) {
		return;
	}
	atomic_val_t state;
	do {
		state = atomic_get(&leds);
		if (state & LED_HOST_SUSPENDED) {
			return;
		}
	} while (!atomic_cas(&leds, state, rep->data[0] & LED_REPORT_MASK));
}

static void control_point_changed(enum bt_hids_cp_evt evt, struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	/* The scan thread owns the LED GPIOs. Discard the old lock state and
	 * ignore output reports until Exit Suspend; there is no saved restore.
	 */
	switch (evt) {
	case BT_HIDS_CP_EVT_HOST_SUSP:
		atomic_set(&leds, LED_HOST_SUSPENDED);
		LOG_INF("[ble] Host suspend: LEDs cleared\n");
		break;
	case BT_HIDS_CP_EVT_HOST_EXIT_SUSP:
		atomic_and(&leds, ~(atomic_val_t)LED_HOST_SUSPENDED);
		LOG_INF("[ble] Host exit suspend\n");
		break;
	default:
		break;
	}
}

static void protocol_changed(enum bt_hids_pm_evt evt, struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	if (evt == BT_HIDS_PM_EVT_BOOT_MODE_ENTERED ||
	    evt == BT_HIDS_PM_EVT_REPORT_MODE_ENTERED) {
		atomic_set_bit_to(&flags, BOOT_MODE, evt == BT_HIDS_PM_EVT_BOOT_MODE_ENTERED);
		atomic_inc(&epoch);
	}
}

static void release_keys(struct bt_conn *conn)
{
	struct report_packet packet = { .index = KEYBOARD_IDX };

	k_mutex_lock(&transport_mutex, K_FOREVER);
	atomic_inc(&epoch);
	/* Releasing keys must not cancel the current host's Suspend state. */
	atomic_and(&leds, ~(atomic_val_t)LED_REPORT_MASK);
	clear_queue();
	if (conn && conn == active_conn && bt_conn_get_security(conn) >= BT_SECURITY_L2) {
		/* Bypass the pairing input gate, which is already closed here. */
		int key_err = send_packet(&packet);
		int consumer_err = 0;
		if (!atomic_test_bit(&flags, BOOT_MODE)) {
			packet.index = CONSUMER_IDX;
			consumer_err = send_packet(&packet);
		}
		LOG_INF("[ble] Release old host: keyboard=%d consumer=%d\n",
		       key_err, consumer_err);
	}
	k_mutex_unlock(&transport_mutex);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		kbd_pairing_connected(conn, err);
		return;
	}
	k_mutex_lock(&transport_mutex, K_FOREVER);
	int hid_err = bt_hids_connected(&hids, conn);
	if (!hid_err) {
		active_conn = bt_conn_ref(conn);
		struct bt_conn_info info;
		if (!bt_conn_get_info(conn, &info)) {
			record_connection_interval(info.le.interval_us, info.le.latency,
						   info.le.timeout);
		}
		/* CCC settings may be restored after this callback. */
		atomic_clear(&flags);
		atomic_clear(&leds);
		atomic_inc(&epoch);
		clear_queue();
	}
	k_mutex_unlock(&transport_mutex);
	kbd_pairing_connected(conn, 0);
	if (hid_err) {
		LOG_INF("[ble] HIDS connection init failed: %d\n", hid_err);
		(void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	k_mutex_lock(&transport_mutex, K_FOREVER);
	if (active_conn == conn) {
		(void)bt_hids_disconnected(&hids, conn);
		bt_conn_unref(active_conn);
		active_conn = NULL;
		atomic_clear(&connection_interval_us);
		atomic_clear(&flags);
		atomic_clear(&leds);
		atomic_inc(&epoch);
		clear_queue();
	}
	k_mutex_unlock(&transport_mutex);
	LOG_INF("[ble] Disconnected: 0x%02x\n", reason);
	kbd_pairing_disconnected(conn);
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	ARG_UNUSED(conn);
	LOG_INF("[ble] Security level=%u err=%u\n", level, err);
}

BT_CONN_CB_DEFINE(kbd_ble_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = kbd_pairing_recycled,
	.security_changed = security_changed,
	.le_param_updated = le_param_updated,
};

static enum bt_security_err pairing_accept(struct bt_conn *conn,
					  const struct bt_conn_pairing_feat *const feat)
{
	ARG_UNUSED(feat);
	return kbd_pairing_accept(conn) ? BT_SECURITY_ERR_SUCCESS :
		BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
}

static void auth_cancel(struct bt_conn *conn) { kbd_pairing_failed(conn); }
static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	kbd_pairing_complete(conn, bonded);
}
static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_INF("[ble] Pairing failed: %u\n", reason);
	kbd_pairing_failed(conn);
}

static struct bt_conn_auth_cb auth_callbacks = {
	.cancel = auth_cancel,
	.pairing_accept = pairing_accept,
};
static struct bt_conn_auth_info_cb auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static int hid_init(void)
{
	struct bt_hids_init_param init = { 0 };

	init.rep_map.data = hid_6kro_report_desc;
	init.rep_map.size = hid_6kro_report_desc_size;
	init.info.bcd_hid = 0x0101;
	init.info.flags = BT_HIDS_REMOTE_WAKE | BT_HIDS_NORMALLY_CONNECTABLE;
	init.inp_rep_group_init.cnt = 2;
	init.inp_rep_group_init.reports[KEYBOARD_IDX] = (struct bt_hids_inp_rep) {
		.id = KBD_HID_REPORT_ID_KEYBOARD,
		.size = KBD_HID_6KRO_REPORT_BYTES,
		.handler = keyboard_notify,
	};
	init.inp_rep_group_init.reports[CONSUMER_IDX] = (struct bt_hids_inp_rep) {
		.id = KBD_HID_REPORT_ID_CONSUMER,
		.size = 1,
		.handler = consumer_notify,
	};
	init.outp_rep_group_init.cnt = 1;
	init.outp_rep_group_init.reports[0] = (struct bt_hids_outp_feat_rep) {
		.id = KBD_HID_REPORT_ID_KEYBOARD,
		.size = 1,
		.handler = output_report,
	};
	init.is_kb = true;
	init.boot_kb_notif_handler = boot_notify;
	init.boot_kb_outp_rep_handler = output_report;
	init.pm_evt_handler = protocol_changed;
	init.conn_cp_evt_handler = control_point_changed;
	return bt_hids_init(&hids, &init);
}

int kbd_ble_init(void)
{
	int err = bt_conn_auth_cb_register(&auth_callbacks);
	if (!err) {
		err = bt_conn_auth_info_cb_register(&auth_info_callbacks);
	}
	if (!err) {
		err = hid_init();
	}
	if (!err) {
		err = bt_enable(NULL);
	}
	if (!err) {
		err = settings_load();
	}
	if (!err) {
		err = kbd_pairing_init(release_keys);
	}
	if (err) {
		LOG_INF("[ble] Initialization failed: %d; input disabled\n", err);
		return err;
	}
	k_mutex_lock(&transport_mutex, K_FOREVER);
	initialized = true;
	k_mutex_unlock(&transport_mutex);
	LOG_INF("[ble] Ready: Fn+Pairing to pair/cancel, Fn+Device Switch for next slot\n");
	return 0;
}

void kbd_ble_handle_actions(uint8_t actions)
{
	/* The body calls this only from the BLE scan loop after successful init. */
	if (actions & KBD_ACTION_DEVICE_SWITCH) {
		kbd_pairing_next_slot();
	}
	if (actions & KBD_ACTION_PAIRING) {
		kbd_pairing_button();
	}
}
