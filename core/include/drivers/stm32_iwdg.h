/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2018-2019, STMicroelectronics - All Rights Reserved
 */

#ifndef __STM32_IWDG_H__
#define __STM32_IWDG_H__

#include <stdint.h>

#define IWDG_HW_ENABLED			BIT(0)
#define IWDG_DISABLE_ON_STOP		BIT(1)
#define IWDG_DISABLE_ON_STANDBY		BIT(2)

void stm32_iwdg_refresh(unsigned int instance);

#endif /*__STM32_IWDG_H__*/
