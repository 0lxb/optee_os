/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2018-2020, STMicroelectronics
 */

#ifndef __STM32_UTIL_H__
#define __STM32_UTIL_H__

#include <assert.h>
#include <drivers/stm32_bsec.h>
#include <kernel/panic.h>
#include <stdbool.h>
#include <stdint.h>
#include <types_ext.h>

/* SoC versioning and device ID */
TEE_Result stm32mp1_dbgmcu_get_chip_version(uint32_t *chip_version);
TEE_Result stm32mp1_dbgmcu_get_chip_dev_id(uint32_t *chip_dev_id);

/* OPP service */
bool stm32mp_supports_cpu_opp(uint32_t opp_id);

/* Backup registers and RAM utils */
vaddr_t stm32mp_bkpreg(unsigned int idx);
vaddr_t stm32mp_bkpsram_base(void);

/* Platform util for the STGEN driver */
vaddr_t stm32mp_stgen_base(void);

/*
 * SYSCFG IO compensation.
 * These functions assume non-secure world is suspended.
 */
void stm32mp_syscfg_enable_io_compensation(void);
void stm32mp_syscfg_disable_io_compensation(void);

/* Platform util for the GIC */
vaddr_t get_gicc_base(void);
vaddr_t get_gicd_base(void);
void stm32mp_gic_set_end_of_interrupt(uint32_t it);

/*
 * Platform util functions for the GPIO driver
 * @bank: Target GPIO bank ID as per DT bindings
 *
 * Platform shall implement these functions to provide to stm32_gpio
 * driver the resource reference for a target GPIO bank. That are
 * memory mapped interface base address, interface offset (see below)
 * and clock identifier.
 *
 * stm32_get_gpio_bank_offset() returns a bank offset that is used to
 * check DT configuration matches platform implementation of the banks
 * description.
 */
vaddr_t stm32_get_gpio_bank_base(unsigned int bank);
unsigned int stm32_get_gpio_bank_offset(unsigned int bank);
unsigned int stm32_get_gpio_bank_clock(unsigned int bank);

/*
 * Platform util for the IWDG driver
 */

/* Get IWDG_* enable flags mask from watchdog configuration read from fuses */
unsigned long stm32_get_iwdg_otp_config(vaddr_t pbase);

/* Conversion between IWDG instance IDs and hardware resources */
size_t stm32mp_iwdg_instance2irq(unsigned int instance);
unsigned int stm32mp_iwdg_irq2instance(size_t irq);
unsigned int stm32mp_iwdg_iomem2instance(vaddr_t pbase);

/* Platform util for PMIC support */
bool stm32mp_with_pmic(void);

/* Power management service */
#ifdef CFG_PSCI_ARM32
void stm32mp_register_online_cpu(void);
#else
static inline void stm32mp_register_online_cpu(void)
{
}
#endif

/*
 * Generic spinlock function that bypass spinlock if MMU is disabled or
 * lock is NULL.
 */
uint32_t may_spin_lock(unsigned int *lock);
void may_spin_unlock(unsigned int *lock, uint32_t exceptions);

/* Return true if @clock_id is shared by secure and non-secure worlds */
bool stm32mp_nsec_can_access_clock(unsigned long clock_id);

#if defined(CFG_STPMIC1)
/* Return true if non-secure world can manipulate regulator @pmic_regu_name */
bool stm32mp_nsec_can_access_pmic_regu(const char *pmic_regu_name);
#else
static inline bool stm32mp_nsec_can_access_pmic_regu(const char *name __unused)
{
	return false;
}
#endif

/* PM sequences specific to SoC STOP mode support */
void stm32mp1_clk_save_context_for_stop(void);
void stm32mp1_clk_restore_context_for_stop(void);

/* Protect the MCU clock subsytem */
void stm32mp1_clk_mcuss_protect(bool enable);

/*
 * Util for PLL1 settings management based on DT OPP table content.
 */
int stm32mp1_clk_compute_all_pll1_settings(uint32_t buck1_voltage);
void stm32mp1_clk_lp_save_opp_pll1_settings(uint8_t *data, size_t size);
bool stm32mp1_clk_pll1_settings_are_valid(void);
int stm32mp1_set_opp_khz(uint32_t freq_khz);
int stm32mp1_round_opp_khz(uint32_t *freq_khz);

/*
 * Util for reset signal assertion/desassertion for stm32 and platform drivers
 * @id: Target peripheral ID, ID used in reset DT bindings
 * @to_us: Timeout out in microsecond, or 0 if not waiting signal state
 */
TEE_Result stm32_reset_assert(unsigned int id, unsigned int timeout_us);
TEE_Result stm32_reset_deassert(unsigned int id, unsigned int timeout_us);

/* Specific reset to manage the MCU hold boot */
void stm32_reset_assert_deassert_mcu(bool assert_not_deassert);

static inline void stm32_reset_set(unsigned int id)
{
	(void)stm32_reset_assert(id, 0);
}

static inline void stm32_reset_release(unsigned int id)
{
	(void)stm32_reset_deassert(id, 0);
}

/* Return true if and only if @reset_id relates to a non-secure peripheral */
bool stm32mp_nsec_can_access_reset(unsigned int reset_id);

/*
 * Structure and API function for BSEC driver to get some platform data.
 *
 * @base: BSEC interface registers physical base address
 * @upper_start: Base ID for the BSEC upper words in the platform
 * @max_id: Max value for BSEC word ID for the platform
 */
struct stm32_bsec_static_cfg {
	paddr_t base;
	unsigned int upper_start;
	unsigned int max_id;
};

void stm32mp_get_bsec_static_cfg(struct stm32_bsec_static_cfg *cfg);

/* Reset function for early watchdog management */
void stm32mp_platform_reset(int cpu);

/* Clock calibration. Returns 0 on success */
#ifdef CFG_STM32_CLKCALIB
int stm32mp_start_clock_calib(unsigned int clock_id);
#else
static inline int stm32mp_start_clock_calib(unsigned int clock_id __unused)
{
	return 1;
}
#endif

/* Platform util for the RTC driver */
bool stm32_rtc_get_read_twice(void);

/* Platform util for the ETZPC driver */

/* Convert a ETZPC mode from DT binding to ETZPC DECPROT configuration */
enum etzpc_decprot_attributes stm32mp_etzpc_binding2decprot(uint32_t mode);

/*
 * Return true if platform is in closed_device mode
 */
bool stm32mp_is_closed_device(void);

/*
 * Shared registers support: common lock for accessing SoC registers
 * shared between several drivers.
 */
void io_mask32_stm32shregs(vaddr_t va, uint32_t value, uint32_t mask);

static inline void io_setbits32_stm32shregs(vaddr_t va, uint32_t value)
{
	io_mask32_stm32shregs(va, value, value);
}

static inline void io_clrbits32_stm32shregs(vaddr_t va, uint32_t value)
{
	io_mask32_stm32shregs(va, 0, value);
}

void io_clrsetbits32_stm32shregs(vaddr_t va, uint32_t clr, uint32_t set);

/*
 * Shared reference counter: increments by 2 on secure increment
 * request, decrements by 2 on secure decrement request. Bit #0
 * is set to 1 on non-secure increment request and reset to 0 on
 * non-secure decrement request. These counters initialize to
 * either 0, 1 or 2 upon their expect default state.
 * Counters saturate to UINT_MAX / 2.
 */
#define SHREFCNT_NONSECURE_FLAG		0x1ul
#define SHREFCNT_SECURE_STEP		0x2ul
#define SHREFCNT_MAX			(UINT_MAX / 2)

/* Return 1 if refcnt increments from 0, else return 0 */
static inline int incr_shrefcnt(unsigned int *refcnt, bool secure)
{
	int rc = !*refcnt;

	if (secure) {
		if (*refcnt < SHREFCNT_MAX) {
			*refcnt += SHREFCNT_SECURE_STEP;
			assert(*refcnt < SHREFCNT_MAX);
		}
	} else {
		*refcnt |= SHREFCNT_NONSECURE_FLAG;
	}

	return rc;
}

/* Return 1 if refcnt decrements to 0, else return 0 */
static inline int decr_shrefcnt(unsigned int *refcnt, bool secure)
{
	int  rc = 0;

	if (secure) {
		if (*refcnt < SHREFCNT_MAX) {
			if (*refcnt < SHREFCNT_SECURE_STEP)
				panic();

			*refcnt -= SHREFCNT_SECURE_STEP;
			rc = !*refcnt;
		}
	} else {
		rc = (*refcnt == SHREFCNT_NONSECURE_FLAG);
		*refcnt &= ~SHREFCNT_NONSECURE_FLAG;
	}

	return rc;
}

static inline int incr_refcnt(unsigned int *refcnt)
{
	return incr_shrefcnt(refcnt, true);
}

static inline int decr_refcnt(unsigned int *refcnt)
{
	return decr_shrefcnt(refcnt, true);
}

/*
 * Shared peripherals and resources registration
 *
 * Resources listed in enum stm32mp_shres assigned at run-time to the
 * non-secure world, to the secure world or shared by both worlds.
 * In the later case, there must exist a secure service in OP-TEE
 * for the non-secure world to access the resource.
 *
 * Resources may be a peripheral, a bus, a clock or a memory.
 *
 * Shared resources driver API functions allows drivers to register the
 * resource as secure, non-secure or shared and to get the resource
 * assignation state.
 */
#define STM32MP1_SHRES_GPIOZ(i)		(STM32MP1_SHRES_GPIOZ_0 + i)

enum stm32mp_shres {
	STM32MP1_SHRES_GPIOZ_0 = 0,
	STM32MP1_SHRES_GPIOZ_1,
	STM32MP1_SHRES_GPIOZ_2,
	STM32MP1_SHRES_GPIOZ_3,
	STM32MP1_SHRES_GPIOZ_4,
	STM32MP1_SHRES_GPIOZ_5,
	STM32MP1_SHRES_GPIOZ_6,
	STM32MP1_SHRES_GPIOZ_7,
	STM32MP1_SHRES_IWDG1,
	STM32MP1_SHRES_USART1,
	STM32MP1_SHRES_SPI6,
	STM32MP1_SHRES_I2C4,
	STM32MP1_SHRES_RNG1,
	STM32MP1_SHRES_HASH1,
	STM32MP1_SHRES_CRYP1,
	STM32MP1_SHRES_I2C6,
	STM32MP1_SHRES_RTC,
	STM32MP1_SHRES_MCU,
	STM32MP1_SHRES_PLL3,
	STM32MP1_SHRES_MDMA,

	STM32MP1_SHRES_COUNT
};

/* Register resource @id as a secure peripheral */
void stm32mp_register_secure_periph(enum stm32mp_shres id);

/* Register resource @id as a non-secure peripheral */
void stm32mp_register_non_secure_periph(enum stm32mp_shres id);

/*
 * Register resource identified by @base as a secure peripheral
 * @base: IOMEM physical base address of the resource
 */
void stm32mp_register_secure_periph_iomem(vaddr_t base);

/*
 * Register resource identified by @base as a non-secure peripheral
 * @base: IOMEM physical base address of the resource
 */
void stm32mp_register_non_secure_periph_iomem(vaddr_t base);

/*
 * Register GPIO resource as a secure peripheral
 * @bank: Bank of the target GPIO
 * @pin: Bit position of the target GPIO in the bank
 */
void stm32mp_register_secure_gpio(unsigned int bank, unsigned int pin);

/*
 * Register GPIO resource as a non-secure peripheral
 * @bank: Bank of the target GPIO
 * @pin: Bit position of the target GPIO in the bank
 */
void stm32mp_register_non_secure_gpio(unsigned int bank, unsigned int pin);

/* Return true if and only if resource @id is registered as secure */
bool stm32mp_periph_is_secure(enum stm32mp_shres id);

/* Return true if and only if GPIO bank @bank is registered as secure */
bool stm32mp_gpio_bank_is_secure(unsigned int bank);

/* Return true if and only if GPIO bank @bank is registered as shared */
bool stm32mp_gpio_bank_is_shared(unsigned int bank);

/* Return true if and only if GPIO bank @bank is registered as non-secure */
bool stm32mp_gpio_bank_is_non_secure(unsigned int bank);

/* Register parent clocks of @clock (ID used in clock DT bindings) as secure */
void stm32mp_register_clock_parents_secure(unsigned long clock_id);

/* Register a resource identified by a ETZPC DECPROT identifier */
void stm32mp_register_etzpc_decprot(unsigned int id,
				    enum etzpc_decprot_attributes attr);

#endif /*__STM32_UTIL_H__*/
