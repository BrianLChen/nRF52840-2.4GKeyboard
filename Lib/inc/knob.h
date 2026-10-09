#ifndef KBD_KNOB_H
#define KBD_KNOB_H

#include <stdint.h>

int knob_init(void);
uint32_t knob_activity_epoch(void);
int knob_suspend(void);
/* Scan thread only; merge consumer pulses selected by keymap's knob_map.
 * Resolve the current Fn layer once per step; preserve matrix consumer bits.
 */
uint8_t knob_report_apply(uint8_t matrix_bits);
/* Advance after USB submit / reliable Gazell queue / BLE queue accepts it. */
void knob_report_sent(uint8_t sent_bits);
/* Scan thread only: discard motion belonging to an unavailable/old host. */
void knob_reset(void);

#endif
