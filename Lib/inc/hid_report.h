#ifndef KBD_HID_REPORT_H
#define KBD_HID_REPORT_H

#include <stdint.h>
#include <kbd_define.h>

/* Full USB/Gazell reports, including the report ID. Scan thread only. */
void hid_report_build_nkro(uint8_t keyboard[KBD_HID_KEYBOARD_REPORT_BYTES],
			   uint8_t consumer[KBD_HID_CONSUMER_REPORT_BYTES]);
/* BLE/Boot payload: modifiers, reserved, six HID usages; no report ID.
 * More than six ordinary keys produces ErrorRollOver in all six slots.
 * Input and output buffers must not overlap.
 */
void hid_report_nkro_to_6kro(const uint8_t nkro[KBD_HID_KEYBOARD_REPORT_BYTES],
			   uint8_t six_kro[KBD_HID_6KRO_REPORT_BYTES]);

#endif
