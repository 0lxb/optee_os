/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2017-2019, STMicroelectronics
 */

#ifndef RCC_SVC_H
#define RCC_SVC_H

#include <stm32mp1_smc.h>

#ifdef CFG_STM32_RCC_SIP
uint32_t rcc_scv_handler(uint32_t x1, uint32_t x2, uint32_t x3);
uint32_t rcc_opp_scv_handler(uint32_t x1, uint32_t x2, uint32_t *res);
#else
static inline uint32_t rcc_scv_handler(uint32_t x1 __unused,
				       uint32_t x2 __unused,
				       uint32_t x3 __unused)
{
	return STM32_SIP_SVC_FAILED;
}
static inline uint32_t rcc_opp_scv_handler(uint32_t x1 __unused,
					   uint32_t x2 __unused,
					   uint32_t *res __unused)

{
	return STM32_SIP_SVC_FAILED;
}
#endif

#endif /*RCC_SVC_H*/
