/*
 * $QNXLicenseC:
 * Copyright 2011, QNX Software Systems. All Rights Reserved.
 *
 * You must obtain a written license from and pay applicable
 * license fees to QNX Software Systems before you may reproduce,
 * modify or distribute this software, or any work that includes
 * all or part of this software.   Free development licenses are
 * available for evaluation and non-commercial purposes.  For more
 * information visit http://licensing.qnx.com or email
 * licensing@qnx.com.
 *
 * This file may contain contributions from others.  Please review
 * this entire file for other proprietary rights or license notices,
 * as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include <pcidrvr.h>

extern pdrvr_entry_t imx6x_entry;

/*
 * This list is matched with the exported symbol list.
 */
static const struct dll_syms imx6x_syms[] = {
	{ "pci_entry", &imx6x_entry },
	{ NULL, NULL }
};

/*
 * This matched the dll file name
 */
#define PCI_IMX6X_LIST		"pci-imx6x.so", imx6x_syms


__SRCVERSION( "$URL:$ $Rev:$" )
