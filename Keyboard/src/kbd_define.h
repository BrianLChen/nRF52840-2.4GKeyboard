#ifndef KBD_DEFINE_H
#define KBD_DEFINE_H

#define KBD_KEY_COUNT 88
#define KBD_MATRIX_SCAN_BYTES (KBD_KEY_COUNT / 8)

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
#define KBD_RGB_THREAD_FPS 20
#define KBD_RGB_THREAD_PERIOD_MS (1000 / KBD_RGB_THREAD_FPS)

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

#define KBD_HID_KEYBOARD_BITMAP_BITS 120
#define KBD_HID_KEYBOARD_BITMAP_BYTES (KBD_HID_KEYBOARD_BITMAP_BITS / 8)
#define KBD_HID_KEYBOARD_REPORT_BYTES (1 + 1 + KBD_HID_KEYBOARD_BITMAP_BYTES)
#define KBD_HID_CONSUMER_REPORT_BYTES 2

#define KBD_HID_KEYBOARD_USAGE_MAX 0x77
#define KBD_HID_KEYBOARD_BITMAP_BIT_OFFSET 16

#endif /* KBD_DEFINE_H */
