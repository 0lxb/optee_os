# 1GB and 512MB DDR targets do not locate secure DDR at the same place.
flavor_dts_file-157A_DK1 = stm32mp157a-dk1.dts
flavor_dts_file-157A_ED1 = stm32mp157a-ed1.dts
flavor_dts_file-157A_EV1 = stm32mp157a-ev1.dts
flavor_dts_file-157C_DK2 = stm32mp157c-dk2.dts
flavor_dts_file-157C_ED1 = stm32mp157c-ed1.dts
flavor_dts_file-157C_EV1 = stm32mp157c-ev1.dts
flavor_dts_file-157D_DK1 = stm32mp157d-dk1.dts
flavor_dts_file-157D_ED1 = stm32mp157d-ed1.dts
flavor_dts_file-157D_EV1 = stm32mp157d-ev1.dts
flavor_dts_file-157F_DK2 = stm32mp157f-dk2.dts
flavor_dts_file-157F_ED1 = stm32mp157f-ed1.dts
flavor_dts_file-157F_EV1 = stm32mp157f-ev1.dts

flavorlist-cryp-512M = $(flavor_dts_file-157C_DK2) \
		       $(flavor_dts_file-157F_DK2)

flavorlist-no_cryp-512M = $(flavor_dts_file-157A_DK1) \
			  $(flavor_dts_file-157D_DK1)

flavorlist-cryp-1G = $(flavor_dts_file-157C_ED1) \
		     $(flavor_dts_file-157C_EV1) \
		     $(flavor_dts_file-157F_ED1) \
		     $(flavor_dts_file-157F_EV1)

flavorlist-no_cryp-1G = $(flavor_dts_file-157A_ED1) \
			$(flavor_dts_file-157A_EV1) \
			$(flavor_dts_file-157D_ED1) \
			$(flavor_dts_file-157D_EV1)

flavorlist-no_cryp = $(flavorlist-no_cryp-512M) \
		     $(flavorlist-no_cryp-1G)

flavorlist-512M = $(flavorlist-cryp-512M) \
		  $(flavorlist-no_cryp-512M)

flavorlist-1G = $(flavorlist-cryp-1G) \
		$(flavorlist-no_cryp-1G)

ifneq ($(PLATFORM_FLAVOR),)
ifeq ($(flavor_dts_file-$(PLATFORM_FLAVOR)),)
$(error Invalid platform flavor $(PLATFORM_FLAVOR))
endif
CFG_EMBED_DTB_SOURCE_FILE ?= $(flavor_dts_file-$(PLATFORM_FLAVOR))
endif

ifneq ($(filter $(CFG_EMBED_DTB_SOURCE_FILE),$(flavorlist-no_cryp)),)
$(call force,CFG_STM32_CRYP,n)
endif

include core/arch/arm/cpu/cortex-a7.mk

$(call force,CFG_CLK_DRIVER,y)
$(call force,CFG_ARM_GIC_PM,y)
$(call force,CFG_BOOT_SECONDARY_REQUEST,y)
$(call force,CFG_GIC,y)
$(call force,CFG_INIT_CNTVOFF,y)
$(call force,CFG_PM,y)
$(call force,CFG_PM_ARM32,y)
$(call force,CFG_PM_STUBS,y)
$(call force,CFG_PSCI_ARM32,y)
$(call force,CFG_SCMI_MSG_DRIVERS,y)
$(call force,CFG_SCMI_MSG_CLOCK,y)
$(call force,CFG_SCMI_MSG_RESET_DOMAIN,y)
$(call force,CFG_SCMI_MSG_SMT,y)
$(call force,CFG_SCMI_MSG_SMT_FASTCALL_ENTRY,y)
$(call force,CFG_SCMI_MSG_VOLTAGE_DOMAIN,y)
$(call force,CFG_SECONDARY_INIT_CNTFRQ,y)
$(call force,CFG_SECURE_TIME_SOURCE_CNTPCT,y)
$(call force,CFG_SM_PLATFORM_HANDLER,y)
$(call force,CFG_WITH_SOFTWARE_PRNG,y)

ifneq ($(filter $(CFG_EMBED_DTB_SOURCE_FILE),$(flavorlist-512M)),)
CFG_TZDRAM_START ?= 0xde000000
CFG_SHMEM_START  ?= 0xdfe00000
CFG_DRAM_SIZE    ?= 0x20000000
endif

CFG_DTB_MAX_SIZE ?= 0x20000

CFG_TZSRAM_START ?= 0x2ffc0000
CFG_TZSRAM_SIZE  ?= 0x0003f000
CFG_STM32MP1_SCMI_SHM_BASE ?= 0x2ffff000
CFG_STM32MP1_SCMI_SHM_SIZE ?= 0x00001000
CFG_TZDRAM_START ?= 0xfe000000
CFG_TZDRAM_SIZE  ?= 0x01e00000
CFG_SHMEM_START  ?= 0xffe00000
CFG_SHMEM_SIZE   ?= 0x00200000
CFG_DRAM_SIZE    ?= 0x40000000

CFG_TEE_CORE_NB_CORE ?= 2
CFG_WITH_PAGER ?= y
CFG_WITH_LPAE ?= y
CFG_MMAP_REGIONS ?= 30
CFG_CORE_HEAP_SIZE ?= 49152
CFG_DTB_MAX_SIZE = 0x20000

# Disable early TA compression to limit HEAP size
CFG_EARLY_TA_COMPRESS ?= n

# Embed public part of this key in OP-TEE OS
CFG_RPROC_SIGN_KEY ?= keys/default_rproc.pem

ifeq ($(CFG_EMBED_DTB_SOURCE_FILE),)
# Some drivers mandate DT support
$(call force,CFG_STM32_CLKCALIB,n)
$(call force,CFG_STM32_I2C,n)
$(call force,CFG_STM32_IWDG,n)
$(call force,CFG_STM32_TIM,n)
$(call force,CFG_STPMIC1,n)
endif

CFG_RPROC_PTA ?= n
CFG_STM32_BSEC ?= y
CFG_STM32_CLKCALIB ?= y
CFG_STM32_CRYP ?= y
CFG_STM32_ETZPC ?= y
CFG_STM32_GPIO ?= y
CFG_STM32_I2C ?= y
CFG_STM32_IWDG ?= y
CFG_STM32_RNG ?= y
CFG_STM32_RTC ?= y
CFG_STM32_TIM ?= y
CFG_STM32_UART ?= y
CFG_STM32MP15_CLK ?= y
CFG_STPMIC1 ?= y
CFG_TZC400 ?= y

ifeq ($(CFG_STM32_CLKCALIB),y)
$(call force,CFG_STM32_TIM,y)
endif

ifeq ($(CFG_STPMIC1),y)
$(call force,CFG_STM32_I2C,y)
$(call force,CFG_STM32_GPIO,y)
endif

# Platform specific configuration
CFG_STM32MP_PANIC_ON_TZC_PERM_VIOLATION ?= y

# SiP/OEM service for non-secure world
CFG_STM32_BSEC_SIP ?= y
CFG_STM32_CLKCALIB_SIP ?= y
CFG_STM32_LOWPOWER_SIP ?= $(CFG_PM)
CFG_STM32_PWR_SIP ?= y
CFG_STM32_RCC_SIP ?= y

# Default use stm32mp1 PM mailbox context version 2
# Use CFG_STM32MP15_PM_CONTEX_VERSION=1 to force version 0 when dealing with
# a TF-A firmware that supports version 1 of the context mailbox.
CFG_STM32MP15_PM_CONTEX_VERSION ?= 2

# Default enable some test facitilites
CFG_TEE_CORE_EMBED_INTERNAL_TESTS ?= y
CFG_WITH_STATS ?= y
CFG_WERROR ?= y

# Default disable some support for pager memory size constraint
CFG_TEE_CORE_LOG_LEVEL ?= 2
CFG_TEE_CORE_DEBUG ?= n
CFG_UNWIND ?= n
CFG_LOCKDEP ?= n
CFG_CORE_ASLR ?= n

# Non-secure UART and GPIO/pinctrl for the output console
CFG_WITH_NSEC_GPIOS ?= y
CFG_WITH_NSEC_UARTS ?= y
# UART instance used for early console (0 disables early console)
CFG_STM32_EARLY_CONSOLE_UART ?= 4

# Generate the STM32 files
CFG_STM32MP15x_STM32IMAGE ?= n
