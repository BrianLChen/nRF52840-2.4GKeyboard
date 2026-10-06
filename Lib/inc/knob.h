#ifndef KBD_KNOB_H
#define KBD_KNOB_H

#include <stdint.h>
#include <stdbool.h>

int knob_init(void);
bool knob_has_pending(void);
/* Called only by the scan thread; preserve matrix consumer bits. */
uint8_t knob_report_apply(uint8_t matrix_bits);
/* Advance a pulse only after USB submit / Gazell FIFO enqueue succeeds. */
void knob_report_sent(uint8_t sent_bits);

#endif
