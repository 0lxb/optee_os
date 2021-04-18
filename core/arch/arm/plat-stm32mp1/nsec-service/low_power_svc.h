/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2018, STMicroelectronics
 */

#ifndef LOW_POWER_SVC_H
#define LOW_POWER_SVC_H

#include <sm/sm.h>
#include <stdint.h>

#ifdef CFG_STM32_LOWPOWER_SIP
uint32_t sr_mode_scv_handler(uint32_t x1, uint32_t x2);
uint32_t cstop_scv_handler(struct sm_ctx *nsec,
			   uint32_t x1, uint32_t x2, uint32_t x3);
uint32_t standby_scv_handler(struct sm_ctx *nsec,
			     uint32_t x1, uint32_t x2, uint32_t x3);
uint32_t shutdown_scv_handler(void);
uint32_t pm_domain_scv_handler(uint32_t x1, uint32_t x2);

uint32_t pm_set_lp_state_scv_handler(uint32_t x1, uint32_t x2);
#else
uint32_t sr_mode_scv_handler(uint32_t x1 __unused, uint32_t x2 __unused)
{
	return STM32_SIP_SVC_FAILED;
}

uint32_t cstop_scv_handler(struct sm_ctx *nsec __unused, uint32_t x1 __unused,
			   uint32_t x2 __unused, uint32_t x3 __unused)
{
	return STM32_SIP_SVC_FAILED;
}

uint32_t standby_scv_handler(struct sm_ctx *nsec __unused, uint32_t x1 __unused,
			     uint32_t x2 __unused, uint32_t x3 __unused)
{
	return STM32_SIP_SVC_FAILED;
}

uint32_t shutdown_scv_handler(void)
{
	return STM32_SIP_SVC_FAILED;
}

uint32_t pm_domain_scv_handler(uint32_t x1 __unused, uint32_t x2 __unused)
{
	return STM32_SIP_SVC_FAILED;
}

uint32_t pm_set_lp_state_scv_handler(uint32_t x1 __unused, uint32_t x2 __unused)
{
	return STM32_SIP_SVC_FAILED;
}
#endif /* CFG_STM32_LOWPOWER_SIP */
#endif /* LOW_POWER_SVC_H */
