 // SPDX-License-Identifier: BSD-2-Clause
 /*
  * Copyright (C) 2020, STMicroelectronics - All Rights Reserved
  */

#include <crypto/crypto.h>
#include <drivers/clk.h>
#include <drivers/stm32_etzpc.h>
#include <drivers/stm32mp1_rcc.h>
#include <dt-bindings/clock/stm32mp1-clks.h>
#include <dt-bindings/reset/stm32mp1-resets.h>
#include <dt-bindings/soc/st,stm32-etzpc.h>
#include <initcall.h>
#include <kernel/pseudo_ta.h>
#include <kernel/user_ta.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>
#include <remoteproc_pta.h>
#include <rproc_pub_key.h>
#include <stm32_util.h>
#include <string.h>

#define PTA_NAME "remoteproc.pta"

#define STM32_M4_FW_ID		0

/* Firmware states */
enum rproc_load_state {
	REMOTEPROC_OFF = 0,
	REMOTEPROC_ON,
};

/*
 * struct rproc_ta_etzpc_rams - memory protection strategy table
 * @pa - Memory physical base address from current CPU space
 * @size - Memory region byte size
 * @etzpc_id - associated ETZPC identifier.
 * @attr - memory access permission according to @etzpc_decprot_attributes
 */
struct rproc_ta_etzpc_rams {
	paddr_t pa;
	size_t size;
	uint32_t etzpc_id;
	enum etzpc_decprot_attributes attr;
};

/*
 * struct rproc_ta_memory_region - Represent a remote processor memory mapping
 * @pa - Memory physical base address from current CPU space
 * @da - Memory physical base address from remote processor space
 * @size - Memory region byte size
 */
struct rproc_ta_memory_region {
	paddr_t pa;
	paddr_t da;
	size_t size;
};

static const struct rproc_ta_etzpc_rams rproc_ta_mp1_m4_rams[] = {
	/* MCU SRAM 1*/
	{
		.pa = MCUSRAM_BASE,
		.size = 0x20000,
		.etzpc_id = STM32MP1_ETZPC_SRAM1_ID,
		.attr = ETZPC_DECPROT_MCU_ISOLATION,
	},
	/* MCU SRAM 2*/
	{
		.pa = MCUSRAM_BASE + 0x20000,
		.size = 0x20000,
		.etzpc_id = STM32MP1_ETZPC_SRAM2_ID,
		.attr = ETZPC_DECPROT_MCU_ISOLATION,
	},

	/* MCU SRAM 3*/
	{
	/* Used as shared memory between the NS and the coprocessor */
		.pa = MCUSRAM_BASE + 0x40000,
		.size = 0x10000,
		.etzpc_id = STM32MP1_ETZPC_SRAM3_ID,
		.attr = ETZPC_DECPROT_NS_RW,
	},
	/* MCU SRAM 4*/
	/* Not used reserved by NS for MDMA */
	{
		.pa = MCUSRAM_BASE + 0x50000,
		.size = 0x10000,
		.etzpc_id = STM32MP1_ETZPC_SRAM4_ID,
		.attr = ETZPC_DECPROT_NS_RW,
	},

	/* MCU RETRAM */
	{
		.pa = RETRAM_BASE,
		.size = RETRAM_SIZE,
		.etzpc_id = STM32MP1_ETZPC_RETRAM_ID,
		.attr = ETZPC_DECPROT_MCU_ISOLATION,
	},
};

static const struct rproc_ta_memory_region rproc_ta_mp1_m4_mems[] = {
	/* MCU SRAM */
	{ .pa = MCUSRAM_BASE, .da = 0x10000000, .size = MCUSRAM_SIZE },
	/* Alias of the MCU SRAM */
	{ .pa = MCUSRAM_BASE, .da = 0x30000000, .size = MCUSRAM_SIZE },
	/* RETRAM */
	{ .pa = RETRAM_BASE, .da = 0x00000000, .size = RETRAM_SIZE },
};

static enum rproc_load_state rproc_ta_state;

static TEE_Result rproc_pta_capabilities(uint32_t pt,
					 TEE_Param params[TEE_NUM_PARAMS])
{
	const uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_VALUE_OUTPUT,
						TEE_PARAM_TYPE_VALUE_OUTPUT,
						TEE_PARAM_TYPE_NONE);

	if (pt != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Support only ELF format */
	params[1].value.a = PTA_REMOTEPROC_ELF_FMT;

	/*
	 * Due to stm32mp1 pager, secure memory is too expensive. Support hash
	 * protected image only, so that firmware image can be loaded from
	 * non-secure memory.
	 */
	params[2].value.a = PTA_REMOTEPROC_FW_WITH_HASH_TABLE;

	return TEE_SUCCESS;
}

static TEE_Result da_to_pa(paddr_t da, size_t size, paddr_t *pa)
{
	const struct rproc_ta_memory_region *mems = rproc_ta_mp1_m4_mems;
	size_t i = 0;

	DMSG("da addr: %#"PRIxPA" size: %zu", da, size);

	for (i = 0; i < ARRAY_SIZE(rproc_ta_mp1_m4_mems); i++) {
		if (da >= mems[i].da &&
		    (da + size) <= (mems[i].da + mems[i].size)) {
			*pa = da - mems[i].da + mems[i].pa;
			DMSG("da %#"PRIxPA" to pa %#"PRIxPA, da, *pa);

			return TEE_SUCCESS;
		}
	}

	return TEE_ERROR_ACCESS_DENIED;
}

static TEE_Result rproc_pta_load_segment(uint32_t pt,
					 TEE_Param params[TEE_NUM_PARAMS])
{
	const uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_MEMREF_INPUT,
						TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_MEMREF_INPUT);
	TEE_Result res = TEE_ERROR_GENERIC;
	paddr_t pa = 0;
	uint8_t *dst = 0;
	uint8_t *src = params[1].memref.buffer;
	size_t size = params[1].memref.size;
	uint8_t *hash = params[3].memref.buffer;
	paddr_t da = (paddr_t)reg_pair_to_64(params[2].value.b,
					     params[2].value.a);

	if (pt != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!hash || params[3].memref.size != TEE_SHA256_HASH_SIZE)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Only STM32_M4_FW_ID supported */
	if (params[0].value.a != STM32_M4_FW_ID) {
		EMSG("Unsupported firmware ID %#"PRIx32, params[0].value.a);
		return TEE_ERROR_NOT_SUPPORTED;
	}

	if (rproc_ta_state != REMOTEPROC_OFF)
		return TEE_ERROR_BAD_STATE;

	/* Get the physical address in A7 mapping */
	res = da_to_pa(da, size, &pa);
	if (res)
		return res;

	/* Get the associated va */
	dst = (void *)core_mmu_get_va(pa, MEM_AREA_IO_SEC);

	/* Copy the segment to the remote processor memory*/
	memcpy(dst, src, size);

	/* Verify that loaded segment is valid */
	res = hash_sha256_check(hash, dst, size);
	if (res)
		memset(dst, 0, size);

	return res;
}

static TEE_Result rproc_pta_set_memory(uint32_t pt,
				       TEE_Param params[TEE_NUM_PARAMS])
{
	const uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_VALUE_INPUT);
	TEE_Result res = TEE_ERROR_GENERIC;
	paddr_t pa = 0;
	vaddr_t dst = 0;
	paddr_t da = params[1].value.a;
	size_t size = params[2].value.a;
	char value = (char)params[3].value.a;

	if (pt != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Only STM32_M4_FW_ID supported */
	if (params[0].value.a != STM32_M4_FW_ID) {
		EMSG("Unsupported firmware ID %#"PRIx32, params[0].value.a);
		return TEE_ERROR_NOT_SUPPORTED;
	}

	if (rproc_ta_state != REMOTEPROC_OFF)
		return TEE_ERROR_BAD_STATE;

	/* Get the physical address in CPU mapping */
	res = da_to_pa(da, size, &pa);
	if (res)
		return res;

	dst = core_mmu_get_va(pa, MEM_AREA_IO_SEC);

	memset((void *)dst, value, size);

	return TEE_SUCCESS;
}

static TEE_Result rproc_pta_da_to_pa(uint32_t pt,
				     TEE_Param params[TEE_NUM_PARAMS])
{
	const uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_VALUE_OUTPUT);
	TEE_Result res = TEE_ERROR_GENERIC;
	paddr_t da = params[1].value.a;
	size_t size = params[2].value.a;
	paddr_t pa = 0;

	DMSG("Conversion for address %#"PRIxPA" size %zu", da, size);

	if (pt != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Only STM32_M4_FW_ID supported */
	if (params[0].value.a != STM32_M4_FW_ID) {
		EMSG("Unsupported firmware ID %#"PRIx32, params[0].value.a);
		return TEE_ERROR_NOT_SUPPORTED;
	}

	/* Target address is expected 32bit, ensure 32bit MSB are zero */
	if (params[1].value.b || params[2].value.b)
		return TEE_ERROR_BAD_PARAMETERS;

	res = da_to_pa(da, size, &pa);
	if (res)
		return res;

	reg_pair_from_64((uint64_t)pa, &params[3].value.b, &params[3].value.a);

	return TEE_SUCCESS;
}

static void rproc_pta_mem_protect(bool secure_access)
{
	unsigned int i = 0;
	const struct rproc_ta_etzpc_rams *ram = NULL;

	/*
	 * MCU RAM banks access permissions for MCU memories depending on
	 * rproc_ta_mp1_m4_rams[].
	 * If memory bank is declared as MCU isolated:
	 *     if secure_access then set to secure world read/write permission
	 *     else set to MCU isolated
	 * else apply memory permission as defined in rproc_ta_mp1_m4_rams[].
	 */
	for (i = 0; i < ARRAY_SIZE(rproc_ta_mp1_m4_rams); i++) {
		enum etzpc_decprot_attributes attr = ETZPC_DECPROT_MAX;

		ram = &rproc_ta_mp1_m4_rams[i];
		attr = ram->attr;

		if (secure_access && attr == ETZPC_DECPROT_MCU_ISOLATION)
			attr = ETZPC_DECPROT_S_RW;

		etzpc_configure_decprot(ram->etzpc_id, attr);
	}
}

static TEE_Result rproc_pta_start(uint32_t pt,
				  TEE_Param params[TEE_NUM_PARAMS])
{
	const uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_NONE,
						TEE_PARAM_TYPE_NONE,
						TEE_PARAM_TYPE_NONE);
	vaddr_t rcc_base = stm32_rcc_base();

	if (pt != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Only STM32_M4_FW_ID supported */
	if (params[0].value.a != STM32_M4_FW_ID) {
		EMSG("Unsupported firmware ID %#"PRIx32, params[0].value.a);
		return TEE_ERROR_NOT_SUPPORTED;
	}

	if (rproc_ta_state != REMOTEPROC_OFF)
		return TEE_ERROR_BAD_STATE;

	clk_enable(CK_MCU);

	/* Configure the Cortex-M4 RAMs as expected to run the firmware */
	rproc_pta_mem_protect(false);

	/*
	 * The firmware is started by deasserting the hold boot and
	 * asserting back to avoid auto restart on a crash.
	 * No need to release the MCU reset as it is automatically released by
	 * the hardware.
	 */
	io_setbits32(rcc_base + RCC_MP_GCR, RCC_MP_GCR_BOOT_MCU);
	io_clrbits32(rcc_base + RCC_MP_GCR, RCC_MP_GCR_BOOT_MCU);

	rproc_ta_state = REMOTEPROC_ON;

	return TEE_SUCCESS;
}

static TEE_Result rproc_pta_stop(uint32_t pt,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	const uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_NONE,
						TEE_PARAM_TYPE_NONE,
						TEE_PARAM_TYPE_NONE);
	const struct rproc_ta_etzpc_rams *ram = NULL;
	vaddr_t rcc_base = stm32_rcc_base();
	unsigned int i = 0;

	if (pt != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Only STM32_M4_FW_ID supported */
	if (params[0].value.a != STM32_M4_FW_ID) {
		EMSG("Unsupported firmware ID %#"PRIx32, params[0].value.a);
		return TEE_ERROR_NOT_SUPPORTED;
	}

	if (rproc_ta_state != REMOTEPROC_ON)
		return TEE_ERROR_BAD_STATE;

	/* The firmware is stopped (reset with holdboot is active) */
	io_clrbits32(rcc_base + RCC_MP_GCR, RCC_MP_GCR_BOOT_MCU);

	stm32_reset_set(MCU_R);

	clk_disable(CK_MCU);

	/*
	 * Cortex-M4 memories are cleaned and access rights restored for the
	 * secure context.
	 */
	rproc_pta_mem_protect(true);
	for (i = 0; i < ARRAY_SIZE(rproc_ta_mp1_m4_rams); i++) {
		ram = &rproc_ta_mp1_m4_rams[i];
		if (ram->attr == ETZPC_DECPROT_MCU_ISOLATION) {
			memset((void *)core_mmu_get_va(ram->pa,
						       MEM_AREA_IO_SEC),
			       0, ram->size);
		}
	}
	rproc_ta_state = REMOTEPROC_OFF;

	return TEE_SUCCESS;
}

static TEE_Result rproc_pta_verify_rsa_signature(TEE_Param *hash,
						 TEE_Param *sig, uint32_t algo)
{
	struct rsa_public_key key = { };
	TEE_Result res = TEE_ERROR_GENERIC;
	uint32_t e = TEE_U32_TO_BIG_ENDIAN(rproc_pub_key_exponent);
	size_t hash_size = (size_t)hash->memref.size;
	size_t sig_size = (size_t)sig->memref.size;

	res = crypto_acipher_alloc_rsa_public_key(&key, sig_size);
	if (res)
		return TEE_ERROR_SECURITY;

	res = crypto_bignum_bin2bn((uint8_t *)&e, sizeof(e), key.e);
	if (res)
		goto out;

	res = crypto_bignum_bin2bn(rproc_pub_key_modulus,
				   rproc_pub_key_modulus_size, key.n);
	if (res)
		goto out;

	res = crypto_acipher_rsassa_verify(algo, &key, hash_size,
					   hash->memref.buffer, hash_size,
					   sig->memref.buffer, sig_size);

out:
	crypto_acipher_free_rsa_public_key(&key);

	return res;
}

static TEE_Result rproc_pta_verify_digest(uint32_t pt,
					  TEE_Param params[TEE_NUM_PARAMS])
{
	struct rproc_pta_key_info *keyinfo = NULL;
	const uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
						TEE_PARAM_TYPE_MEMREF_INPUT,
						TEE_PARAM_TYPE_MEMREF_INPUT,
						TEE_PARAM_TYPE_MEMREF_INPUT);

	if (pt != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Only STM32_M4_FW_ID supported */
	if (params[0].value.a != STM32_M4_FW_ID) {
		EMSG("Unsupported firmware ID %#"PRIx32, params[0].value.a);
		return TEE_ERROR_NOT_SUPPORTED;
	}

	if (rproc_ta_state != REMOTEPROC_OFF)
		return TEE_ERROR_BAD_STATE;

	keyinfo = params[1].memref.buffer;

	if (!keyinfo ||
	    RPROC_PTA_GET_KEYINFO_SIZE(keyinfo) != params[1].memref.size)
		return TEE_ERROR_BAD_PARAMETERS;

	if (keyinfo->algo != TEE_ALG_RSASSA_PKCS1_V1_5_SHA256)
		return TEE_ERROR_NOT_SUPPORTED;

	return rproc_pta_verify_rsa_signature(&params[2], &params[3],
					      keyinfo->algo);
}

static TEE_Result rproc_pta_invoke_command(void *pSessionContext __unused,
					   uint32_t cmd_id,
					   uint32_t param_types,
					   TEE_Param params[TEE_NUM_PARAMS])
{
	switch (cmd_id) {
	case PTA_REMOTEPROC_HW_CAPABILITIES:
		return rproc_pta_capabilities(param_types, params);
	case PTA_REMOTEPROC_LOAD_SEGMENT_SHA256:
		return rproc_pta_load_segment(param_types, params);
	case PTA_REMOTEPROC_SET_MEMORY:
		return rproc_pta_set_memory(param_types, params);
	case PTA_REMOTEPROC_FIRMWARE_START:
		return rproc_pta_start(param_types, params);
	case PTA_REMOTEPROC_FIRMWARE_STOP:
		return rproc_pta_stop(param_types, params);
	case PTA_REMOTEPROC_FIRMWARE_DA_TO_PA:
		return rproc_pta_da_to_pa(param_types, params);
	case PTA_REMOTEPROC_VERIFY_DIGEST:
		return rproc_pta_verify_digest(param_types, params);
	default:
		break;
	}

	return TEE_ERROR_NOT_IMPLEMENTED;
}

/*
 * Trusted Application entry points
 */
static TEE_Result
	rproc_pta_open_session(uint32_t param_types __unused,
			       TEE_Param params[TEE_NUM_PARAMS] __unused,
			       void **sess_ctx __unused)
{
	struct tee_ta_session *s = tee_ta_get_calling_session();

	/* Check that we're called from a user TA */
	if (!s)
		return TEE_ERROR_ACCESS_DENIED;

	if (!is_user_ta_ctx(s->ctx))
		return TEE_ERROR_ACCESS_DENIED;

	return TEE_SUCCESS;
}

static TEE_Result rproc_pta_init(void)
{
	vaddr_t rcc_base = stm32_rcc_base();

	/* Configure the Cortex-M4 rams access right for secure context only */
	rproc_pta_mem_protect(true);

	/* Initialise the context */
	rproc_ta_state = REMOTEPROC_OFF;

	/* Ensure that the MCU is HOLD */
	io_clrbits32(rcc_base + RCC_MP_GCR, RCC_MP_GCR_BOOT_MCU);
	stm32_reset_set(MCU_R);

	return TEE_SUCCESS;
}
service_init_late(rproc_pta_init);

pseudo_ta_register(.uuid = PTA_REMOTEPROC_UUID, .name = PTA_NAME,
		   .flags = PTA_DEFAULT_FLAGS,
		   .invoke_command_entry_point = rproc_pta_invoke_command,
		   .open_session_entry_point = rproc_pta_open_session);
