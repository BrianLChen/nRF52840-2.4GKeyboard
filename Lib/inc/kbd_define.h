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
 * USB/Gazell have independent scan settings, separate from report rates.
 * BLE has a separate fixed/dynamic scan setting.
 * debounce_set_scan_period_us() scales the counter
 * threshold for the active period, with at least two samples per transition.
 */
#define KBD_USB_SCAN_PERIOD_US 200  /* 5 kHz */
#define KBD_GZLL_SCAN_PERIOD_US 200 /* 5 kHz */
/* BLE matrix scan configuration: edit Keyboard_Body/prj.conf.
 * CONFIG_KBD_BLE_FIXED_SCAN_PERIOD_US: fixed period in microseconds;
 *   200 selects a target of 5 kHz, 1000 selects 1 kHz.
 *   0 selects the dynamic policy: connection interval divided by
 *   CONFIG_KBD_BLE_SCANS_PER_INTERVAL, clamped between
 *   CONFIG_KBD_BLE_SCAN_MIN_US and CONFIG_KBD_BLE_SCAN_MAX_US.
 * Option definitions: Lib/bluetooth/Kconfig.
 * Runtime calculation: kbd_ble_scan_period_us() in Lib/bluetooth/ble_keyboard.c.
 * BLE report timing is configured separately by CONFIG_KBD_BLE_REPORT_PERIOD_US.
 */
/* Default debounce units/legacy keyscan interval; active modes set runtime units. */
#define KBD_SCAN_PERIOD_US KBD_USB_SCAN_PERIOD_US
/* Debounced physical transitions waiting for a report slot. */
#define KBD_PHYSICAL_REPORT_QUEUE_SIZE 32
#define KBD_DEBOUNCE_TIME_US 2000
#define KBD_DEBOUNCE_MIN_SAMPLES 2
/* Rounded-up default threshold; the runtime setter also enforces the minimum. */
#define KBD_DEBOUNCE_COUNTER_THRESHOLD \
	((KBD_DEBOUNCE_TIME_US + KBD_SCAN_PERIOD_US - 1) / KBD_SCAN_PERIOD_US)

/* Four A/B transitions per electrical cycle, matching one old A falling edge. */
#define KBD_KNOB_STEPS_PER_PERIOD 4
#define KBD_KNOB_SAMPLE_PERIOD_US 250

/* Restart this interval on every mode contact edge; require one valid contact. */
#define KBD_MODE_SWITCH_DEBOUNCE_MS 50

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
/* Application queue retains each report across failed Gazell TX attempts. */
#define KBD_GZLL_PENDING_REPORTS 32
#define KBD_GZLL_TIMESLOT_PERIOD_US 600
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
/* Physical input and active macros postpone sleep; radio traffic does not. */
#define KBD_WIRELESS_SLEEP_TIMEOUT_MS 180000U
#define KBD_WIRELESS_DISCONNECT_COUNTER_PERIOD_MS 100
#define KBD_WIRELESS_DISCONNECT_COUNTER_THRESHOLD 20

#define KBD_HID_REPORT_ID_KEYBOARD 1
#define KBD_HID_REPORT_ID_CONSUMER 2

/*
 * Application report-update periods. USB polling uses the same USB value.
 *
 * Change each mode's value independently. Values are in
 * microseconds because Zephyr HID polling APIs and devicetree use us.
 */
/* 4000 = 250 Hz; 2000 = 500 Hz; 1000 = 1000 Hz. */
#define KBD_USB_REPORT_PERIOD_US 1000
#define KBD_GZLL_REPORT_PERIOD_US 1000
#define KBD_HID_REPORT_POLLING_PERIOD_US KBD_USB_REPORT_PERIOD_US

/* Application report updates (not matrix scans or Gazell RF timeslots).
 * USB and Gazell are independently configurable above. BLE uses
 * CONFIG_KBD_BLE_FIXED_SCAN_PERIOD_US and CONFIG_KBD_BLE_REPORT_PERIOD_US
 * in Keyboard_Body/prj.conf (zero retains their legacy dynamic defaults).
 * A slot submits at most one physical state, with keyboard/consumer reports
 * tracked separately. Unchanged reports are still suppressed.
 */

#define KBD_HID_KEYBOARD_BITMAP_BITS 120
#define KBD_HID_KEYBOARD_BITMAP_BYTES (KBD_HID_KEYBOARD_BITMAP_BITS / 8)
#define KBD_HID_KEYBOARD_REPORT_BYTES (1 + 1 + KBD_HID_KEYBOARD_BITMAP_BYTES)
#define KBD_HID_CONSUMER_REPORT_BYTES 2
#define KBD_HID_6KRO_KEY_COUNT 6
#define KBD_HID_6KRO_REPORT_BYTES (2 + KBD_HID_6KRO_KEY_COUNT)

#define KBD_HID_KEYBOARD_USAGE_MAX 0x77
#define KBD_HID_KEYBOARD_BITMAP_BIT_OFFSET 16

#endif /* KBD_DEFINE_H */
