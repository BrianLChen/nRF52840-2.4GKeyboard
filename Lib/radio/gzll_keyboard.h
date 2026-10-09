#ifndef KBD_GZLL_KEYBOARD_H
#define KBD_GZLL_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* All APIs are called by the scan thread, exclusively in 2.4G mode. */
int kbd_gzll_init(void);
/* Copies a complete HID report; -ENOBUFS leaves ownership with the caller. */
int kbd_gzll_submit(const uint8_t *report, size_t len);
/* Drain completion/ACK data, retry pending reports and send idle refreshes. */
void kbd_gzll_process(void);
uint8_t kbd_gzll_led_state(void);
bool kbd_gzll_connected(void);
/* No application reports awaiting radio ACK; keep-alives do not count. */
bool kbd_gzll_tx_idle(void);
/* On a lost link with an active macro, drop old composite snapshots. A packet
 * already in flight cannot be recalled, but is never retried/reconfirmed.
 * The caller must invalidate report_prev and submit fresh physical snapshots.
 */
void kbd_gzll_discard_pending(void);
/* Disable and wait for the disabled callback before peripheral shutdown. */
int kbd_gzll_stop(void);

#endif
