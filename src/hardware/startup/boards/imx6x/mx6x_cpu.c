/*
 * $QNXLicenseC: 
 * Copyright 2012 QNX Software Systems.  
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
#include "mx6x_startup.h"
#include <arm/mx6x.h>

unsigned mx6_chip_rev = MX6_CHIP_REV_UNDEFINED;
unsigned mx6_chip_type = MX6_CHIP_TYPE_UNDEFINED;

void set_mx6_chip_rev()
{
	mx6_chip_rev = in32(MX6X_BOOTROM_BASE + MX6X_BOOTROM_SILICON_REV);
	if ( (mx6_chip_rev != MX6_CHIP_REV_1_0) && (mx6_chip_rev != MX6_CHIP_REV_1_1) && (mx6_chip_rev != MX6_CHIP_REV_2_0))
	{
		kprintf("startup: warning, unknown i.MX6 chip revision: 0x%x\n", mx6_chip_rev);
		mx6_chip_rev = MX6_CHIP_REV_UNDEFINED;
	}
}

unsigned get_mx6_chip_rev()
{
	if (mx6_chip_rev == MX6_CHIP_REV_UNDEFINED) 
		set_mx6_chip_rev();
	return mx6_chip_rev;
}

void set_mx6_chip_type()
{
	// TODO waiting for documentation for MX6X_ANADIG_CHIP_INFO register
	mx6_chip_type = in32(MX6X_ANATOP_BASE + MX6X_ANADIG_CHIP_INFO) >> 16;
	if ( (mx6_chip_type != MX6_CHIP_TYPE_QUAD_OR_DUAL) && (mx6_chip_type != MX6_CHIP_TYPE_DUAL_LITE_OR_SOLO))  
	{
		kprintf("startup: warning, unknown i.MX6 chip type: 0x%x\n", mx6_chip_type);
		mx6_chip_type = MX6_CHIP_TYPE_UNDEFINED;
	}
}

unsigned get_mx6_chip_type()
{
	if (mx6_chip_type == MX6_CHIP_TYPE_UNDEFINED)
		 set_mx6_chip_type();
	return mx6_chip_type;
}

__SRCVERSION( "$URL$ $Rev$" )
