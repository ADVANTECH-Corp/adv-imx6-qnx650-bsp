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







#ifndef _server_h_
#define _server_h_

#include	<unistd.h>
#include	<stdio.h>
#include	<stdarg.h>
#include	<stdlib.h>
#include	<stddef.h>
#include	<string.h>
#include	<errno.h>
#include	<signal.h>
#include	<dl.h>
#include	<pthread.h>
#include	<gulliver.h>
#include	<sys/neutrino.h>
#include	<sys/iofunc.h>
#include	<sys/iomgr.h>
#include	<sys/iomsg.h>
#include	<sys/dispatch.h>
#include	<sys/rsrcdbmgr.h>
#include	<pcidrvr.h>
#include	<inttypes.h>
#include	<hw/pci.h>
#include	<sys/pci_serv.h>
#include	<sys/procmgr.h>
#include	<sys/slogcodes.h>
#include	<sys/mman.h>

#define	DEBUGFUNC()	do { \
					if (verbose > 3) \
					pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "%s", __FUNCTION__); \
					} while (0);

#define		NUM_DEVICES	20
#define		SERV_MAX_IRQS	16
#define		DEFAULT_SIZE	256
#define		MEG1			(1024 * 1024)
#define		KB4				(4 * 1024)

#define	CARDBUS_PRI_BUS		offsetof (struct _pci_cardbus_config_regs, Pci_Bus_Num)
#define	CARDBUS_BUS			offsetof (struct _pci_cardbus_config_regs, Cardbus_Bus_Num)
#define	CARDBUS_SUB_BUS		offsetof (struct _pci_cardbus_config_regs, Sub_Bus_Num)

#define		READ	0
#define		WRITE	1

#define PCI_IO_SPACE			0x01		//Note: These must tie up with pcidrvr.h
#define PCI_MEM_SPACE			0x02
#define PCI_ROM_SPACE			0x04
#define	PCI_ISA_SPACE			0x08
#define PCI_BMSTR_SPACE			0x10
#define	PCI_MEM_PREFETCH		0x20
#define	PCI_DEVICE_ENABLED		0x80

#define	PCI_MEM_PER_BUS			0x4000000		//Try 64Mb per bus

#define PCI_SUBORDINATE_BUS_MASK	0x00ff0000
#define PCI_SECONDARY_BUS_MASK		0x0000ff00
#define PCI_PRIMARY_BUS_MASK		0x000000ff

#define	PCI_PRIMARY_BUS_INSERT(x, y)	(((x) & ~PCI_PRIMARY_BUS_MASK) | ((y) <<  0))
#define	PCI_SECONDARY_BUS_INSERT(x, y)	(((x) & ~PCI_SECONDARY_BUS_MASK) | ((y) <<  8))
#define	PCI_SUBORDINATE_BUS_INSERT(x, y) (((x) & ~PCI_SUBORDINATE_BUS_MASK) | ((y) << 16))

#define	PCI_TYPE_PCIX				1
#define	PCI_TYPE_PCIEXPRESS			2
#define	PCI_TYPE_MSI				4
#define	PCI_TYPE_MSIX				8

union Bus_Access
{
	struct	PCIAccess {
		uchar_t 	BusNumber;			// Bus number 0-255
		uchar_t 	DevFuncNumber;		// Device (bits 7:3) and function (in bits 2:0) IDs
		ushort_t 	PCIreserved;			
	} PCIAccess;
	struct	EISAAccess {
		uchar_t 	SlotNumber;			// Card slot number
		uchar_t		FunctionNumber;		// (E)ISA sub-function number on multi function card
		ushort_t	EISAreserved;		
	} EISAAccess;
	struct	PnPAccess {
		uchar_t 	CSN;				// Card slot number determined during ISA isolation
		uchar_t		LogicalDevNumber;	// Logical device number
		ushort_t	Read_data_port;		// Read data I/O port determined during ISA isolation
	} PnPAccess;
	struct	PCMCIAAccess {
		ushort_t 	LogicalSocket;		// Card logical socket #
		ushort_t 	PCMCIAReserved1;	// Reserved
	} PCMCIAAccess;
};

struct BaseRegister {
	uint64_t	Address;
	uint64_t	Size;
	uint32_t	Flags;
	uint32_t	Filler;
};

typedef	struct	_bus_map	BusMap;
typedef	struct	_server_device	Device;
typedef	struct	_pci_bridge	pci_bridge_t;

typedef	struct _bus_map {
	BusMap		*next;
	BusMap		*prev;
	BusMap		*peer;
	Device		*bdev;
	uint8_t		primary_bus;
	uint8_t		secondary_bus;
	uint8_t		subordinate_bus;
	uint8_t		filler [5];
	uint64_t	io_reqd;
	uint64_t	mem_reqd;
	uint64_t	pref_reqd;
	uint64_t	mem_start;
	uint64_t	mem_end;
	uint64_t	io_start;
	uint64_t	io_end;
	uint64_t	prefetch_mem_start;
	uint64_t	prefetch_mem_end;
	int8_t		name [8];
	pci_bus_t	*pci_bus;
	uint32_t	filler2;
	} BUSMAP;

struct	_server_device {
	Device		*next;
	BusMap		*bmap;
	uint8_t		BusNumber;
	uint8_t		DevFuncNumber;
	uint8_t		HeaderType;
	uint8_t		Filler;
	uint32_t	IrqNumber;
	uint16_t	VendorId;
	uint16_t	DeviceId;
	uint16_t	SubVendorId;
	uint16_t	SubDeviceId;
	uint32_t	ClassCode;
   	struct		BaseRegister BaseReg[6];
	struct		BaseRegister ROMBase;
	struct		BaseRegister mem_alloc;
	struct		BaseRegister pref_alloc;
	struct		BaseRegister io_alloc;
	uint32_t	AttachFlags;	/* The flags that were used to attach this device */
	uint8_t		LockFlg;		/* Number of time this device has been attached */
	uint8_t		Index;
	uint8_t		Min_Gnt;
	uint8_t		Max_Lat;
	uint8_t		RevisionId;
	uint8_t		Persistent;
	uint8_t		IntPin;
	uint8_t		MgrAttach;
	uint8_t		PciType;
	uint8_t		PmCap;
	uint16_t	PmState;
};

typedef	struct _dev_list {
	iofunc_ocb_t	ocb;
	int				num_devices, lockflg;
	Device			*dev_list [NUM_DEVICES];
	} DEV_LIST;

typedef struct _pdrvr {
	pdrvr_entry_t	*entry;
	void			*hdl;
	void 			*next;
	} pdrvr_t;

typedef	struct {
	uint64_t		start_cpu_addr;
	uint64_t		end_cpu_addr;
	uint64_t		start_pci_addr;
	uint64_t		end_pci_addr;
	} addr_map_t;

typedef	struct _pci_bridge {
	pci_bridge_t	*next;
	uint8_t			bus;
	uint8_t			devfn;
	uint8_t			sec_bus;
	uint8_t			sub_bus;
	uint32_t		class;
	} _pci_bridge_t;

//extern int numdevices, listcount;

#endif

__SRCVERSION( "$URL: http://svn/product/tags/restricted/bsp/nto650/ti-j5-evm/latest/src/hardware/pci/include/server.h $ $Rev: 655789 $" )
