#ifndef HID_DESCRIPTOR_H
#define HID_DESCRIPTOR_H

#include <stddef.h>
#include <stdint.h>

/*
 * Shared HID report descriptor.
 *
 * Keeping the descriptor in Lib makes the report contract visible to both the
 * keyboard body and the dongle side. Transport-specific code should submit or
 * forward reports, but it should not redefine the report layout.
 */
extern const uint8_t hid_report_desc[];
extern const size_t hid_report_desc_size;
/* BLE Report Map: 6-KRO keyboard (ID 1) and consumer bitmap (ID 2).
 * GATT payloads exclude the ID byte; Boot keyboard uses the same 8 bytes.
 */
extern const uint8_t hid_6kro_report_desc[];
extern const size_t hid_6kro_report_desc_size;

#endif /* HID_DESCRIPTOR_H */
