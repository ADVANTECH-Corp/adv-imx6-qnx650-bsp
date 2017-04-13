/*
 * $QNXLicenseC:
 * Copyright 2009,2012 QNX Software Systems. 
 * 
 * Licensed under the Apache License, Version 2.0 (the "License"). You 
 * may not reproduce, modify or distribute this software except in 
 * compliance with the License. You may obtain a copy of the License 
 * at: http://www.apache.org/licenses/LICENSE-2.0 
 * 
 * Unless required by applicable law or agreed to in writing, software 
 * distributed under the License is distributed on an "AS IS" basis, 
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 *
 * This file may contain contributions from others, either as 
 * contributors under the License or as licensors under other terms.  
 * Please review this entire file for other proprietary rights or license 
 * notices, as well as the QNX Development Suite License Guide at 
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include "startup.h"

/*
 * This board contains a PL310 L2 cache controller
 */

extern struct callout_rtn	cache_pl310;

/*
 * L2 is enabled by default. 
 *
 * This default configuration can be overridden by -le/-ld option.
 */
int		l2_enable = 1;

#define L2_CACHE_ID				0x000
#define L2_CACHE_TYPE				0x004
#define L2_CTRL					0x100
#define L2_AUX_CTRL				0x104
#define L2_TAG_RAM_L_CTRL			0x108
#define L2_DATA_RAM_L_CTRL			0x10c
#define L2_EVENT_CNT_CTRL			0x200
#define L2_EVENT_CNT_1_CFG			0x204
#define L2_EVENT_CNT_0_CFG			0x208
#define L2_EVENT_CNT_1_VAL			0x20c
#define L2_EVENT_CNT_0_VAL			0x210

#define L2_INTR_MASK				0x214
#define L2_MASK_INTR_STATUS			0x218
#define L2_RAW_INTR_STATUS			0x21C
#define L2_INTR_CLEAR				0x220
#define L2_CACHE_SYNC				0x730
#define L2_INVAL_LINE_BY_PA			0x770
#define L2_INVAL_BY_WAY				0x77C
#define L2_CLEAN_LINE_BY_PA			0x7B0
#define L2_CLEAN_LINE_BY_WAY			0x7B8
#define L2_CLEAN_BY_WAY				0x7Bc
#define L2_CLEAN_INVAL_LINE_BY_PA		0x7F0
#define L2_CLEAN_INVAL_LINE_BY_WAY		0x7F8
#define L2_CLEAN_INVAL_BY_WAY			0x7FC

#define L2_DEBUG_CTRL				0xF40
#define L2_PREFETCH_CTRL			0xF60
#define L2_POWER_CTRL				0xF80

#define L2_ALL_WAYS				0xFFFF

static void
l2_write(unsigned reg, unsigned val, unsigned complete_mask)
{
	out32(reg, val);
	while (in32(reg) & complete_mask)
		;
}

void
init_cacheattr()
{
	struct cacheattr_entry *cache;
	unsigned	l2_base = 0x00A02000;

	// disable L2 cache
	out32(l2_base + L2_CTRL, 0);

	// invalidate all ways and perform sync operation
	l2_write(l2_base + L2_INVAL_BY_WAY, L2_ALL_WAYS, L2_ALL_WAYS);
	l2_write(l2_base + L2_CACHE_SYNC, 0, 1);

	if (debug_flag) {
		kprintf("L2 cache %s\n", l2_enable ? "enabled" : "disabled");
	}
	if (l2_enable == 0) {
		return;
	}

	/*
	 * Set tag RAM latencies for this board
	 * setup = 2 (3 cycle)
	 * read  = 3 (4 cycle)
	 * write = 1 (2 cycle)
	 */
	out32(l2_base + L2_TAG_RAM_L_CTRL, (2 << 0) | (3 << 4) | (1 << 8));

	/*
	 * Set data RAM latencies for this board
	 * setup = 2 (3 cycle)
	 * read  = 3 (4 cycle)
	 * write = 1 (2 cycle)
	 */
	out32(l2_base + L2_DATA_RAM_L_CTRL, (2 << 0) | (3 << 4) | (1 << 8));

	/* aux control register initialized by ROM monitor */

	/* 
	 * Double line-fill enable, INCR double line fill enable
	 */
	out32(l2_base + L2_PREFETCH_CTRL, in32(l2_base + L2_PREFETCH_CTRL) | 0x40800000);
	
	/*
	 * Dynamic clock gating enable, standby mode enable
	 */
	out32(l2_base + L2_POWER_CTRL, in32(l2_base + L2_POWER_CTRL) | 0x3);

	// enable L2 cache
	out32(l2_base + L2_CTRL, 1);

	/*
	 * Add the L2 cache callouts
	 *
	 * FIXME: callouts currently flush the whole cache...
	 */
	system_icache_idx =
	system_dcache_idx = add_cache(CACHE_LIST_END,
								  CACHE_FLAG_UNIFIED,
								  32,
								  (1024*1024)/32,
								  &cache_pl310);
	cache = lsp.cacheattr.p;
	cache += system_icache_idx;
	callout_register_data(&cache->control, (void *)l2_base);
}

__SRCVERSION("$URL$ $Rev$");

