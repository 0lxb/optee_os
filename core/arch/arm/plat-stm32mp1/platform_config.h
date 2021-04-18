/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2017-2018, STMicroelectronics
 */

#ifndef PLATFORM_CONFIG_H
#define PLATFORM_CONFIG_H

#include <mm/generic_ram_layout.h>

/* Enable/disable use of the core0 reset control from RCC */
#undef STM32MP1_USE_MPU0_RESET

/* Make stacks aligned to data cache line length */
#define STACK_ALIGNMENT			32

#if defined(CFG_WITH_PAGER)
#if defined(CFG_WITH_LPAE)
/*
 * Optimize unpaged memory size:
 * - one table for the level2 table for overall vmem range
 * - two tables for TEE RAM fine grain mapping [2ffc.0000 301f.ffff]
 * - one table for a 2MByte dynamic shared virtual memory (SHM_VASPACE)
 */
#define MAX_XLAT_TABLES			4
#else
/*
 * Optimize unpaged memory size:
 * - two tables for TEE RAM mapping [2ffc.0000 300f.ffff]
 * - one table for secure internal RAMs (PM: ROMed core TEE RAM)
 * - one table for non-secure internal RAMs (PM: DDR first page)
 * - two tables for a 2MByte dynamiq shared virtual memory (SHM_VASPACE)
 */
#define MAX_XLAT_TABLES			6
#endif /*CFG_WITH_LPAE*/
#else
/* Be generous with this setup that has plenty of secure RAM */
#define MAX_XLAT_TABLES			10
#endif /*CFG_WITH_PAGER*/

/* SoC interface registers base address */
#define MCUSRAM_BASE			0x30000000ul
#define RETRAM_BASE			0x38000000ul
#define BKPSRAM_BASE			0x54000000
#define BSEC_BASE			0x5c005000
#define ETZPC_BASE			0x5c007000
#define CRYP1_BASE			0x54001000
#define DBGMCU_BASE			0x50081000
#define DDR_BASE			0xc0000000ul
#define DDRCTRL_BASE			0x5a003000
#define DDRPHYC_BASE			0x5a004000
#define GIC_BASE			0xa0021000ul
#define GPIOA_BASE			0x50002000
#define GPIOB_BASE			0x50003000
#define GPIOC_BASE			0x50004000
#define GPIOD_BASE			0x50005000
#define GPIOE_BASE			0x50006000
#define GPIOF_BASE			0x50007000
#define GPIOG_BASE			0x50008000
#define GPIOH_BASE			0x50009000
#define GPIOI_BASE			0x5000a000
#define GPIOJ_BASE			0x5000b000
#define GPIOK_BASE			0x5000c000
#define GPIOZ_BASE			0x54004000
#define HASH1_BASE			0x54002000
#define I2C4_BASE			0x5c002000
#define I2C6_BASE			0x5c009000
#define IWDG1_BASE			0x5c003000
#define IWDG2_BASE			0x5a002000
#define PWR_BASE			0x50001000
#define RCC_BASE			0x50000000
#define RNG1_BASE			0x54003000
#define RTC_BASE			0x5c004000
#define SPI6_BASE			0x5c001000
#define STGEN_BASE			0x5c008000
#define SYSCFG_BASE			0x50020000
#define SYSRAM_BASE			0x2ffc0000
#define TAMP_BASE			0x5c00a000
#define TIM1_BASE			0x44000000
#define TIM2_BASE			0x40000000
#define TIM3_BASE			0x40001000
#define TIM4_BASE			0x40002000
#define TIM5_BASE			0x40003000
#define TIM6_BASE			0x40004000
#define TIM7_BASE			0x40005000
#define TIM8_BASE			0x44001000
#define TIM12_BASE			0x40006000
#define TIM13_BASE			0x40007000
#define TIM14_BASE			0x40008000
#define TIM15_BASE			0x44006000
#define TIM16_BASE			0x44007000
#define TIM17_BASE			0x44008000
#define TZC_BASE			0x5c006000
#define UART1_BASE			0x5c000000
#define UART2_BASE			0x4000e000
#define UART3_BASE			0x4000f000
#define UART4_BASE			0x40010000
#define UART5_BASE			0x40011000
#define UART6_BASE			0x44003000
#define UART7_BASE			0x40018000
#define UART8_BASE			0x40019000

/* DDR expected size if not found in device tree */
#define STM32MP1_DDR_SIZE_DFLT		(1 * 1024 * 1024 * 1024)

/* Console configuration */
#define STM32MP1_DEBUG_USART_BASE	UART4_BASE
#define GIC_SPI_UART4			84

#define CONSOLE_UART_BASE		STM32MP1_DEBUG_USART_BASE
#define CONSOLE_UART_SIZE		1024

/* BSEC OTP resources */
#define STM32MP1_OTP_MAX_ID		0x5FU
#define STM32MP1_UPPER_OTP_START	0x20U

#define OTP_MAX_SIZE			(STM32MP1_OTP_MAX_ID + 1U)

#define CFG0_OTP			"cfg0_otp"
#define CFG0_OTP_SECURED_POS		6

#define HW2_OTP				"hw2_otp"
#define HW2_OTP_IWDG_HW_ENABLE_SHIFT	3
#define HW2_OTP_IWDG_FZ_STOP_SHIFT	5
#define HW2_OTP_IWDG_FZ_STANDBY_SHIFT	7

#define PART_NUMBER_OTP			"part_number_otp"
#define PART_NUMBER_OTP_PART_MASK	GENMASK_32(7, 0)
#define PART_NUMBER_OTP_PART_SHIFT	0

#define HW2_OTP_IWDG_HW_ENABLE_SHIFT		3
#define HW2_OTP_IWDG_FZ_STOP_SHIFT		5
#define HW2_OTP_IWDG_FZ_STANDBY_SHIFT		7

/* GIC resources */
#define GIC_SIZE			0x2000
#define GICC_OFFSET			0x1000
#define GICD_OFFSET			0x0000

#define GIC_NON_SEC_SGI_0		0
#define GIC_SEC_SGI_0			8
#define GIC_SEC_SGI_1			9
#define GIC_SPI_SEC_PHY_TIMER		29

#define TARGET_CPU0_GIC_MASK		BIT(0)
#define TARGET_CPU1_GIC_MASK		BIT(1)
#define TARGET_CPUS_GIC_MASK		GENMASK_32(CFG_TEE_CORE_NB_CORE - 1, 0)

#define STM32MP_GIC_PRIORITY_CSTOP	0xc0

/*
 * GPIO banks: 11 non secure banks (A to K) and 1 secure bank (Z)
 * Bank register's base address is computed from the bank ID listed here.
 */
#define GPIOS_NSEC_COUNT		11
#define GPIOS_NSEC_BASE			GPIOA_BASE
#define GPIOS_NSEC_SIZE			(GPIOS_NSEC_COUNT * SMALL_PAGE_SIZE)

#define STM32MP1_GPIOZ_MAX_COUNT	1
#define STM32MP1_GPIOZ_PIN_MAX_COUNT	8

#define GPIO_BANK_OFFSET		0x1000U

/* Bank IDs used in GPIO driver API */
#define GPIO_BANK_A			0U
#define GPIO_BANK_B			1U
#define GPIO_BANK_C			2U
#define GPIO_BANK_D			3U
#define GPIO_BANK_E			4U
#define GPIO_BANK_F			5U
#define GPIO_BANK_G			6U
#define GPIO_BANK_H			7U
#define GPIO_BANK_I			8U
#define GPIO_BANK_J			9U
#define GPIO_BANK_K			10U
#define GPIO_BANK_Z			25U

/* IWDG resources */
#define IWDG1_INST			0
#define IWDG2_INST			1

#define STM32MP1_IRQ_IWDG1		182U
#define STM32MP1_IRQ_IWDG2		183U

/* RCC platform resources */
#define RCC_WAKEUP_IT			177

/* TAMP resources */
#define TAMP_BKP_REGISTER_OFF		0x100

/* TZC resources */
#define STM32MP1_IRQ_TZC		36

#define STM32MP1_TZC_A7_ID		0
#define STM32MP1_TZC_M4_ID		1
#define STM32MP1_TZC_LCD_ID		3
#define STM32MP1_TZC_GPU_ID		4
#define STM32MP1_TZC_MDMA_ID		5
#define STM32MP1_TZC_DMA_ID		6
#define STM32MP1_TZC_USB_HOST_ID	7
#define STM32MP1_TZC_USB_OTG_ID		8
#define STM32MP1_TZC_SDMMC_ID		9
#define STM32MP1_TZC_ETH_ID		10
#define STM32MP1_TZC_DAP_ID		15

/* USART/UART resources */
#define USART1_BASE			UART1_BASE
#define USART2_BASE			UART2_BASE
#define USART3_BASE			UART3_BASE
#define USART6_BASE			UART6_BASE

/* DBGMCU resources */
#define DBGMCU_IDC			0x0
#define DBGMCU_IDC_DEV_ID_MASK		GENMASK_32(11, 0)
#define DBGMCU_IDC_REV_ID_MASK		GENMASK_32(31, 16)
#define DBGMCU_IDC_REV_ID_SHIFT		16

/* BKPSRAM layout */
#define BKPSRAM_SIZE			0x1000
#define BKPSRAM_PM_OFFSET		0x000
#define BKPSRAM_PM_SIZE			(BKPSRAM_PM_MAILBOX_SIZE + \
						BKPSRAM_PM_CONTEXT_SIZE)

#define BKPSRAM_PM_MAILBOX_OFFSET	BKPSRAM_PM_OFFSET
#define BKPSRAM_PM_MAILBOX_SIZE		0x100
#define BKPSRAM_PM_CONTEXT_OFFSET	(BKPSRAM_PM_MAILBOX_OFFSET + \
						BKPSRAM_PM_MAILBOX_SIZE)
#define BKPSRAM_PM_CONTEXT_SIZE		0xF00

/* SYSRAM layout */
#define SYSRAM_SIZE			0x40000
#define SYSRAM_NS_SIZE			(SYSRAM_SIZE - SYSRAM_SEC_SIZE)

/* Non-secure SYSRAM must be above (higher addresses) secure SYSRAM */
#if (CFG_STM32MP1_SCMI_SHM_BASE >= SYSRAM_BASE) && \
	((CFG_STM32MP1_SCMI_SHM_BASE + CFG_STM32MP1_SCMI_SHM_SIZE) <= \
	 (SYSRAM_BASE + SYSRAM_SIZE))
#define SYSRAM_SEC_SIZE		(CFG_STM32MP1_SCMI_SHM_BASE - SYSRAM_BASE)
#else
#define SYSRAM_SEC_SIZE		SYSRAM_SIZE
#endif

/* RETRAM layout */
#define RETRAM_SIZE			0x10000

/* MCUSRAM layout */
#define MCUSRAM_SIZE			0x60000

/* SoC part numbers and revisions */
#define STM32MP1_CHIP_ID		0x500

#define STM32MP157C_PART_NB		0x05000000
#define STM32MP157A_PART_NB		0x05000001
#define STM32MP153C_PART_NB		0x05000024
#define STM32MP153A_PART_NB		0x05000025
#define STM32MP151C_PART_NB		0x0500002E
#define STM32MP151A_PART_NB		0x0500002F
#define STM32MP157F_PART_NB		0x05000080
#define STM32MP157D_PART_NB		0x05000081
#define STM32MP153F_PART_NB		0x050000A4
#define STM32MP153D_PART_NB		0x050000A5
#define STM32MP151F_PART_NB		0x050000AE
#define STM32MP151D_PART_NB		0x050000AF

#define STM32MP1_CHIP_DEFAULT_VERSION	0

#define STM32MP1_REV_A			0x00001000
#define STM32MP1_REV_B			0x00002000
#define STM32MP1_REV_Z			0x00002001

/* OPP */
#define PLAT_OPP_ID1			1U
#define PLAT_OPP_ID2			2U
#define PLAT_MAX_OPP_NB			2U
#define PLAT_MAX_PLLCFG_NB		6U

#endif /*PLATFORM_CONFIG_H*/
