/*
 * $QNXLicenseC: 
 * Copyright 2009, QNX Software Systems.  
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

extern pdrvr_entry_t p2020_entry;

/*
 * This list is matched with the exported symbol list.
 */
static const struct dll_syms p2020_syms[] = {
	{ "pci_entry", &p2020_entry },
	{ NULL, NULL }
};

/*
 * This matched the dll file name
 */
#define PCI_p2020_LIST		"pci-p2020.so", p2020_syms


__SRCVERSION( "$URL: $ $Rev: $" )
