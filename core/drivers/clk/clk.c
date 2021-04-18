// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2020, STMicroelectronics - All Rights Reserved
 * Author(s): Ludovic Barre, <ludovic.barre@st.com> for STMicroelectronics.
 */

#include <assert.h>
#include <drivers/clk.h>
#include <stdbool.h>

static const struct clk_ops *ops;

TEE_Result clk_enable(unsigned long id)
{
	assert(ops && ops->enable);

	return ops->enable(id);
}

void clk_disable(unsigned long id)
{
	assert(ops && ops->disable);

	ops->disable(id);
}

unsigned long clk_get_rate(unsigned long id)
{
	assert(ops && ops->get_rate);

	return ops->get_rate(id);
}

unsigned long clk_get_parent(unsigned long id)
{
	assert(ops);

	if (ops->get_parent)
		return ops->get_parent(id);

	return CLK_UNKNOWN_ID;
}

bool clk_is_enabled(unsigned long id)
{
	assert(ops && ops->is_enabled);

	return ops->is_enabled(id);
}

void clk_provider_register(const struct clk_ops *ops_ptr)
{
	assert(!ops && ops_ptr && ops_ptr->enable &&
	       ops_ptr->disable && ops_ptr->get_rate &&
	       ops_ptr->is_enabled);

	ops = ops_ptr;
}
