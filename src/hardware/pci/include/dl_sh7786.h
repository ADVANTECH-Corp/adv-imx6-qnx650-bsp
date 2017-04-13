/*
 * Copyright 2003, QNX Software Systems Ltd. All Rights Reserved.
 *
 * This source code may contain confidential information of QNX Software
 * Systems Ltd.  (QSSL) and its licensors. Any use, reproduction,
 * modification, disclosure, distribution or transfer of this software,
 * or any software which includes or is based upon any of this code, is
 * prohibited unless expressly authorized by QSSL by written agreement. For
 * more information (including whether this source code file has been
 * published) please email licensing@qnx.com.
 */


/******************************************************************************
 *
 *	Copyright (C) Renesas Technology Corp. 2008  All rights reserved.
 *
******************************************************************************/
/*
 * 2008/10/15 (Rev1.171)
 * - Porting to Urquell ( from Pilsner).
 */

#include <pcidrvr.h>

extern pdrvr_entry_t sh7786_entry;				// Rev1.171

/*
 * This list is matched with the exported symbol list.
 */
static const struct dll_syms sh7786_syms[] = {			// Rev1.171
	{ "pci_entry", &sh7786_entry },				// Rev1.171
	{ NULL, NULL }
};

/*
 * This matched the dll file name
 */
#define PCI_SH7786_LIST		"pci-sh7786.so", sh7786_syms	// Rev1.171


__SRCVERSION( "$URL: http://svn/product/tags/restricted/bsp/nto650/ti-j5-evm/latest/src/hardware/pci/include/dl_sh7786.h $ $Rev: 655789 $" )
