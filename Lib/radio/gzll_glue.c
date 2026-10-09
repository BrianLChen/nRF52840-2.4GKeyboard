/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#include <nrf_gzll_glue.h>
#include <gzll_glue.h>
#include <helpers/nrfx_gppi.h>
#include <hal/nrf_ppi.h>
#include <mpsl/mpsl_lib.h>
#include <multithreading_lock.h>
#include "radio_mode.h"

/* Local NCS 3.4.0 glue: MPSL owns the single RADIO vector at link time;
 * Gazell replaces its dynamic handler only after MPSL has been stopped.
 * This is exclusive boot-mode selection, not simultaneous radio operation.
 */
BUILD_ASSERT(IS_ENABLED(CONFIG_SOC_NRF52840));
BUILD_ASSERT(IS_ENABLED(CONFIG_MPSL_DYNAMIC_INTERRUPTS));
BUILD_ASSERT(!IS_ENABLED(CONFIG_GAZELL), "Do not link the SDK's static Gazell glue");

#if defined(CONFIG_ZERO_LATENCY_IRQS)
#define GAZELL_HIGH_IRQ_FLAGS IRQ_ZERO_LATENCY
#else
#define GAZELL_HIGH_IRQ_FLAGS 0
#endif

#define GAZELL_TIMER_IRQN           TIMER2_IRQn
NRF_TIMER_Type * const nrf_gzll_timer = NRF_TIMER2;
IRQn_Type        const nrf_gzll_timer_irqn = GAZELL_TIMER_IRQN;

#define GAZELL_SWI_IRQN             SWI0_IRQn
IRQn_Type        const nrf_gzll_swi_irqn   = GAZELL_SWI_IRQN;

__IOM uint32_t *nrf_gzll_ppi_eep0;
__IOM uint32_t *nrf_gzll_ppi_tep0;
__IOM uint32_t *nrf_gzll_ppi_eep1;
__IOM uint32_t *nrf_gzll_ppi_tep1;
__IOM uint32_t *nrf_gzll_ppi_eep2;
__IOM uint32_t *nrf_gzll_ppi_tep2;

uint32_t nrf_gzll_ppi_chen_msk_0_and_1;
uint32_t nrf_gzll_ppi_chen_msk_2;


static void gazell_radio_irq_handler(const void *args)
{
	ARG_UNUSED(args);
	nrf_gzll_radio_irq_handler();
}

ISR_DIRECT_DECLARE(gazell_timer_irq_handler)
{
	nrf_gzll_timer_irq_handler();

	return 0;
}

static void gazell_swi_irq_handler(void *args)
{
	ARG_UNUSED(args);

	nrf_gzll_swi_irq_handler();
}

static bool non_ble_prepared;

int kbd_radio_prepare_non_ble(void)
{
	if (non_ble_prepared) {
		return 0;
	}
	int err = MULTITHREADING_LOCK_ACQUIRE();
	if (err) {
		return err;
	}
	err = mpsl_lib_uninit();
	MULTITHREADING_LOCK_RELEASE();
	if (!err) {
		non_ble_prepared = true;
	}
	return err;
}

bool gzll_glue_init(void)
{
	bool is_ok = true;
	const struct device *clkctrl = DEVICE_DT_GET_ONE(nordic_nrf_clock);
	int err;
	nrf_ppi_channel_t ppi_channel[3];
	nrfx_gppi_handle_t handle;
	uint8_t i;

	if (!non_ble_prepared) {
		return false;
	}

	irq_disable(RADIO_IRQn);
	irq_disable(GAZELL_TIMER_IRQN);
	irq_disable(GAZELL_SWI_IRQN);

	IRQ_CONNECT(GAZELL_SWI_IRQN,
		    1,
		    gazell_swi_irq_handler,
		    NULL,
		    0);

	IRQ_DIRECT_CONNECT(GAZELL_TIMER_IRQN,
			   0,
			   gazell_timer_irq_handler,
			   GAZELL_HIGH_IRQ_FLAGS);

	/* MPSL already supplies ARM_IRQ_DIRECT_DYNAMIC_CONNECT for RADIO. */
	irq_connect_dynamic(RADIO_IRQn, 0, gazell_radio_irq_handler, NULL,
			    GAZELL_HIGH_IRQ_FLAGS);
	NVIC_ClearPendingIRQ(RADIO_IRQn);
	NVIC_ClearPendingIRQ(GAZELL_TIMER_IRQN);
	NVIC_ClearPendingIRQ(GAZELL_SWI_IRQN);

	if (!device_is_ready(clkctrl)) {
		return false;
	}

	for (i = 0; i < 3; i++) {
		err = nrfx_gppi_domain_conn_alloc(0, 0, &handle);
		if (err < 0) {
			while (i > 0) {
				nrfx_gppi_domain_conn_free((nrfx_gppi_handle_t)ppi_channel[--i]);
			}
			is_ok = false;
			break;
		}
		ppi_channel[i] = (nrf_ppi_channel_t)handle;
	}

	if (is_ok) {
		nrf_gzll_ppi_eep0 = &NRF_PPI->CH[ppi_channel[0]].EEP;
		nrf_gzll_ppi_tep0 = &NRF_PPI->CH[ppi_channel[0]].TEP;
		nrf_gzll_ppi_eep1 = &NRF_PPI->CH[ppi_channel[1]].EEP;
		nrf_gzll_ppi_tep1 = &NRF_PPI->CH[ppi_channel[1]].TEP;
		nrf_gzll_ppi_eep2 = &NRF_PPI->CH[ppi_channel[2]].EEP;
		nrf_gzll_ppi_tep2 = &NRF_PPI->CH[ppi_channel[2]].TEP;

		nrf_gzll_ppi_chen_msk_0_and_1 = ((1 << ppi_channel[0]) |
						 (1 << ppi_channel[1]));
		nrf_gzll_ppi_chen_msk_2 = (1 << ppi_channel[2]);
	}

	return is_ok;
}

void nrf_gzll_delay_us(uint32_t usec_to_wait)
{
	k_busy_wait(usec_to_wait);
}

void nrf_gzll_request_xosc(void)
{
	z_nrf_clock_bt_ctlr_hf_request();

	/* Wait 1.5ms with 9% tolerance.
	 * 1500 * 1.09 = 1635
	 */
	k_busy_wait(1635);
}

void nrf_gzll_release_xosc(void)
{
	z_nrf_clock_bt_ctlr_hf_release();
}
