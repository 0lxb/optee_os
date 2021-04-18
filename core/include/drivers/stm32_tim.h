/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2018-2019, STMicroelectronics
 */

#ifndef STM32_TIM_H
#define STM32_TIM_H

enum stm32_tim_cal {
	HSI_CAL = 0,
	CSI_CAL
};

unsigned long stm32_tim_hsi_freq(void);
unsigned long stm32_tim_csi_freq(void);

/*
 * Get the timer frequence callback function for a target clock calibration
 * @timer_freq_cb - Output callback function
 * @type - Target clock calibration ID
 */
void stm32_tim_freq_func(unsigned long (**timer_freq_cb)(void),
			 enum stm32_tim_cal type);

#endif /* STM32_TIM_H */
