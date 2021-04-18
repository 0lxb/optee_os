global-incdirs-y += .

srcs-y += stm32mp1_svc_setup.c
srcs-$(CFG_STM32_BSEC_SIP) += bsec_svc.c
srcs-$(CFG_STM32_LOWPOWER_SIP) += low_power_svc.c
srcs-$(CFG_STM32_PWR_SIP) += pwr_svc.c
srcs-$(CFG_STM32_RCC_SIP) += rcc_svc.c
