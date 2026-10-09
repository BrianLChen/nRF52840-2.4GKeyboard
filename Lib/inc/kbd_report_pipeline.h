#ifndef KBD_REPORT_PIPELINE_H
#define KBD_REPORT_PIPELINE_H

#include <stdbool.h>
#include <stdint.h>
#include <kbd_define.h>

/* Single scan-thread owner. Matrix states contain no macro/knob pulses.
 * Reset drops old session input and installs a current-state baseline,
 * without changing the report clock.
 */
void kbd_report_physical_reset(const uint8_t *keyboard, const uint8_t *consumer);
/* Save a debounced transition, including a release between report slots.
 * False means the full queue's newest slot was replaced by the latest state;
 * older transitions remain ordered, and the final release is retained.
 */
bool kbd_report_capture(const uint8_t *keyboard, const uint8_t *consumer);
void kbd_report_physical_get(uint8_t *keyboard, uint8_t *consumer);
/* Retire the head only after both report types have been accepted (or are
 * unchanged). Transport backpressure must leave it pending for the next slot.
 */
void kbd_report_physical_accepted(void);
/* Monotonic microseconds. First call/period change is immediately eligible.
 * Missed slots are skipped, never burst-replayed, without shifting the phase.
 */
bool kbd_report_due(uint64_t now_us, uint32_t period_us);

#endif
