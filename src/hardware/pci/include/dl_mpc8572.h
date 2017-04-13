/*
 * $QNXLicenseC: 
 * Copyright 2007, QNX Software Systems.  
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

#include <pcidrvr.h>

extern pdrvr_entry_t mpc8572_entry;

/*
 * This list is matched with the exported symbol list.
 */
static const struct dll_syms mpc8572_syms[] = {
	{ "pci_entry", &mpc8572_entry },
	{ NULL, NULL }
};

/*
 * This matched the dll file name
 */
#define PCI_MPC8572_LIST		"pci-mpc8572.so", mpc8572_syms


__SRCVERSION( "$URL: http://svn/product/tags/restricted/bsp/nto650/ti-j5-evm/latest/src/hardware/pci/include/dl_mpc8572.h $ $Rev: 655789 $" )
