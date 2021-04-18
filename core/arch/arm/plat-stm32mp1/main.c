// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2016-2018, Linaro Limited
 * Copyright (c) 2017-2021, STMicroelectronics
 */

#include <boot_api.h>
#include <config.h>
#include <console.h>
#include <drivers/clk.h>
#include <drivers/gic.h>
#include <drivers/stm32_etzpc.h>
#include <drivers/stm32_iwdg.h>
#include <drivers/stm32_uart.h>
#include <drivers/stm32mp1_pmic.h>
#include <drivers/stm32mp1_rcc.h>
#include <drivers/stpmic1.h>
#include <dt-bindings/clock/stm32mp1-clks.h>
#include <dt-bindings/soc/st,stm32-etzpc.h>
#include <io.h>
#include <kernel/boot.h>
#include <kernel/dt.h>
#include <kernel/interrupt.h>
#include <kernel/misc.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/tee_misc.h>
#include <mm/core_memprot.h>
#include <platform_config.h>
#include <sm/psci.h>
#include <stm32_util.h>
#include <trace.h>

#ifdef CFG_WITH_NSEC_GPIOS
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, GPIOS_NSEC_BASE, GPIOS_NSEC_SIZE);
#endif
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, I2C4_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, I2C6_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, IWDG1_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, IWDG2_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, RNG1_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, RTC_BASE, SMALL_PAGE_SIZE);
#ifdef CFG_WITH_NSEC_UARTS
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, USART1_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, USART2_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, USART3_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, UART4_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, UART5_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, USART6_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, UART7_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_NSEC, UART8_BASE, SMALL_PAGE_SIZE);
#endif

register_phys_mem_pgdir(MEM_AREA_IO_SEC, BKPSRAM_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, BSEC_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, DBGMCU_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, DDRCTRL_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, DDRPHYC_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, ETZPC_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, GIC_BASE, GIC_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, GPIOZ_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, I2C4_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, I2C6_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, IWDG1_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, PWR_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, RCC_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, RNG1_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, RTC_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, STGEN_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, SYSCFG_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TAMP_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM1_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM2_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM3_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM4_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM5_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM6_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM7_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM8_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM12_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM13_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM14_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM15_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM16_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TIM17_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TZC_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, USART1_BASE, SMALL_PAGE_SIZE);

#if DDR_BASE < CFG_TZDRAM_START
register_dynamic_shm(DDR_BASE, CFG_TZDRAM_START - DDR_BASE);
#endif

#define DRAM_END		(DDR_BASE + CFG_DRAM_SIZE)
#define TZDRAM_END		(CFG_TZDRAM_START + CFG_TZDRAM_SIZE)

#if DRAM_END > TZDRAM_END
register_dynamic_shm(TZDRAM_END, DRAM_END - TZDRAM_END);
#endif

/* Map non-secure DDR bottom for the low power sequence */
register_phys_mem(MEM_AREA_RAM_NSEC, DDR_BASE, SMALL_PAGE_SIZE);

#ifdef CFG_RPROC_PTA
/* Map MCU RETRAM as read write for Cortex-M4 firmware management */
register_phys_mem(MEM_AREA_IO_SEC, RETRAM_BASE, RETRAM_SIZE);

/* Map MCU SRAM as read write for Cortex-M4 firmware management */
register_phys_mem(MEM_AREA_IO_SEC, MCUSRAM_BASE, MCUSRAM_SIZE);
#endif

/* Map TEE physical RAM as read-only for content storage when suspending */
register_phys_mem(MEM_AREA_ROM_SEC, TEE_RAM_START, TEE_RAM_PH_SIZE);

#define _ID2STR(id)		(#id)
#define ID2STR(id)		_ID2STR(id)

static TEE_Result platform_banner(void)
{
#ifdef CFG_EMBED_DTB
	IMSG("Platform stm32mp1: flavor %s - DT %s",
		ID2STR(PLATFORM_FLAVOR),
		ID2STR(CFG_EMBED_DTB_SOURCE_FILE));
#else
	IMSG("Platform stm32mp1: flavor %s - no device tree",
		ID2STR(PLATFORM_FLAVOR));
#endif

	return TEE_SUCCESS;
}
service_init(platform_banner);

/*
 * Console
 *
 * CFG_STM32_EARLY_CONSOLE_UART specifies the ID of the UART used for
 * trace console. Value 0 disables the early console.
 *
 * We cannot use the generic serial_console support since probing
 * the console requires the platform clock driver to be already
 * up and ready which is done only once service_init are completed.
 */
static struct stm32_uart_pdata console_data;

void console_init(void)
{
	/* Early console initialization before MMU setup */
	struct uart {
		paddr_t pa;
		bool secure;
	} uarts[] = {
		[0] = { .pa = 0 },
		[1] = { .pa = USART1_BASE, .secure = true, },
		[2] = { .pa = USART2_BASE, .secure = false, },
		[3] = { .pa = USART3_BASE, .secure = false, },
		[4] = { .pa = UART4_BASE, .secure = false, },
		[5] = { .pa = UART5_BASE, .secure = false, },
		[6] = { .pa = USART6_BASE, .secure = false, },
		[7] = { .pa = UART7_BASE, .secure = false, },
		[8] = { .pa = UART8_BASE, .secure = false, },
	};

	COMPILE_TIME_ASSERT(ARRAY_SIZE(uarts) > CFG_STM32_EARLY_CONSOLE_UART);

	if (!uarts[CFG_STM32_EARLY_CONSOLE_UART].pa)
		return;

	/* No clock yet bound to the UART console */
	console_data.clock = DT_INFO_INVALID_CLOCK;

	console_data.secure = uarts[CFG_STM32_EARLY_CONSOLE_UART].secure;
	stm32_uart_init(&console_data, uarts[CFG_STM32_EARLY_CONSOLE_UART].pa);

	register_serial_console(&console_data.chip);

	IMSG("Early console on UART#%u", CFG_STM32_EARLY_CONSOLE_UART);
}

#ifdef CFG_DT
static TEE_Result init_console_from_dt(void)
{
	struct stm32_uart_pdata *pd = NULL;
	void *fdt = NULL;
	int node = 0;
	TEE_Result res = TEE_ERROR_GENERIC;

	fdt = get_embedded_dt();
	res = get_console_node_from_dt(fdt, &node, NULL, NULL);
	if (res == TEE_ERROR_ITEM_NOT_FOUND) {
		fdt = get_external_dt();
		res = get_console_node_from_dt(fdt, &node, NULL, NULL);
		if (res == TEE_ERROR_ITEM_NOT_FOUND)
			return TEE_SUCCESS;
		if (res != TEE_SUCCESS)
			return res;
	}

	pd = stm32_uart_init_from_dt_node(fdt, node);
	if (!pd) {
		IMSG("DTB disables console");
		register_serial_console(NULL);
		return TEE_SUCCESS;
	}

	/* Replace early console with the new one */
	console_flush();
	console_data = *pd;
	free(pd);
	register_serial_console(&console_data.chip);
	IMSG("DTB enables console (%ssecure)", pd->secure ? "" : "non-");

	return TEE_SUCCESS;
}

/* Probe console from DT once clock inits (service init level) are completed */
service_init_late(init_console_from_dt);

static TEE_Result initialize_pll1_settings(void)
{
	uint32_t cpu_voltage = 0U;
	int ret = 0;

	if (stm32mp1_clk_pll1_settings_are_valid())
		return TEE_SUCCESS;

	if (stm32mp_dt_pmic_status() > 0) {
		stm32mp_get_pmic();

		ret = stpmic1_regulator_voltage_get(
				stm32mp_pmic_get_cpu_supply_name());
		if (ret < 0)
			return ret;

		cpu_voltage = (uint32_t)ret;

		stm32mp_put_pmic();
	}

	if (stm32mp1_clk_compute_all_pll1_settings(cpu_voltage))
		panic();

	return TEE_SUCCESS;
}

/* Compute PLL1 settings once PMIC init is completed */
driver_init_late(initialize_pll1_settings);

static TEE_Result disable_usb_phy_regulator(void)
{
	if (stm32mp_dt_pmic_status() > 0) {
		const char *name = stm32mp_pmic_get_usb_supply_name();

		if (!name)
			return TEE_SUCCESS;

		stm32mp_get_pmic();
		if (stpmic1_regulator_disable(name))
			panic();

		stm32mp_put_pmic();
	}

	return TEE_SUCCESS;
}

/* Disable USB regulator to avoid initialization issues */
boot_final(disable_usb_phy_regulator);
#endif

static uintptr_t stm32_dbgmcu_base(void)
{
	static void *va;

	if (!cpu_mmu_enabled())
		return DBGMCU_BASE;

	if (!va)
		va = phys_to_virt(DBGMCU_BASE, MEM_AREA_IO_SEC);

	return (uintptr_t)va;
}

/* SoC versioning util, returns default ID if can't access DBGMCU */
TEE_Result stm32mp1_dbgmcu_get_chip_version(uint32_t *chip_version)
{
	uint32_t id = STM32MP1_CHIP_DEFAULT_VERSION;

	if (!chip_version)
		return TEE_ERROR_BAD_PARAMETERS;

	if (stm32_bsec_read_debug_conf() & BSEC_DBGSWGEN)
		id = (io_read32(stm32_dbgmcu_base() + DBGMCU_IDC) &
		      DBGMCU_IDC_REV_ID_MASK) >> DBGMCU_IDC_REV_ID_SHIFT;

	*chip_version = id;

	return TEE_SUCCESS;
}

/* SoC device ID util, returns default ID if can't access DBGMCU */
TEE_Result stm32mp1_dbgmcu_get_chip_dev_id(uint32_t *chip_dev_id)
{
	uint32_t id = STM32MP1_CHIP_ID;

	if (!chip_dev_id)
		return TEE_ERROR_BAD_PARAMETERS;

	if (stm32_bsec_read_debug_conf() & BSEC_DBGSWGEN)
		id = io_read32(stm32_dbgmcu_base() + DBGMCU_IDC) &
		     DBGMCU_IDC_DEV_ID_MASK;

	*chip_dev_id = id;

	return TEE_SUCCESS;
}

/*
 * GIC init, used also for primary/secondary boot core wake completion
 */
static struct gic_data gic_data;

void itr_core_handler(void)
{
	gic_it_handle(&gic_data);
}

void main_init_gic(void)
{
	assert(cpu_mmu_enabled());

	gic_init(&gic_data, get_gicc_base(), get_gicd_base());
	itr_init(&gic_data.chip);

	stm32mp_register_online_cpu();
}

void main_secondary_init_gic(void)
{
	gic_cpu_init(&gic_data);

	stm32mp_register_online_cpu();
}

static TEE_Result init_stm32mp1_drivers(void)
{
	/* Without secure DTB support, some drivers must be inited */
	if (!IS_ENABLED(CFG_EMBED_DTB))
		stm32_etzpc_init(ETZPC_BASE);

	/* Secure internal memories for the platform, once ETZPC is ready */
	etzpc_configure_tzma(0, ETZPC_TZMA_ALL_SECURE);
	etzpc_lock_tzma(0);

	COMPILE_TIME_ASSERT(((SYSRAM_BASE + SYSRAM_SIZE) <= CFG_TZSRAM_START) ||
			    ((SYSRAM_BASE <= CFG_TZSRAM_START) &&
			     (SYSRAM_SEC_SIZE >= CFG_TZSRAM_SIZE)));

	etzpc_configure_tzma(1, SYSRAM_SEC_SIZE >> SMALL_PAGE_SHIFT);
#ifdef STM32MP1_USE_MPU0_RESET
	/* BootROM needs unlocked for independent reset */
#else
	etzpc_lock_tzma(1);
#endif

	return TEE_SUCCESS;
}
service_init_late(init_stm32mp1_drivers);

vaddr_t get_gicc_base(void)
{
	struct io_pa_va base = { .pa = GIC_BASE + GICC_OFFSET };

	return io_pa_or_va_secure(&base);
}

vaddr_t get_gicd_base(void)
{
	struct io_pa_va base = { .pa = GIC_BASE + GICD_OFFSET };

	return io_pa_or_va_secure(&base);
}

void stm32mp_get_bsec_static_cfg(struct stm32_bsec_static_cfg *cfg)
{
	cfg->base = BSEC_BASE;
	cfg->upper_start = STM32MP1_UPPER_OTP_START;
	cfg->max_id = STM32MP1_OTP_MAX_ID;
}

bool stm32mp_is_closed_device(void)
{
	uint32_t otp_id = 0;
	size_t bit_len = 0;
	uint32_t otp_value = 0;
	TEE_Result result = TEE_ERROR_GENERIC;

	if (stm32_bsec_find_otp_in_nvmem_layout(CFG0_OTP, &otp_id, &bit_len))
		panic();

	if (bit_len != 8)
		panic();

	/* Non closed_device platform expects fuse well programmed to 0 */
	result = stm32_bsec_shadow_read_otp(&otp_value, otp_id);
	if (!result && !(otp_value & BIT(CFG0_OTP_SECURED_POS)))
		return false;

	return true;
}

bool __weak stm32mp_with_pmic(void)
{
	return false;
}

uint32_t may_spin_lock(unsigned int *lock)
{
	if (!lock || !cpu_mmu_enabled())
		return 0;

	return cpu_spin_lock_xsave(lock);
}

void may_spin_unlock(unsigned int *lock, uint32_t exceptions)
{
	if (!lock || !cpu_mmu_enabled())
		return;

	cpu_spin_unlock_xrestore(lock, exceptions);
}

static vaddr_t stm32_tamp_base(void)
{
	static struct io_pa_va base = { .pa = TAMP_BASE };

	return io_pa_or_va_secure(&base);
}

static vaddr_t bkpreg_base(void)
{
	return stm32_tamp_base() + TAMP_BKP_REGISTER_OFF;
}

vaddr_t stm32mp_bkpreg(unsigned int idx)
{
	return bkpreg_base() + (idx * sizeof(uint32_t));
}

vaddr_t stm32mp_bkpsram_base(void)
{
	struct io_pa_va base = { .pa = BKPSRAM_BASE };

	return io_pa_or_va(&base);
}

vaddr_t stm32mp_stgen_base(void)
{
	struct io_pa_va base = { .pa = STGEN_BASE };

	return io_pa_or_va(&base);
}

vaddr_t stm32_get_gpio_bank_base(unsigned int bank)
{
	static struct io_pa_va gpios_nsec_base = { .pa = GPIOS_NSEC_BASE };
	static struct io_pa_va gpioz_base = { .pa = GPIOZ_BASE };

	/* Get secure mapping address for GPIOZ */
	if (bank == GPIO_BANK_Z)
		return io_pa_or_va_secure(&gpioz_base);

	COMPILE_TIME_ASSERT(GPIO_BANK_A == 0);
	assert(bank <= GPIO_BANK_K);

	return io_pa_or_va_nsec(&gpios_nsec_base) + (bank * GPIO_BANK_OFFSET);
}

unsigned int stm32_get_gpio_bank_offset(unsigned int bank)
{
	if (bank == GPIO_BANK_Z)
		return 0;

	assert(bank <= GPIO_BANK_K);
	return bank * GPIO_BANK_OFFSET;
}

unsigned int stm32_get_gpio_bank_clock(unsigned int bank)
{
	if (bank == GPIO_BANK_Z)
		return GPIOZ;

	assert(bank <= GPIO_BANK_K);
	return GPIOA + bank;
}

static int get_part_number(uint32_t *part_nb)
{
	uint32_t part_number = 0;
	uint32_t dev_id = 0;
	uint32_t otp = 0;
	size_t bit_len = 0;

	assert(part_nb);

	if (stm32mp1_dbgmcu_get_chip_dev_id(&dev_id))
		return -1;

	if (stm32_bsec_find_otp_in_nvmem_layout(PART_NUMBER_OTP,
						&otp, &bit_len))
		return -1;

	if (bit_len != 8)
		panic();

	if (stm32_bsec_read_otp(&part_number, otp))
		return -1;

	part_number = (part_number & PART_NUMBER_OTP_PART_MASK) >>
		      PART_NUMBER_OTP_PART_SHIFT;

	*part_nb = part_number | (dev_id << 16);

	return 0;
}

bool stm32mp_supports_cpu_opp(uint32_t opp_id)
{
	uint32_t part_number = 0;
	uint32_t id = 0;

	if (get_part_number(&part_number)) {
		DMSG("Cannot get part number\n");
		panic();
	}

	switch (opp_id) {
	case PLAT_OPP_ID1:
	case PLAT_OPP_ID2:
		id = opp_id;
		break;
	default:
		return false;
	}

	switch (part_number) {
	case STM32MP157F_PART_NB:
	case STM32MP157D_PART_NB:
	case STM32MP153F_PART_NB:
	case STM32MP153D_PART_NB:
	case STM32MP151F_PART_NB:
	case STM32MP151D_PART_NB:
		return true;
	default:
		return id == PLAT_OPP_ID1;
	}
}

enum etzpc_decprot_attributes stm32mp_etzpc_binding2decprot(uint32_t mode)
{
	switch (mode) {
	case DECPROT_S_RW:
		return ETZPC_DECPROT_S_RW;
	case DECPROT_NS_R_S_W:
		return ETZPC_DECPROT_NS_R_S_W;
	case DECPROT_MCU_ISOLATION:
		return ETZPC_DECPROT_MCU_ISOLATION;
	case DECPROT_NS_RW:
		return ETZPC_DECPROT_NS_RW;
	default:
		panic();
	}
}

unsigned int stm32mp_iwdg_irq2instance(size_t irq)
{
	int id = irq - STM32MP1_IRQ_IWDG1;

	assert(id >= IWDG1_INST && id <= IWDG2_INST);
	return (unsigned int)id;
}

size_t stm32mp_iwdg_instance2irq(unsigned int instance)
{
	return instance + STM32MP1_IRQ_IWDG1;
}

unsigned int stm32mp_iwdg_iomem2instance(vaddr_t pbase)
{
	switch (pbase) {
	case IWDG1_BASE:
		return IWDG1_INST;
	case IWDG2_BASE:
		return IWDG2_INST;
	default:
		panic();
	}
}

unsigned long stm32_get_iwdg_otp_config(vaddr_t pbase)
{
	unsigned int idx = 0;
	unsigned long iwdg_cfg = 0;
	uint32_t otp_id = 0;
	size_t bit_len = 0;
	uint32_t otp_value = 0;

	idx = stm32mp_iwdg_iomem2instance(pbase);

	if (stm32_bsec_find_otp_in_nvmem_layout(HW2_OTP, &otp_id, &bit_len))
		panic();

	if (bit_len != 32)
		panic();

	if (stm32_bsec_read_otp(&otp_value, otp_id))
		panic();

	if (otp_value & BIT(idx + HW2_OTP_IWDG_HW_ENABLE_SHIFT))
		iwdg_cfg |= IWDG_HW_ENABLED;

	if (otp_value & BIT(idx + HW2_OTP_IWDG_FZ_STOP_SHIFT))
		iwdg_cfg |= IWDG_DISABLE_ON_STOP;

	if (otp_value & BIT(idx + HW2_OTP_IWDG_FZ_STANDBY_SHIFT))
		iwdg_cfg |= IWDG_DISABLE_ON_STANDBY;

	return iwdg_cfg;
}

#ifdef CFG_STM32_RTC
/*
 * Return true if RTC needs to be read twice not once for a reliable value.
 *
 * This function determines the number of needed RTC calendar read operations
 * to get consistent values: RTC may need to be read twice depending on clock
 * frequencies.
 * If APB1 frequency is less than 7 times the RTC one, the software has to
 * read the calendar time and date register twice.
 */
bool stm32_rtc_get_read_twice(void)
{
	unsigned long apb1_freq = 0;
	unsigned long rtc_freq = 0;
	uint32_t apb1_div = 0;
	vaddr_t rcc_base = stm32_rcc_base();

	switch ((io_read32(rcc_base + RCC_BDCR) &
		 RCC_BDCR_RTCSRC_MASK) >> RCC_BDCR_RTCSRC_SHIFT) {
	case 1:
		rtc_freq = clk_get_rate(CK_LSE);
		break;
	case 2:
		rtc_freq = clk_get_rate(CK_LSI);
		break;
	case 3:
		rtc_freq = clk_get_rate(CK_HSE);
		rtc_freq /= (io_read32(rcc_base + RCC_RTCDIVR) &
			     RCC_DIVR_DIV_MASK) + 1U;
		break;
	default:
		panic();
	}

	apb1_div = io_read32(rcc_base + RCC_APB1DIVR) & RCC_APBXDIV_MASK;
	apb1_freq = clk_get_rate(CK_MCU) >> apb1_div;

	return apb1_freq < (rtc_freq * 7U);
}
#endif
