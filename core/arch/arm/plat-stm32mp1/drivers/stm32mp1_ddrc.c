// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2017-2020, STMicroelectronics - All Rights Reserved
 * Copyright (c) 2017, ARM Limited and Contributors. All rights reserved.
 */

#include <arm32.h>
#include <boot_api.h>
#include <drivers/stm32_rtc.h>
#include <drivers/stm32mp1_ddrc.h>
#include <drivers/stm32mp1_pwr.h>
#include <drivers/stm32mp1_rcc.h>
#include <dt-bindings/clock/stm32mp1-clks.h>
#include <kernel/delay.h>
#include <kernel/panic.h>
#include <io.h>
#include <mm/core_memprot.h>
#include <platform_config.h>
#include <string.h>
#include <trace.h>

#define TIMEOUT_500US		500

static enum stm32mp1_ddr_sr_mode saved_ddr_sr_mode;

static vaddr_t get_ddrctrl_base(void)
{
	static struct io_pa_va base __nex_data = { .pa = DDRCTRL_BASE };

	return io_pa_or_va(&base);
}

static vaddr_t get_ddrphy_base(void)
{
	static struct io_pa_va base __nex_data = { .pa = DDRPHYC_BASE };

	return io_pa_or_va(&base);
}

static void ddr_disable_clock(void)
{
	vaddr_t rcc_base = stm32_rcc_base();

	/* Disable all clocks */
	io_clrbits32(rcc_base + RCC_DDRITFCR,
		     RCC_DDRITFCR_DDRC1EN |
		     RCC_DDRITFCR_DDRC2EN |
		     RCC_DDRITFCR_DDRPHYCAPBEN |
		     RCC_DDRITFCR_DDRCAPBEN);
}

static void ddr_enable_clock(void)
{
	vaddr_t rcc_base = stm32_rcc_base();

	/* Enable all clocks */
	io_setbits32(rcc_base + RCC_DDRITFCR,
		     RCC_DDRITFCR_DDRC1EN |
		     RCC_DDRITFCR_DDRC2EN |
		     RCC_DDRITFCR_DDRPHYCEN |
		     RCC_DDRITFCR_DDRPHYCAPBEN |
		     RCC_DDRITFCR_DDRCAPBEN);
}

static void do_sw_handshake(void)
{
	vaddr_t ddrctrl_base = get_ddrctrl_base();

	io_clrbits32(ddrctrl_base + DDRCTRL_SWCTL, DDRCTRL_SWCTL_SW_DONE);
}

static void do_sw_ack(void)
{
	uint64_t timeout_ref = 0;
	vaddr_t ddrctrl_base = get_ddrctrl_base();

	io_setbits32(ddrctrl_base + DDRCTRL_SWCTL, DDRCTRL_SWCTL_SW_DONE);

	timeout_ref = timeout_init_us(TIMEOUT_500US);
	while (!timeout_elapsed(timeout_ref))
		if (io_read32(ddrctrl_base + DDRCTRL_SWSTAT) &
		    DDRCTRL_SWSTAT_SW_DONE_ACK)
			return;

	panic();
}

static int ddr_sw_self_refresh_in(void)
{
	uint64_t timeout_ref = 0;
	uint32_t operating_mode = 0;
	uint32_t selref_type = 0;
	uint8_t op_mode_changed = 0;
	vaddr_t pwr_base = stm32_pwr_base();
	vaddr_t rcc_base = stm32_rcc_base();
	vaddr_t ddrctrl_base = get_ddrctrl_base();
	vaddr_t ddrphy_base = get_ddrphy_base();

	io_clrbits32(rcc_base + RCC_DDRITFCR, RCC_DDRITFCR_AXIDCGEN);

	/* Blocks AXI ports from taking anymore transactions */
	io_clrbits32(ddrctrl_base + DDRCTRL_PCTRL_0, DDRCTRL_PCTRL_N_PORT_EN);
	io_clrbits32(ddrctrl_base + DDRCTRL_PCTRL_1, DDRCTRL_PCTRL_N_PORT_EN);

	/*
	 * Waits unit all AXI ports are idle
	 * Poll PSTAT.rd_port_busy_n = 0
	 * Poll PSTAT.wr_port_busy_n = 0
	 */
	timeout_ref = timeout_init_us(TIMEOUT_500US);
	while (io_read32(ddrctrl_base + DDRCTRL_PSTAT))
		if (timeout_elapsed(timeout_ref))
			goto pstat_failed;

	/* SW Self-Refresh entry */
	io_setbits32(ddrctrl_base + DDRCTRL_PWRCTL, DDRCTRL_PWRCTL_SELFREF_SW);

	/*
	 * Wait operating mode change in self-refresh mode
	 * with STAT.operating_mode[1:0]==11.
	 * Ensure transition to self-refresh was due to software
	 * by checking also that STAT.selfref_type[1:0]=2.
	 */
	timeout_ref = timeout_init_us(TIMEOUT_500US);
	while (!timeout_elapsed(timeout_ref)) {
		uint32_t stat = io_read32(ddrctrl_base + DDRCTRL_STAT);

		operating_mode = stat & DDRCTRL_STAT_OPERATING_MODE_MASK;
		selref_type = stat & DDRCTRL_STAT_SELFREF_TYPE_MASK;

		if ((operating_mode == DDRCTRL_STAT_OPERATING_MODE_SR) &&
		    (selref_type == DDRCTRL_STAT_SELFREF_TYPE_SR)) {
			op_mode_changed = 1;
			break;
		}
	}

	if (op_mode_changed == 0U)
		goto selfref_sw_failed;

	/* IOs powering down (PUBL registers) */
	io_setbits32(ddrphy_base + DDRPHYC_ACIOCR, DDRPHYC_ACIOCR_ACPDD);
	io_setbits32(ddrphy_base + DDRPHYC_ACIOCR, DDRPHYC_ACIOCR_ACPDR);
	io_clrsetbits32(ddrphy_base + DDRPHYC_ACIOCR,
			DDRPHYC_ACIOCR_CKPDD_MASK, DDRPHYC_ACIOCR_CKPDD_0);
	io_clrsetbits32(ddrphy_base + DDRPHYC_ACIOCR,
			DDRPHYC_ACIOCR_CKPDR_MASK, DDRPHYC_ACIOCR_CKPDR_0);
	io_clrsetbits32(ddrphy_base + DDRPHYC_ACIOCR,
			DDRPHYC_ACIOCR_CSPDD_MASK, DDRPHYC_ACIOCR_CSPDD_0);
	io_clrbits32(ddrphy_base + DDRPHYC_ACIOCR, DDRPHYC_ACIOCR_ACOE);
	io_setbits32(ddrphy_base + DDRPHYC_DXCCR, DDRPHYC_DXCCR_DXPDD);
	io_setbits32(ddrphy_base + DDRPHYC_DXCCR, DDRPHYC_DXCCR_DXPDR);
	io_clrsetbits32(ddrphy_base + DDRPHYC_DSGCR,
			DDRPHYC_DSGCR_ODTPDD_MASK, DDRPHYC_DSGCR_ODTPDD_0);
	io_setbits32(ddrphy_base + DDRPHYC_DSGCR, DDRPHYC_DSGCR_NL2PD);
	io_clrsetbits32(ddrphy_base + DDRPHYC_DSGCR,
			DDRPHYC_DSGCR_CKEPDD_MASK, DDRPHYC_DSGCR_CKEPDD_0);

	/* Disable PZQ cell (PUBL register) */
	io_setbits32(ddrphy_base + DDRPHYC_ZQ0CR0, DDRPHYC_ZQ0CRN_ZQPD);

	/* Activate sw retention in PWRCTRL */
	io_setbits32(pwr_base + PWR_CR3_OFF, PWR_CR3_DDRRETEN);

	/* Switch controller clocks (uMCTL2/PUBL) to DLL ref clock */
	io_setbits32(rcc_base + RCC_DDRITFCR, RCC_DDRITFCR_GSKPCTRL);

	/* Disable all DLLs: GLITCH window */
	io_setbits32(ddrphy_base + DDRPHYC_ACDLLCR, DDRPHYC_ACDLLCR_DLLDIS);
	io_setbits32(ddrphy_base + DDRPHYC_DX0DLLCR, DDRPHYC_DXNDLLCR_DLLDIS);
	io_setbits32(ddrphy_base + DDRPHYC_DX1DLLCR, DDRPHYC_DXNDLLCR_DLLDIS);
	io_setbits32(ddrphy_base + DDRPHYC_DX2DLLCR, DDRPHYC_DXNDLLCR_DLLDIS);
	io_setbits32(ddrphy_base + DDRPHYC_DX3DLLCR, DDRPHYC_DXNDLLCR_DLLDIS);

	/* Switch controller clocks (uMCTL2/PUBL) to DLL output clock */
	io_clrbits32(rcc_base + RCC_DDRITFCR, RCC_DDRITFCR_GSKPCTRL);

	/* Disable all clocks */
	ddr_disable_clock();

	return 0;

selfref_sw_failed:
	/* This bit should be cleared to restore DDR in its previous state */
	io_clrbits32(ddrctrl_base + DDRCTRL_PWRCTL, DDRCTRL_PWRCTL_SELFREF_SW);

pstat_failed:
	io_setbits32(ddrctrl_base + DDRCTRL_PCTRL_0, DDRCTRL_PCTRL_N_PORT_EN);
	io_setbits32(ddrctrl_base + DDRCTRL_PCTRL_1, DDRCTRL_PCTRL_N_PORT_EN);

	return -1;
}

static int ddr_sw_self_refresh_exit(void)
{
	uint64_t timeout_ref = 0;
	vaddr_t rcc_base = stm32_rcc_base();
	vaddr_t pwr_base = stm32_pwr_base();
	vaddr_t ddrctrl_base = get_ddrctrl_base();
	vaddr_t ddrphy_base = get_ddrphy_base();

	/* Enable all clocks */
	ddr_enable_clock();

	do_sw_handshake();

	/* Mask dfi_init_complete_en */
	io_clrbits32(ddrctrl_base + DDRCTRL_DFIMISC,
		     DDRCTRL_DFIMISC_DFI_INIT_COMPLETE_EN);

	do_sw_ack();

	/* Switch controller clocks (uMCTL2/PUBL) to DLL ref clock */
	io_setbits32(rcc_base + RCC_DDRITFCR, RCC_DDRITFCR_GSKPCTRL);

	/* Enable all DLLs: GLITCH window */
	io_clrbits32(ddrphy_base + DDRPHYC_ACDLLCR, DDRPHYC_ACDLLCR_DLLDIS);
	io_clrbits32(ddrphy_base + DDRPHYC_DX0DLLCR, DDRPHYC_DXNDLLCR_DLLDIS);
	io_clrbits32(ddrphy_base + DDRPHYC_DX1DLLCR, DDRPHYC_DXNDLLCR_DLLDIS);
	io_clrbits32(ddrphy_base + DDRPHYC_DX2DLLCR, DDRPHYC_DXNDLLCR_DLLDIS);
	io_clrbits32(ddrphy_base + DDRPHYC_DX3DLLCR, DDRPHYC_DXNDLLCR_DLLDIS);

	/* Additional delay to avoid early DLL clock switch */
	udelay(50);

	/* Switch controller clocks (uMCTL2/PUBL) to DLL ref clock */
	io_clrbits32(rcc_base + RCC_DDRITFCR, RCC_DDRITFCR_GSKPCTRL);
	io_clrbits32(ddrphy_base + DDRPHYC_ACDLLCR, DDRPHYC_ACDLLCR_DLLSRST);
	udelay(10);
	io_setbits32(ddrphy_base + DDRPHYC_ACDLLCR, DDRPHYC_ACDLLCR_DLLSRST);

	/* PHY partial init: (DLL lock and ITM reset) */
	io_write32(ddrphy_base + DDRPHYC_PIR,
		   DDRPHYC_PIR_DLLSRST | DDRPHYC_PIR_DLLLOCK |
		   DDRPHYC_PIR_ITMSRST | DDRPHYC_PIR_INIT);

	/* Need to wait at least 10 clock cycles before accessing PGSR */
	udelay(10);

	timeout_ref = timeout_init_us(TIMEOUT_500US);
	while (!(io_read32(ddrphy_base + DDRPHYC_PGSR) & DDRPHYC_PGSR_IDONE))
		if (timeout_elapsed(timeout_ref))
			return -1;

	do_sw_handshake();

	/* Unmask dfi_init_complete_en to uMCTL2 */
	io_setbits32(ddrctrl_base + DDRCTRL_DFIMISC,
		     DDRCTRL_DFIMISC_DFI_INIT_COMPLETE_EN);

	do_sw_ack();

	/* Deactivate sw retention in PWR */
	io_clrbits32(pwr_base + PWR_CR3_OFF, PWR_CR3_DDRRETEN);

	/* Enable PZQ cell (PUBL register) */
	io_clrbits32(ddrphy_base + DDRPHYC_ZQ0CR0, DDRPHYC_ZQ0CRN_ZQPD);

	/* Enable pad drivers */
	io_clrbits32(ddrphy_base + DDRPHYC_ACIOCR, DDRPHYC_ACIOCR_ACPDD);
	io_setbits32(ddrphy_base + DDRPHYC_ACIOCR, DDRPHYC_ACIOCR_ACOE);
	io_clrbits32(ddrphy_base + DDRPHYC_ACIOCR, DDRPHYC_ACIOCR_CKPDD_MASK);
	io_clrbits32(ddrphy_base + DDRPHYC_ACIOCR, DDRPHYC_ACIOCR_CSPDD_MASK);
	io_clrbits32(ddrphy_base + DDRPHYC_DXCCR, DDRPHYC_DXCCR_DXPDD);
	io_clrbits32(ddrphy_base + DDRPHYC_DXCCR, DDRPHYC_DXCCR_DXPDR);
	io_clrbits32(ddrphy_base + DDRPHYC_DSGCR, DDRPHYC_DSGCR_ODTPDD_MASK);
	io_clrbits32(ddrphy_base + DDRPHYC_DSGCR, DDRPHYC_DSGCR_NL2PD);
	io_clrbits32(ddrphy_base + DDRPHYC_DSGCR, DDRPHYC_DSGCR_CKEPDD_MASK);

	/* Remove selfrefresh */
	io_clrbits32(ddrctrl_base + DDRCTRL_PWRCTL, DDRCTRL_PWRCTL_SELFREF_SW);

	/* Wait operating_mode == normal */
	timeout_ref = timeout_init_us(TIMEOUT_500US);
	while (1) {
		if ((io_read32(ddrctrl_base + DDRCTRL_STAT) &
		     DDRCTRL_STAT_OPERATING_MODE_MASK) ==
	            DDRCTRL_STAT_OPERATING_MODE_NORMAL)
			break;

		if (timeout_elapsed(timeout_ref))
			return -1;
	}

	/* AXI ports are no longer blocked from taking transactions */
	io_setbits32(ddrctrl_base + DDRCTRL_PCTRL_0, DDRCTRL_PCTRL_N_PORT_EN);
	io_setbits32(ddrctrl_base + DDRCTRL_PCTRL_1, DDRCTRL_PCTRL_N_PORT_EN);

	io_setbits32(rcc_base + RCC_DDRITFCR, RCC_DDRITFCR_AXIDCGEN);

	return 0;
}

uint32_t get_ddrphy_calibration(void)
{
	vaddr_t ddrphy_base = get_ddrphy_base();
	uint32_t zcal = io_read32(ddrphy_base + DDRPHYC_ZQ0CR0);

	return (zcal & DDRPHYC_ZQ0CRN_ZDATA_MASK) >> DDRPHYC_ZQ0CRN_ZDATA_SHIFT;
}

int ddr_standby_sr_entry(void)
{
	vaddr_t pwr_base = stm32_pwr_base();

	if (ddr_sw_self_refresh_in())
		return -1;

	/* Enable I/O retention mode in standby */
	io_setbits32(pwr_base + PWR_CR3_OFF, PWR_CR3_DDRSREN);

	return 0;
}

int ddr_standby_sr_exit(void)
{
	return ddr_sw_self_refresh_exit();
}

static void ddr_sr_mode_ssr(void)
{
	vaddr_t rcc_ddritfcr = stm32_rcc_base() + RCC_DDRITFCR;
	vaddr_t ddrctrl_base = get_ddrctrl_base();

	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRC1LPEN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRC2LPEN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRC1EN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRC2EN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRCAPBLPEN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRPHYCAPBLPEN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRCAPBEN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRPHYCAPBEN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRPHYCEN);
	io_clrbits32(rcc_ddritfcr, RCC_DDRITFCR_AXIDCGEN);
	io_clrbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRCKMOD_MASK);

	/* Disable HW LP interface of uMCTL2 */
	io_clrbits32(ddrctrl_base + DDRCTRL_HWLPCTL, DDRCTRL_HWLPCTL_HW_LP_EN);

	/* Configure Automatic LP modes of uMCTL2 */
	io_clrsetbits32(ddrctrl_base + DDRCTRL_PWRTMG,
			DDRCTRL_PWRTMG_SELFREF_TO_X32_MASK,
			DDRCTRL_PWRTMG_SELFREF_TO_X32_0);

	/*
	 * Disable Clock disable with LP modes
	 * (used in RUN mode for LPDDR2 with specific timing).
	 */
	io_clrbits32(ddrctrl_base + DDRCTRL_PWRCTL,
		     DDRCTRL_PWRCTL_EN_DFI_DRAM_CLK_DISABLE);

	/* Disable automatic Self-Refresh mode */
	io_clrbits32(ddrctrl_base + DDRCTRL_PWRCTL,
		     DDRCTRL_PWRCTL_SELFREF_EN);
}

static void ddr_sr_mode_asr(void)
{
	vaddr_t rcc_ddritfcr = stm32_rcc_base() + RCC_DDRITFCR;
	vaddr_t ddrctrl_base = get_ddrctrl_base();

	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_AXIDCGEN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRC1LPEN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRC2LPEN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRPHYCLPEN);
	io_clrsetbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRCKMOD_MASK,
			RCC_DDRITFCR_DDRCKMOD_ASR1);

	/* Enable HW LP interface of uMCTL2 */
	io_setbits32(ddrctrl_base + DDRCTRL_HWLPCTL, DDRCTRL_HWLPCTL_HW_LP_EN);

	/* Configure Automatic LP modes of uMCTL2 */
	io_clrsetbits32(ddrctrl_base + DDRCTRL_PWRTMG,
			DDRCTRL_PWRTMG_SELFREF_TO_X32_MASK,
			DDRCTRL_PWRTMG_SELFREF_TO_X32_0);

	/*
	 * Enable Clock disable with LP modes
	 * (used in RUN mode for LPDDR2 with specific timing).
	 */
	io_setbits32(ddrctrl_base + DDRCTRL_PWRCTL,
		     DDRCTRL_PWRCTL_EN_DFI_DRAM_CLK_DISABLE);

	/* Enable automatic Self-Refresh for ASR mode */
	io_setbits32(ddrctrl_base + DDRCTRL_PWRCTL,
		     DDRCTRL_PWRCTL_SELFREF_EN);
}

static void ddr_sr_mode_hsr(void)
{
	vaddr_t rcc_ddritfcr = stm32_rcc_base() + RCC_DDRITFCR;
	vaddr_t ddrctrl_base = get_ddrctrl_base();

	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_AXIDCGEN);
	io_clrbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRC1LPEN);
	io_clrbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRC2LPEN);
	io_setbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRPHYCLPEN);
	io_clrsetbits32(rcc_ddritfcr, RCC_DDRITFCR_DDRCKMOD_MASK,
			   RCC_DDRITFCR_DDRCKMOD_HSR1);

	/* Enable HW LP interface of uMCTL2 */
	io_setbits32(ddrctrl_base + DDRCTRL_HWLPCTL, DDRCTRL_HWLPCTL_HW_LP_EN);

	/* Configure Automatic LP modes of uMCTL2 */
	io_clrsetbits32(ddrctrl_base + DDRCTRL_PWRTMG,
			DDRCTRL_PWRTMG_SELFREF_TO_X32_MASK,
			DDRCTRL_PWRTMG_SELFREF_TO_X32_0);

	/*
	 * Enable Clock disable with LP modes
	 * (used in RUN mode for LPDDR2 with specific timing).
	 */
	io_setbits32(ddrctrl_base + DDRCTRL_PWRCTL,
		     DDRCTRL_PWRCTL_EN_DFI_DRAM_CLK_DISABLE);
}

static enum stm32mp1_ddr_sr_mode ddr_read_sr_mode(void)
{
	uint32_t pwrctl = io_read32(get_ddrctrl_base() + DDRCTRL_PWRCTL);
	uint32_t mask = DDRCTRL_PWRCTL_EN_DFI_DRAM_CLK_DISABLE |
			DDRCTRL_PWRCTL_SELFREF_EN;

	switch (pwrctl & mask) {
	case 0U:
		return DDR_SSR_MODE;

	case DDRCTRL_PWRCTL_EN_DFI_DRAM_CLK_DISABLE:
		return DDR_HSR_MODE;

	case DDRCTRL_PWRCTL_EN_DFI_DRAM_CLK_DISABLE | DDRCTRL_PWRCTL_SELFREF_EN:
		return DDR_ASR_MODE;

	default:
		return DDR_SR_MODE_INVALID;
	}
}

static void ddr_set_sr_mode(enum stm32mp1_ddr_sr_mode mode)
{
	switch (mode) {
	case DDR_SSR_MODE:
		ddr_sr_mode_ssr();
		break;

	case DDR_HSR_MODE:
		ddr_sr_mode_hsr();
		break;

	case DDR_ASR_MODE:
		ddr_sr_mode_asr();
		break;

	default:
		EMSG("Unknown Self Refresh mode\n");
		panic();
	}
}

void ddr_save_sr_mode(enum stm32mp1_ddr_sr_mode mode)
{
	/* Save current mode before setting new one */
	saved_ddr_sr_mode = ddr_read_sr_mode();
	ddr_set_sr_mode(mode);
}

void ddr_restore_sr_mode(void)
{
	ddr_set_sr_mode(saved_ddr_sr_mode);
}
