// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2017-2018, STMicroelectronics - All Rights Reserved
 */

#include <assert.h>
#include <config.h>
#include <dt-bindings/power/stm32mp1-power.h>
#include <kernel/boot.h>
#include <kernel/panic.h>
#include <libfdt.h>
#include <stm32mp_pm.h>
#include <trace.h>
#include <util.h>

#include "context.h"
#include "power.h"

#define DT_PWR_COMPAT			"st,stm32mp1,pwr-reg"
#define SYSTEM_SUSPEND_SUPPORTED_MODES	"system_suspend_supported_soc_modes"
#define SYSTEM_OFF_MODE			"system_off_soc_mode"

static uint32_t deepest_suspend_mode;
static uint32_t system_off_mode;
static uint8_t stm32mp1_supported_soc_modes[STM32_PM_MAX_SOC_MODE];

/* Boot with all domains ON */
static bool stm32mp1_pm_dom[STM32MP1_PD_MAX_PM_DOMAIN] = {
	[STM32MP1_PD_VSW] = false,
	[STM32MP1_PD_CORE_RET] = false,
	[STM32MP1_PD_CORE] = false
};

bool need_to_backup_cpu_context(unsigned int soc_mode)
{
	switch (soc_mode) {
	case STM32_PM_CSTOP_ALLOW_STANDBY_DDR_SR:
		return true;
	case STM32_PM_CSLEEP_RUN:
	case STM32_PM_CSTOP_ALLOW_STOP:
	case STM32_PM_CSTOP_ALLOW_LP_STOP:
	case STM32_PM_CSTOP_ALLOW_LPLV_STOP:
	case STM32_PM_CSTOP_ALLOW_STANDBY_DDR_OFF:
	case STM32_PM_SHUTDOWN:
		return false;
	default:
		EMSG("Invalid mode 0x%x", soc_mode);
		panic();
	}
}

bool need_to_backup_stop_context(unsigned int soc_mode)
{
	switch (soc_mode) {
	case STM32_PM_CSTOP_ALLOW_STOP:
	case STM32_PM_CSTOP_ALLOW_LP_STOP:
	case STM32_PM_CSTOP_ALLOW_LPLV_STOP:
		return true;
	default:
		return false;
	}
}

static bool get_pm_domain_state(uint8_t mode)
{
	bool res = true;
	enum stm32mp1_pm_domain id = STM32MP1_PD_MAX_PM_DOMAIN;

	while (res && (id > mode)) {
		id--;
		res &= stm32mp1_pm_dom[id];
	}

	return res;
}

int stm32mp1_set_pm_domain_state(enum stm32mp1_pm_domain domain, bool status)
{
	if (domain >= STM32MP1_PD_MAX_PM_DOMAIN)
		return -1;

	stm32mp1_pm_dom[domain] = status;

	return 0;
}

#ifdef CFG_DT
static int fdt_read_uint32_array(void *fdt, int node, const char *prop_name,
				 uint32_t *array, uint32_t count)
{
	const fdt32_t *cuint = NULL;
	int len = 0;
	uint32_t i = 0;

	cuint = fdt_getprop(fdt, node, prop_name, &len);
	if (!cuint)
		return -FDT_ERR_NOTFOUND;

	if ((uint32_t)len != (count * sizeof(uint32_t)))
		return -FDT_ERR_BADLAYOUT;

	for (i = 0; i < ((uint32_t)len / sizeof(uint32_t)); i++) {
		*array = fdt32_to_cpu(*cuint);
		array++;
		cuint++;
	}

	return 0;
}

static void save_supported_mode(void *fdt, int pwr_node)
{
	int len = 0;
	uint32_t count = 0;
	unsigned int i = 0;
	uint32_t supported[ARRAY_SIZE(stm32mp1_supported_soc_modes)] = { };
	const void *prop = 0;

	prop = fdt_getprop(fdt, pwr_node, SYSTEM_SUSPEND_SUPPORTED_MODES, &len);
	if (!prop)
		panic();

	count = (uint32_t)len / sizeof(uint32_t);
	if (count > STM32_PM_MAX_SOC_MODE)
		panic();

	if (fdt_read_uint32_array(fdt, pwr_node, SYSTEM_SUSPEND_SUPPORTED_MODES,
				  &supported[0], count) < 0)
		panic("PWR DT");

	for (i = 0; i < count; i++) {
		if (supported[i] >= STM32_PM_MAX_SOC_MODE)
			panic("Invalid mode");

		stm32mp1_supported_soc_modes[supported[i]] = true;
	}
}
#endif

static bool is_supported_mode(uint32_t soc_mode)
{
	assert(soc_mode < ARRAY_SIZE(stm32mp1_supported_soc_modes));
	return stm32mp1_supported_soc_modes[soc_mode] == 1;
}

uint32_t stm32mp1_get_lp_soc_mode(uint32_t psci_mode)
{
	uint32_t mode = 0;

	if (psci_mode == PSCI_MODE_SYSTEM_OFF)
		return system_off_mode;

	mode = deepest_suspend_mode;

	if ((mode == STM32_PM_CSTOP_ALLOW_STANDBY_DDR_SR) &&
	    (!get_pm_domain_state(STM32MP1_PD_CORE_RET) ||
	     !is_supported_mode(mode)))
		mode = STM32_PM_CSTOP_ALLOW_LPLV_STOP;

	if ((mode == STM32_PM_CSTOP_ALLOW_LPLV_STOP) &&
	    (!get_pm_domain_state(STM32MP1_PD_CORE) ||
	     !is_supported_mode(mode)))
		mode = STM32_PM_CSTOP_ALLOW_LP_STOP;

	if ((mode == STM32_PM_CSTOP_ALLOW_LP_STOP) &&
	    !is_supported_mode(mode))
		mode = STM32_PM_CSTOP_ALLOW_STOP;

	if ((mode == STM32_PM_CSTOP_ALLOW_STOP) &&
	    !is_supported_mode(mode))
		mode = STM32_PM_CSLEEP_RUN;

	return mode;
}

int stm32mp1_set_lp_deepest_soc_mode(uint32_t psci_mode, uint32_t soc_mode)
{
	if (soc_mode >= STM32_PM_MAX_SOC_MODE)
		return -1;

	if (psci_mode == PSCI_MODE_SYSTEM_SUSPEND) {
		deepest_suspend_mode = soc_mode;

		if (!IS_ENABLED(CFG_STM32_CRYP) &&
		    deepest_suspend_mode == STM32_PM_CSTOP_ALLOW_STANDBY_DDR_SR)
			deepest_suspend_mode = STM32_PM_CSTOP_ALLOW_LPLV_STOP;
	}

	if (psci_mode == PSCI_MODE_SYSTEM_OFF)
		system_off_mode = soc_mode;

	return 0;
}

#ifdef CFG_DT
static int dt_get_pwr_node(void *fdt)
{
	return fdt_node_offset_by_compatible(fdt, -1, DT_PWR_COMPAT);
}

static TEE_Result stm32mp1_init_lp_states(void)
{
	void *fdt = NULL;
	int pwr_node = -1;
	const fdt32_t *cuint = NULL;

	fdt = get_embedded_dt();
	if (fdt)
		pwr_node = dt_get_pwr_node(fdt);

	if (pwr_node >= 0)
		cuint = fdt_getprop(fdt, pwr_node, SYSTEM_OFF_MODE, NULL);

	if (!fdt || (pwr_node < 0) || !cuint) {
		IMSG("No power configuration found in DT");
		return TEE_SUCCESS;
	}

	system_off_mode = fdt32_to_cpu(*cuint);

	/* Initialize suspend support to the deepest possible mode */
	if (IS_ENABLED(CFG_STM32_CRYP))
		deepest_suspend_mode = STM32_PM_CSTOP_ALLOW_STANDBY_DDR_SR;
	else
		deepest_suspend_mode = STM32_PM_CSTOP_ALLOW_LPLV_STOP;

	save_supported_mode(fdt, pwr_node);

	DMSG("Power configuration: shutdown to %u, suspend to %u",
	     stm32mp1_get_lp_soc_mode(PSCI_MODE_SYSTEM_OFF),
	     stm32mp1_get_lp_soc_mode(PSCI_MODE_SYSTEM_SUSPEND));

	return TEE_SUCCESS;
}
#else
static TEE_Result stm32mp1_init_lp_states(void)
{
	if (IS_ENABLED(CFG_STM32_CRYP))
		deepest_suspend_mode = STM32_PM_CSTOP_ALLOW_STANDBY_DDR_SR;
	else
		deepest_suspend_mode = STM32_PM_CSTOP_ALLOW_LPLV_STOP;

	system_off_mode = STM32_PM_SHUTDOWN;

	DMSG("Power configuration: shutdown to %u, suspend to %u",
	     stm32mp1_get_lp_soc_mode(PSCI_MODE_SYSTEM_OFF),
	     stm32mp1_get_lp_soc_mode(PSCI_MODE_SYSTEM_SUSPEND));

	return TEE_SUCCESS;
}
#endif
service_init(stm32mp1_init_lp_states);
