/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2020, STMicroelectronics - All Rights Reserved
 */

#ifndef CLK_H
#define CLK_H

#include <stdbool.h>
#include <tee_api_types.h>

#define CLK_UNKNOWN_ID	ULONG_MAX

/*
 * Minimal generic clock framework where platform is expected implement a
 * single clock provider and each individual clock identified with a unique
 * unsigned long identifier.
 */
struct clk_ops {
	TEE_Result (*enable)(unsigned long id);
	void (*disable)(unsigned long id);
	unsigned long (*get_rate)(unsigned long id);
	unsigned long (*get_parent)(unsigned long id);
	bool (*is_enabled)(unsigned long id);
};

TEE_Result clk_enable(unsigned long id);
void clk_disable(unsigned long id);
unsigned long clk_get_rate(unsigned long id);
bool clk_is_enabled(unsigned long id);
unsigned long clk_get_parent(unsigned long id);

void clk_provider_register(const struct clk_ops *ops);

#endif /* CLK_H */
