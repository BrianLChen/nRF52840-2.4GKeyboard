#ifndef KBD_MACRO_H
#define KBD_MACRO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kbd_define.h>

#define KBD_MACRO_SLOT_COUNT 4U
#define KBD_MACRO_KEYS_PER_FRAME 8U
#define KBD_MACRO_MAX_FRAMES 128U

/* Each frame replaces the complete macro-owned key set, not the physical set.
 * at_ms is a nominal offset. Adjacent offsets define minimum firmware gaps:
 * the next gap starts when the previous accepted frame's transport drains.
 * Use one frame for simultaneous keys, strictly increasing offsets, and an
 * empty final frame. Codes use keymap.h bitmap positions, NOT HID usages.
 */
struct kbd_macro_frame {
	uint32_t at_ms;
	uint8_t key_count;
	uint16_t keys[KBD_MACRO_KEYS_PER_FRAME];
};

struct kbd_macro_program {
	const struct kbd_macro_frame *frames;
	size_t frame_count;
};

const struct kbd_macro_program *kbd_macro_program_get(uint8_t id);

/* All runtime APIs are scan-thread only. No timers, threads or allocation.
 * Disabling forgets the old session; its queued reports must be invalidated
 * by the transport owner. Cancel instead submits a physical-only release in
 * the current session and waits for it to drain before allowing a restart.
 */
void kbd_macro_set_enabled(bool enabled);
bool kbd_macro_start(uint8_t id, uint64_t now_ms);
void kbd_macro_cancel(void);
bool kbd_macro_active(void);

/* Call on report-update slots AFTER obtaining a physical-only NKRO report.
 * transport_idle permits a new frame, never blocks physical input/retries.
 * ordinary_key_limit=0 means NKRO; BLE uses 6. If the union exceeds that
 * limit, cancel the macro and leave the physical report untouched.
 */
void kbd_macro_apply(uint8_t report[KBD_HID_KEYBOARD_REPORT_BYTES],
		     uint64_t now_ms, bool transport_idle,
		     uint8_t ordinary_key_limit);
/* Call only when this slot's complete merged keyboard report is accepted,
 * or equals an already accepted snapshot in the SAME transport session.
 * Never call this on a failed submit or for a consumer-only report.
 */
void kbd_macro_report_accepted(void);

#endif
