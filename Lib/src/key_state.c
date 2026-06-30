#include "key_state.h"

#include <stddef.h>

static struct key_state keys[KBD_KEY_COUNT];

struct key_state *key_state_buffer_get(void)
{
	return keys;
}

struct key_state *key_state_get(uint16_t key_index)
{
	if (key_index >= KBD_KEY_COUNT) {
		return NULL;
	}

	return &keys[key_index];
}
