// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2018-2020, STMicroelectronics - All Rights Reserved
 * Copyright (c) 2017-2018, ARM Limited and Contributors. All rights reserved.
 */

#include <arm32.h>
#include <boot_api.h>
#include <drivers/clk.h>
#include <drivers/gic.h>
#include <drivers/stm32_rng.h>
#include <drivers/stm32_rtc.h>
#include <drivers/stm32mp1_ddrc.h>
#include <dt-bindings/clock/stm32mp1-clks.h>
#include <dt-bindings/power/stm32mp1-power.h>
#include <generated/context_asm_defines.h>
#include <initcall.h>
#include <io.h>
#include <kernel/cache_helpers.h>
#include <kernel/delay.h>
#include <keep.h>
#include <kernel/panic.h>
#include <kernel/pm.h>
#include <mm/core_memprot.h>
#include <mm/mobj.h>
#include <platform_config.h>
#include <stdlib.h>
#include <stm32_util.h>
#include <stm32mp_pm.h>
#include <string.h>

#include "context.h"
#include "power.h"

#define TRAINING_AREA_SIZE		64

/*
 * STANDBY_CONTEXT_MAGIC0:
 * Context provides magic, resume entry, zq0cr0 zdata and DDR training buffer.
 *
 * STANDBY_CONTEXT_MAGIC1:
 * Context provides magic, resume entry, zq0cr0 zdata, DDR training buffer
 * and PLL1 dual OPP settings structure (86 bytes).
 */
#define STANDBY_CONTEXT_MAGIC0		(0x0001 << 16)
#define STANDBY_CONTEXT_MAGIC1		(0x0002 << 16)

#if CFG_STM32MP15_PM_CONTEX_VERSION == 1
#define STANDBY_CONTEXT_MAGIC	(STANDBY_CONTEXT_MAGIC0 | TRAINING_AREA_SIZE)
#elif CFG_STM32MP15_PM_CONTEX_VERSION == 2
#define STANDBY_CONTEXT_MAGIC	(STANDBY_CONTEXT_MAGIC1 | TRAINING_AREA_SIZE)
#else
#error Invalid value for CFG_STM32MP15_PM_CONTEX_VERSION
#endif

#if (PLAT_MAX_OPP_NB != 2) || (PLAT_MAX_PLLCFG_NB != 6)
#error STANDBY_CONTEXT_MAGIC1 does not support expected PLL1 settings
#endif

/* pll_settings structure size definitions (reference to clock driver) */
#define PLL1_SETTINGS_SIZE		(((PLAT_MAX_OPP_NB * \
					  (PLAT_MAX_PLLCFG_NB + 3)) + 1) * \
					 sizeof(uint32_t))

/*
 * Context saved in TEE RAM during lower power sequence.
 * Can be allocated if to big for static allocation.
 *
 * @stgen_cnt_h: Upper 32bit of the STGEN counter
 * @stgen_cnt_l: Lower 32bit of the STGEN counter
 * @rtc: RTC time read at suspend
 */
struct pm_context {
	uint32_t stgen_cnt_h;
	uint32_t stgen_cnt_l;
	struct stm32_rtc_calendar rtc;
};

static struct pm_context plat_ctx;

/*
 * BKPSRAM contains a mailbox used with early boot stages for resume sequence.
 * The mailbox content data that must be restored before OP-TEE is resumed.
 *
 * @magic: magic value read by early boot stage for consistency
 * @zq0cr0_zdata: DDRPHY configuration to be restored.
 * @ddr_training_backup: DDR area saved at suspend and backed up at resume
 */
struct pm_mailbox {
	uint32_t magic;
	uint32_t core0_resume_ep;
	uint32_t zq0cr0_zdata;
	uint8_t ddr_training_backup[TRAINING_AREA_SIZE];
#if CFG_STM32MP15_PM_CONTEX_VERSION >= 2
	uint8_t pll1_settings[PLL1_SETTINGS_SIZE];
#endif
};

/*
 * BKPSRAM contains OP-TEE resume instruction sequence which restores
 * TEE RAM content. The BKPSRAM contains restoration materials
 * (key, tag) and the resume entry point in restored TEE RAM.
 */
static struct retram_resume_ctx *get_retram_resume_ctx(void)
{
	vaddr_t bkpsram_base = stm32mp_bkpsram_base();
	vaddr_t context_base = bkpsram_base + BKPSRAM_PM_CONTEXT_OFFSET;

	return (struct retram_resume_ctx *)context_base;
}

static struct pm_mailbox *get_pm_mailbox(void)
{
	vaddr_t bkpsram_base = stm32mp_bkpsram_base();
	vaddr_t mailbox_base = bkpsram_base + BKPSRAM_PM_MAILBOX_OFFSET;

	return (struct pm_mailbox *)mailbox_base;
}

#if TRACE_LEVEL >= TRACE_DEBUG
static void __maybe_unused dump_context(void)
{
	struct pm_mailbox *mailbox = get_pm_mailbox();
	struct retram_resume_ctx *ctx = get_retram_resume_ctx();

	clk_enable(RTCAPB);

	DMSG("Backup registers: address 0x%" PRIx32 ", magic 0x%" PRIx32,
		*(uint32_t *)stm32mp_bkpreg(BCKR_CORE1_BRANCH_ADDRESS),
		*(uint32_t *)stm32mp_bkpreg(BCKR_CORE1_MAGIC_NUMBER));

	clk_disable(RTCAPB);

	clk_enable(BKPSRAM);

	DMSG("BKPSRAM mailbox:  0x%" PRIx32 ", zd 0x%" PRIx32 ", ep 0x%" PRIx32,
		mailbox->magic, mailbox->zq0cr0_zdata,
		mailbox->core0_resume_ep);

	DMSG("BKPSRAM context:  teeram backup @%" PRIx32 ", resume @0x%" PRIx32,
		ctx->teeram_bkp_pa, ctx->resume_pa);

	clk_disable(BKPSRAM);
}
#else
static void __maybe_unused dump_context(void)
{
}
#endif

/*
 * Save and restore functions
 */
static void save_time(void)
{
        vaddr_t stgen = stm32mp_stgen_base();

	plat_ctx.stgen_cnt_h = io_read32(stgen + CNTCVU_OFFSET);
	plat_ctx.stgen_cnt_l = io_read32(stgen + CNTCVL_OFFSET);
	if (plat_ctx.stgen_cnt_l < 10)
		plat_ctx.stgen_cnt_h = io_read32(stgen + CNTCVU_OFFSET);

	clk_enable(RTC);
	stm32_rtc_get_calendar(&plat_ctx.rtc);
}

#if TRACE_LEVEL >= TRACE_DEBUG
static void print_ccm_decryption_duration(void)
{
	vaddr_t stgen = stm32mp_stgen_base();
	struct retram_resume_ctx *ctx = get_retram_resume_ctx();

	clk_enable(BKPSRAM);

	DMSG("CCM decryption duration %llums",
		((unsigned long long)ctx->stgen_cnt * 1000) /
		io_read32(stgen + CNTFID_OFFSET));

	clk_enable(BKPSRAM);
}
#else
static void print_ccm_decryption_duration(void)
{
}
#endif

static void restore_time(void)
{
	struct stm32_rtc_calendar current_calendar = { };
	unsigned long long stdby_time_in_ms = 0;
	unsigned long long cnt = 0;
	vaddr_t stgen = stm32mp_stgen_base();
	struct retram_resume_ctx __maybe_unused *ctx = get_retram_resume_ctx();

	stm32_rtc_get_calendar(&current_calendar);
	stdby_time_in_ms = stm32_rtc_diff_calendar(&current_calendar,
						   &plat_ctx.rtc);

	cnt = ((uint64_t)plat_ctx.stgen_cnt_h << 32) | plat_ctx.stgen_cnt_l;
	cnt += (stdby_time_in_ms * io_read32(stgen + CNTFID_OFFSET)) / 1000U;

	io_clrbits32(stgen + CNTCR_OFFSET, CNTCR_EN);
	io_write32(stgen + CNTCVL_OFFSET, (uint32_t)cnt);
	io_write32(stgen + CNTCVU_OFFSET, (uint32_t)(cnt >> 32));
	io_setbits32(stgen + CNTCR_OFFSET, CNTCR_EN);

	/* Balance clock enable(RTC) at save_time() */
	clk_disable(RTC);

	print_ccm_decryption_duration();
}

static bool __maybe_unused pm_cb_is_valid(void (*cb)(enum pm_op op, void *hdl),
					  void *hdl)
{
	void *cb_voidp = (void *)(vaddr_t)cb;
	paddr_t cb_phy = virt_to_phys(cb_voidp);
	paddr_t hdl_phy = virt_to_phys(hdl);
	bool valid = false;

	valid = (phys_to_virt(cb_phy, MEM_AREA_TEE_RAM_RX) == cb_voidp) &&
		((phys_to_virt(hdl_phy, MEM_AREA_TEE_RAM_RX) == hdl) ||
		 (phys_to_virt(hdl_phy, MEM_AREA_TEE_RAM_RO) == hdl) ||
		 (phys_to_virt(hdl_phy, MEM_AREA_TEE_RAM_RW) == hdl));

	if (!valid)
		EMSG("pm_cb mandates unpaged arguments %p %p", cb_voidp, hdl);

	return valid;
}

uintptr_t stm32mp_pm_retram_resume_ep(void)
{
	struct retram_resume_ctx *ctx = get_retram_resume_ctx();

	return (uintptr_t)&ctx->resume_sequence;
}

/* Clear the content of the PM mailbox */
void stm32mp_pm_wipe_context(void)
{
	struct retram_resume_ctx *ctx = get_retram_resume_ctx();
	struct pm_mailbox *mailbox = get_pm_mailbox();

	clk_enable(BKPSRAM);

	memset(ctx, 0xa5, sizeof(*ctx));
	memset(mailbox, 0xa5, sizeof(*mailbox));

	clk_disable(BKPSRAM);
}

static struct mobj *teeram_bkp_mobj;

static void init_retram_resume_resources(void)
{
	struct retram_resume_ctx *ctx = get_retram_resume_ctx();
	size_t __maybe_unused csize = 0;
	paddr_t __maybe_unused pa = 0;

	COMPILE_TIME_ASSERT(sizeof(struct pm_mailbox) <
			    BKPSRAM_PM_MAILBOX_SIZE);
	COMPILE_TIME_ASSERT(sizeof(struct retram_resume_ctx) <
			    BKPSRAM_PM_CONTEXT_SIZE);
	csize = (vaddr_t)stm32mp_bkpsram_image_end -
		(vaddr_t)stm32mp_bkpsram_resume;
	assert((sizeof(*ctx) + csize) < BKPSRAM_PM_CONTEXT_SIZE);

	teeram_bkp_mobj = mobj_mm_alloc(mobj_sec_ddr, TEE_RAM_PH_SIZE,
					&tee_mm_sec_ddr);
	if (!teeram_bkp_mobj)
		panic();

	assert((mobj_get_va(teeram_bkp_mobj, 0) != NULL) &&
	       (mobj_get_pa(teeram_bkp_mobj, 0, 0, &pa) == 0));

	clk_enable(BKPSRAM);
	memset(ctx, 0, sizeof(*ctx));
	clk_disable(BKPSRAM);
}

/*
 * When returning from STANDBY, the 64 first bytes of DDR will be overwritten
 * during DDR DQS training. This area must then be saved before going to
 * standby in the PM mailbox with the earlier boot stages.
 */
static void save_ddr_training_area(void)
{
	struct pm_mailbox *mailbox = get_pm_mailbox();
	size_t size = sizeof(mailbox->ddr_training_backup);
	paddr_t pa = DDR_BASE;
	void *va = phys_to_virt(pa, MEM_AREA_RAM_NSEC);

	memcpy(&mailbox->ddr_training_backup[0], va, size);

}

/*
 * When returning from STANDBY, warm boot boot stage needs to access to PLL1
 * settings. This avoids to re-compute them and optimizes performances. This
 * structure must then be saved before going to STANDBY in the PM mailbox
 * shared with the warm boot boot stage.
 */
#if CFG_STM32MP15_PM_CONTEX_VERSION >= 2
static void save_pll1_settings(void)
{
	struct pm_mailbox *mailbox = get_pm_mailbox();
	size_t size = sizeof(mailbox->pll1_settings);
	uint8_t *data = &mailbox->pll1_settings[0];

	stm32mp1_clk_lp_save_opp_pll1_settings(data, size);
}
#endif

static void load_earlyboot_pm_mailbox(void)
{
	struct pm_mailbox *mailbox = get_pm_mailbox();

	COMPILE_TIME_ASSERT(sizeof(struct pm_mailbox) <
			    BKPSRAM_PM_MAILBOX_SIZE);

	assert(clk_is_enabled(BKPSRAM));

	memset(mailbox, 0, sizeof(*mailbox));

	mailbox->zq0cr0_zdata = get_ddrphy_calibration();

	save_ddr_training_area();

#if CFG_STM32MP15_PM_CONTEX_VERSION >= 2
	save_pll1_settings();
#endif
}

#if defined(CFG_STM32_RNG) && defined(CFG_STM32_CRYP)
/*
 * CRYP relies on standard format for CCM IV/B0/CRT0 data. Our sequence uses
 * no AAD, 4 bytes to encode the payload byte size and a 11 byte nonce.
 */
#define PM_CCM_Q			4
#define PM_CCM_Q_FLAGS			(PM_CCM_Q - 1)
#define PM_CCM_TAG_LEN			16
#define PM_CCM_TAG_FLAGS		(((PM_CCM_TAG_LEN - 2) / 2) << 3)

static void save_teeram_in_ddr(void)
{
	struct retram_resume_ctx *ctx = get_retram_resume_ctx();
	size_t __maybe_unused size = (vaddr_t)stm32mp_bkpsram_image_end -
				     (vaddr_t)stm32mp_bkpsram_resume;
	paddr_t pa = 0;
	struct ccm_unpg_ctx *ccm = &ctx->ccm_ctx;
	void *teeram = phys_to_virt(TEE_RAM_START, MEM_AREA_ROM_SEC);
	void *teeram_bkp = mobj_get_va(teeram_bkp_mobj, 0);

	COMPILE_TIME_ASSERT(PM_CTX_CCM_KEY_SIZE == sizeof(ccm->key));
	COMPILE_TIME_ASSERT(PM_CTX_CCM_CTR1_SIZE == sizeof(ccm->ctr1));
	COMPILE_TIME_ASSERT(PM_CTX_CCM_B0_SIZE == sizeof(ccm->b0));
	COMPILE_TIME_ASSERT(PM_CTX_CCM_CTR0_SIZE == sizeof(ccm->ctr0));
	COMPILE_TIME_ASSERT(PM_CTX_CCM_TAG_SIZE == sizeof(ccm->tag));

	assert(clk_is_enabled(BKPSRAM) &&
	       clk_is_enabled(CRYP1));

	memcpy(ctx->resume_sequence,
	       (void *)(vaddr_t)stm32mp_bkpsram_resume, size);

	memset(ctx, 0, sizeof(*ctx));
	ctx->resume_pa = virt_to_phys((void *)(vaddr_t)stm32mp_sysram_resume);
	if (mobj_get_pa(teeram_bkp_mobj, 0, 0, &pa))
		panic();

	ctx->teeram_bkp_pa = (uint32_t)pa;
	ctx->cryp1_base = (uint32_t)phys_to_virt(CRYP1_BASE, MEM_AREA_IO_SEC);
	ctx->rcc_base = (uint32_t)phys_to_virt(RCC_BASE, MEM_AREA_IO_SEC);
	ctx->stgen_base = (uint32_t)phys_to_virt(STGEN_BASE, MEM_AREA_IO_SEC);

	if (stm32_rng_read((uint8_t *)ccm->key, sizeof(ccm->key)))
		panic();

	assert(((PM_CCM_TAG_FLAGS & ~0x38U) | (PM_CCM_Q_FLAGS & ~0x07U)) == 0);
	COMPILE_TIME_ASSERT(PM_CCM_Q <= 4);
	COMPILE_TIME_ASSERT(TEE_RAM_PH_SIZE > UINT16_MAX);
	COMPILE_TIME_ASSERT(TEE_RAM_PH_SIZE < UINT32_MAX);

	if (stm32_rng_read((uint8_t *)ccm->ctr1, sizeof(ccm->ctr1)))
		panic();

	ccm->ctr1[0] &= GENMASK_32(24, 0);
	memcpy(ccm->b0, ccm->ctr1, sizeof(ccm->b0));
	memcpy(ccm->ctr0, ccm->ctr1, sizeof(ccm->ctr0));

	ccm->ctr0[0] |= PM_CCM_Q_FLAGS << 24;
	ccm->ctr0[3] = 0;
	ccm->ctr1[0] |= PM_CCM_Q_FLAGS << 24;
	ccm->ctr1[3] = 1;
	ccm->b0[0] |= (PM_CCM_Q_FLAGS | PM_CCM_TAG_FLAGS) << 24;
	ccm->b0[3] = TEE_RAM_PH_SIZE;

	stm32mp_ccm_encrypt_teeram(ctx, teeram_bkp, teeram, TEE_RAM_PH_SIZE);
	dcache_clean_range(teeram_bkp, TEE_RAM_PH_SIZE);

	memcpy(ctx->ccm_ref_tag, ccm->tag, sizeof(ctx->ccm_ref_tag));

	DMSG("CCM encryption duration %llums",
		((unsigned long long)ctx->stgen_cnt * 1000) /
		io_read32(ctx->stgen_base + CNTFID_OFFSET));
	ctx->stgen_cnt = 0;
}
#else
static void save_teeram_in_ddr(void)
{
	panic("Mandates RNG and CRYP support");
}
#endif /*CFG_STM32_RNG*/

/* Finalize the PM mailbox now that everything is loaded */
static void enable_pm_mailbox(unsigned int suspend)
{
	struct pm_mailbox *mailbox = get_pm_mailbox();
	uint32_t magic = 0;
	uint32_t hint = 0;

	assert(clk_is_enabled(BKPSRAM) &&
	       clk_is_enabled(RTCAPB));

	if (suspend) {
		magic = BOOT_API_A7_CORE0_MAGIC_NUMBER;
		mailbox->magic = STANDBY_CONTEXT_MAGIC;

		hint = virt_to_phys(&get_retram_resume_ctx()->resume_sequence);
	} else {
		mailbox->magic = 0;
	}

	io_write32(stm32mp_bkpreg(BCKR_CORE1_MAGIC_NUMBER), magic);
	io_write32(stm32mp_bkpreg(BCKR_CORE1_BRANCH_ADDRESS), hint);

	mailbox->core0_resume_ep = hint;
}

static void gate_pm_context_clocks(bool enable)
{
	static bool clocks_enabled;

	if (enable) {
		assert(!clocks_enabled);
		clk_enable(BKPSRAM);
		clk_enable(RTCAPB);
		clk_enable(CRYP1);
		clocks_enabled = true;
		return;
	}

	/* Suspended TEE RAM state left the clocks enabled */
	if (clocks_enabled) {
		clk_disable(BKPSRAM);
		clk_disable(RTCAPB);
		clk_disable(CRYP1);
		clocks_enabled = false;
	}
}

/*
 * Context (TEE RAM content + peripherals) must be restored
 * only if system may reach STANDBY state.
 */
TEE_Result stm32mp_pm_save_context(unsigned int soc_mode)
{
	TEE_Result res = TEE_ERROR_GENERIC;

	save_time();

	if (!need_to_backup_cpu_context(soc_mode)) {
		if (need_to_backup_stop_context(soc_mode))
			stm32mp1_clk_save_context_for_stop();

		return TEE_SUCCESS;
	}

	gate_pm_context_clocks(true);
	load_earlyboot_pm_mailbox();
	res = pm_change_state(PM_OP_SUSPEND, 0);
	if (res)
		return res;

	save_teeram_in_ddr();
	enable_pm_mailbox(1);

	return TEE_SUCCESS;
}

void stm32mp_pm_restore_context(unsigned int soc_mode)
{
	if (need_to_backup_cpu_context(soc_mode)) {
		if (pm_change_state(PM_OP_RESUME, 0))
			panic();

		gate_pm_context_clocks(false);
	} else if (need_to_backup_stop_context(soc_mode)) {
		stm32mp1_clk_restore_context_for_stop();
	}

	restore_time();
}

void stm32mp_pm_shutdown_context(void)
{
	gate_pm_context_clocks(true);
	load_earlyboot_pm_mailbox();
	enable_pm_mailbox(0);
	gate_pm_context_clocks(false);
}

static TEE_Result init_pm_support(void)
{
	init_retram_resume_resources();

	stm32mp_pm_wipe_context();

	return TEE_SUCCESS;
}
driver_init(init_pm_support);
