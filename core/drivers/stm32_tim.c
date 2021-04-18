// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2018-2020, STMicroelectronics
 */

#include <arm.h>
#include <drivers/clk.h>
#include <drivers/serial.h>
#include <drivers/stm32_tim.h>
#include <initcall.h>
#include <io.h>
#include <keep.h>
#include <kernel/boot.h>
#include <kernel/delay.h>
#include <kernel/dt.h>
#include <kernel/panic.h>
#include <libfdt.h>
#include <mm/core_memprot.h>
#include <stm32_util.h>

#define TIM_CR1			0x00		/* Control Register 1      */
#define TIM_CR2			0x04		/* Control Register 2      */
#define TIM_SMCR		0x08		/* Slave mode control reg  */
#define TIM_DIER		0x0C		/* DMA/interrupt register  */
#define TIM_SR			0x10		/* Status register	   */
#define TIM_EGR			0x14		/* Event Generation Reg    */
#define TIM_CCMR1		0x18		/* Capt/Comp 1 Mode Reg    */
#define TIM_CCMR2		0x1C		/* Capt/Comp 2 Mode Reg    */
#define TIM_CCER		0x20		/* Capt/Comp Enable Reg    */
#define TIM_CNT			0x24		/* Counter		   */
#define TIM_PSC			0x28		/* Prescaler               */
#define TIM_ARR			0x2C		/* Auto-Reload Register    */
#define TIM_CCR1		0x34		/* Capt/Comp Register 1    */
#define TIM_CCR2		0x38		/* Capt/Comp Register 2    */
#define TIM_CCR3		0x3C		/* Capt/Comp Register 3    */
#define TIM_CCR4		0x40		/* Capt/Comp Register 4    */
#define TIM_BDTR		0x44		/* Break and Dead-Time Reg */
#define TIM_DCR			0x48		/* DMA control register    */
#define TIM_DMAR		0x4C		/* DMA transfer register   */
#define TIM_AF1			0x60		/* Alt Function Reg 1      */
#define TIM_AF2			0x64		/* Alt Function Reg 2      */
#define TIM_TISEL		0x68		/* Input Selection         */

#define TIM_CR1_CEN		BIT(0)
#define TIM_SMCR_SMS		GENMASK_32(2, 0) /* Slave mode selection */
#define TIM_SMCR_TS		GENMASK_32(6, 4) /* Trigger selection */
#define TIM_CCMR_CC1S_TI1	BIT(0)		/* IC1/IC3 selects TI1/TI3 */
#define TIM_CCMR_CC1S_TI2	BIT(1)		/* IC1/IC3 selects TI2/TI4 */
#define TIM_CCMR_CC2S_TI2	BIT(8)		/* IC2/IC4 selects TI2/TI4 */
#define TIM_CCMR_CC2S_TI1	BIT(9)		/* IC2/IC4 selects TI1/TI3 */
#define TIM_CCER_CC1E		BIT(0)		/* Capt/Comp 1  out Ena    */
#define TIM_CCER_CC1P		BIT(1)		/* Capt/Comp 1  Polarity   */
#define TIM_CCER_CC1NP		BIT(3)		/* Capt/Comp 1N Polarity   */
#define TIM_SR_UIF		BIT(0)		/* UIF interrupt flag      */
#define TIM_SR_CC1IF		BIT(1)		/* CC1 interrupt flag      */
#define TIM_TISEL_TI1SEL_MASK	GENMASK_32(3, 0)
#define TIM_SMCR_SMS_RESET	BIT(2)
#define TIM_SMCR_TS_SHIFT	4
#define TIM_SMCR_TS_TI1FP1	0x5

#define TIM_COMPAT		"st,stm32-timers"
#define TIM_TIMEOUT_US		100000
#define TIM_TIMEOUT_STEP_US	10
#define TIM_PRESCAL_HSI		10
#define TIM_PRESCAL_CSI		7
#define TIM_MIN_FREQ_CALIB	50000000U
#define TIM_THRESHOLD		1U

struct stm32_tim_instance {
	struct io_pa_va base;
	unsigned long clk;
	unsigned long freq;
	uint8_t cal_input;
};

static vaddr_t timer_base(struct stm32_tim_instance *timer)
{
	return io_pa_or_va(&timer->base);
}

/* Currently support HSI and CSI calibratrion */
#define TIM_MAX_INSTANCE	2
static struct stm32_tim_instance stm32_tim[TIM_MAX_INSTANCE];

static int timer_get_dt_node(void *fdt, struct dt_node_info *info, int offset)
{
	int node = fdt_node_offset_by_compatible(fdt, offset, TIM_COMPAT);

	if (node < 0)
		return -FDT_ERR_NOTFOUND;

	_fdt_fill_device_info(fdt, info, node);

	return node;
}

static int timer_config(struct stm32_tim_instance *timer)
{
	vaddr_t base = timer_base(timer);

	clk_enable(timer->clk);

	timer->freq = clk_get_rate(timer->clk);
	if (timer->freq < TIM_MIN_FREQ_CALIB) {
		EMSG("Calibration: timer not accurate enough");
		clk_disable(timer->clk);
		return -1;
	}

	if ((io_read32(base + TIM_TISEL) & TIM_TISEL_TI1SEL_MASK) !=
	    timer->cal_input) {
		io_clrsetbits32(base + TIM_CCMR1,
				TIM_CCMR_CC1S_TI1 | TIM_CCMR_CC1S_TI2,
				TIM_CCMR_CC1S_TI1);

		io_clrbits32(base + TIM_CCER,
			     TIM_CCER_CC1P | TIM_CCER_CC1NP);

		io_clrsetbits32(base + TIM_SMCR,
				TIM_SMCR_TS | TIM_SMCR_SMS,
				(TIM_SMCR_TS_TI1FP1 << TIM_SMCR_TS_SHIFT) |
				TIM_SMCR_SMS_RESET);

		io_write32(base + TIM_TISEL, timer->cal_input);
		io_setbits32(base + TIM_CR1, TIM_CR1_CEN);
		io_setbits32(base + TIM_CCER, TIM_CCER_CC1E);
	}

	clk_disable(timer->clk);

	return 0;
}

static uint32_t timer_start_capture(struct stm32_tim_instance *timer)
{
	uint64_t timeout_ref = 0;
	uint64_t timeout_conv = 0;
	uint32_t counter = 0;
	uint32_t old_counter = 0;
	vaddr_t base = timer_base(timer);

	if (timer_config(timer))
		return 0;

	clk_enable(timer->clk);

	io_write32(base + TIM_SR, 0);

	timeout_ref = timeout_init_us(TIM_TIMEOUT_US / TIM_TIMEOUT_STEP_US);

	while (!timeout_elapsed(timeout_ref))
		if (io_read32(base + TIM_SR) & TIM_SR_UIF)
			break;
	if (!(io_read32(base + TIM_SR) & TIM_SR_UIF))
		goto bail;

	io_write32(base + TIM_SR, 0);

	timeout_conv = timeout_init_us(TIM_TIMEOUT_US);

	do {
		if (timeout_elapsed(timeout_conv)) {
			counter = 0;
			goto bail;
		}

		timeout_ref = timeout_init_us(TIM_TIMEOUT_US /
					      TIM_TIMEOUT_STEP_US);
		while (!timeout_elapsed(timeout_ref))
			if (io_read32(base + TIM_SR) & TIM_SR_CC1IF)
				break;

		if (!(io_read32(base + TIM_SR) & TIM_SR_CC1IF)) {
			counter = 0;
			goto bail;
		}

		old_counter = counter;
		counter = io_read32(base + TIM_CCR1);
	} while (MAX(counter, old_counter) - MIN(counter, old_counter) >
		 TIM_THRESHOLD);

bail:
	clk_disable(timer->clk);

	return counter;
}

unsigned long stm32_tim_hsi_freq(void)
{
	struct stm32_tim_instance *timer = &stm32_tim[HSI_CAL];
	uint32_t counter = 0;

	if (timer->base.pa)
		counter = timer_start_capture(timer);

	if (!counter)
		return 0;

	return (timer->freq / counter) << TIM_PRESCAL_HSI;
}
DECLARE_KEEP_PAGER(stm32_tim_hsi_freq);

unsigned long stm32_tim_csi_freq(void)
{
	struct stm32_tim_instance *timer = &stm32_tim[CSI_CAL];
	uint32_t counter = 0;

	if (timer->base.pa)
		counter = timer_start_capture(timer);

	if (!counter)
		return 0;

	return (timer->freq / counter) << TIM_PRESCAL_CSI;
}
DECLARE_KEEP_PAGER(stm32_tim_csi_freq);

static void _init_stm32_tim(void)
{
	void *fdt = get_embedded_dt();
	struct dt_node_info dt_timer = { };
	int node = -1;
	static bool inited;

	if (inited)
		return;
	inited = true;

	if (!fdt)
		panic();

	for (node = timer_get_dt_node(fdt, &dt_timer, node);
	     node != -FDT_ERR_NOTFOUND;
	     node = timer_get_dt_node(fdt, &dt_timer, node)) {
		struct stm32_tim_instance *timer = NULL;
		const uint32_t *cuint = NULL;

		if (!(dt_timer.status & DT_STATUS_OK_SEC))
			continue;

		cuint = fdt_getprop(fdt, node, "st,hsi-cal-input", NULL);
		if (cuint) {
			timer = &stm32_tim[HSI_CAL];
			timer->base.pa = dt_timer.reg;
			timer->clk = dt_timer.clock;
			timer->freq = clk_get_rate(timer->clk);
			timer->cal_input = fdt32_to_cpu(*cuint);
			if (timer_config(timer)) {
				timer->base.pa = 0;
				continue;
			}
		}

		cuint = fdt_getprop(fdt, node, "st,csi-cal-input", NULL);
		if (cuint) {
			timer = &stm32_tim[CSI_CAL];
			timer->base.pa = dt_timer.reg;
			timer->clk = dt_timer.clock;
			timer->freq = clk_get_rate(timer->clk);
			timer->cal_input = fdt32_to_cpu(*cuint);
			if (timer_config(timer)) {
				timer->base.pa = 0;
				continue;
			}
		}
	}
}

void stm32_tim_freq_func(unsigned long (**timer_freq_cb)(void),
			 enum stm32_tim_cal type)
{
	_init_stm32_tim();

	*timer_freq_cb = NULL;

	switch (type) {
	case HSI_CAL:
		if (stm32_tim[HSI_CAL].base.pa)
			*timer_freq_cb = stm32_tim_hsi_freq;
		break;

	case CSI_CAL:
		if (stm32_tim[CSI_CAL].base.pa)
			*timer_freq_cb = stm32_tim_csi_freq;
		break;
	default:
		panic();
	}
}

static TEE_Result init_stm32_tim(void)
{
	_init_stm32_tim();

	return TEE_SUCCESS;
}
driver_init(init_stm32_tim);
