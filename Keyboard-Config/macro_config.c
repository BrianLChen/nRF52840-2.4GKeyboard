#include <kbd_macro.h>
#include <keymap.h>

/* Fn+F6 test: hold W physically while playing to verify independent input.
 * Nominal timeline: A -> A+B -> B -> none -> C+D -> none.
 * Firmware gaps are minimums; scan quantization and transport drain add time.
 */
static const struct kbd_macro_frame demo_frames[] = {
	{ .at_ms =   0, .key_count = 1, .keys = { KEYBOARD_A } },
	{ .at_ms =  80, .key_count = 2, .keys = { KEYBOARD_A, KEYBOARD_B } },
	{ .at_ms = 160, .key_count = 1, .keys = { KEYBOARD_B } },
	{ .at_ms = 260, .key_count = 0 },
	{ .at_ms = 400, .key_count = 2, .keys = { KEYBOARD_C, KEYBOARD_D } },
	{ .at_ms = 480, .key_count = 0 },
};

static const struct kbd_macro_program programs[KBD_MACRO_SLOT_COUNT] = {
	[0] = { .frames = demo_frames,
		.frame_count = sizeof(demo_frames) / sizeof(demo_frames[0]) },
	/* Slots 1..3 are disabled until given a valid frame array. */
};

const struct kbd_macro_program *kbd_macro_program_get(uint8_t id)
{
	return id < KBD_MACRO_SLOT_COUNT ? &programs[id] : NULL;
}
