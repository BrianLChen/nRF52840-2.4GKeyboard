/*
 * Key scanning backend for keyboards that read shift registers through SPI.
 *
 * The keyboard switch states are latched by the shift register load pin, then
 * clocked into the MCU through the SPI MISO line.
 * The controller's cs-gpios entry 0 drives active-low 74HC165 CE#.
 */
#ifndef KEYSCAN_SPI_H
#define KEYSCAN_SPI_H

#include <stdint.h>

#include <kbd_define.h>

#include <zephyr/kernel.h>

#define KEYSCAN_KEY_COUNT KBD_KEY_COUNT
#define KEYSCAN_SCAN_BYTES KBD_MATRIX_SCAN_BYTES
#define KEYSCAN_INTERVAL K_USEC(KBD_SCAN_PERIOD_US)

int keyscan_init(void);
int keyscan_read(void);
void keyscan_log_changes(void);
/* Normalized scan bitmap: 1 = pressed, 0 = released. */
const uint8_t *keyscan_state_get(void);

#endif /* KEYSCAN_SPI_H */
