#ifndef KBD_DEFINE_H
#define KBD_DEFINE_H

/*
 * Keyboard matrix geometry.
 *
 * KBD_KEY_COUNT is the logical key count used by key_state/keymap/report code.
 * The current shift-register scan path returns one bit per key, packed into
 * bytes.
 */
#define KBD_KEY_COUNT 88
#define KBD_MATRIX_SCAN_BYTES (KBD_KEY_COUNT / 8)

/*
 * Matrix scan/debounce timing.
 *
 * The scan timer runs once per KBD_SCAN_PERIOD_US. Debounce code converts
 * KBD_DEBOUNCE_TIME_US into a counter threshold, so changing the scan period
 * keeps debounce time expressed in real microseconds.
 */
#define KBD_SCAN_PERIOD_US 1000
#define KBD_DEBOUNCE_TIME_US 2000
#define KBD_DEBOUNCE_COUNTER_THRESHOLD \
	(KBD_DEBOUNCE_TIME_US / KBD_SCAN_PERIOD_US)

/*
 * Define KBD_ENABLE_LOG in the build when firmware logs are needed.
 * Without it, project module logs are compiled out.
 */
#ifdef KBD_ENABLE_LOG
#define KBD_LOG_LEVEL LOG_LEVEL_INF
#else
#define KBD_LOG_LEVEL LOG_LEVEL_NONE
#endif

#define KBD_MAIN_THREAD_PRIORITY 2
#define KBD_RGB_THREAD_PRIORITY 7
#define KBD_RGB_THREAD_STACK_SIZE 1024
/*
 * RGB update rate. The RGB thread is lower priority than scan/report handling,
 * so visual effects should not delay keyboard input.
 */
#define KBD_RGB_THREAD_FPS 20
#define KBD_RGB_THREAD_PERIOD_MS (1000 / KBD_RGB_THREAD_FPS)

/*
 * 2.4G Gazell radio configuration.
 *
 * The dongle must use the same base address, pipe prefix, and hopping channel
 * table. The channel values are Gazell RF channel numbers.
 */
#define KBD_GZLL_PIPE_NUMBER 1
#define KBD_GZLL_BASE_ADDRESS_1 0xbc010827UL
#define KBD_GZLL_PIPE_PREFIX 0x27
#define KBD_GZLL_CHANNEL_COUNT 5
#define KBD_GZLL_CHANNEL_TABLE { 4, 25, 42, 63, 77 }

/*
 * The 2.4G body always sends the full keyboard report size.
 *
 * This keeps the dongle simple: it can forward the received payload directly
 * to USB without knowing the keyboard's keymap, layer rules, or report layout
 * decisions.
 */
#define KBD_GZLL_TX_PAYLOAD_BYTES KBD_HID_KEYBOARD_REPORT_BYTES

/*
 * Retry/hopping behavior follows Gazell device mode semantics.
 *
 * A finite retry count is important because TX failed callbacks are used by
 * the keyboard side to maintain the disconnect counter.
 */
#define KBD_GZLL_MAX_TX_ATTEMPTS 100
#define KBD_GZLL_TIMESLOTS_PER_CHANNEL 3
#define KBD_GZLL_OUT_OF_SYNC_TIMESLOTS_PER_CHANNEL 20
#define KBD_GZLL_SYNC_LIFETIME 45

/*
 * Dongle returns the HID keyboard LED output byte in the Gazell ACK payload.
 * bit0 NumLock, bit1 CapsLock, bit2 ScrollLock, bit3 Compose, bit4 Kana.
 */
#define KBD_GZLL_ACK_PAYLOAD_BYTES 1

/*
 * 2.4G connection supervision.
 *
 * TX success means the dongle is reachable and should reset the disconnect
 * counter. A failed normal key report may update the counter immediately,
 * because it represents real user input. A failed keep-alive packet should
 * update the counter only once per KBD_WIRELESS_DISCONNECT_COUNTER_PERIOD_MS,
 * otherwise the keep-alive traffic can make the keyboard disconnect too fast.
 *
 * Keep-alive packets are for radio link maintenance only. They must not reset
 * the wireless sleep/idle timer.
 */
#define KBD_WIRELESS_KEEP_ALIVE_PERIOD_MS 20
#define KBD_WIRELESS_DISCONNECT_COUNTER_PERIOD_MS 100
#define KBD_WIRELESS_DISCONNECT_COUNTER_THRESHOLD 20

#define KBD_HID_REPORT_ID_KEYBOARD 1
#define KBD_HID_REPORT_ID_CONSUMER 2

/*
 * USB HID report polling period.
 *
 * Select one report rate by uncommenting one line. The value is in
 * microseconds because Zephyr HID polling APIs and devicetree use us.
 */
/* #define KBD_HID_REPORT_POLLING_PERIOD_US 4000 */ /* 250 Hz */
/* #define KBD_HID_REPORT_POLLING_PERIOD_US 2000 */ /* 500 Hz */
#define KBD_HID_REPORT_POLLING_PERIOD_US 1000       /* 1000 Hz */

#define KBD_HID_KEYBOARD_BITMAP_BITS 120
#define KBD_HID_KEYBOARD_BITMAP_BYTES (KBD_HID_KEYBOARD_BITMAP_BITS / 8)
#define KBD_HID_KEYBOARD_REPORT_BYTES (1 + 1 + KBD_HID_KEYBOARD_BITMAP_BYTES)
#define KBD_HID_CONSUMER_REPORT_BYTES 2

#define KBD_HID_KEYBOARD_USAGE_MAX 0x77
#define KBD_HID_KEYBOARD_BITMAP_BIT_OFFSET 16

#endif /* KBD_DEFINE_H */
