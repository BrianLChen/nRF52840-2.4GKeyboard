#ifndef KBD_PAIRING_H_
#define KBD_PAIRING_H_

#include <zephyr/bluetooth/conn.h>

/* 在 bt_enable() / settings_load() 成功后初始化；失败时禁止启动广播。 */
/* release_keys 在管理线程中调用：尝试释放旧电脑按键，并清空本地 HID 状态。 */
int kbd_pairing_init(void (*release_keys)(struct bt_conn *conn));
void kbd_pairing_button(void);
void kbd_pairing_next_slot(void);
void kbd_pairing_connected(struct bt_conn *conn, uint8_t err);
void kbd_pairing_disconnected(struct bt_conn *conn);
void kbd_pairing_recycled(void);
void kbd_pairing_complete(struct bt_conn *conn, bool bonded);
void kbd_pairing_failed(struct bt_conn *conn);
bool kbd_pairing_accept(struct bt_conn *conn);
bool kbd_pairing_is_advertising(void);
bool kbd_pairing_can_send(struct bt_conn *conn);
/* Scan thread only, never a Bluetooth callback or system work item.
 * -EBUSY leaves pairing operational; other failures require reboot.
 * Success stops advertising and waits for the connection to be recycled.
 * No bond/slot is deleted. Only reboot may restart after success.
 */
bool kbd_pairing_can_poweroff(void);
int kbd_pairing_prepare_poweroff(void);

#endif
