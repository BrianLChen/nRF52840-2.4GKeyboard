#ifndef KBD_RADIO_MODE_H
#define KBD_RADIO_MODE_H

/* Gazell only: once after boot, before Gazell startup; bt_enable must not
 * have run. Do not call for USB: its MPSL-backed clock driver stays active.
 * Returning to USB/BLE requires the existing mode-switch reboot.
 */
int kbd_radio_prepare_non_ble(void);

#endif
