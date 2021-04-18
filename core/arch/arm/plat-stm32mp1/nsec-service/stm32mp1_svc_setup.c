// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2017-2020, STMicroelectronics
 */

#include <kernel/thread.h>
#include <drivers/scmi-msg.h>
#include <sm/optee_smc.h>
#include <sm/sm.h>
#include <stm32_util.h>

#include "bsec_svc.h"
#include "low_power_svc.h"
#include "pwr_svc.h"
#include "rcc_svc.h"
#include "stm32mp1_smc.h"

#ifdef CFG_STM32_CLKCALIB_SIP
static uint32_t calib_scv_handler(uint32_t x1)
{
	unsigned long clock_id = x1;

	if (stm32mp_start_clock_calib(clock_id))
		return STM32_SIP_SVC_FAILED;

	return STM32_SIP_SVC_OK;
}
#else
static uint32_t calib_scv_handler(uint32_t __unused x1)
{
	return STM32_SIP_SVC_FAILED;
}
#endif

static enum sm_handler_ret sip_service(struct sm_ctx *ctx,
				       struct thread_smc_args *args)
{
	switch (OPTEE_SMC_FUNC_NUM(args->a0)) {
	case STM32_SIP_SVC_FUNC_CALL_COUNT:
		args->a0 = STM32_SIP_SVC_FUNCTION_COUNT;
		break;
	case STM32_SIP_SVC_FUNC_VERSION:
		args->a0 = STM32_SIP_SVC_VERSION_MAJOR;
		args->a1 = STM32_SIP_SVC_VERSION_MINOR;
		break;
	case STM32_SIP_SVC_FUNC_UID:
		args->a0 = STM32_SIP_SVC_UID_0;
		args->a1 = STM32_SIP_SVC_UID_1;
		args->a2 = STM32_SIP_SVC_UID_2;
		args->a3 = STM32_SIP_SVC_UID_3;
		break;
	case STM32_SIP_SVC_FUNC_SCMI_AGENT0:
		scmi_smt_fastcall_smc_entry(0);
		args->a0 = STM32_SIP_SVC_OK;
		break;
	case STM32_SIP_SVC_FUNC_SCMI_AGENT1:
		scmi_smt_fastcall_smc_entry(1);
		args->a0 = STM32_SIP_SVC_OK;
		break;
	case STM32_SIP_SVC_FUNC_BSEC:
		bsec_main(args);
		break;
	case STM32_SIP_SVC_FUNC_RCC:
		args->a0 = rcc_scv_handler(args->a1, args->a2, args->a3);
		break;
        case STM32_SIP_SVC_FUNC_RCC_OPP:
                args->a0 = rcc_opp_scv_handler(args->a1, args->a2, &args->a1);
                break;
	case STM32_SIP_SVC_FUNC_CAL:
		args->a0 = calib_scv_handler(args->a1);
		break;
	case STM32_SIP_SVC_FUNC_PWR:
		args->a0 = pwr_scv_handler(args->a1, args->a2, args->a3);
		break;
	case STM32_SIP_FUNC_SR_MODE:
		args->a0 = sr_mode_scv_handler(args->a1, args->a2);
		break;
	case STM32_SIP_FUNC_CSTOP:
		args->a0 = cstop_scv_handler(ctx, args->a1, args->a2, args->a3);
		break;
	case STM32_SIP_FUNC_STANDBY:
		args->a0 = standby_scv_handler(ctx, args->a1, args->a2, args->a3);
		break;
	case STM32_SIP_FUNC_SHUTDOWN:
		args->a0 = shutdown_scv_handler();
		break;
	case STM32_SIP_FUNC_PD_DOMAIN:
		args->a0 = pm_domain_scv_handler(args->a1, args->a2);
		break;
	default:
		return SM_HANDLER_PENDING_SMC;
	}

	return SM_HANDLER_SMC_HANDLED;
}

static enum sm_handler_ret oem_service(struct sm_ctx *ctx __unused,
				       struct thread_smc_args *args)
{
	switch (OPTEE_SMC_FUNC_NUM(args->a0)) {
	case STM32_OEM_SVC_FUNC_CALL_COUNT:
		args->a0 = STM32_OEM_SVC_FUNCTION_COUNT;
		break;
	case STM32_OEM_SVC_FUNC_VERSION:
		args->a0 = STM32_OEM_SVC_VERSION_MAJOR;
		args->a1 = STM32_OEM_SVC_VERSION_MINOR;
		break;
	case STM32_OEM_SVC_FUNC_UID:
		args->a0 = STM32_OEM_SVC_UID_0;
		args->a1 = STM32_OEM_SVC_UID_1;
		args->a2 = STM32_OEM_SVC_UID_2;
		args->a3 = STM32_OEM_SVC_UID_3;
		break;
	case STM32_OEM_SVC_FUNC_LP_FORCE_PARAMS:
		args->a0 = pm_set_lp_state_scv_handler(args->a1, args->a2);
		break;
	default:
		return SM_HANDLER_PENDING_SMC;
	}

	return SM_HANDLER_SMC_HANDLED;
}

enum sm_handler_ret sm_platform_handler(struct sm_ctx *ctx)
{
	struct thread_smc_args *args = (void *)&ctx->nsec.r0;

	if (!OPTEE_SMC_IS_FAST_CALL(args->a0))
		return SM_HANDLER_PENDING_SMC;

	switch (OPTEE_SMC_OWNER_NUM(args->a0)) {
	case OPTEE_SMC_OWNER_SIP:
		return sip_service(ctx, args);
	case OPTEE_SMC_OWNER_OEM:
		return oem_service(ctx, args);
	default:
		return SM_HANDLER_PENDING_SMC;
	}
}
