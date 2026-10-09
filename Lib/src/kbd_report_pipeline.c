#include <kbd_report_pipeline.h>
#include <stddef.h>
#include <string.h>

_Static_assert(KBD_PHYSICAL_REPORT_QUEUE_SIZE >= 2, "Need at least two physical states");

struct physical_report {
	uint8_t keyboard[KBD_HID_KEYBOARD_REPORT_BYTES];
	uint8_t consumer[KBD_HID_CONSUMER_REPORT_BYTES];
};

static struct physical_report latest;
static struct physical_report pending[KBD_PHYSICAL_REPORT_QUEUE_SIZE];
static size_t head, count;
static uint64_t next_report_us;
static uint32_t report_period_us;

void kbd_report_physical_reset(const uint8_t *keyboard, const uint8_t *consumer)
{
	memcpy(latest.keyboard, keyboard, sizeof(latest.keyboard));
	memcpy(latest.consumer, consumer, sizeof(latest.consumer));
	head = 0;
	/* The new session's baseline precedes any subsequently captured press. */
	pending[0] = latest;
	count = 1;
}

bool kbd_report_capture(const uint8_t *keyboard, const uint8_t *consumer)
{
	if (!memcmp(latest.keyboard, keyboard, sizeof(latest.keyboard)) &&
	    !memcmp(latest.consumer, consumer, sizeof(latest.consumer))) {
		return true;
	}
	memcpy(latest.keyboard, keyboard, sizeof(latest.keyboard));
	memcpy(latest.consumer, consumer, sizeof(latest.consumer));
	bool room = count < KBD_PHYSICAL_REPORT_QUEUE_SIZE;
	size_t slot = (head + (room ? count : count - 1)) % KBD_PHYSICAL_REPORT_QUEUE_SIZE;
	pending[slot] = latest;
	if (room) {
		count++;
	}
	return room;
}

void kbd_report_physical_get(uint8_t *keyboard, uint8_t *consumer)
{
	const struct physical_report *report = count ? &pending[head] : &latest;
	memcpy(keyboard, report->keyboard, sizeof(report->keyboard));
	memcpy(consumer, report->consumer, sizeof(report->consumer));
}

void kbd_report_physical_accepted(void)
{
	if (count) {
		head = (head + 1) % KBD_PHYSICAL_REPORT_QUEUE_SIZE;
		count--;
	}
}

bool kbd_report_due(uint64_t now_us, uint32_t period_us)
{
	if (!period_us) {
		return false;
	}
	if (period_us != report_period_us) {
		report_period_us = period_us;
		next_report_us = now_us + period_us;
		return true;
	}
	if (now_us < next_report_us) {
		return false;
	}
	next_report_us += ((now_us - next_report_us) / period_us + 1U) * period_us;
	return true;
}
