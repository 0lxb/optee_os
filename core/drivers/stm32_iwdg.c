// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2017-2020, STMicroelectronics - All Rights Reserved
 */

#include <assert.h>
#include <drivers/clk.h>
#include <drivers/stm32_iwdg.h>
#include <io.h>
#include <keep.h>
#include <kernel/boot.h>
#include <kernel/delay.h>
#include <kernel/dt.h>
#include <kernel/interrupt.h>
#include <kernel/misc.h>
#include <kernel/panic.h>
#include <kernel/pm.h>
#include <libfdt.h>
#include <mm/core_memprot.h>
#include <stm32_util.h>
#include <stm32mp_pm.h>
#include <string.h>
#include <trace.h>

/* IWDG Compatibility */
#define IWDG_COMPAT		"st,stm32mp1-iwdg"
#define IWDG_TIMEOUT_US		100000U

/* IWDG registers offsets */
#define IWDG_KR_OFFSET		0x00U
#define IWDG_PR_OFFSET		0x04U
#define IWDG_RLR_OFFSET		0x08U
#define IWDG_SR_OFFSET		0x0CU
#define IWDG_EWCR_OFFSET	0x14U

/* Registers values */
#define IWDG_KR_ACCESS_KEY	0x5555
#define IWDG_KR_RELOAD_KEY	0xAAAA
#define IWDG_KR_START_KEY	0xCCCC

#define IWDG_PR_DIV_4		0x00
#define IWDG_PR_DIV_256		0x06

#define IWDG_RLR_MAX_VAL	0xFFF

#define IWDG_SR_EWU		BIT(3)

#define IWDG_EWCR_EWIE		BIT(15)
#define IWDG_EWCR_EWIC		BIT(14)
#define IWDG_EWCR_EWIT_MASK	GENMASK_32(11, 0)

struct stm32_iwdg_instance {
	struct io_pa_va base;
	unsigned long clock;
	uint8_t instance;
	uint8_t flags;
};

static struct stm32_iwdg_instance *stm32_iwdg;
static size_t stm32_iwdg_count;

static vaddr_t get_base(struct stm32_iwdg_instance *iwdg)
{
	return io_pa_or_va(&iwdg->base);
}

static int stm32_iwdg_get_dt_node(void *fdt, struct dt_node_info *info,
				  int offset)
{
	int node = fdt_node_offset_by_compatible(fdt, offset, IWDG_COMPAT);

	if (node < 0) {
		if (offset == -1)
			DMSG("No IDWG found");

		return -FDT_ERR_NOTFOUND;
	}

	_fdt_fill_device_info(fdt, info, node);

	return node;
}

static struct stm32_iwdg_instance *get_iwdg(unsigned int instance)
{
	size_t i = 0;

	assert(instance <= UINT8_MAX);
	for (i = 0; i < stm32_iwdg_count; i++)
		if (stm32_iwdg[i].instance == instance)
			return &stm32_iwdg[i];

	return NULL;
}

static enum itr_return stm32_iwdg_it_handler(struct itr_handler *handler)
{
	unsigned int __maybe_unused cpu = get_core_pos();
	unsigned int instance = stm32mp_iwdg_irq2instance(handler->it);
	struct stm32_iwdg_instance *iwdg = get_iwdg(instance);
	vaddr_t iwdg_base = get_base(iwdg);

	DMSG("CPU %u IT Watchdog %d\n", cpu, instance + 1);

	stm32_iwdg_refresh(instance);

	clk_enable(iwdg->clock);

	io_setbits32(iwdg_base + IWDG_EWCR_OFFSET, IWDG_EWCR_EWIC);

	clk_disable(iwdg->clock);

#ifdef CFG_PM
	/*
	 * Ack interrupt as we do not return from next call.
	 * And interrupt is no more considered as pending here.
	 */
	stm32mp_gic_set_end_of_interrupt(handler->it);

	stm32_cores_reset();
#else
	panic("Watchdog");
#endif

	return ITRR_HANDLED;
}
DECLARE_KEEP_PAGER(stm32_iwdg_it_handler);

static int stm32_iwdg_get_secure_timeout(void *fdt, int node)
{
	const fdt32_t *cuint = NULL;

	cuint = fdt_getprop(fdt, node, "secure-timeout-sec", NULL);
	if (!cuint)
		return -1;

	return (int)fdt32_to_cpu(*cuint);
}

static int fdt_get_clock_id_by_name(void *fdt, int node, const char *name)
{
	const fdt32_t *cuint = NULL;
	int index = 0;
	int len = 0;

	index = fdt_stringlist_search(fdt, node, "clock-names", name);
	if (index < 0)
		return index;

	cuint = fdt_getprop(fdt, node, "clocks", &len);
	if (!cuint)
		return -FDT_ERR_NOTFOUND;

	if ((index * (int)sizeof(uint32_t)) > len)
		return -FDT_ERR_BADVALUE;

	cuint += (index << 1) + 1;
	return (int)fdt32_to_cpu(*cuint);
}

static TEE_Result stm32_iwdg_conf_etimeout(void *fdt, int node,
					   struct stm32_iwdg_instance *iwdg)
{
	int id_lsi = 0;
	int dt_secure_timeout = stm32_iwdg_get_secure_timeout(fdt, node);
	uint32_t reload = 0;
	uint32_t status = 0;
	uint64_t timeout_ref = 0;
	unsigned long long reload_ll = 0;
	vaddr_t iwdg_base = get_base(iwdg);
	struct itr_handler *itr = NULL;

	if (dt_secure_timeout < 0)
		return TEE_SUCCESS;

	if (dt_secure_timeout == 0)
		return TEE_ERROR_GENERIC;

	id_lsi = fdt_get_clock_id_by_name(fdt, node, "lsi");
	if (id_lsi < 0)
		return TEE_ERROR_GENERIC;

	/* Prescaler fix to 256 */
	reload_ll = (unsigned long long)dt_secure_timeout *
		    clk_get_rate(id_lsi);
	reload = ((uint32_t)(reload_ll >> 8) - 1) & IWDG_EWCR_EWIT_MASK;

	clk_enable(iwdg->clock);

	io_write32(iwdg_base + IWDG_KR_OFFSET, IWDG_KR_START_KEY);
	io_write32(iwdg_base + IWDG_KR_OFFSET, IWDG_KR_ACCESS_KEY);
	io_write32(iwdg_base + IWDG_PR_OFFSET, IWDG_PR_DIV_256);
	io_write32(iwdg_base + IWDG_EWCR_OFFSET, reload | IWDG_EWCR_EWIE);

	timeout_ref = timeout_init_us(IWDG_TIMEOUT_US);
	while (!timeout_elapsed(timeout_ref))
		if (!(io_read32(iwdg_base + IWDG_SR_OFFSET) & IWDG_SR_EWU))
			break;

	status = io_read32(iwdg_base + IWDG_SR_OFFSET) & IWDG_SR_EWU;
	clk_disable(iwdg->clock);
	if (status)
		return TEE_ERROR_GENERIC;

	itr = calloc(1, sizeof(*itr));
	if (!itr)
		panic("out of memory");

	itr->it = stm32mp_iwdg_instance2irq(iwdg->instance);
	itr->handler = stm32_iwdg_it_handler;
	itr_add(itr);
	itr_enable(itr->it);

	return TEE_SUCCESS;
}

void stm32_iwdg_refresh(unsigned int instance)
{
	struct stm32_iwdg_instance *iwdg = get_iwdg(instance);

	if (!iwdg)
		return;

	clk_enable(iwdg->clock);

	io_write32(get_base(iwdg) + IWDG_KR_OFFSET, IWDG_KR_RELOAD_KEY);

	clk_disable(iwdg->clock);
}

static TEE_Result iwdg_init(void)
{
	int node = -1;
	TEE_Result res = TEE_ERROR_GENERIC;
	struct dt_node_info dt_info = { };
	void *fdt = NULL;
	size_t count = 0;

	fdt = get_embedded_dt();
	if (!fdt)
		panic();

	assert(!stm32_iwdg && !stm32_iwdg_count);

	for (node = stm32_iwdg_get_dt_node(fdt, &dt_info, node);
	     node != -FDT_ERR_NOTFOUND;
	     node = stm32_iwdg_get_dt_node(fdt, &dt_info, node)) {
		struct stm32_iwdg_instance iwdg = { };
		enum teecore_memtypes memtype = MEM_AREA_MAXTYPE;
		uint32_t hw_init = 0;

		iwdg.base.pa = dt_info.reg;
		iwdg.clock = (unsigned long)dt_info.clock;
		iwdg.instance = stm32mp_iwdg_iomem2instance(iwdg.base.pa);

		memtype = ((dt_info.status & DT_STATUS_OK_NSEC) != 0) ?
			  MEM_AREA_IO_NSEC : MEM_AREA_IO_SEC;
		iwdg.base.va = (vaddr_t)phys_to_virt(iwdg.base.pa, memtype);

		/* DT can specify low power cases */
		if (!fdt_getprop(fdt, node, "stm32,enable-on-stop", NULL))
			iwdg.flags |= IWDG_DISABLE_ON_STOP;

		if (!fdt_getprop(fdt, node, "stm32,enable-on-standby", NULL))
			iwdg.flags |= IWDG_DISABLE_ON_STANDBY;

		hw_init = stm32_get_iwdg_otp_config(iwdg.base.pa);

		if (hw_init & IWDG_HW_ENABLED) {
			if (dt_info.status == DT_STATUS_DISABLED)
				panic("IWDG HW enabled");

			iwdg.flags |= IWDG_HW_ENABLED;
		}

		if (hw_init & IWDG_DISABLE_ON_STOP)
			iwdg.flags |= IWDG_DISABLE_ON_STOP;

		if (hw_init & IWDG_DISABLE_ON_STANDBY)
			iwdg.flags |= IWDG_DISABLE_ON_STANDBY;

		if (dt_info.status == DT_STATUS_DISABLED)
			continue;

		DMSG("IWDG%u found, %ssecure", iwdg.instance + 1,
		     (dt_info.status & DT_STATUS_OK_NSEC) ? "non-" : "");

		if (dt_info.status & DT_STATUS_OK_NSEC)
			stm32mp_register_non_secure_periph_iomem(iwdg.base.pa);
		else
			stm32mp_register_secure_periph_iomem(iwdg.base.pa);

		res = stm32_iwdg_conf_etimeout(fdt, node, &iwdg);
		if (res) {
			EMSG("IWDG%x early timeout config failed (%d)\n",
			     iwdg.instance + 1, res);
			panic();
		}

		stm32_iwdg = realloc(stm32_iwdg, (count + 1) * sizeof(iwdg));
		if (!stm32_iwdg)
			panic("out of memory");

		memcpy(&stm32_iwdg[count], &iwdg, sizeof(iwdg));
		count++;
	}

	stm32_iwdg_count = count;

	DMSG("%u IWDG instance%s found", count, count > 1 ? "s" : "");

	return TEE_SUCCESS;
}
driver_init(iwdg_init);
