#include <knob.h>
#include <kbd_define.h>
#include <keymap.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(knob, KBD_LOG_LEVEL);

#define KNOB_NODE DT_ALIAS(kbd_knob)
K_MSGQ_DEFINE(knob_events, sizeof(int32_t), 16, sizeof(int32_t));
static int32_t remaining;
static uint16_t pending_code = KBD_KEY_NONE;
static uint8_t active_bit;
static uint8_t sent_consumer_bits;
static bool releasing;
static atomic_t activity_epoch;

static void knob_input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);
	if (evt->type != INPUT_EV_REL || evt->code != INPUT_REL_WHEEL || !evt->value) {
		return;
	}
	if (k_msgq_put(&knob_events, &evt->value, K_NO_WAIT) != 0) {
		LOG_WRN("Knob event queue full");
	}
	atomic_inc(&activity_epoch);
}

uint32_t knob_activity_epoch(void)
{
	return (uint32_t)atomic_get(&activity_epoch);
}

int knob_suspend(void)
{
	return pm_device_action_run(DEVICE_DT_GET(KNOB_NODE), PM_DEVICE_ACTION_SUSPEND);
}

/* Never accept unrelated gpio-keys events from the DK or other devices. */
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(KNOB_NODE), knob_input_cb, NULL);

int knob_init(void)
{
	return device_is_ready(DEVICE_DT_GET(KNOB_NODE)) ? 0 : -ENODEV;
}

void knob_reset(void)
{
	k_msgq_purge(&knob_events);
	remaining = 0;
	pending_code = KBD_KEY_NONE;
	active_bit = 0;
	sent_consumer_bits = 0;
	releasing = false;
}

uint8_t knob_report_apply(uint8_t matrix_bits)
{
	if (!active_bit) {
		if (!remaining && k_msgq_get(&knob_events, &remaining, K_NO_WAIT) != 0) {
			return matrix_bits;
		}
		if (pending_code == KBD_KEY_NONE) {
			/* Positive gpio-qdec motion is CCW on this PCB; negative is CW.
			 * Resolve once per step, including any wait for a held matrix key.
			 * Later Fn changes must not redirect this pending press/release.
			 */
			enum kbd_knob_direction direction = remaining > 0 ?
				KBD_KNOB_CCW : KBD_KNOB_CW;
			pending_code = keymap_get_knob_code(keymap_get_active_layer(), direction);
			if (pending_code < CONSUMER_VOLUME_INCREASE ||
			    pending_code > CONSUMER_AL_CALCULATOR) {
				if (pending_code != KBD_KEY_NONE) {
					LOG_WRN("Unsupported knob keymap code %u", pending_code);
				}
				/* Drop this step safely, including an explicitly disabled mapping. */
				remaining += remaining > 0 ? -1 : 1;
				pending_code = KBD_KEY_NONE;
				return matrix_bits;
			}
		}
		uint8_t bit = BIT(pending_code - CONSUMER_VOLUME_INCREASE);
		/* Wait for both physical release and its successful report before
		 * starting a pulse on the same bit, otherwise the press is unchanged.
		 */
		if ((matrix_bits | sent_consumer_bits) & bit) {
			return matrix_bits;
		}
		active_bit = bit;
		pending_code = KBD_KEY_NONE;
		remaining += remaining > 0 ? -1 : 1;
	}
	return matrix_bits | (releasing ? 0 : active_bit);
}

void knob_report_sent(uint8_t sent_bits)
{
	sent_consumer_bits = sent_bits;
	if (!active_bit) {
		return;
	}
	if (!releasing && (sent_bits & active_bit)) {
		releasing = true;
	} else if (releasing && !(sent_bits & active_bit)) {
		active_bit = 0;
		releasing = false;
	}
}
