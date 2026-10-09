#include "keymap.h"

#include <stddef.h>
#include <string.h>

#include <key_state.h>

static bool fn_key_map[KBD_KEY_COUNT];
static bool key_pressed_prev[KBD_KEY_COUNT];
static bool action_key_held[KBD_KEY_COUNT];

/*
 * Rotary mapping: row 0 = normal, row 1 = Fn; CW/CCW follow the PCB's
 * existing direction convention. Keep both layers at volume by default.
 * Use CONSUMER_* codes from keymap.h, or KBD_KEY_NONE to disable a direction.
 * For example, Fn can use CONSUMER_NEXT_TRACK / CONSUMER_PREVIOUS_TRACK.
 * Keyboard codes, modifiers and internal actions are not supported here.
 */
static const uint16_t knob_map[KBD_KEYMAP_LAYER_COUNT][KBD_KNOB_DIRECTION_COUNT] = {
	[0] = {
		[KBD_KNOB_CW] = CONSUMER_VOLUME_INCREASE,
		[KBD_KNOB_CCW] = CONSUMER_VOLUME_DECREASE,
	},
	[1] = {
		[KBD_KNOB_CW] = CONSUMER_VOLUME_INCREASE,
		[KBD_KNOB_CCW] = CONSUMER_VOLUME_DECREASE,
	},
};

static const uint16_t key_table[KBD_KEYMAP_LAYER_COUNT][KBD_KEY_COUNT] = {
	{
		MODIFIER_LEFT_CTRL, MODIFIER_LEFT_UI, MODIFIER_LEFT_SHIFT, KEYBOARD_CAPS_LOCK,
		KEYBOARD_TAB, KEYBOARD_TILDE, KEYBOARD_ESCAPE, KEYBOARD_1,
		KEYBOARD_Q, KEYBOARD_A, KEYBOARD_Z, MODIFIER_LEFT_ALT,
		KEYBOARD_X, KEYBOARD_S, KEYBOARD_W, KEYBOARD_2,
		KEYBOARD_F1, KEYBOARD_F2, KEYBOARD_3, KEYBOARD_E,
		KEYBOARD_D, KEYBOARD_C, KEYBOARD_V, KEYBOARD_F,
		KEYBOARD_R, KEYBOARD_4, KEYBOARD_F3, KEYBOARD_F4,
		KEYBOARD_5, KEYBOARD_T, KEYBOARD_G, KEYBOARD_B,
		KEYBOARD_SPACEBAR, KEYBOARD_N, KEYBOARD_H, KEYBOARD_Y,
		KEYBOARD_6, KEYBOARD_F5, KEYBOARD_7, KEYBOARD_U,
		KEYBOARD_J, KEYBOARD_M, MODIFIER_RIGHT_ALT, KEYBOARD_COMMA,
		KEYBOARD_K, KEYBOARD_I, KEYBOARD_8, KEYBOARD_F6,
		KEYBOARD_F7, KEYBOARD_9, KEYBOARD_O, KEYBOARD_L,
		KEYBOARD_PERIOD, MODIFIER_RIGHT_UI, KBD_KEY_FN, KEYBOARD_SLASH,
		KEYBOARD_SEMI_COLON, KEYBOARD_P, KEYBOARD_0, KEYBOARD_F8,
		KEYBOARD_F9, KEYBOARD_MINUS, KEYBOARD_OPEN_BRACKET, KEYBOARD_QUOTE,
		KEYBOARD_CLOSE_BRACKET, KEYBOARD_PLUS, KEYBOARD_F10, KEYBOARD_F11,
		KEYBOARD_F12, KEYBOARD_BACKSPACE, KEYBOARD_BACKSLASH, KEYBOARD_ENTER,
		MODIFIER_RIGHT_SHIFT, MODIFIER_RIGHT_CTRL, KEYBOARD_LEFT, KEYBOARD_DOWN,
		KEYBOARD_UP, KEYBOARD_END, KEYBOARD_HOME, KEYBOARD_DELETE,
		KEYBOARD_PRINTSCREEN, KEYBOARD_PAGEUP, KEYBOARD_PAGEDOWN, KEYBOARD_RIGHT,
		CONSUMER_MUTE,
	},
	{
		MODIFIER_LEFT_CTRL, MODIFIER_LEFT_UI, MODIFIER_LEFT_SHIFT, KEYBOARD_CAPS_LOCK,
		KEYBOARD_TAB, KEYBOARD_TILDE, KEYBOARD_ESCAPE, KEYBOARD_1,
		KEYBOARD_Q, KEYBOARD_A, KEYBOARD_Z, MODIFIER_LEFT_ALT,
		KEYBOARD_X, KEYBOARD_S, KEYBOARD_W, KEYBOARD_2,
		KEYBOARD_F1, KEYBOARD_F2, KEYBOARD_3, KEYBOARD_E,
		KEYBOARD_D, KEYBOARD_C, KEYBOARD_V, KEYBOARD_F,
		KEYBOARD_R, KEYBOARD_4, KEYBOARD_F3, KBD_KEY_MACRO_0,
		KEYBOARD_5, KEYBOARD_T, KEYBOARD_G, KEYBOARD_B,
		KEYBOARD_SPACEBAR, KEYBOARD_N, KEYBOARD_H, KEYBOARD_Y,
		KEYBOARD_6, KEYBOARD_F5, KEYBOARD_7, KEYBOARD_U,
		KEYBOARD_J, KEYBOARD_M, MODIFIER_RIGHT_ALT, KEYBOARD_COMMA,
		KEYBOARD_K, KEYBOARD_I, KEYBOARD_8, KBD_KEY_MACRO_0,
		/* Fn+O: next device; Fn+P: enter/cancel pairing. Move these
		 * internal codes within this layer to choose different shortcuts.
		 */
		/* Fn+F4/F6: demo macro; Fn+F7: cancel. Normal F4/F6/F7 are unchanged. */
		KBD_KEY_MACRO_CANCEL, KEYBOARD_9, KBD_KEY_DEVICE_SWITCH, KEYBOARD_L,
		KEYBOARD_PERIOD, MODIFIER_RIGHT_UI, KBD_KEY_FN, KEYBOARD_SLASH,
		KEYBOARD_SEMI_COLON, KBD_KEY_PAIRING, KEYBOARD_0, KEYBOARD_F8,
		KEYBOARD_F9, KEYBOARD_MINUS, KEYBOARD_OPEN_BRACKET, KEYBOARD_QUOTE,
		KEYBOARD_CLOSE_BRACKET, KEYBOARD_PLUS, KEYBOARD_F10, KEYBOARD_F11,
		CONSUMER_AL_CALCULATOR, KEYBOARD_BACKSPACE, KEYBOARD_BACKSLASH, KEYBOARD_ENTER,
		MODIFIER_RIGHT_SHIFT, MODIFIER_RIGHT_CTRL, KEYBOARD_LEFT, KEYBOARD_DOWN,
		KEYBOARD_UP, KEYBOARD_END, KEYBOARD_HOME, KEYBOARD_INSERT,
		CONSUMER_PLAY_PAUSE, KEYBOARD_PAGEUP, KEYBOARD_PAGEDOWN, KEYBOARD_RIGHT,
		CONSUMER_MUTE,
	},
};

void keymap_init(void)
{
	memset(fn_key_map, 0, sizeof(fn_key_map));
	memset(key_pressed_prev, 0, sizeof(key_pressed_prev));
	memset(action_key_held, 0, sizeof(action_key_held));

	for (uint16_t key = 0; key < KBD_KEY_COUNT; key++) {
		for (uint8_t layer = 0; layer < KBD_KEYMAP_LAYER_COUNT; layer++) {
			if (key_table[layer][key] == KBD_KEY_FN) {
				fn_key_map[key] = true;
			}
		}
	}
}

bool keymap_is_fn_key(uint16_t key_index)
{
	if (key_index >= KBD_KEY_COUNT) {
		return false;
	}

	return fn_key_map[key_index];
}

uint16_t keymap_get_code(uint8_t layer, uint16_t key_index)
{
	if (layer >= KBD_KEYMAP_LAYER_COUNT || key_index >= KBD_KEY_COUNT) {
		return KBD_KEY_NONE;
	}

	return key_table[layer][key_index];
}

uint16_t keymap_get_knob_code(uint8_t layer, enum kbd_knob_direction direction)
{
	if (layer >= KBD_KEYMAP_LAYER_COUNT ||
	    (unsigned int)direction >= KBD_KNOB_DIRECTION_COUNT) {
		return KBD_KEY_NONE;
	}

	return knob_map[layer][direction];
}

uint8_t keymap_get_active_layer(void)
{
	struct key_state *keys = key_state_buffer_get();

	for (uint16_t key = 0; key < KBD_KEY_COUNT; key++) {
		if (fn_key_map[key] && keys[key].press_status) {
			return 1;
		}
	}

	return 0;
}

uint8_t keymap_update_actions(void)
{
	struct key_state *keys = key_state_buffer_get();
	uint8_t layer = keymap_get_active_layer();
	uint8_t actions = KBD_ACTION_NONE;

	for (uint16_t key = 0; key < KBD_KEY_COUNT; key++) {
		bool pressed = keys[key].press_status;

		if (!pressed) {
			action_key_held[key] = false;
		} else if (!key_pressed_prev[key] && !fn_key_map[key]) {
			/* Resolve on physical press: changing Fn alone cannot trigger
			 * pairing or device switching for an already held normal key.
			 */
			uint16_t code = keymap_get_code(layer, key);
			switch (code) {
			case KBD_KEY_PAIRING:
				actions |= KBD_ACTION_PAIRING;
				action_key_held[key] = true;
				break;
			case KBD_KEY_DEVICE_SWITCH:
				actions |= KBD_ACTION_DEVICE_SWITCH;
				action_key_held[key] = true;
				break;
			case KBD_KEY_MACRO_CANCEL:
				actions |= KBD_ACTION_MACRO_CANCEL;
				action_key_held[key] = true;
				break;
			default:
				if (code >= KBD_KEY_MACRO_0 && code <= KBD_KEY_MACRO_3) {
					actions |= KBD_ACTION_MACRO_0 << (code - KBD_KEY_MACRO_0);
					action_key_held[key] = true;
				}
				break;
			}
		}
		key_pressed_prev[key] = pressed;
	}

	return actions;
}

bool keymap_is_action_key(uint16_t key_index)
{
	return key_index < KBD_KEY_COUNT && action_key_held[key_index];
}
