/* Bluetooth settings 的 RAM 事务：配对成功之前禁止临时身份落盘。 */
#ifndef PAIRING_STORE_H_
#define PAIRING_STORE_H_

#include <stddef.h>

void pairing_store_begin(void);
void pairing_store_discard(void);
int pairing_store_commit(const void *slot_record, size_t len);

#endif
