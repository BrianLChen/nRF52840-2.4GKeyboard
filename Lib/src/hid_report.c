#include <hid_report.h>
#include <key_state.h>
#include <keymap.h>
#include <string.h>

static void set_code(uint16_t code, uint8_t *keyboard, uint8_t *consumer)
{
	if (code == KBD_KEY_NONE || code == KBD_KEY_FN ||
	    code == KBD_KEY_PAIRING || code == KBD_KEY_DEVICE_SWITCH) {
		return;
	}
	if (code >= CONSUMER_VOLUME_INCREASE && code <= CONSUMER_AL_CALCULATOR) {
		consumer[1] |= 1U << (code - CONSUMER_VOLUME_INCREASE);
	} else if (code >= 8U && code < KBD_HID_KEYBOARD_REPORT_BYTES * 8U) {
		/* Check before narrowing: keymap codes are bitmap positions. */
		keyboard[code / 8U] |= 1U << (code % 8U);
	}
}

void hid_report_build_nkro(uint8_t keyboard[KBD_HID_KEYBOARD_REPORT_BYTES],
			   uint8_t consumer[KBD_HID_CONSUMER_REPORT_BYTES])
{
	struct key_state *keys = key_state_buffer_get();
	uint8_t layer = keymap_get_active_layer();

	memset(keyboard, 0, KBD_HID_KEYBOARD_REPORT_BYTES);
	memset(consumer, 0, KBD_HID_CONSUMER_REPORT_BYTES);
	keyboard[0] = KBD_HID_REPORT_ID_KEYBOARD;
	consumer[0] = KBD_HID_REPORT_ID_CONSUMER;
	for (uint16_t key = 0; key < KBD_KEY_COUNT; key++) {
		if (keys[key].press_status && !keymap_is_fn_key(key) &&
		    !keymap_is_action_key(key)) {
			set_code(keymap_get_code(layer, key), keyboard, consumer);
		}
	}
}

void hid_report_nkro_to_6kro(const uint8_t nkro[KBD_HID_KEYBOARD_REPORT_BYTES],
			   uint8_t six_kro[KBD_HID_6KRO_REPORT_BYTES])
{
	uint8_t count = 0;

	memset(six_kro, 0, KBD_HID_6KRO_REPORT_BYTES);
	six_kro[0] = nkro[1];
	for (uint16_t usage = 1; usage <= KBD_HID_KEYBOARD_USAGE_MAX; usage++) {
		uint16_t bit = usage + KBD_HID_KEYBOARD_BITMAP_BIT_OFFSET;

		if (!(nkro[bit / 8U] & (1U << (bit % 8U)))) {
			continue;
		}
		if (count == KBD_HID_6KRO_KEY_COUNT) {
			memset(&six_kro[2], 0x01, KBD_HID_6KRO_KEY_COUNT);
			return;
		}
		six_kro[2 + count++] = (uint8_t)usage;
	}
}
