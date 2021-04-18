// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2017-2019, STMicroelectronics
 */

#include <drivers/stm32mp1_rcc.h>
#include <dt-bindings/clock/stm32mp1-clks.h>
#include <inttypes.h>
#include <io.h>
#include <mm/core_memprot.h>
#include <platform_config.h>
#include <stm32_util.h>
#include <trace.h>

#include "rcc_svc.h"
#include "stm32mp1_smc.h"

static bool offset_is_clear_register(uint32_t __maybe_unused offset)
{
	/* All allowed registers are non set/clear registers  */
	return false;
}

static void access_allowed_mask(uint32_t request, uint32_t offset,
				uint32_t value, uint32_t allowed_mask)
{
	vaddr_t va = stm32_rcc_base() + offset;

	if (!allowed_mask)
		return;

	switch (request) {
	case STM32_SIP_SVC_REG_WRITE:
		if (offset_is_clear_register(offset)) {
			/* CLR registers show SET state, not CLR state */
			io_write32(va, value & allowed_mask);
		} else {
			io_mask32_stm32shregs(va, value, allowed_mask);
		}
		FMSG("wrt 0x%" PRIx32 "=0x%" PRIx32 " => 0x%" PRIx32,
			offset, value, io_read32(va));
		break;

	case STM32_SIP_SVC_REG_SET:
		if (offset_is_clear_register(offset)) {
			/* CLR registers show SET state, not CLR state */
			io_write32(va, value & allowed_mask);
		} else {
			io_setbits32_stm32shregs(va, value & allowed_mask);
		}
		FMSG("set 0x%" PRIx32 "=0x%" PRIx32 " => 0x%" PRIx32,
			offset, value, io_read32(va));
		break;

	case STM32_SIP_SVC_REG_CLEAR:
		/* Nothing to do on CLR registers */
		if (!offset_is_clear_register(offset))
			io_clrbits32_stm32shregs(va, value & allowed_mask);
		FMSG("clear 0x%" PRIx32 "=0x%" PRIx32 " => 0x%" PRIx32,
			offset, value, io_read32(va));
		break;

	default:
		break;
	}
}

static uint32_t raw_allowed_access_request(uint32_t request,
					   uint32_t offset, uint32_t value)
{
	uint32_t allowed_mask = 0;

	switch (offset) {
	case RCC_MP_CIER:
	case RCC_MP_CIFR:
		allowed_mask = RCC_MP_CIFR_WKUPF;
		break;
	case RCC_MP_GCR:
		allowed_mask = RCC_MP_GCR_BOOT_MCU;
		break;
	default:
		return STM32_SIP_SVC_INVALID_PARAMS;
	}

	access_allowed_mask(request, offset, value, allowed_mask);

	return STM32_SIP_SVC_OK;
}

uint32_t rcc_scv_handler(uint32_t x1, uint32_t x2, uint32_t x3)
{
	uint32_t request = x1;
	uint32_t offset = x2;
	uint32_t value = x3;

	/*
	 * Argument x2 can be either the register physical address of the
	 * register offset toward RCC_BASE.
	 */
	if (offset & ~RCC_OFFSET_MASK) {
		if ((offset & ~RCC_OFFSET_MASK) != RCC_BASE)
			return STM32_SIP_SVC_INVALID_PARAMS;

		offset &= RCC_OFFSET_MASK;
	}

	DMSG_RAW("RCC service: %s 0x%" PRIx32 " at offset 0x%" PRIx32,
		 request == STM32_SIP_SVC_REG_WRITE ? "write" :
		 request == STM32_SIP_SVC_REG_SET ? "set" : "clear",
		 value, offset);

	return raw_allowed_access_request(request, offset, value);
}

uint32_t rcc_opp_scv_handler(uint32_t x1, uint32_t x2, uint32_t *res)
{
	uint32_t cmd = x1;
	uint32_t opp = x2 / 1000U; /* KHz */

	switch (cmd) {
	case STM32_SIP_SVC_RCC_OPP_SET:
		if (stm32mp1_set_opp_khz(opp))
			return STM32_SIP_SVC_FAILED;
		break;

	case STM32_SIP_SVC_RCC_OPP_ROUND:
		if(stm32mp1_round_opp_khz(&opp))
			return STM32_SIP_SVC_FAILED;

		if (MUL_OVERFLOW(opp, 1000, res))
			return STM32_SIP_SVC_FAILED;
		break;

	default:
		return STM32_SIP_SVC_INVALID_PARAMS;
	}

	return STM32_SIP_SVC_OK;
}
