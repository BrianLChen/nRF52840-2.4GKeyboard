#include "gzll_keyboard.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <gzll_glue.h>
#include <nrf_gzll.h>
#include <kbd_define.h>

LOG_MODULE_REGISTER(kbd_gzll, KBD_LOG_LEVEL);

struct report_packet {
	uint8_t data[KBD_GZLL_TX_PAYLOAD_BYTES];
};

/* Single producer/consumer: only the scan thread touches this ring. */
static struct report_packet pending[KBD_GZLL_PENDING_REPORTS];
static size_t head, count;
static struct report_packet confirmed[2];
static struct report_packet in_flight;
static uint8_t channels[] = KBD_GZLL_CHANNEL_TABLE;
BUILD_ASSERT(ARRAY_SIZE(channels) == KBD_GZLL_CHANNEL_COUNT);
BUILD_ASSERT(KBD_GZLL_PENDING_REPORTS > 0);
BUILD_ASSERT(KBD_GZLL_TX_PAYLOAD_BYTES <= NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH);
static bool busy, running, keep_alive, connected, retry_wait;
static bool discard_in_flight;
static uint8_t refresh_index, failures, led_state;
static uint32_t last_tx_ms, last_failure_ms;

/* At most ONE packet is handed to Gazell. No further packet is submitted
 * until its completion is consumed, so completion events cannot overflow.
 * The callback publishes the result; all FIFO reads and state changes are
 * performed by the scan thread, never the system workqueue.
 */
enum tx_completion { TX_NONE, TX_SUCCESS, TX_FAILED };
static atomic_t completion;
K_SEM_DEFINE(gzll_disabled_sem, 0, 1);

static unsigned int report_index(const uint8_t *report)
{
	return report[0] == KBD_HID_REPORT_ID_CONSUMER ? 1U : 0U;
}

int kbd_gzll_submit(const uint8_t *report, size_t len)
{
	if (!running) {
		return -ESHUTDOWN;
	}
	if (!report ||
	    !((len == KBD_HID_KEYBOARD_REPORT_BYTES &&
	       report[0] == KBD_HID_REPORT_ID_KEYBOARD) ||
	      (len == KBD_HID_CONSUMER_REPORT_BYTES &&
	       report[0] == KBD_HID_REPORT_ID_CONSUMER))) {
		return -EINVAL;
	}
	if (count == ARRAY_SIZE(pending)) {
		return -ENOBUFS;
	}
	struct report_packet *packet = &pending[(head + count) % ARRAY_SIZE(pending)];
	memset(packet, 0, sizeof(*packet));
	memcpy(packet->data, report, len);
	count++;
	return 0;
}

static void drain_ack_payloads(bool apply)
{
	uint8_t payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
	while (nrf_gzll_get_rx_fifo_packet_count(KBD_GZLL_PIPE_NUMBER) > 0) {
		uint32_t len = sizeof(payload);
		if (!nrf_gzll_fetch_packet_from_rx_fifo(KBD_GZLL_PIPE_NUMBER, payload, &len)) {
			LOG_ERR("Cannot drain Gazell ACK FIFO");
			break;
		}
		if (apply && len >= KBD_GZLL_ACK_PAYLOAD_BYTES) {
			led_state = payload[0] & 0x07;
		}
	}
}

void kbd_gzll_process(void)
{
	uint32_t now = k_uptime_get_32();
	if (!running) {
		return;
	}

	atomic_val_t result = atomic_set(&completion, TX_NONE);
	if (busy && result != TX_NONE) {
		busy = false;
		if (result == TX_SUCCESS) {
			retry_wait = false;
			connected = true;
			failures = 0;
			last_failure_ms = now;
			if (!keep_alive) {
				if (!discard_in_flight) {
					confirmed[report_index(in_flight.data)] = in_flight;
					head = (head + 1) % ARRAY_SIZE(pending);
					count--;
				}
			}
		} else {
			/* A failed packet is removed by Gazell, but remains at our
			 * queue head. Retry it before later press/release reports.
			 */
			if (!keep_alive || now - last_failure_ms >=
			    KBD_WIRELESS_DISCONNECT_COUNTER_PERIOD_MS) {
				last_failure_ms = now;
				if (failures < KBD_WIRELESS_DISCONNECT_COUNTER_THRESHOLD) {
					failures++;
				}
			}
			if (failures >= KBD_WIRELESS_DISCONNECT_COUNTER_THRESHOLD) {
				connected = false;
				led_state = 0;
			}
			last_tx_ms = now; /* Bound retries while the receiver is absent. */
			retry_wait = true;
		}
		discard_in_flight = false;
	}

	/* Drain every ACK, independently of metadata notifications. */
	drain_ack_payloads(connected);
	if (busy) {
		return;
	}
	/* Retry backoff also applies to real reports after a failed transmission. */
	if (retry_wait && now - last_tx_ms < KBD_WIRELESS_KEEP_ALIVE_PERIOD_MS) {
		return;
	}
	if (!count && now - last_tx_ms < KBD_WIRELESS_KEEP_ALIVE_PERIOD_MS) {
		return;
	}

	keep_alive = count == 0;
	in_flight = keep_alive ? confirmed[refresh_index] : pending[head];
	/* Refreshes carry valid HID state, never an all-zero keyboard release.
	 * Alternating both IDs also restores state after a dongle reboot.
	 */
	if (nrf_gzll_add_packet_to_tx_fifo(KBD_GZLL_PIPE_NUMBER, in_flight.data,
					sizeof(in_flight.data))) {
		busy = true;
		last_tx_ms = now;
		if (keep_alive) {
			refresh_index ^= 1;
		}
	}
}

uint8_t kbd_gzll_led_state(void)
{
	return led_state;
}

bool kbd_gzll_connected(void)
{
	return connected;
}

bool kbd_gzll_tx_idle(void)
{
	return running && connected && count == 0 && (!busy || keep_alive);
}

void kbd_gzll_discard_pending(void)
{
	discard_in_flight = busy && !keep_alive;
	head = count = 0;
	memset(confirmed, 0, sizeof(confirmed));
	confirmed[0].data[0] = KBD_HID_REPORT_ID_KEYBOARD;
	confirmed[1].data[0] = KBD_HID_REPORT_ID_CONSUMER;
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t info)
{
	ARG_UNUSED(info);
	if (pipe == KBD_GZLL_PIPE_NUMBER) {
		atomic_set(&completion, TX_SUCCESS);
	}
}

void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t info)
{
	ARG_UNUSED(info);
	if (pipe == KBD_GZLL_PIPE_NUMBER) {
		atomic_set(&completion, TX_FAILED);
	}
}

void nrf_gzll_disabled(void)
{
	k_sem_give(&gzll_disabled_sem);
}

void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t info)
{
	ARG_UNUSED(pipe);
	ARG_UNUSED(info);
}

int kbd_gzll_stop(void)
{
	running = false;
	k_sem_reset(&gzll_disabled_sem);
	if (nrf_gzll_is_enabled()) {
		nrf_gzll_disable();
		if (k_sem_take(&gzll_disabled_sem, K_MSEC(100)) != 0) {
			return -ETIMEDOUT;
		}
	}
	drain_ack_payloads(false);
	return 0;
}

int kbd_gzll_init(void)
{
	if (!gzll_glue_init() || !nrf_gzll_init(NRF_GZLL_MODE_DEVICE)) {
		return -EIO;
	}
	if (!nrf_gzll_set_base_address_1(KBD_GZLL_BASE_ADDRESS_1) ||
	    !nrf_gzll_set_address_prefix_byte(KBD_GZLL_PIPE_NUMBER, KBD_GZLL_PIPE_PREFIX) ||
	    !nrf_gzll_set_channel_table(channels, ARRAY_SIZE(channels)) ||
	    !nrf_gzll_set_timeslot_period(KBD_GZLL_TIMESLOT_PERIOD_US) ||
	    !nrf_gzll_set_tx_power(NRF_GZLL_TX_POWER_0_DBM) ||
	    !nrf_gzll_set_timeslots_per_channel(KBD_GZLL_TIMESLOTS_PER_CHANNEL) ||
	    !nrf_gzll_set_timeslots_per_channel_when_device_out_of_sync(
		KBD_GZLL_OUT_OF_SYNC_TIMESLOTS_PER_CHANNEL) ||
	    !nrf_gzll_set_device_channel_selection_policy(
		NRF_GZLL_DEVICE_CHANNEL_SELECTION_POLICY_USE_CURRENT) ||
	    !nrf_gzll_set_sync_lifetime(KBD_GZLL_SYNC_LIFETIME)) {
		return -EIO;
	}
	nrf_gzll_set_max_tx_attempts(KBD_GZLL_MAX_TX_ATTEMPTS);
	head = count = 0;
	busy = keep_alive = connected = retry_wait = false;
	discard_in_flight = false;
	refresh_index = failures = led_state = 0;
	memset(confirmed, 0, sizeof(confirmed));
	confirmed[0].data[0] = KBD_HID_REPORT_ID_KEYBOARD;
	confirmed[1].data[0] = KBD_HID_REPORT_ID_CONSUMER;
	atomic_set(&completion, TX_NONE);
	last_failure_ms = k_uptime_get_32();
	last_tx_ms = last_failure_ms - KBD_WIRELESS_KEEP_ALIVE_PERIOD_MS;
	if (!nrf_gzll_enable()) {
		return -EIO;
	}
	running = true;
	kbd_gzll_process();
	return 0;
}
