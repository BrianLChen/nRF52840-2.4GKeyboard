#ifndef KBD_KNOB_H
#define KBD_KNOB_H

#include <stdint.h>

int knob_init(void);
/* Called only by the scan thread; preserve matrix consumer bits. */
uint8_t knob_report_apply(uint8_t matrix_bits);
/* Advance a pulse only after USB submit / Gazell FIFO enqueue succeeds. */
void knob_report_sent(uint8_t sent_bits);

#endif
