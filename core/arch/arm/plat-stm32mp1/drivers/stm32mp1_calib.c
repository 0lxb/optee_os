// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2018-2020, STMicroelectronics
 */

#include <arm.h>
#include <drivers/clk.h>
#include <drivers/stm32_tim.h>
#include <drivers/stm32mp1_rcc.h>
#include <dt-bindings/clock/stm32mp1-clks.h>
#include <initcall.h>
#include <io.h>
#include <libfdt.h>
#include <keep.h>
#include <kernel/boot.h>
#include <kernel/delay.h>
#include <kernel/dt.h>
#include <kernel/interrupt.h>
#include <kernel/panic.h>
#include <kernel/pm.h>
#include <limits.h>
#include <mm/core_memprot.h>
#include <stm32_util.h>
#include <string.h>

#define CALIBRATION_TIMEOUT_US		10000

/* List of forbiden values for HSI and CSI */
static const uint16_t fbv_hsi[] = {
	512, 480, 448, 416, 384, 352, 320, 288,
	256, 224, 192, 160, 128, 96, 64, 32, 0,
};
static const uint16_t fbv_csi[] = {
	256, 240, 224, 208, 192, 176, 160, 144,
	128, 112, 96, 80, 64, 48, 32, 16, 0,
};

struct stm32mp1_trim_boundary_t {
	unsigned int max;	/* Max boundary trim value around forbidden value */
	unsigned int min;	/* Min boundary trim value around forbidden value */
};

struct stm32mp1_clk_cal {
	const uint16_t *fbv;
	unsigned int cal_ref;
	int trim_max;
	int trim_min;
	unsigned int boundary_max;
	unsigned long ref_freq;
	unsigned int freq_margin;
	unsigned long (*get_freq)(void);
	void (*set_trim)(unsigned int cal);
	unsigned int (*get_trim)(void);
	struct stm32mp1_trim_boundary_t boundary[16];
};

static void hsi_set_trim(unsigned int cal);
static unsigned int hsi_get_trimed_cal(void);
static void csi_set_trim(unsigned int cal);
static unsigned int csi_get_trimed_cal(void);

static struct stm32mp1_clk_cal *hsi_calib;
static struct stm32mp1_clk_cal *csi_calib;

static const struct stm32mp1_clk_cal hsi_calib_config = {
	.fbv = fbv_hsi,
	.trim_max = 63,
	.trim_min = -64,
	.ref_freq = 0,
	.freq_margin = 5,
	.set_trim = hsi_set_trim,
	.get_trim = hsi_get_trimed_cal,
};

static const struct stm32mp1_clk_cal csi_calib_config = {
	.fbv = fbv_csi,
	.trim_max = 15,
	.trim_min = -16,
	.ref_freq = 0,
	.freq_margin = 8,
	.set_trim = csi_set_trim,
	.get_trim = csi_get_trimed_cal,
};

static int get_signed_value(uint8_t val)
{
	return (int8_t)(val << 1) / 2;
}

static void hsi_set_trim(unsigned int cal)
{
	int clk_trim = (int)cal - (int)hsi_calib->cal_ref;
	uint32_t trim = ((uint32_t)clk_trim << RCC_HSICFGR_HSITRIM_SHIFT) &
			RCC_HSICFGR_HSITRIM_MASK;

	io_clrsetbits32(stm32_rcc_base() + RCC_HSICFGR,
			RCC_HSICFGR_HSITRIM_MASK, trim);
}
DECLARE_KEEP_PAGER(hsi_set_trim);

static unsigned int hsi_get_trimed_cal(void)
{
	uint32_t utrim = (io_read32(stm32_rcc_base() + RCC_HSICFGR) &
			  RCC_HSICFGR_HSITRIM_MASK) >>
			 RCC_HSICFGR_HSITRIM_SHIFT;
	int trim = get_signed_value((uint8_t)utrim);

	if (trim + (int)hsi_calib->cal_ref < 0)
		return 0;

	return hsi_calib->cal_ref + trim;
}
DECLARE_KEEP_PAGER(hsi_get_trimed_cal);

static void csi_set_trim(unsigned int cal)
{
	int clk_trim = (int)cal - (int)csi_calib->cal_ref +
		       csi_calib->trim_max + 1;
	uint32_t trim = ((uint32_t)clk_trim << RCC_CSICFGR_CSITRIM_SHIFT) &
			RCC_CSICFGR_CSITRIM_MASK;

	io_clrsetbits32(stm32_rcc_base() + RCC_CSICFGR,
			RCC_CSICFGR_CSITRIM_MASK, trim);
}
DECLARE_KEEP_PAGER(csi_set_trim);

static unsigned int csi_get_trimed_cal(void)
{
	uint32_t trim = (io_read32(stm32_rcc_base() + RCC_CSICFGR) &
			 RCC_CSICFGR_CSITRIM_MASK) >>
			RCC_CSICFGR_CSITRIM_SHIFT;

	return (int)trim - csi_calib->trim_max + (int)csi_calib->cal_ref - 1;
}
DECLARE_KEEP_PAGER(csi_get_trimed_cal);

static unsigned int trim_increase(struct stm32mp1_clk_cal *clk_cal,
				  unsigned int cal)
{
	struct stm32mp1_trim_boundary_t *boundary = NULL;
	unsigned int new_cal = 0;
	int i = 0;

	/* By default: last calibration value */
	new_cal = cal;

	/* Start from Lowest cal value */
	for (i = (int)clk_cal->boundary_max - 1; i >= 0; i--) {
		boundary = &clk_cal->boundary[i];

		if (cal < boundary->min) {
			new_cal = boundary->min;
			break;
		}

		if ((cal >= boundary->min) && (cal < boundary->max)) {
			new_cal = cal + 1;
			break;
		}
	}

	return new_cal;
}

static unsigned int trim_decrease(struct stm32mp1_clk_cal *clk_cal,
				  unsigned int cal)
{
	struct stm32mp1_trim_boundary_t *boundary = NULL;
	unsigned int new_cal = 0;
	unsigned int i = 0;

	/* By default: last calibration value */
	new_cal = cal;

	/* Start from Highest cal value */
	for (i = 0; i < clk_cal->boundary_max; i++) {
		boundary = &clk_cal->boundary[i];

		if (cal > boundary->max) {
			new_cal = boundary->max;
			break;
		}

		if ((cal > boundary->min) && (cal <= boundary->max)) {
			new_cal = cal - 1;
			break;
		}
	}

	return new_cal;
}

static void rcc_calibration(struct stm32mp1_clk_cal *clk_cal)
{
	unsigned long margin = (clk_cal->ref_freq *
				clk_cal->freq_margin) / 1000;
	unsigned long min = clk_cal->ref_freq - margin;
	unsigned long max = clk_cal->ref_freq + margin;
	unsigned long freq = clk_cal->get_freq();
	int trim = 0;
	int new_trim = 0;
	unsigned long conv = 0;
	unsigned long min_conv = ULONG_MAX;
	uint64_t timeout_ref = 0;

	if ((freq >= min) && (freq <= max))
		return;

	trim = clk_cal->get_trim();
	timeout_ref = timeout_init_us(CALIBRATION_TIMEOUT_US);
	do {
		if (freq < clk_cal->ref_freq)
			new_trim = trim_increase(clk_cal, trim);
		else
			new_trim = trim_decrease(clk_cal, trim);

		clk_cal->set_trim(new_trim);
		freq = clk_cal->get_freq();
		if (freq == 0) {
			/* Calibration will be stopped */
			clk_cal->ref_freq = 0U;
			return;
		}
		conv = (clk_cal->ref_freq < freq) ?
			freq - clk_cal->ref_freq : clk_cal->ref_freq - freq;
		if (conv < min_conv) {
			min_conv = conv;
			trim = new_trim;
		}

		if (timeout_elapsed(timeout_ref))
			break;

	} while (conv == min_conv);

	clk_cal->set_trim(trim);
	freq = clk_cal->get_freq();
	if ((freq < min) || (freq > max)) {
		EMSG("%s Calibration : Freq %lu , trim %i\n",
		     (clk_cal->set_trim == hsi_set_trim) ? "HSI" : "CSI",
		     freq, trim);
	}
}

static void save_trim(struct stm32mp1_clk_cal *clk_cal,
		      unsigned int i, unsigned int max, unsigned int min)
{
	clk_cal->boundary[i].max = max;
	clk_cal->boundary[i].min = min;
}

static int trim_find_prev_boundary(struct stm32mp1_clk_cal *clk_cal,
				   unsigned int x1)
{
	unsigned int x = x1;
	unsigned long freq = 0;

	clk_cal->set_trim(x1 + 1);
	freq = clk_cal->get_freq();

	while (x >= (clk_cal->cal_ref + clk_cal->trim_min)) {
		x--;
		clk_cal->set_trim(x);

		if (clk_cal->get_freq() <= freq)
			break;
	};

	return x;
}

static void trim_table_init(struct stm32mp1_clk_cal *clk_cal)
{
	const uint16_t *trim_fbv = clk_cal->fbv;
	unsigned int min = 0;
	unsigned int max = 0;
	int boundary = 0;
	int i = 0;

	max = clk_cal->cal_ref + clk_cal->trim_max;
	min = clk_cal->cal_ref + clk_cal->trim_min;

	while (trim_fbv[i]) {
		unsigned int x = 0;
		unsigned int x1 = trim_fbv[i];
		unsigned int x2 = trim_fbv[i + 1];

		if ((max <= x2) || (min >= x1)) {
			i++;
			if (boundary != 0)
				goto out;

			continue;
		}

		/* Take forbiden value + 1 */
		x2 = x2 + 1;
		if (x2 < min)
			x2 = min;

		if (boundary == 0) {
			/* Save first boundary */
			save_trim(clk_cal, boundary, max, x2);
			boundary++;
			i++;
			continue;
		}

		x = trim_find_prev_boundary(clk_cal, x1);
		/* Save boundary values */
		save_trim(clk_cal, boundary, x - 1, x2);
		boundary++;
		i++;
	};
out:
	clk_cal->boundary_max = boundary;
}

/* Timer countdown/delay argument for the target calibration periodicity */
static uint32_t timer_val;

#define CNTP_CTL_ENABLE		BIT(0)
#define CNTP_CTL_IMASK		BIT(1)
#define CNTP_CTL_ISTATUS	BIT(2)

static void arm_timer(void)
{
	if (!timer_val)
		return;

	write_cntp_ctl(read_cntp_ctl() & ~(CNTP_CTL_ENABLE | CNTP_CTL_IMASK));
	write_cntp_tval(timer_val);
	write_cntp_ctl(read_cntp_ctl() | CNTP_CTL_ENABLE);
}

static void arm_timer_with_period(uint32_t period_sec)
{
	timer_val = period_sec * read_cntfrq();

	if (timer_val > INT32_MAX)
		timer_val = INT32_MAX;

	DMSG("Calibration timeout set to %"PRIu32, timer_val / read_cntfrq());

	arm_timer();
}

static void calib_period(void)
{
	(void)stm32mp_start_clock_calib(CK_HSI);
	(void)stm32mp_start_clock_calib(CK_CSI);

	arm_timer();
}

static enum itr_return arm_cntp_it_handler(struct itr_handler *handler __unused)
{
	if (timer_val)
		calib_period();

	return ITRR_HANDLED;
}
static struct itr_handler arm_cntp_handler = {
	.it = GIC_SPI_SEC_PHY_TIMER,
	.handler = arm_cntp_it_handler,
};
DECLARE_KEEP_PAGER(arm_cntp_handler);

static TEE_Result timer_pm(enum pm_op op, uint32_t pm_hint __unused,
			   const struct pm_callback_handle *handle __unused)
{
	if (op == PM_OP_RESUME && timer_val)
		calib_period();

	return TEE_SUCCESS;
}
DECLARE_KEEP_PAGER(timer_pm);

static TEE_Result init_arm_cntp_timer(void)
{
	itr_add(&arm_cntp_handler);
	itr_enable(arm_cntp_handler.it);

	register_pm_driver_cb(timer_pm, NULL);

	return TEE_SUCCESS;
}
driver_init(init_arm_cntp_timer);

static void init_periodic_calibration(void *fdt, int node)
{
	uint32_t period = 0;
        int lenp = 0;
        const fdt32_t *cuint = fdt_getprop(fdt, node, "st,cal-sec", &lenp);

	if (cuint)
		period = fdt32_to_cpu(*cuint);

	DMSG("Calib period %us", period);
	arm_timer_with_period(period);
}

int stm32mp_start_clock_calib(unsigned int clock_id)
{
	struct stm32mp1_clk_cal *clk_calib = NULL;

	switch (clock_id) {
	case CK_HSI:
		clk_calib = hsi_calib;
		break;
	case CK_CSI:
		clk_calib = csi_calib;
		break;
	default:
		DMSG("Cannot calibrate clock %u", clock_id);
		return 1;
	}

	if (clk_calib->ref_freq == 0U)
		return 1;

	DMSG("%s", clock_id == CK_HSI ? "HSI" : "CSI");
	rcc_calibration(clk_calib);

	return 0;
}

static int init_hsi_calibration(void *fdt, int node)
{
	if (!fdt_getprop(fdt, node, "st,hsi-cal", NULL))
		return 0;

	hsi_calib = calloc(1, sizeof(*hsi_calib));
	assert(hsi_calib);
	memcpy(hsi_calib, &hsi_calib_config, sizeof(*hsi_calib));

	stm32_tim_freq_func(&hsi_calib->get_freq, HSI_CAL);
	if (hsi_calib->get_freq == NULL) {
		free(hsi_calib);
		return -1;
	}

	hsi_calib->ref_freq = clk_get_rate(CK_HSI);

	hsi_calib->cal_ref = (io_read32(stm32_rcc_base() + RCC_HSICFGR) &
			       RCC_HSICFGR_HSICAL_MASK) >>
			      RCC_HSICFGR_HSICAL_SHIFT;

	trim_table_init(hsi_calib);
	hsi_calib->set_trim(hsi_calib->cal_ref);
	stm32mp_start_clock_calib(CK_HSI);
	return 1;
}

static int init_csi_calibration(void *fdt, int node)
{
	if (!fdt_getprop(fdt, node, "st,csi-cal", NULL))
		return 0;

	csi_calib = calloc(1, sizeof(*csi_calib));
	assert(csi_calib);
	memcpy(csi_calib, &csi_calib_config, sizeof(*csi_calib));

	stm32_tim_freq_func(&csi_calib->get_freq, CSI_CAL);
	if (csi_calib->get_freq == NULL) {
		free(csi_calib);
		return -1;
	}

	csi_calib->ref_freq = clk_get_rate(CK_CSI);

	csi_calib->cal_ref = (io_read32(stm32_rcc_base() + RCC_CSICFGR) &
			      RCC_CSICFGR_CSICAL_MASK) >>
			     RCC_CSICFGR_CSICAL_SHIFT;
	trim_table_init(csi_calib);
	csi_calib->set_trim(csi_calib->cal_ref);
	stm32mp_start_clock_calib(CK_CSI);
	return 1;
}

static TEE_Result init_stm32mp1_calib(void)
{
	void *fdt = NULL;
	int rcc_node = 0;
	int res_csi = 0;
	int res_hsi = 0;

	fdt = get_embedded_dt();
	if (!fdt)
		panic();

	rcc_node = fdt_node_offset_by_compatible(fdt, -1, DT_RCC_CLK_COMPAT);
	if (rcc_node < 0)
		panic();

	res_hsi = init_hsi_calibration(fdt, rcc_node);
	if (res_hsi < 0)
		panic("HSI calibration init failed");
	res_csi = init_csi_calibration(fdt, rcc_node);
	if (res_csi < 0)
		panic("CSI calibration init failed");
	if (res_csi || res_hsi)
		init_periodic_calibration(fdt, rcc_node);

	return TEE_SUCCESS;
}
driver_init(init_stm32mp1_calib);
