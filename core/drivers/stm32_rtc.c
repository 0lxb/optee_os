// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2018-2020, STMicroelectronics - All Rights Reserved
 *
 */

#include <arm32.h>
#include <drivers/clk.h>
#include <drivers/stm32_rtc.h>
#include <initcall.h>
#include <io.h>
#include <kernel/boot.h>
#include <kernel/dt.h>
#include <kernel/panic.h>
#include <libfdt.h>
#include <mm/core_memprot.h>
#include <stm32_util.h>

#define RTC_COMPAT		"st,stm32mp1-rtc"

#define RTC_TR			0x00
#define RTC_DR			0x04
#define RTC_SSR			0x08
#define RTC_ICSR		0x0C
#define RTC_PRER		0x10
#define RTC_WUTR		0x14
#define RTC_CR			0x18
#define RTC_SMCR		0x20
#define RTC_WPR			0x24
#define RTC_CALR		0x28
#define RTC_SHIFTR		0x2C
#define RTC_TSTR		0x30
#define RTC_TSDR		0x34
#define RTC_TSSSR		0x38
#define RTC_ALRMAR		0x40
#define RTC_ALRMASSR		0x44
#define RTC_ALRMBR		0x48
#define RTC_ALRMBSSR		0x4C
#define RTC_SR			0x50
#define RTC_SCR			0x5C
#define RTC_OR			0x60

#define RTC_TR_SU_MASK		GENMASK_32(3, 0)
#define RTC_TR_ST_MASK		GENMASK_32(6, 4)
#define RTC_TR_ST_SHIFT		4
#define RTC_TR_MNU_MASK		GENMASK_32(11, 8)
#define RTC_TR_MNU_SHIFT	8
#define RTC_TR_MNT_MASK		GENMASK_32(14, 12)
#define RTC_TR_MNT_SHIFT	12
#define RTC_TR_HU_MASK		GENMASK_32(19, 16)
#define RTC_TR_HU_SHIFT		16
#define RTC_TR_HT_MASK		GENMASK_32(21, 20)
#define RTC_TR_HT_SHIFT		20
#define RTC_TR_PM		BIT(22)

#define RTC_DR_DU_MASK		GENMASK_32(3, 0)
#define RTC_DR_DT_MASK		GENMASK_32(5, 4)
#define RTC_DR_DT_SHIFT		4
#define RTC_DR_MU_MASK		GENMASK_32(11, 8)
#define RTC_DR_MU_SHIFT		8
#define RTC_DR_MT		BIT(12)
#define RTC_DR_MT_SHIFT		12
#define RTC_DR_WDU_MASK		GENMASK_32(15, 13)
#define RTC_DR_WDU_SHIFT	13
#define RTC_DR_YU_MASK		GENMASK_32(19, 16)
#define RTC_DR_YU_SHIFT		16
#define RTC_DR_YT_MASK		GENMASK_32(23, 20)
#define RTC_DR_YT_SHIFT		20

#define RTC_SSR_SS_MASK		GENMASK_32(15, 0)

#define RTC_ICSR_RSF		BIT(5)

#define RTC_PRER_PREDIV_S_MASK	GENMASK_32(15, 0)

#define RTC_CR_BYPSHAD		BIT(5)
#define RTC_CR_BYPSHAD_SHIFT	5
#define RTC_CR_TAMPTS		BIT(25)

#define RTC_SMCR_TS_DPROT	BIT(3)
#define RTC_SR_TSF		BIT(3)
#define RTC_SCR_CTSF		BIT(3)
#define RTC_SR_TSOVF		BIT(4)
#define RTC_SCR_CTSOVF		BIT(4)

#define RTC_TSDR_MU_MASK	GENMASK_32(11, 8)
#define RTC_TSDR_MU_SHIFT	8
#define RTC_TSDR_DT_MASK	GENMASK_32(5, 4)
#define RTC_TSDR_DT_SHIFT	4
#define RTC_TSDR_DU_MASK	GENMASK_32(3, 0)
#define RTC_TSDR_DU_SHIFT	0

#define RTC_WPR_KEY1		0xCA
#define RTC_WPR_KEY2		0x53
#define RTC_WPR_KEY_LOCK	0xFF

#define RTC_FLAGS_READ_TWICE	BIT(0)
#define RTC_FLAGS_SECURE	BIT(1)

struct rtc_device {
	struct io_pa_va base;
	uint16_t clock;
	uint8_t flags;
};

/* Expect a single RTC instance */
struct rtc_device rtc_dev;

static vaddr_t get_base(void)
{
	assert(rtc_dev.base.pa);
	return io_pa_or_va(&rtc_dev.base);
}

static void stm32_rtc_write_unprotect(void)
{
	vaddr_t rtc_base = get_base();

	io_write32(rtc_base + RTC_WPR, RTC_WPR_KEY1);
	io_write32(rtc_base + RTC_WPR, RTC_WPR_KEY2);
}

static void stm32_rtc_write_protect(void)
{
	vaddr_t rtc_base = get_base();

	io_write32(rtc_base + RTC_WPR, RTC_WPR_KEY_LOCK);
}

static bool stm32_rtc_get_bypshad(void)
{
	return io_read32(get_base() + RTC_CR) & RTC_CR_BYPSHAD;
}

/* Get calendar data from RTC devicecalendar valueregister values */
static void stm32_rtc_read_calendar(struct stm32_rtc_calendar *calendar)
{
	vaddr_t rtc_base = get_base();
	bool bypshad = stm32_rtc_get_bypshad();

	if (!bypshad) {
		/* Shadow RTC registers */
		io_clrbits32(rtc_base + RTC_ICSR, RTC_ICSR_RSF);
		while (!(io_read32(rtc_base + RTC_ICSR) & RTC_ICSR_RSF))
			;
	}

	calendar->ssr = io_read32(rtc_base + RTC_SSR);
	calendar->tr = io_read32(rtc_base + RTC_TR);
	calendar->dr = io_read32(rtc_base + RTC_DR);
}

/* Fill the RTC timestamp structure from a given RTC time-in-day value */
static void stm32_rtc_get_time(struct stm32_rtc_calendar *cal,
			       struct stm32_rtc_time *tm)
{
	tm->hour = (((cal->tr & RTC_TR_HT_MASK) >> RTC_TR_HT_SHIFT) * 10) +
		   ((cal->tr & RTC_TR_HU_MASK) >> RTC_TR_HU_SHIFT);

	if (cal->tr & RTC_TR_PM)
		tm->hour += 12;

	tm->min = (((cal->tr & RTC_TR_MNT_MASK) >> RTC_TR_MNT_SHIFT) * 10) +
		  ((cal->tr & RTC_TR_MNU_MASK) >> RTC_TR_MNU_SHIFT);
	tm->sec = (((cal->tr & RTC_TR_ST_MASK) >> RTC_TR_ST_SHIFT) * 10) +
		  (cal->tr & RTC_TR_SU_MASK);
}

/* Fill the RTC timestamp structure from a given RTC date value */
static void stm32_rtc_get_date(struct stm32_rtc_calendar *cal,
			       struct stm32_rtc_time *tm)
{
	tm->wday = (((cal->dr & RTC_DR_WDU_MASK) >> RTC_DR_WDU_SHIFT));

	tm->day = (((cal->dr & RTC_DR_DT_MASK) >> RTC_DR_DT_SHIFT) * 10) +
		  (cal->dr & RTC_DR_DU_MASK);

	tm->month = (((cal->dr & RTC_DR_MT) >> RTC_DR_MT_SHIFT) * 10) +
		    ((cal->dr & RTC_DR_MU_MASK) >> RTC_DR_MU_SHIFT);

	tm->year = (((cal->dr & RTC_DR_YT_MASK) >> RTC_DR_YT_SHIFT) * 10) +
		   ((cal->dr & RTC_DR_YU_MASK) >> RTC_DR_YU_SHIFT) + 2000;
}

/* Update time value with RTC timestamp */
static void stm32_rtc_read_timestamp(struct stm32_rtc_time *time)
{
	struct stm32_rtc_calendar cal_tamp = { };
	vaddr_t rtc_base = get_base();

	cal_tamp.tr = io_read32(rtc_base + RTC_TSTR);
	cal_tamp.dr = io_read32(rtc_base + RTC_TSDR);
	stm32_rtc_get_time(&cal_tamp, time);
	stm32_rtc_get_date(&cal_tamp, time);
}

void stm32_rtc_get_calendar(struct stm32_rtc_calendar *calendar)
{
	clk_enable(rtc_dev.clock);

	stm32_rtc_read_calendar(calendar);

	/* RTC may need to be read twice, depending of clocks configuration */
	if (rtc_dev.flags & RTC_FLAGS_READ_TWICE) {
		uint32_t tr_save = calendar->tr;

		stm32_rtc_read_calendar(calendar);

		if (calendar->tr != tr_save)
			stm32_rtc_read_calendar(calendar);
	}

	clk_disable(rtc_dev.clock);
}

/* Return difference in milliseconds on second fraction */
static uint32_t stm32_rtc_get_second_fraction(struct stm32_rtc_calendar *cal)
{
	uint32_t prediv_s = io_read32(get_base() + RTC_PRER) &
			    RTC_PRER_PREDIV_S_MASK;
	uint32_t ss = cal->ssr & RTC_SSR_SS_MASK;

	return ((prediv_s - ss) * 1000) / (prediv_s + 1);
}

/* Return absolute difference in milliseconds on second fraction */
static signed long long stm32_rtc_diff_frac(struct stm32_rtc_calendar *cur,
					    struct stm32_rtc_calendar *ref)
{
	return (signed long long)stm32_rtc_get_second_fraction(cur) -
	       (signed long long)stm32_rtc_get_second_fraction(ref);
}

/* Return absolute difference in milliseconds on seconds-in-day fraction */
static signed long long stm32_rtc_diff_time(struct stm32_rtc_time *current,
					    struct stm32_rtc_time *ref)
{
	signed long long curr_s = 0;
	signed long long ref_s = 0;

	curr_s = (signed long long)current->sec +
		 (((signed long long)current->min +
		  (((signed long long)current->hour * 60))) * 60);

	ref_s = (signed long long)ref->sec +
		(((signed long long)ref->min +
		 (((signed long long)ref->hour * 60))) * 60);

	return (curr_s - ref_s) * 1000U;
}

static bool stm32_is_a_leap_year(uint32_t year)
{
	return ((year % 4) == 0) &&
	       (((year % 100) != 0) || ((year % 400) == 0));
}

/* Return absolute difference in milliseconds on day-in-year fraction */
static signed long long stm32_rtc_diff_date(struct stm32_rtc_time *current,
					    struct stm32_rtc_time *ref)
{
	uint32_t diff_in_days = 0;
	uint32_t m = 0;
	const uint8_t month_len[NB_MONTHS] = {
		31, 28, 31, 30, 31, 30,
		31, 31, 30, 31, 30, 31
	};

	/* Get the number of non-entire month days */
	if (current->day >= ref->day)
		diff_in_days += current->day - ref->day;
	else
		diff_in_days += month_len[ref->month - 1] -
				ref->day + current->day;

	/* Get the number of entire months, and compute the related days */
	if (current->month > (ref->month + 1))
		for (m = ref->month + 1; m < current->month && m < 12; m++)
			diff_in_days += month_len[m - 1];

	if (current->month < (ref->month - 1)) {
		for (m = 1; m < current->month && m < 12; m++)
			diff_in_days += month_len[m - 1];

		for (m = ref->month + 1; m < 12; m++)
			diff_in_days += month_len[m - 1];
	}

	/* Get complete years */
	if (current->year > (ref->year + 1))
		diff_in_days += (current->year - ref->year - 1) * 365;

	/* Particular cases: leap years (one day more) */
	if (diff_in_days > 0) {
		if (current->year == ref->year) {
			if (stm32_is_a_leap_year(current->year) &&
			    ref->month <= 2 &&
			    current->month >= 3 && current->day <= 28)
				diff_in_days++;
		} else {
			uint32_t y = 0;

			/* Ref year is leap */
			if (stm32_is_a_leap_year(ref->year) &&
			    ref->month <= 2 && ref->day <= 28)
				diff_in_days++;

			/* Current year is leap */
			if (stm32_is_a_leap_year(current->year) &&
			    current->month >= 3)
				diff_in_days++;

			/* Interleaved years are leap */
			for (y = ref->year + 1; y < current->year; y++)
				if (stm32_is_a_leap_year(y))
					diff_in_days++;
		}
	}

	return (24 * 60 * 60 * 1000) * (signed long long)diff_in_days;
}

unsigned long long stm32_rtc_diff_calendar(struct stm32_rtc_calendar *cur,
					   struct stm32_rtc_calendar *ref)
{
	signed long long diff_in_ms = 0;
	struct stm32_rtc_time curr_t = { };
	struct stm32_rtc_time ref_t = { };

	stm32_rtc_get_date(cur, &curr_t);
	stm32_rtc_get_date(ref, &ref_t);
	stm32_rtc_get_time(cur, &curr_t);
	stm32_rtc_get_time(ref, &ref_t);

	diff_in_ms += stm32_rtc_diff_frac(cur, ref);
	diff_in_ms += stm32_rtc_diff_time(&curr_t, &ref_t);
	diff_in_ms += stm32_rtc_diff_date(&curr_t, &ref_t);

	return (unsigned long long)diff_in_ms;
}

void stm32_rtc_get_timestamp(struct stm32_rtc_time *tamp_ts)
{
	vaddr_t rtc_base = get_base();

	clk_enable(rtc_dev.clock);

	if (io_read32(rtc_base + RTC_SR) & RTC_SR_TSF) {
		/* Timestamp for tamper event */
		stm32_rtc_read_timestamp(tamp_ts);
		io_setbits32(rtc_base + RTC_SCR, RTC_SCR_CTSF);

		/* Overflow detection */
		if (io_read32(rtc_base + RTC_SR) & RTC_SR_TSOVF)
			io_setbits32(rtc_base + RTC_SCR, RTC_SCR_CTSOVF);
	}

	clk_disable(rtc_dev.clock);
}

void stm32_rtc_set_tamper_timestamp(void)
{
	vaddr_t rtc_base = get_base();

	clk_enable(rtc_dev.clock);

	stm32_rtc_write_unprotect();

	/* Enable tamper timestamper */
	io_setbits32(rtc_base + RTC_CR, RTC_CR_TAMPTS);

	/* Secure Timestamp bit */
	io_clrbits32(rtc_base + RTC_SMCR, RTC_SMCR_TS_DPROT);

	stm32_rtc_write_protect();

	clk_disable(rtc_dev.clock);
}

bool stm32_rtc_is_timestamp_enable(void)
{
	bool ret = false;

	clk_enable(rtc_dev.clock);

	ret = io_read32(get_base() + RTC_CR) & RTC_CR_TAMPTS;

	clk_disable(rtc_dev.clock);

	return ret;
}

#ifdef CFG_DT
static unsigned int get_second_clock(void *fdt, int offs)
{
	const fdt32_t *cuint = NULL;
	int len = 0;

	cuint = fdt_getprop(fdt, offs, "clocks", &len);
	if (len == 3) {
		EMSG("RTC expects 2 clocks, %d found", len);
		return DT_INFO_INVALID_CLOCK;
	}

	return fdt32_to_cpu(*(cuint + 3));
}

static TEE_Result stm32_rtc_init(void)
{
	int node = 0;
	struct dt_node_info dt_info = { };
	void *fdt = get_embedded_dt();

	if (!fdt)
		panic();

	node = fdt_node_offset_by_compatible(fdt, -1, RTC_COMPAT);
	if (node < 0)
		return TEE_SUCCESS;

	_fdt_fill_device_info(fdt, &dt_info, node);

	rtc_dev.base.pa = dt_info.reg;

	if (dt_info.status == DT_STATUS_OK_SEC) {
		rtc_dev.flags |= RTC_FLAGS_SECURE;
		stm32mp_register_secure_periph_iomem(rtc_dev.base.pa);
		rtc_dev.base.va = (vaddr_t)phys_to_virt(rtc_dev.base.pa,
							MEM_AREA_IO_SEC);
		/* Unbalanced clock enable: keep RTC running */
		clk_enable(get_second_clock(fdt, node));
	} else {
		stm32mp_register_non_secure_periph_iomem(rtc_dev.base.pa);
		rtc_dev.base.va = (vaddr_t)phys_to_virt(rtc_dev.base.pa,
							MEM_AREA_IO_NSEC);
	}

	rtc_dev.clock = (unsigned long)dt_info.clock;

	if (stm32_rtc_get_read_twice())
		rtc_dev.flags |= RTC_FLAGS_READ_TWICE;

	return 0;
}
driver_init(stm32_rtc_init);
#endif
