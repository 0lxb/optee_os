// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2017-2018, STMicroelectronics - All Rights Reserved
 * Copyright (c) 2017, ARM Limited and Contributors. All rights reserved.
 */

#include <arm32.h>
#include <drivers/stm32mp1_ddrc.h>
#include <dt-bindings/power/stm32mp1-power.h>
#include <kernel/boot.h>
#include <kernel/delay.h>
#include <kernel/interrupt.h>
#include <kernel/misc.h>
#include <kernel/panic.h>
#include <platform_config.h>
#include <sm/pm.h>
#include <sm/psci.h>
#include <stm32mp_pm.h>
#include <stm32_util.h>
#include <trace.h>

#include "stm32mp1_smc.h"
#include "low_power_svc.h"
#include "../pm/power.h"
#include "../pm/context.h"

#undef DDR_SR_TEST

#ifdef DDR_SR_TEST
uint32_t sr_mode_scv_handler(uint32_t __maybe_unused x1, uint32_t x2)
{
	unsigned int mode = x2;

	DMSG("DDR selfrefresh mode 0x%" PRIx32 ", 0x%" PRIx32, mode, x1);

	switch (mode) {
	case STM32_SIP_SVC_SR_MODE_SSR:
		ddr_sr_mode_ssr();
		break;
	case STM32_SIP_SVC_SR_MODE_ASR:
		ddr_sr_mode_asr();
		break;
	case STM32_SIP_SVC_SR_MODE_HSR:
		ddr_sr_mode_hsr();
		break;
	default:
		return STM32_SIP_SVC_INVALID_PARAMS;
	}

	return STM32_SIP_SVC_OK;
}
#else
uint32_t sr_mode_scv_handler(uint32_t __unused x1, uint32_t __unused x2)
{
	return STM32_SIP_SVC_FAILED;
}
#endif

uint32_t cstop_scv_handler(struct sm_ctx __unused *ctx, uint32_t __unused x1,
			   uint32_t __unused x2, uint32_t __unused x3)
{
	DMSG("core %u", get_core_pos());

	stm32mp1_set_lp_deepest_soc_mode(PSCI_MODE_SYSTEM_SUSPEND,
					 STM32_PM_CSTOP_ALLOW_LPLV_STOP);

	return (psci_system_suspend(ctx->nsec.mon_lr, 0, &ctx->nsec) == 0) ?
		STM32_SIP_SVC_OK : STM32_SIP_SVC_FAILED;
}

uint32_t standby_scv_handler(struct sm_ctx *ctx, uint32_t __unused x1,
			     uint32_t __unused x2, uint32_t x3)
{
	uint32_t nsec_resume_ep = x3;

	DMSG("core %u", get_core_pos());

	if (nsec_resume_ep == 0U) {
		shutdown_scv_handler();
		panic();
	}

	stm32mp1_set_lp_deepest_soc_mode(PSCI_MODE_SYSTEM_SUSPEND,
					 STM32_PM_CSTOP_ALLOW_STANDBY_DDR_SR);

	return (psci_system_suspend(nsec_resume_ep, 0, &ctx->nsec) == 0) ?
		STM32_SIP_SVC_OK : STM32_SIP_SVC_FAILED;
}

uint32_t shutdown_scv_handler(void)
{
	DMSG("core %u", get_core_pos());

	if (!stm32mp_with_pmic()) {
		return STM32_SIP_SVC_FAILED;
	}

	psci_system_off();
	panic();
}

uint32_t pm_domain_scv_handler(uint32_t id, uint32_t enable)
{
	unsigned int pd = id;

	DMSG("%sable PD %u", enable != 0 ? "En" : "Dis", pd);

	switch (pd) {
	case STM32MP1_PD_VSW:
	case STM32MP1_PD_CORE_RET:
	case STM32MP1_PD_CORE:
		break;
	default:
		return STM32_SIP_SVC_INVALID_PARAMS;
	}

	stm32mp1_set_pm_domain_state(pd, enable);

	return STM32_SIP_SVC_OK;
}

#ifdef CFG_TEE_CORE_DEBUG
uint32_t pm_set_lp_state_scv_handler(uint32_t request, uint32_t state)
{
	uint32_t power_mode;

	switch (request) {
	case STM32_OEM_SVC_LP_FORCE_SUSPEND_PARAMS:
		DMSG("Set suspend mode to %u", state);
		power_mode = PSCI_MODE_SYSTEM_SUSPEND;
		break;
	case STM32_OEM_SVC_LP_FORCE_OFF_PARAMS:
		DMSG("Set off mode to %u", state);
		power_mode = PSCI_MODE_SYSTEM_OFF;
		break;
	default:
		return STM32_OEM_SVC_INVALID_PARAMS;
	}

	if (stm32mp1_set_lp_deepest_soc_mode(power_mode, state) < 0) {
		return STM32_OEM_SVC_FAILED;
	}

	return STM32_OEM_SVC_OK;
}
#else
uint32_t pm_set_lp_state_scv_handler(uint32_t __unused mode,
				     uint32_t __unused state)
{
	return STM32_SIP_SVC_FAILED;
}
#endif
