#ifndef DEBOUNCE_H
#define DEBOUNCE_H

#include <stdbool.h>
#include <stdint.h>

/* Scan thread only, before starting/changing the timer. Keeps stable states. */
void debounce_set_scan_period_us(uint32_t period_us);
bool debounce_update(void);

#endif /* DEBOUNCE_H */
