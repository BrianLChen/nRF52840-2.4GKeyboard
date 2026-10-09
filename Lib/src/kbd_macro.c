#include <kbd_macro.h>
#include <keymap.h>
#include <string.h>

static bool enabled;
static bool running, pending, draining, final_frame;
static const struct kbd_macro_program *program;
static size_t frame_index;
static uint64_t deadline_ms;
static uint8_t held[KBD_HID_KEYBOARD_REPORT_BYTES];

static void reset_runtime(void)
{
	running = pending = draining = final_frame = false;
	program = NULL;
	frame_index = 0;
	deadline_ms = 0;
	memset(held, 0, sizeof(held));
}

static bool program_valid(const struct kbd_macro_program *candidate)
{
	if (!candidate || !candidate->frames || !candidate->frame_count ||
	    candidate->frame_count > KBD_MACRO_MAX_FRAMES ||
	    candidate->frames[candidate->frame_count - 1].key_count != 0) {
		return false;
	}
	for (size_t i = 0; i < candidate->frame_count; i++) {
		const struct kbd_macro_frame *frame = &candidate->frames[i];
		if (frame->key_count > KBD_MACRO_KEYS_PER_FRAME ||
		    (i && frame->at_ms <= candidate->frames[i - 1].at_ms)) {
			return false;
		}
		for (size_t j = 0; j < frame->key_count; j++) {
			uint16_t code = frame->keys[j];
			bool modifier = code >= MODIFIER_LEFT_CTRL && code <= MODIFIER_RIGHT_UI;
			bool ordinary = code >= KEYBOARD_A &&
				code <= KBD_HID_KEYBOARD_USAGE_MAX + KBD_HID_KEYBOARD_BITMAP_BIT_OFFSET;
			if (!modifier && !ordinary) {
				return false; /* No Fn, actions, media keys or HID error usages. */
			}
		}
	}
	return true;
}

void kbd_macro_set_enabled(bool available)
{
	enabled = available;
	if (!enabled) {
		reset_runtime();
	}
}

bool kbd_macro_start(uint8_t id, uint64_t now_ms)
{
	if (!enabled || running || id >= KBD_MACRO_SLOT_COUNT) {
		return false;
	}
	const struct kbd_macro_program *candidate = kbd_macro_program_get(id);
	if (!program_valid(candidate)) {
		return false;
	}
	reset_runtime();
	program = candidate;
	running = true;
	deadline_ms = now_ms + program->frames[0].at_ms;
	return true;
}

void kbd_macro_cancel(void)
{
	if (!running || program == NULL) {
		return;
	}
	memset(held, 0, sizeof(held));
	program = NULL;
	pending = final_frame = true;
	draining = false;
}

bool kbd_macro_active(void)
{
	return running;
}

void kbd_macro_apply(uint8_t report[KBD_HID_KEYBOARD_REPORT_BYTES],
		     uint64_t now_ms, bool transport_idle,
		     uint8_t ordinary_key_limit)
{
	if (!running) {
		return;
	}
	if (draining && transport_idle) {
		draining = false;
		if (final_frame) {
			reset_runtime();
			return;
		}
		/* Start the next gap after drain, avoiding compressed pulses on a
		 * slow link. Physical traffic can stretch the macro, never stop scan.
		 */
		deadline_ms = now_ms + program->frames[frame_index].at_ms -
			program->frames[frame_index - 1].at_ms;
	}
	if (!pending && !draining && transport_idle && now_ms >= deadline_ms) {
		const struct kbd_macro_frame *frame = &program->frames[frame_index];
		memset(held, 0, sizeof(held));
		for (size_t i = 0; i < frame->key_count; i++) {
			uint16_t code = frame->keys[i];
			held[code / 8U] |= 1U << (code % 8U);
		}
		pending = true;
		final_frame = frame_index + 1 == program->frame_count;
	}
	if (ordinary_key_limit) {
		unsigned int count = 0;
		for (size_t i = 2; i < sizeof(held); i++) {
			uint8_t bits = report[i] | held[i];
			for (; bits; bits &= (uint8_t)(bits - 1U)) {
				count++;
			}
		}
		if (count > ordinary_key_limit) {
			kbd_macro_cancel();
		}
	}
	/* Never merge byte 0 (report ID), and never change physical state. */
	for (size_t i = 1; i < sizeof(held); i++) {
		report[i] |= held[i];
	}
}

void kbd_macro_report_accepted(void)
{
	if (running && pending) {
		pending = false;
		draining = true;
		frame_index++;
	}
}
