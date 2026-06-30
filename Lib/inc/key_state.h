#ifndef KEY_STATE_H
#define KEY_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include <kbd_define.h>

struct key_state {
	bool scan_status;
	uint8_t debounce_counter;
	bool press_status;
};

struct key_state *key_state_buffer_get(void);
struct key_state *key_state_get(uint16_t key_index);

#endif /* KEY_STATE_H */
