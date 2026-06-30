#include <hid_descriptor.h>

#include <kbd_define.h>

/*
 * Composite HID descriptor used by wired USB mode.
 *
 * Report ID 1 is an NKRO keyboard report:
 *   byte 0: report ID
 *   byte 1: modifier bits
 *   byte 2..16: keyboard usage bitmap
 *
 * Report ID 2 is a compact consumer-control bitmap for media keys.
 */
const uint8_t hid_report_desc[] = {
    0x05, 0x01, /* Usage Page (Generic Desktop) */
    0x09, 0x06, /* Usage (Keyboard) */
    0xa1, 0x01, /* Collection (Application) */
    0x85, KBD_HID_REPORT_ID_KEYBOARD,

    0x05, 0x07, /* Usage Page (Keyboard/Keypad) */
    0x75, 0x01, /* Report Size (1) */
    0x95, 0x08, /* Report Count (8) */
    0x19, 0xe0, /* Usage Minimum (Left Control) */
    0x29, 0xe7, /* Usage Maximum (Right GUI) */
    0x15, 0x00, /* Logical Minimum (0) */
    0x25, 0x01, /* Logical Maximum (1) */
    0x81, 0x02, /* Input (Data, Variable, Absolute) */

    0x95, KBD_HID_KEYBOARD_BITMAP_BITS,
    0x75, 0x01, /* Report Size (1) */
    0x15, 0x00, /* Logical Minimum (0) */
    0x25, 0x01, /* Logical Maximum (1) */
    0x05, 0x07, /* Usage Page (Keyboard/Keypad) */
    0x19, 0x00, /* Usage Minimum (Reserved) */
    0x29, KBD_HID_KEYBOARD_USAGE_MAX,
    0x81, 0x02, /* Input (Data, Variable, Absolute) */

    0x95, 0x05, /* Report Count (5) */
    0x75, 0x01, /* Report Size (1) */
    0x05, 0x08, /* Usage Page (LEDs) */
    0x19, 0x01, /* Usage Minimum (Num Lock) */
    0x29, 0x05, /* Usage Maximum (Kana) */
    0x91, 0x02, /* Output (Data, Variable, Absolute) */
    0x95, 0x01, /* Report Count (1) */
    0x75, 0x03, /* Report Size (3) */
    0x91, 0x03, /* Output (Constant, Variable, Absolute) */
    0xc0,       /* End Collection */

    0x05, 0x0c, /* Usage Page (Consumer) */
    0x09, 0x01, /* Usage (Consumer Control) */
    0xa1, 0x01, /* Collection (Application) */
    0x85, KBD_HID_REPORT_ID_CONSUMER,
    0x05, 0x0c,       /* Usage Page (Consumer) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x08,       /* Report Count (8) */
    0x09, 0xe9,       /* Usage (Volume Increment) */
    0x09, 0xea,       /* Usage (Volume Decrement) */
    0x09, 0xe2,       /* Usage (Mute) */
    0x09, 0xcd,       /* Usage (Play/Pause) */
    0x09, 0xb7,       /* Usage (Stop) */
    0x09, 0xb5,       /* Usage (Scan Next Track) */
    0x09, 0xb6,       /* Usage (Scan Previous Track) */
    0x0a, 0x92, 0x01, /* Usage (Calculator) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0xc0,             /* End Collection */
};

const size_t hid_report_desc_size = sizeof(hid_report_desc);
