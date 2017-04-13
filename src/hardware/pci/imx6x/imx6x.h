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

#ifndef	_IMX6X_H_INCLUDED
#define	_IMX6X_H_INCLUDED

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <gulliver.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <signal.h>
#include <fcntl.h>
#include <pcidrvr.h>
#include <sys/platform.h>
#include <sys/rsrcdbmgr.h>
#include <sys/slogcodes.h>
#include <sys/slog.h>

#include <hw/inout.h>


// ----------------------------------------------
//  iMX6x settings
// ----------------------------------------------

/* PCIe controller has these address ranges:
 *  ARB = 0100_0000 - 01FF_BFFF = 16,368KB = PCIe
 *  DBI = 01FF_C000 - 01FF_FFFF = 16KB     = PCIe Registers
 */

// !WARNING! explosive eggshells!
// Be extremely careful when changing these values!
#define	PCI_MEM_BASE		0x01000000	// note: 20 LSB must be 0!
#define	PCI_MEM_SIZE		  0xF00000	// 15MB (20 LSB must be 0)
#define	PCI_CFG_BASE		0x01F00000
#define	PCI_CFG_SIZE		   0x10000	// 64KB
#define	PCI_IO_BASE			0x01F20000
#define	PCI_IO_SIZE			   0x10000	// 64KB

#define PCI_DBI_BASE 		0x01FFC000
#define PCI_DBI_SIZE		    0x4000	// 16KB

#define SIZE_4KB			0x1000
#define SIZE_64KB			0x10000

// Note: the PCI interrupts are back-to front on the iMX6
// e.g. IRQ_A => PCI_4, instead of IRQ_A => PCI_1
#define	PCI_IRQ_A			155
#define	PCI_IRQ_B			154
#define	PCI_IRQ_C			153
#define	PCI_IRQ_D			152

typedef	struct
{
	/* We must mmap the config region ourselves */
	uint32_t	pci_cfg_pbase;
	uint32_t	pci_cfg_vbase;

	/* We setup the viewports for these but
	 * let the PCI subsystem do the mmap */
	uint32_t	pci_io_base;
	uint32_t	pci_io_size;
	uint32_t	pci_mem_base;
	uint32_t	pci_mem_size;

	/* PCI DBI area */
	uint32_t	pci_dbi_base;

	/* peripheral registers to mmap */
	uint32_t	ccm_base;
	uint32_t	ccm_analog_base;	// used for PMU too
	uint32_t	gpio_base;
	uint32_t	iomux_base;		// includes IOMUX_GPR* registers

	/* status for whether the link is trained */
	int			link_trained;

	/* cache current io mapping value */
	uint32_t	current_cfg_addr;

	/* resource seeding structure */
	rsrc_alloc_t	ralloc[3];

} imx6x_dev_t;


int		imx6x_attach(char *options, void **hdl);
int		imx6x_detach(void *hdl);
int		imx6x_cnfg_bridge(void *hdl, uint32_t bus, uint32_t devfunc, pci_bus_t *pbus);
int		imx6x_read_cnfg(void *hdl, uint32_t bus, uint32_t devfunc, uint32_t reg, uint32_t width, void *buf);
int		imx6x_write_cnfg(void *hdl, uint32_t bus, uint32_t devfunc, uint32_t reg, uint32_t width, const uint32_t data);
int		imx6x_special_cycle(void *hdl, uint32_t bus, uint32_t data);
int 	imx6x_map_irq(void *hdl, uint32_t bus, uint32_t devfunc, uint32_t line, uint32_t pin, uint32_t flags);
int 	imx6x_avail_irq(void *hdl, uint32_t bus, uint32_t devfunc, uint32_t *list, uint32_t *nelm);
int 	imx6x_map_addr(void *hdl, uint64_t iaddr, uint64_t *oaddr, uint32_t type);

#endif

__SRCVERSION( "$URL$ $Rev$" )
