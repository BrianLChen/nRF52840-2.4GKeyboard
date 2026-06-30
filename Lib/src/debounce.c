#include "debounce.h"

#include <kbd_define.h>
#include <key_state.h>

bool debounce_update(void)
{
	bool changed = false;
	struct key_state *keys = key_state_buffer_get();

	for (uint16_t i = 0; i < KBD_KEY_COUNT; i++) {
		struct key_state *key = &keys[i];

		if (key->scan_status) {
			if (key->debounce_counter < KBD_DEBOUNCE_COUNTER_THRESHOLD) {
				key->debounce_counter++;
			}

			if (!key->press_status &&
			    key->debounce_counter >= KBD_DEBOUNCE_COUNTER_THRESHOLD) {
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
