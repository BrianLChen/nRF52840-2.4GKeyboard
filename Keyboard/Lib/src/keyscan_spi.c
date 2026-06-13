#include "keyscan_spi.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <key_state.h>

LOG_MODULE_REGISTER(keyscan_spi, KBD_LOG_LEVEL);

#define KEYSCAN_SPI_NODE DT_ALIAS(keyscan_spi)
#define KEYSCAN_USER_NODE DT_PATH(zephyr_user)

static const struct device *const scan_spi = DEVICE_DT_GET(KEYSCAN_SPI_NODE);
static const struct gpio_dt_spec scan_load =
	GPIO_DT_SPEC_GET(KEYSCAN_USER_NODE, keyscan_load_gpios);

static const struct spi_config scan_spi_cfg = {
	.frequency = 4000000U,
	.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
	.slave = 0,
};

static uint8_t tx_buf[1];
static uint8_t scan_state[KEYSCAN_SCAN_BYTES];
static uint8_t prev_scan_state[KEYSCAN_SCAN_BYTES];

static void keyscan_update_key_states(void)
{
	struct key_state *keys = key_state_buffer_get();

	for (uint16_t key = 0; key < KBD_KEY_COUNT; key++) {
		uint8_t byte_index = key / 8U;
		uint8_t bit_mask = BIT(key % 8U);

		keys[key].scan_status = (scan_state[byte_index] & bit_mask) != 0U;
	}
}

int keyscan_init(void)
{
	int ret;

	if (!device_is_ready(scan_spi)) {
		LOG_ERR("Shift-register scan SPI device is not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&scan_load)) {
		LOG_ERR("Shift-register load GPIO device is not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&scan_load, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure shift-register load pin, %d", ret);
		return ret;
	}

	ret = keyscan_read();
	if (ret != 0) {
		LOG_ERR("Initial shift-register scan failed, %d", ret);
		return ret;
	}

	memcpy(prev_scan_state, scan_state, sizeof(prev_scan_state));

	LOG_INF("Shift-register SPI scan initialized");

	return 0;
}

int keyscan_read(void)
{
	const struct spi_buf tx = {
		.buf = tx_buf,
		.len = sizeof(tx_buf),
	};
	const struct spi_buf rx = {
		.buf = scan_state,
		.len = sizeof(scan_state),
	};
	const struct spi_buf_set tx_set = {
		.buffers = &tx,
		.count = 1,
	};
	const struct spi_buf_set rx_set = {
		.buffers = &rx,
		.count = 1,
	};
	int ret;

	gpio_pin_set_dt(&scan_load, 1);
	ret = spi_transceive(scan_spi, &scan_spi_cfg, &tx_set, &rx_set);
	gpio_pin_set_dt(&scan_load, 0);

	if (ret == 0) {
		keyscan_update_key_states();
	}

	return ret;
}

void keyscan_log_changes(void)
{
	for (int i = 0; i < KEYSCAN_SCAN_BYTES; i++) {
		uint8_t changed = scan_state[i] ^ prev_scan_state[i];

		while (changed != 0U) {
			uint8_t bit = find_lsb_set(changed) - 1U;
			uint8_t mask = BIT(bit);
			uint8_t key = (i * 8U) + bit;
			bool pressed = (scan_state[i] & mask) != 0U;

			LOG_INF("matrix key %u %s", key,
				pressed ? "pressed" : "released");
			changed &= ~mask;
		}
	}

	memcpy(prev_scan_state, scan_state, sizeof(prev_scan_state));
}

const uint8_t *keyscan_state_get(void)
{
	return scan_state;
}
