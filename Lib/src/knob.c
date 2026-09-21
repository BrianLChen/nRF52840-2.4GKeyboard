#include <knob.h>
#include <kbd_define.h>
#include <keymap.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(knob, KBD_LOG_LEVEL);

#define KNOB_NODE DT_ALIAS(kbd_knob)
K_MSGQ_DEFINE(knob_events, sizeof(int32_t), 16, sizeof(int32_t));
static int32_t remaining;
static uint8_t active_bit;
static uint8_t sent_consumer_bits;
static bool releasing;

static void knob_input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);
	if (evt->type != INPUT_EV_REL || evt->code != INPUT_REL_WHEEL || !evt->value) {
		return;
	}
	if (k_msgq_put(&knob_events, &evt->value, K_NO_WAIT) != 0) {
		LOG_WRN("Knob event queue full");
	}
}

/* Never accept unrelated gpio-keys events from the DK or other devices. */
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(KNOB_NODE), knob_input_cb, NULL);

int knob_init(void)
{
	return device_is_ready(DEVICE_DT_GET(KNOB_NODE)) ? 0 : -ENODEV;
}

uint8_t knob_report_apply(uint8_t matrix_bits)
{
	if (!active_bit) {
		if (!remaining && k_msgq_get(&knob_events, &remaining, K_NO_WAIT) != 0) {
			return matrix_bits;
		}
		/* With A first, positive gpio-qdec motion has B high at A falling:
		 * old nRF5 calls that CCW (volume down); negative is CW (volume up).
		 */
		uint16_t code = remaining > 0 ? CONSUMER_VOLUME_DECREASE :
					      CONSUMER_VOLUME_INCREASE;
		uint8_t bit = BIT(code - CONSUMER_VOLUME_INCREASE);
		/* Wait for both physical release and its successful report before
		 * starting a pulse on the same bit, otherwise the press is unchanged.
		 */
		if ((matrix_bits | sent_consumer_bits) & bit) {
			return matrix_bits;
		}
		active_bit = bit;
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
