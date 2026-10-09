#ifndef KBD_BLE_KEYBOARD_H
#define KBD_BLE_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>
#include <kbd_define.h>

/* Call once, only in BLE mode. Mode changes reboot the device. */
int kbd_ble_init(void);
void kbd_ble_handle_actions(uint8_t actions);
bool kbd_ble_keyboard_ready(void);
bool kbd_ble_consumer_ready(void);
/* Nonblocking scan-thread check: application TX queue drained for this epoch.
 * This is not a host receipt acknowledgment; SDK/controller buffering remains.
 */
bool kbd_ble_tx_idle(uint32_t input_epoch);
uint32_t kbd_ble_input_epoch(void);
uint8_t kbd_ble_led_state(void);
/* Fixed scan override, or negotiated interval / configured scan count.
 * The dynamic policy is clamped to the BLE scan limits.
 * Before connection, uses the preferred minimum interval as a fallback.
 */
uint32_t kbd_ble_scan_period_us(void);
/* Application report slots: fixed override or the legacy dynamic period,
 * independently of any fixed scan override.
 * Independent of scanning; actual delivery still follows BLE scheduling.
 */
uint32_t kbd_ble_report_period_us(void);
/* Deep sleep only. Call from the scan thread, never a callback/work item.
 * -EBUSY leaves BLE running. After success or any other error, reboot is
 * required before using BLE again. Saved pairings are preserved.
 */
bool kbd_ble_can_sleep(void);
int kbd_ble_stop(void);
/* Scan-thread APIs: 0 means unchanged or copied into the bounded TX queue.
 * A negative result leaves the caller's state pending for the next scan.
 * The queue is discarded on disconnect, pairing, slot/protocol/CCC changes.
 * Pass the epoch observed before preparing the report; -ESTALE rejects
 * reports prepared for a connection/protocol that changed in the meantime.
 */
int kbd_ble_submit_keyboard(const uint8_t report[KBD_HID_6KRO_REPORT_BYTES],
			    uint32_t input_epoch);
int kbd_ble_submit_consumer(uint8_t bits, uint32_t input_epoch);

#endif
