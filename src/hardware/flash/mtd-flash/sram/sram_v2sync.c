/*
 * $QNXLicenseC:
 * Copyright 2008, QNX Software Systems. 
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





#include <sys/f3s_mtd.h>

/*
 * Summary
 *
 * MTD Version: 2 only
 * Bus Width:   All
 *
 * Description
 *
 * This is the only MTDv2 sync callout for RAM / SRAM.
 */

int32_t f3s_sram_v2sync(f3s_dbase_t *dbase,
                        f3s_access_t *access,
                        uint32_t flags,
                        uint32_t offset)
{
	return (EOK);
}


__SRCVERSION( "$URL: http://svn/product/tags/public/bsp/nto650/freescale-mx6q-sabrelite/latest/src/hardware/flash/mtd-flash/sram/sram_v2sync.c $ $Rev: 633202 $" );
