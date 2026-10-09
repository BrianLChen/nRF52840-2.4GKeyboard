#include "debounce.h"

#include <kbd_define.h>
#include <key_state.h>
#include <zephyr/sys/util.h>

static uint8_t debounce_threshold = MAX(KBD_DEBOUNCE_MIN_SAMPLES,
				       KBD_DEBOUNCE_COUNTER_THRESHOLD);

void debounce_set_scan_period_us(uint32_t period_us)
{
	struct key_state *keys = key_state_buffer_get();

	if (!period_us) {
		return;
	}
	uint32_t threshold = DIV_ROUND_UP(KBD_DEBOUNCE_TIME_US, period_us);
	debounce_threshold = (uint8_t)CLAMP(threshold, KBD_DEBOUNCE_MIN_SAMPLES, UINT8_MAX);
	/* Discard partial debounce progress in old sample units, not the stable
	 * pressed state. This cannot synthesize release/press edges on a rate change.
	 */
	for (uint16_t i = 0; i < KBD_KEY_COUNT; i++) {
		keys[i].debounce_counter = keys[i].press_status ? debounce_threshold : 0;
	}
}

bool debounce_update(void)
{
	bool changed = false;
	struct key_state *keys = key_state_buffer_get();

	for (uint16_t i = 0; i < KBD_KEY_COUNT; i++) {
		struct key_state *key = &keys[i];

		if (key->scan_status) {
			if (key->debounce_counter < debounce_threshold) {
				key->debounce_counter++;
			}

			if (!key->press_status &&
			    key->debounce_counter >= debounce_threshold) {
				key->press_status = true;
				changed = true;
			}
		} else {
			if (key->debounce_counter > 0U) {
				key->debounce_counter--;
			}

			if (key->press_status && key->debounce_counter == 0U) {
				key->press_status = false;
				changed = true;
			}
		}
	}

	return changed;
}
