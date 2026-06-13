#include "rgb.h"

#include <kbd_define.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rgb, KBD_LOG_LEVEL);

void rgb_effect_dummy(void)
{
	LOG_DBG("RGB thread: update RGB");
}

void rgb_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("RGB thread started");

	while (true) {
		rgb_effect_dummy();
		k_sleep(K_MSEC(KBD_RGB_THREAD_PERIOD_MS));
	}
}

K_THREAD_DEFINE(rgb_thread_id,
		KBD_RGB_THREAD_STACK_SIZE,
		rgb_thread_entry,
		NULL, NULL, NULL,
		KBD_RGB_THREAD_PRIORITY,
		0,
		0);
