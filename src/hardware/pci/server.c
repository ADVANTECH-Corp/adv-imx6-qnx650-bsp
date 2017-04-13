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


/*

PCI device configuration manager

*/

struct _dev_list;
#define IOFUNC_OCB_T	struct _dev_list

#include	<server.h>

#define		MAX_BUSES	10

union _pci_data {
	struct _pci_present		present;
	struct _pci_config		config;
	struct _pci_config_hdl	config_hdl;
	struct _pci_device		device;
	struct _pci_class		class;
	struct _pci_map			map;
	struct _pci_version		version;
	struct _pci_attach		lock;
	struct _pci_detach		unlock;
	struct _pci_map_irq		map_irq;
	struct _pci_route_opt	route_opt;
};

static	const	pci_bus_t	_x86_pci_bus = {
	0,			/* minimum grant */
	255,		/* maximum latency */
	0,			/* devsel time = fast */
	1,			/* we support fast back-to-back */
	1,			/* we support prefetch */
	0,			/* we don't support 66 Mhz */
	0,			/* we don't support 64 bits */
	1,			/* initially one device on bus (ie. us) */
	132000000,	/* bandwidth: in 0.25us cycles/sec */
	0			/* subordinate bus */
};

pdrvr_t		*pci_drivers;

static		uint8_t		lastbus, endbus, cardbus;
static		uint8_t		clast_bus, clast_devfn;
static		uint8_t		*pciex_space = NULL;
static		int			numdevices, listcount;
static		int			scan_buses;
static		int			map_irqs = 1;
static		int			cur_scan_bus;
static		int			dont_touch = 0, hw_override = 0;
static		int			program_display_device = 0;
static		int			mem_page_cnt;
static		int			num_pci_bridges = 0;
static		int			bridge_count = 0;
static		int			num_host_bridges = 0;
static		int			class_override = 0;
static		uint32_t	version, hardware;
static		uint32_t	enum_xtrans_bridge = 0;
static		uint32_t	host_bridge_class = 0xffffffff;
static		uint64_t	mem_cpu_low, mem_cpu_high, mem_pci_low, mem_pci_high;
static		uint64_t	io_cpu_low, io_cpu_high, io_pci_low, io_pci_high;
static		Device		*DeviceHead, *DeviceTail;
static		Device		*last_device = NULL;
static		BUSMAP		*bus_map;
static		addr_map_t	*pci_memory;
static		rsrc_alloc_t	*mem_pages;
static		_pci_bridge_t	*pci_bridges = NULL;
static		void		*ll_handle;
static		char		*progname = "pci_server:";
static		char		res_name [100];
static		resmgr_io_funcs_t io_funcs;

int			verbose = 0;
uint32_t	pci_ex_bar = 0;
uint32_t	pci_ex_size = 0;

static	int			pci_free_resource (struct BaseRegister *BaseReg, uint8_t bus);
static	uint32_t	pci_free_irq (uint32_t irq);
static	int			scan_windows (uint8_t bus, uint8_t devfn, Device *device);
static	Device		*search_busdev (uint8_t bus, uint8_t devfunc);
static	BUSMAP		*find_bus (uint8_t bus);


/***********************************************************************/
/* Output messages to slogger. Also to stderr if verbose greater than  */
/* 4.                                                                  */
/***********************************************************************/

ssize_t		pci_slogf (int opcode, int severity, char *fmt, ...)

{
ssize_t		ret;
va_list		arglist;

	if (verbose > 4) {
		va_start (arglist, fmt);
		vfprintf (stderr, fmt, arglist);
		fprintf (stderr, "\n");
		va_end (arglist);
		}
	va_start (arglist, fmt);
	ret = vslogf (opcode, severity, fmt, arglist);
	va_end (arglist);
	return (ret);
}

/**************************************************************************/
/* This routine currently plugs in fixed values into the version and      */
/* hardware fields. The lastbus field will contain the number of the last */
/* PCI bus found during the bus scan.                                     */
/**************************************************************************/

static	int		pci_bios_present (uint32_t *last_bus, uint32_t *vers, uint32_t *hardw)

{
	if (!lastbus) {		//No PCI found
		return (PCI_UNSUPPORTED_FUNCT);
		}
	*last_bus = (uint32_t) lastbus;
	*vers = version;
	*hardw = hardware;
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* Search the Device structures for a particular bus and devfn. When      */
/* found, cache this structure for future use.                            */
/**************************************************************************/

static	int		find_pciex_device (uint8_t bus, uint8_t devfn)

{
	if ((last_device = search_busdev (bus, devfn)) == NULL)
		return (PCI_DEVICE_NOT_FOUND);
	clast_bus = bus;
	clast_devfn = devfn;
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* Read/write the extended PCI Express configuration space, if this is    */
/* a PCI Express device.                                                  */
/**************************************************************************/

static	int		pciex_check (uint8_t bus, uint8_t devfn, uint32_t offset, uint32_t cnt)

{
int			status;

	if (pciex_space == NULL)
		return (PCI_UNSUPPORTED_FUNCT);
	if ((offset + (cnt * 4)) > 4096)
		return (PCI_BAD_REGISTER_NUMBER);
	if (bus != clast_bus && devfn != clast_devfn) {
		if ((status = find_pciex_device (bus, devfn)) != PCI_SUCCESS)
			return (status);
		}
	if (last_device->PciType != PCI_TYPE_PCIEXPRESS)
		return (PCI_BAD_REGISTER_NUMBER);
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* All calls to the low level PCI routines will read/write 32-bit         */
/* quantities. The following 6 routines will extract the relevant         */
/* information from these 32-bit fields.                                  */
/**************************************************************************/

static	int		PCI_read_config8 (uint8_t bus, uint8_t devfn, uint32_t Offset, uint32_t Cnt, void *ConfigBuffer)

{
int			i, offset, cnt, status = 0;
uint8_t		*p;
uint8_t		temp [256];

	if (Cnt > sizeof (temp))
		return (PCI_BUFFER_TOO_SMALL);
	offset = Offset & ~3;
	cnt = (Cnt+3) >> 2;
	if (cnt > 64)
		cnt = 64;
	if (offset >= 0x100) {
		if ((status = pciex_check (bus, (uint8_t) devfn, offset, cnt)) != PCI_SUCCESS) {
			return (status);
			}
		}
	for (i = 0, p = temp; i < cnt; i++, p += sizeof (ulong_t), offset += sizeof (ulong_t)) {
		if ((status = (*pci_drivers->entry->read_cnfg) (ll_handle, bus, devfn, offset, 4, p)) != PCI_SUCCESS) {
			if (verbose > 3)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: status %x", __FUNCTION__, status);
			return (status);
			}
		}
	memcpy (ConfigBuffer, &temp [Offset % 4], Cnt);
	return (status);
}

/**************************************************************************/
/* See PCI_read_config8                                                   */
/**************************************************************************/

static	int		PCI_read_config16 (uint8_t bus, uint8_t devfn, uint32_t Offset, uint32_t Cnt, void *ConfigBuffer)

{
int			i, offset, cnt, status = 0;
uint16_t	*p;
uint8_t		temp [256];
uint32_t	dword;

	if ((Cnt * 2) > sizeof (temp))
		return (PCI_BUFFER_TOO_SMALL);
	if (Offset & 1)			//Can't have odd offsets
		return (PCI_BAD_REGISTER_NUMBER);
	offset = Offset & ~3;
	cnt = (Cnt+1) >> 1;
	if (cnt > 64)
		cnt = 64;
	if (offset >= 0x100) {
		if ((status = pciex_check (bus, (uint8_t) devfn, offset, cnt)) != PCI_SUCCESS) {
			return (status);
			}
		}
	for (i = 0, p = (uint16_t *) temp; i < cnt; i++, offset += sizeof (ulong_t)) {
		if ((status = (*pci_drivers->entry->read_cnfg) (ll_handle, bus, devfn, offset, 4, &dword)) != PCI_SUCCESS) {
			if (verbose > 3)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: status %x", __FUNCTION__, status);
			return (status);
			}
		dword = ENDIAN_LE32 (dword);
		*p++ = dword & 0xffff;
		*p++ = (dword & 0xffff0000) >> 16;
		}
	memcpy (ConfigBuffer, &temp [Offset % 4], Cnt * 2);
	return (status);
}

/**************************************************************************/
/* See PCI_read_config8                                                   */
/**************************************************************************/

static	int		PCI_read_config32 (uint8_t bus, uint8_t devfn, uint32_t Offset, uint32_t Cnt, void *ConfigBuffer)

{
int			i, status = 0;
uint32_t	*p;

	if (Offset & 3)			//Can't have odd offsets
		return (PCI_BAD_REGISTER_NUMBER);
	if (Cnt > 64)
		Cnt = 64;
	if (Offset >= 0x100) {
		if ((status = pciex_check (bus, (uint8_t) devfn, Offset, Cnt)) != PCI_SUCCESS) {
			return (status);
			}
		}
	for (i = 0, p = ConfigBuffer; i < Cnt; i++, p++, Offset += sizeof (ulong_t)) {
		if ((status = (*pci_drivers->entry->read_cnfg) (ll_handle, bus, devfn, Offset, 4, (uint8_t *) p)) != PCI_SUCCESS) {
			if (verbose > 3)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: status %x", __FUNCTION__, status);
			return (status);
			}
		*p = ENDIAN_LE32 (*p);
		}
	return (status);
}

/**************************************************************************/
/* The PCI write routines are similar to the read functions, except that  */
/* where a value less than 32-bits is written, a read will first have to  */
/* be performed and the new value ored into this 32-bit value.            */
/**************************************************************************/

static	int		PCI_write_config8 (uint8_t bus, uint8_t devfn, uint32_t Offset, uint32_t Cnt, void *ConfigBuffer)

{
int			i, offset, cnt, status = 0;
uint8_t		*p;
uint8_t		temp [256];

	if (Cnt > sizeof (temp))
		return (PCI_BUFFER_TOO_SMALL);
	offset = Offset & ~3;
	cnt = (Cnt+3) >> 2;
	if (cnt > 64)
		cnt = 64;
	if (offset >= 0x100) {
		if ((status = pciex_check (bus, (uint8_t) devfn, offset, cnt)) != PCI_SUCCESS) {
			return (status);
			}
		}
	for (i = 0, p = temp; i < cnt; i++, p += sizeof (ulong_t), offset += sizeof (ulong_t)) {
		if ((status = (*pci_drivers->entry->read_cnfg) (ll_handle, bus, devfn, offset, 4, p)) != PCI_SUCCESS) {
			if (verbose > 3)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: status %x", __FUNCTION__, status);
			return (status);
			}
		}
	memcpy (&temp [Offset % 4], ConfigBuffer, Cnt);
	offset = Offset & ~3;
	for (i = 0, p = temp; i < cnt; i++, p += sizeof (ulong_t), offset += sizeof (ulong_t)) {
		if ((status = (*pci_drivers->entry->write_cnfg) (ll_handle, bus, devfn, offset, 4, (*(uint32_t *) p))) != PCI_SUCCESS) {
			return (status);
			}
		}
	return (status);
}

/**************************************************************************/
/* See PCI_write_config8                                                  */
/**************************************************************************/

static	int		PCI_write_config16 (uint8_t bus, uint8_t devfn, uint32_t Offset, uint32_t Cnt, void *ConfigBuffer)

{
int			i, offset, cnt, status = 0;
uint16_t	*p, *q;
uint8_t		temp [256];
uint32_t	dword;

	if ((Cnt * 2) > sizeof (temp))
		return (PCI_BUFFER_TOO_SMALL);
	if (Offset & 1)			//Can't have odd offsets
		return (PCI_BAD_REGISTER_NUMBER);
	offset = Offset & ~3;
	cnt = (Cnt+1) >> 1;
	if (cnt > 64)
		cnt = 64;
	if (offset >= 0x100) {
		if ((status = pciex_check (bus, (uint8_t) devfn, offset, cnt)) != PCI_SUCCESS) {
			return (status);
			}
		}
	for (i = 0, p = (uint16_t *) temp; i < cnt; i++, offset += sizeof (ulong_t)) {
		if ((status = (*pci_drivers->entry->read_cnfg) (ll_handle, bus, devfn, offset, 4, &dword)) != PCI_SUCCESS) {
			if (verbose > 3)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: status %x", __FUNCTION__, status);
			return (status);
			}
		dword = ENDIAN_LE32 (dword);
		*p++ = dword & 0xffff;
		*p++ = (dword & 0xffff0000) >> 16;
		}
	for (i = 0, p = (uint16_t *) &temp [Offset % 4], q = (uint16_t *) ConfigBuffer; i < Cnt; i++)
		*p++ = *q++;
	offset = Offset & ~3;
	for (i = 0, p = (uint16_t *) temp; i < cnt; i++, offset += sizeof (ulong_t)) {
		dword = *p++;
		dword |= *p++ << 16;
		dword = ENDIAN_LE32 (dword);
		if ((status = (*pci_drivers->entry->write_cnfg) (ll_handle, bus, devfn, offset, 4, dword)) != PCI_SUCCESS) {
			return (status);
			}
		}
	return (status);
}

/**************************************************************************/
/* See PCI_write_config8                                                  */
/**************************************************************************/

static	int		PCI_write_config32 (uint8_t bus, uint8_t devfn, uint32_t Offset, uint32_t Cnt, void *ConfigBuffer)

{
int			i, status = 0;
const		uint32_t	*p;
uint32_t	dword;

	if (Offset & 3)			//Can't have odd offsets
		return (PCI_BAD_REGISTER_NUMBER);
	if (Offset >= 0x100) {
		if ((status = pciex_check (bus, (uint8_t) devfn, Offset, Cnt)) != PCI_SUCCESS) {
			return (status);
			}
		}
	for (i = 0, p = ConfigBuffer; i < Cnt; i++, p++, Offset += sizeof (ulong_t)) {
		dword = ENDIAN_LE32 (*p);
		if ((status = (*pci_drivers->entry->write_cnfg) (ll_handle, bus, devfn, Offset, 4, dword)) != PCI_SUCCESS) {
			if (verbose > 3)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: status %x", __FUNCTION__, status);
			return (status);
			}
		}
	return (status);
}

/**************************************************************************/
/* Turn on access to the PCI device                                       */
/**************************************************************************/

static void	on_pci (uint8_t bus, uint8_t devfn, uint16_t flags)

{
uint16_t	cr;

	if (dont_touch)
		return;
	PCI_read_config16 (bus, devfn, offsetof (struct _pci_config_regs, Command), 1, (char *)&cr);
	cr |= flags;
	PCI_write_config16 (bus, devfn, offsetof (struct _pci_config_regs, Command), 1, (char *)&cr);
}

/**************************************************************************/
/* Turn off access to the PCI device, except for configuration            */
/**************************************************************************/

static uint16_t	off_pci (uint8_t bus, uint8_t devfn)

{
uint16_t	cr, ret;

	PCI_read_config16 (bus, devfn, offsetof (struct _pci_config_regs, Command), 1, (char *)&cr);
	ret = cr;
	if (dont_touch)
		return (ret);
	cr &= ~3;
	PCI_write_config16 (bus, devfn, offsetof (struct _pci_config_regs, Command), 1, (char *)&cr);
	return (ret);
}

/**************************************************************************/
/* Check device capabilities                                              */
/**************************************************************************/

static int	check_capabilities (uint8_t bus, uint8_t devfn, Device *device)

{
uint8_t		cap_ptr, cap;
uint16_t	pwr;
int			capab;
	
	if ((device->HeaderType & 0x7f) == 2)	/* CardBus device */
		capab = offsetof (struct _pci_cardbus_config_regs, Capabilities_Pointer);
	else
		capab = offsetof (struct _pci_config_regs, Capabilities_Pointer);
	if (PCI_read_config8 (bus, devfn, capab, 1, &cap_ptr) != PCI_SUCCESS) {
		return (-1);
		}

	if (cap_ptr) {

		while (1) {
			if (PCI_read_config8 (bus, devfn, cap_ptr, 1, &cap) != PCI_SUCCESS) {
				return (-1);
				}
			if (cap_ptr == 0 || cap_ptr == 0xff) {
				pci_slogf (_SLOGC_PCI, _SLOG_WARNING, "%s Invalid capability pointer %x",
					__FUNCTION__, cap_ptr);
				return (EOK);
				}
			if (cap == PCI_CAP_PCIX) {
				device->PciType = PCI_TYPE_PCIX;
				
				if (verbose > 2)
					pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Bus %x devfunc %x PciX", bus,
						devfn);					
				}
			if (cap == PCI_CAP_PCI_EXPRESS) {
				device->PciType = PCI_TYPE_PCIEXPRESS;
				
				if (verbose > 2)
					pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Bus %x devfunc %x PciExpress", bus,
						devfn);					
				}
			if (cap == PCI_CAP_POWER_MANAGEMENT) {
				if (PCI_read_config16 (bus, devfn, cap_ptr + 4, 1, &pwr) != PCI_SUCCESS) {
					return (-1);
					}

				if (verbose > 2)
					pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Bus %x devfunc %x pwr %x", bus,
						devfn, pwr);
				device->PmState = pwr;
				device->PmCap = cap_ptr;
				}
			if (PCI_read_config8 (bus, devfn, cap_ptr + 1, 1,
				&cap_ptr) != PCI_SUCCESS) {
				return (-1);
				}
			if (!cap_ptr || cap_ptr == 0xff)
				return (EOK);
			}
				
		}

	return (EOK);
}

/**************************************************************************/
/* Allocate a new pci bus structure every time a new bus is discovered.   */
/**************************************************************************/

static	BUSMAP	*alloc_bus (uint8_t pribus, uint8_t secbus)

{
pci_bus_t		*bus;
_pci_bridge_t	*br = pci_bridges;
BUSMAP			*new_map, *map = bus_map, *prev_map;
int				found = 0;

	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: pri %d - sec %d", __FUNCTION__, pribus, secbus);

	if ((bus = calloc (1, sizeof (pci_bus_t))) == NULL) {
		return (NULL);
		}
	if ((new_map = calloc (1, sizeof (BUSMAP))) == NULL) {
		free (bus);
		return (NULL);
		}

	new_map->primary_bus = pribus;
	new_map->secondary_bus = secbus;
	if (secbus > endbus) {
		endbus = secbus;
		}
	if (map == NULL) {
		bus_map = new_map;
		}
	else {
		if ((map = find_bus (pribus)) == NULL) {
			free (bus);
			free (new_map);
			return (NULL);
			}
		new_map->prev = map;
		if (map->next == NULL) {
			map->next = new_map;
//			map->subordinate_bus = secbus;
			}
		else {
			map = map->next;
			if (map->primary_bus == pribus) {
				if (map->peer == NULL) {
					map->peer = new_map;
					}
				else {
					while (map->peer) {
						prev_map = map;
						map = map->peer;
						if (map->secondary_bus > secbus) {
							new_map->peer = map;
							map->prev = new_map;
							prev_map->peer = new_map;
							break;
							}
						else {
							if (map->peer == NULL) {
								map->peer = new_map;
								break;
								}
							}
						}
					}
				}
			}
		}
	new_map->pci_bus = bus;
	*new_map->pci_bus = _x86_pci_bus;		/* initialise the structure */
	bus->sub_bus = secbus;
	while (br) {
		if (br->bus == pribus && br->sec_bus == secbus) {
			found = 1;
			break;
			}
		br = br->next;
		}
	if (found) {
		if (br->sec_bus >= lastbus) {
			lastbus = br->sec_bus + 1;
			bus_map->subordinate_bus = br->sec_bus;
			}
		new_map->subordinate_bus = br->sub_bus;
		}
	else {
		lastbus++;
		if (secbus > lastbus) {
			lastbus = secbus + 1;
			bus_map->subordinate_bus = secbus;
			}
		else {
			bus_map->subordinate_bus = lastbus;
			}
		}
	sprintf ((char *)new_map->name, "bus%02d", secbus);
	return (new_map);
}

/**************************************************************************/
/* Find the bus structure associated with a particular bus number and     */
/* return a pointer to this structure.                                    */
/**************************************************************************/

static	BUSMAP	*find_bus (uint8_t bus)

{
BUSMAP	*map = bus_map;

	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: %d", __FUNCTION__, bus);
	while (1) {
		if (map == NULL) {
			break;
			}
		if (map->secondary_bus == bus) {
			return (map);
			}
		if (map->secondary_bus < bus) {
			if (map->subordinate_bus < bus) {
				if (map->peer) {
					map = map->peer;
					continue;
					}
				else {
					if (map->next) {
						map = map->next;
						continue;
						}
					else {
						break;
						}
					}
				}
			if (map->next) {
				map = map->next;
				continue;
				}
			else {
				if (map->peer) {
					map = map->peer;
					continue;
					}
				else {
					break;
					}
				}
			}
		else {
			if (map->peer) {
				map = map->peer;
				continue;
				}
			else {
				break;
				}
			}
		}
	return (NULL);
}

/**************************************************************************/
/* This routine creates a name by which memory or I/O resources will be   */
/* allocated. The names are created from the bus number on which the      */
/* resource needs to be allocated. eg. pcimemory/Bus0 or io/Bus0/Bus1     */
/**************************************************************************/

static	int		make_name (uint32_t flags, uint8_t bus, uint8_t pref)

{
char			tmp [10];
int				i, j, cnt = 0;
uint8_t			x [MAX_BUSES];
BUSMAP			*map;

	if (flags & RSRCDBMGR_PCI_MEMORY)
		strcpy (res_name, "pcimemory");
	else
		if (flags & RSRCDBMGR_IO_PORT)
			strcpy (res_name, "io");
		else {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Invalid reserve flag %x",
				__FUNCTION__, flags);
			return (-1);
			}
	if (bus) {
		if ((map = find_bus (bus)) == NULL) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Map not found - bus %d",
				__FUNCTION__, bus);
			return (-1);
			}
		x [0] = bus;
		cnt++;
		if (map->primary_bus != 0) {
			map = map->prev;
			while (map) {
				if (map->primary_bus == 0)
					break;
				x [cnt++] = map->primary_bus;
				map = map->prev;
				}
			}
		}
	x [cnt++] = 0;

	for (i = 0, j = cnt - 1; i < cnt; i++, j--) {
		sprintf (tmp, "/Bus%d", x [j]);
		strcat (res_name, tmp);
		}

	if (pref) {
		strcat (res_name, "-pref");
		}

	return (0);
}

/**************************************************************************/
/* Reserve a named resource from PCI memory space or I/O space. The name  */
/* is created depending on which bus the resource needs to be reserved.   */
/* See the make_name() function.                                          */
/**************************************************************************/

static	int		reserve_resource (uint64_t start, uint64_t end, uint32_t flags, uint8_t bus, uint8_t pref)

{
rsrc_alloc_t	alloc;

	memset ((char *) &alloc, 0, sizeof (alloc));
	if (make_name (flags, bus, pref))
		return (-1);
	alloc.start = start;
	alloc.end = end;
	alloc.flags = RSRCDBMGR_FLAG_NAME;
	alloc.name = res_name;
	if (rsrcdbmgr_create (&alloc, 1) == -1) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to seed Mem - Start %llx - End %llx",
			__FUNCTION__, alloc.start, alloc.end);
		return (-1);
		}
	if (verbose)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: Start %llx - End %llx - Name %s",
			__FUNCTION__, alloc.start, alloc.end, alloc.name);
	return (0);
}

/**************************************************************************/
/* This routine finds the upper and lower ranges of PCI memory and I/O    */
/* space. It does this by allocating 1 byte of the resource from the low  */
/* range and then 1 byte from the upper range. A call is then made to the */
/* low-level routines to map these addresses to PCI space.                */
/**************************************************************************/

static	void	find_ranges (void)

{
rsrc_request_t	rreq;
uint64_t		map_addr;
int				i, count;
rsrc_alloc_t	*list;

	mem_page_cnt = 0;
	if ((count = rsrcdbmgr_query (NULL, 0, 0, RSRCDBMGR_PCI_MEMORY)) == -1) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Query failed", __FUNCTION__);
		}
	else {
		if ((mem_pages = calloc (count, sizeof (rsrc_alloc_t))) == NULL) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: calloc failed", __FUNCTION__);
			}
		else {
			if ((count = rsrcdbmgr_query (mem_pages, count, 0, RSRCDBMGR_PCI_MEMORY)) == -1) {
				pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Query1 failed", __FUNCTION__);
				}
			else {
				mem_page_cnt = count;
				}
			}
		}

	//Sort the PCI memory in ascending size order
	if (mem_page_cnt > 1) {
		while (1) {
			int			swap = 0;
			uint64_t	s, e, s1, s2;
			for (i = 0, list = mem_pages; i < mem_page_cnt - 1; i++, list++) {
				s1 = list->end - list->start;
				s2 = list [1].end - list [1].start;
				if (s1 > s2) {
					s = list->start;
					e = list->end;
					list->start = list [1].start;
					list->end = list [1].end;
					list [1].start = s;
					list [1].end = e;
					swap = 1;
					}
				}
			if (! swap)
				break;
			}
		}

	if (verbose > 2) {
		for (i = 0, list = mem_pages; i < count; i++, list++) {
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Start %llx - End %llx", list->start, list->end);
			}
		}

	if ((pci_memory = calloc (mem_page_cnt, sizeof (addr_map_t))) == NULL) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Addr map calloc failed", __FUNCTION__);
		return;
		}

	mem_cpu_high = mem_pci_high = 0;
	mem_cpu_low = mem_pci_low = 0xffffffff;
	io_cpu_low = io_cpu_high = io_pci_low = io_pci_high = 0;

	for (i = 0, list = mem_pages; i < mem_page_cnt; i++, list++) {
		pci_memory [i].start_cpu_addr = list->start;
		pci_memory [i].end_cpu_addr = list->end;
		(*pci_drivers->entry->map_addr) (ll_handle, list->start, &pci_memory [i].start_pci_addr, PCI_MEM_SPACE | PCI_MAP_ADDR_PCI);
		(*pci_drivers->entry->map_addr) (ll_handle, list->end, &pci_memory [i].end_pci_addr, PCI_MEM_SPACE | PCI_MAP_ADDR_PCI);
		if (verbose > 2) {
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Low memory CPU %llx - PCI %llx", list->start, pci_memory [i].start_pci_addr);
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "High memory CPU %llx - PCI %llx", list->end, pci_memory [i].end_pci_addr);
			}
		if (list->start < mem_cpu_low) {
			mem_cpu_low = list->start;
			mem_pci_low = pci_memory [i].start_pci_addr;
			}
		if (list->end > mem_cpu_high) {
			mem_cpu_high = list->end;
			mem_pci_high = pci_memory [i].end_pci_addr;
			}
		if (reserve_resource (mem_cpu_low, mem_cpu_high, RSRCDBMGR_PCI_MEMORY, 0, 0)) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to seed Mem - Start %llx - End %llx",
				__FUNCTION__, mem_cpu_low, mem_cpu_high);
			}
		}

	memset ((char *) &rreq, 0, sizeof (rreq));
	rreq.length = 1;
	rreq.start = rreq.end = 0;
	rreq.flags = RSRCDBMGR_IO_PORT;
	rsrcdbmgr_attach (&rreq, 1);
	(*pci_drivers->entry->map_addr) (ll_handle, rreq.start, &map_addr, PCI_IO_SPACE | PCI_MAP_ADDR_PCI);
	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Low port CPU %llx - PCI %llx", rreq.start, map_addr);
	io_cpu_low = rreq.start;
	io_pci_low = map_addr;
	rsrcdbmgr_detach (&rreq, 1);

	memset ((char *) &rreq, 0, sizeof (rreq));
	rreq.length = 1;
	rreq.start = rreq.end = 0;
	rreq.flags = RSRCDBMGR_IO_PORT | RSRCDBMGR_FLAG_TOPDOWN;
	rsrcdbmgr_attach (&rreq, 1);
#ifdef	__X86__
	if (rreq.start > 0xffb0) {
		rreq.start = 0xffb0;
		}
#endif
	(*pci_drivers->entry->map_addr) (ll_handle, rreq.start, &map_addr, PCI_IO_SPACE | PCI_MAP_ADDR_PCI);
	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "High port CPU %llx - PCI %llx", rreq.start - 1, map_addr);
	io_cpu_high = rreq.start;
	io_pci_high = map_addr;
	rsrcdbmgr_detach (&rreq, 1);
	if (reserve_resource (io_cpu_low, io_cpu_high, RSRCDBMGR_IO_PORT, 0, 0)) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to seed I/O - Start %llx - End %llx",
			__FUNCTION__, io_cpu_low, io_cpu_high);
		}
}

/**************************************************************************/
/* This routine checks a memory or I/O address to see whether it is       */
/* within the ranges determined by the find_ranges() routine.             */
/**************************************************************************/

int		check_range (uint64_t start, uint64_t length, int type, uint8_t bus)

{
uint64_t	low, high;
BUSMAP		*map = NULL;

	low = high = 0;
	if (bus) {
		if ((map = find_bus (bus)) == NULL) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: bus not found %d", __FUNCTION__, bus);
			return (EINVAL);
			}
		}

	if (type & PCI_IO_SPACE) {
		if (bus && (map->io_start || map->io_end)) {
			low = map->io_start;
			high = map->io_end;
			}
		else {
			low = io_pci_low;
			high = io_pci_high;
			}
		if (low == 0xf000 && length == 0xfff) {	/* Special case for bridges */
			high = 0xffff;
			}
		}
	else {
		if (bus) {
			if (type & PCI_MEM_PREFETCH) {
				low = map->prefetch_mem_start;
				high = map->prefetch_mem_end;
				}
			else {
				low = map->mem_start;
				high = map->mem_end;
				}
			}
//		else {
		if (!bus || (!low && !high)) {
			low = mem_pci_low;
			high = mem_pci_high;
			}
		}
		
	if (start >= low && ((start + length - 1) <= high))
		return (0);

#ifdef	__X86__		//Only temporary - must be fixed!!!
	if (type & PCI_ROM_SPACE) {
		if (start >= 0xa0000 && ((start + length - 1) <= 0xfffff))
			return (0);
		}
#endif

	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Range failed %llx - length %llx", start, length);

	return (1);
}

/**************************************************************************/
/* Allocate a memory or I/O resource from the resource manager and place  */
/* the result in the BaseReg structure.                                   */
/**************************************************************************/

static	int		pci_alloc_resource (struct BaseRegister *BaseReg, uint32_t align, uint8_t bus)

{
rsrc_request_t	rreq;
BUSMAP			*busmap = NULL;
int				i;

	if (BaseReg->Flags & PCI_DEVICE_ENABLED) {	//Don't alloc a resource if device enabled
		return (PCI_SUCCESS);
		}

	if (bus) {		//We only need the bus structure for buses > 0
		if ((busmap = find_bus (bus)) == NULL) {
			return (EINVAL);
			}
		}

	memset ((char *) &rreq, 0, sizeof (rreq));
	rreq.length = BaseReg->Size < 256 ? 256 : BaseReg->Size;
	if (BaseReg->Flags & PCI_MEM_SPACE || BaseReg->Flags & PCI_ROM_SPACE || BaseReg->Flags & PCI_MEM_PREFETCH) {
		BaseReg->Address &= 0x0f;		//Keep flags
		for (i = 0; i < mem_page_cnt; i++) {
			rreq.flags = RSRCDBMGR_FLAG_NAME | RSRCDBMGR_FLAG_ALIGN;
			if (make_name (RSRCDBMGR_PCI_MEMORY, bus, (BaseReg->Flags & PCI_MEM_PREFETCH) ? 1 : 0))
				return (EINVAL);
			if (bus) {
				if (BaseReg->Flags & PCI_MEM_PREFETCH) {
					(*pci_drivers->entry->map_addr) (ll_handle, busmap->prefetch_mem_start, &rreq.start, PCI_MEM_SPACE | PCI_MAP_ADDR_CPU);
					(*pci_drivers->entry->map_addr) (ll_handle, busmap->prefetch_mem_end, &rreq.end, PCI_MEM_SPACE | PCI_MAP_ADDR_CPU);
					}
				else {
					(*pci_drivers->entry->map_addr) (ll_handle, busmap->mem_start, &rreq.start, PCI_MEM_SPACE | PCI_MAP_ADDR_CPU);
					(*pci_drivers->entry->map_addr) (ll_handle, busmap->mem_end, &rreq.end, PCI_MEM_SPACE | PCI_MAP_ADDR_CPU);
					}
				}
			else {
				rreq.start = pci_memory [i].start_cpu_addr;
				rreq.end = pci_memory [i].end_cpu_addr;
				rreq.flags |= (RSRCDBMGR_FLAG_RANGE | RSRCDBMGR_FLAG_TOPDOWN);
//				rreq.flags |= RSRCDBMGR_FLAG_RANGE;
				}
			rreq.align = align ? align : rreq.length;
			if (rreq.start == rreq.end) {
				rreq.start = rreq.end = 0;		//ppc mapped addresses are non-zero!
				rreq.flags |= RSRCDBMGR_FLAG_TOPDOWN;
				}
			else {
				rreq.flags |= RSRCDBMGR_FLAG_RANGE;
				}
			rreq.name = res_name;
			if (verbose > 2)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Memory Request Start %llx - End %llx - Size = %llx - Name %s",
					rreq.start, rreq.end, rreq.length, res_name);
			if (rsrcdbmgr_attach (&rreq, 1) == EOK) {
				break;
				}
			if (verbose > 2)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Failed");
			if (bus)
				return (EINVAL);
			}
		}
	else {
		if (BaseReg->Flags & PCI_IO_SPACE) {
			BaseReg->Address &= 0x3;		//Keep flags
			if (bus) {
				(*pci_drivers->entry->map_addr) (ll_handle, busmap->io_start, &rreq.start, PCI_IO_SPACE | PCI_MAP_ADDR_CPU);
				(*pci_drivers->entry->map_addr) (ll_handle, busmap->io_end, &rreq.end, PCI_IO_SPACE | PCI_MAP_ADDR_CPU);
				}
			else {
				rreq.start = rreq.end = 0;
				}
			if (make_name (RSRCDBMGR_IO_PORT, bus, 0))
				return (EINVAL);
			rreq.name = res_name;
			rreq.align = align ? align : rreq.length;
			if (rreq.start == rreq.end) {
				rreq.start = io_cpu_low;
				rreq.end = io_cpu_high;
				rreq.flags = RSRCDBMGR_FLAG_ALIGN | RSRCDBMGR_FLAG_TOPDOWN | RSRCDBMGR_FLAG_RANGE;
				}
			else {
				rreq.flags = RSRCDBMGR_FLAG_ALIGN | RSRCDBMGR_FLAG_RANGE;
				}
			rreq.flags |= RSRCDBMGR_FLAG_NAME;
			if (verbose > 2)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Port Request Start %llx - End %llx - Size = %llx - Name %s",
					rreq.start, rreq.end, rreq.length, res_name);
			if (rsrcdbmgr_attach (&rreq, 1)) {
				if (verbose > 2)
					pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Failed");
				return (EINVAL);
				}
			}
		else {
			return (EINVAL);
			}
		}

	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Address = %llx - End %llx", rreq.start, rreq.end);
	BaseReg->Address |= rreq.start;		//Or address with flags
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* Free a previously allocated memory or I/O resource                     */
/* CardBus device resources must be returned to their controller's        */
/* primary bus.                                                           */
/**************************************************************************/

static	int		pci_free_resource (struct BaseRegister *BaseReg, uint8_t bus)

{
rsrc_request_t	rreq;
BUSMAP			*busmap = NULL;
Device			*tmp_dev;
char			tmp [20];
uint8_t			f_bus = bus;

	if (bus) {		//We only need the bus name for buses > 0
		if ((busmap = find_bus (bus)) == NULL) {
			return (EINVAL);
			}
		tmp_dev = busmap->bdev;
		f_bus = ((tmp_dev->HeaderType & 2) ? busmap->primary_bus : bus);
		if (verbose)
			pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "%s: Header Type %x",
				__FUNCTION__, tmp_dev->HeaderType);
		}

	memset ((char *) &rreq, 0, sizeof (rreq));
	rreq.length = BaseReg->Size < 256 ? 256 : BaseReg->Size;
	if (BaseReg->Flags & PCI_MEM_SPACE || BaseReg->Flags & PCI_ROM_SPACE || BaseReg->Flags & PCI_MEM_PREFETCH) {
		if (make_name (RSRCDBMGR_PCI_MEMORY, f_bus, (BaseReg->Flags & PCI_MEM_PREFETCH) ? 1 : 0))
			return (EINVAL);
		rreq.start = PCI_MEM_ADDR (BaseReg->Address);
		rreq.flags = RSRCDBMGR_FLAG_NAME | RSRCDBMGR_FLAG_RANGE;
		BaseReg->Address &= 0x0f;		//Keep flags
		if (verbose > 2)
			sprintf (tmp, "Free Memory");
		}
	else {
		if (BaseReg->Flags & PCI_IO_SPACE) {
			if (make_name (RSRCDBMGR_IO_PORT, f_bus, 0))
				return (EINVAL);
			rreq.start = PCI_IO_ADDR (BaseReg->Address);
			rreq.flags = RSRCDBMGR_FLAG_NAME | RSRCDBMGR_FLAG_RANGE;
			BaseReg->Address &= 0x3;		//Keep flags
			if (verbose > 2)
				sprintf (tmp, "Free Port");
			}
		else {
			return (EINVAL);
			}
		}
	rreq.end = rreq.start + rreq.length - 1;
	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s Start %llx - End %llx - Length %llx - %s",
					tmp, rreq.start, rreq.end, rreq.length, res_name);
	rreq.name = res_name;
	if (rsrcdbmgr_detach (&rreq, 1)) {
		if (verbose > 2)
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Failed");
		return (EINVAL);
		}

	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Address = %llx", rreq.start);
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* Request an IRQ from the resource manager. Step through list for cnt    */
/* items until an IRQ can be allocated.                                   */
/**************************************************************************/

static	uint32_t	pci_alloc_irq (uint32_t	*list, int cnt, uint8_t bus)

{
rsrc_request_t	*rreq, *req1;
int				i;
uint32_t		irq;

	if (!cnt)
		return (0);

	if ((rreq = calloc (cnt, sizeof (rsrc_request_t))) == NULL) {
		return (0);
		}
	for (i = 0, req1 = rreq; i < (cnt & 0xff); i++, req1++) {
		req1->length = 1;
		req1->start = *list;
		req1->end = *list++;
		req1->flags = RSRCDBMGR_IRQ | RSRCDBMGR_FLAG_RANGE | ( cnt == 1 ? RSRCDBMGR_FLAG_SHARE : 0 );
		if (cnt && i < ((cnt & 0xff) - 1))
			req1->flags |= RSRCDBMGR_FLAG_LIST;
		}

	if (rsrcdbmgr_attach (rreq, cnt) != -1) {
		if (verbose > 2)
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Alloc IRQ %lld - Flags %x", rreq->start, rreq->flags);
		irq = (uint32_t) rreq->start;
		free (rreq);
		return (irq);
		}

	if (verbose > 2) {
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: IRQ Failed 0x%llx - Flags %x",
			__FUNCTION__, rreq->start, rreq->flags);
		}
	free (rreq);
	return (0);
}

/**************************************************************************/
/* Return an IRQ to the resource manager.                                 */
/**************************************************************************/

static	uint32_t	pci_free_irq (uint32_t irq)

{
rsrc_request_t	rreq;

	memset ((char *) &rreq, 0, sizeof (rreq));
	rreq.length = 1;
	rreq.start = irq;
	rreq.end = irq;
	rreq.flags = RSRCDBMGR_IRQ | RSRCDBMGR_FLAG_RANGE;
	if (! rsrcdbmgr_detach (&rreq, 1)) {
		if (verbose > 2)
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Free IRQ %d", irq);
		return (rreq.start);
		}
	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: IRQ Free Failed", __FUNCTION__);
	return (0);
}

/**************************************************************************/
/* Reserve an IRQ from the resource database manager.                     */
/**************************************************************************/
#ifndef	__PPC__

static	int		pci_reserve_irq (uint32_t irq)

{
rsrc_alloc_t	ralloc;

	memset ((char *) &ralloc, 0, sizeof (rsrc_alloc_t));
	ralloc.start = ralloc.end = irq;
	ralloc.flags = RSRCDBMGR_IRQ | RSRCDBMGR_FLAG_RSVP;
	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "reserve IRQ %lld", ralloc.start);
	return (rsrcdbmgr_create (&ralloc, 1));
}
#endif

/**************************************************************************/
/* Configure a PCI-PCI bridge's I/O aperture with a start and end address */
/**************************************************************************/

static	int		configure_bridge_io (uint8_t bus, uint8_t devfn, uint64_t start_addr, uint64_t end_addr)

{
int			bit_32 = 0;
union {
	uint8_t		io_base [2];
	uint16_t	word;
	} un;
uint16_t	base, limit;

	if (PCI_read_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, IO_Base),
		1, &un.word)) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Read config error", __FUNCTION__);
		return (-1);
		}
	if (verbose)
		pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "io_base0 %x - io_base1 %x", un.io_base [0], un.io_base [1]);
	if (un.io_base [0] & 1)	/* 32-bit addressing */
		bit_32 = 1;
	un.io_base [0] = (uint8_t)((start_addr & 0xf000) >> 8);
	un.io_base [1] = (uint8_t)((end_addr & 0xf000) >> 8);
	un.word = ENDIAN_LE16 (un.word);
	PCI_write_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, IO_Base),
		1, &un.word);
	if (bit_32) {
		PCI_read_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, IO_Base_Upper16),
			1, &base);
		PCI_read_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, IO_Limit_Upper16),
			1, &limit);
		if (verbose)
			pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "base %x - limit %x", base, limit);
		base = (uint16_t)(start_addr >> 16);
		limit = (uint16_t)(end_addr >> 16);
		PCI_write_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, IO_Base_Upper16),
			1, &base);
		PCI_write_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, IO_Limit_Upper16),
			1, &limit);
		}
	if (PCI_read_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Command),
		1, &base)) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Read config error", __FUNCTION__);
		return (-1);
		}
	base |= (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MASTER_ENABLE);
	PCI_write_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Command),
		1, &base);
	return (0);
}

/**************************************************************************/
/* Configure a PCI-PCI bridge's memory aperture with a start and end      */
/* address.                                                               */
/**************************************************************************/

static	int		configure_bridge_mem (uint8_t bus, uint8_t devfn, uint64_t start_addr, uint64_t end_addr)

{
uint16_t	base;
union {
	uint16_t	word [2];
	uint32_t	dword;
	} un;

	if (PCI_read_config32 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Memory_Base),
		1, &un.dword)) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Read config error", __FUNCTION__);
		return (-1);
		}
	if (verbose)
		pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "word0 %x - word1 %x", un.word [0], un.word [1]);
#if	defined (__BIGENDIAN__)
	un.word [1] = (uint16_t)((start_addr & 0xfff00000) >> 16);
	un.word [0] = (uint16_t)((end_addr & 0xfff00000) >> 16);
#else
	un.word [0] = (uint16_t)((start_addr & 0xfff00000) >> 16);
	un.word [1] = (uint16_t)((end_addr & 0xfff00000) >> 16);
#endif
	PCI_write_config32 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Memory_Base),
		1, &un.dword);
	if (PCI_read_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Command),
		1, &base)) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Read config error", __FUNCTION__);
		return (-1);
		}
	base |= (PCI_COMMAND_MEM_ENABLE | PCI_COMMAND_MASTER_ENABLE);
	PCI_write_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Command),
		1, &base);
	return (0);
}

/**************************************************************************/
/* Configure a PCI-PCI bridge's prefetchable memory aperture with a start */
/* and end address.                                                       */
/**************************************************************************/

static	int		configure_bridge_pref (uint8_t bus, uint8_t devfn, uint64_t start_addr, uint64_t end_addr)

{
int				bit_64 = 0;
uint32_t		base, limit;
union {
	uint16_t	word [2];
	uint32_t	dword;
	} un;

	if (PCI_read_config32 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Prefetchable_Memory_Base),
		1, &un.dword)) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Read config error", __FUNCTION__);
		return (-1);
		}
	if (verbose)
		pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "word0 %x - word1 %x", un.word [0], un.word [1]);
	if (un.word [0] & 1)	/* 64-bit addressing */
		bit_64 = 1;
#if	defined (__BIGENDIAN__)
	un.word [1] = (uint16_t)((start_addr & 0xfff00000) >> 16);
	un.word [0] = (uint16_t)((end_addr & 0xfff00000) >> 16);
#else
	un.word [0] = (uint16_t)((start_addr & 0xfff00000) >> 16);
	un.word [1] = (uint16_t)((end_addr & 0xfff00000) >> 16);
#endif
	PCI_write_config32 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Prefetchable_Memory_Base),
		1, &un.dword);
	if (bit_64) {
		PCI_read_config32 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Prefetchable_Base_Upper32),
			1, &base);
		PCI_read_config32 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Prefetchable_Limit_Upper32),
			1, &limit);
		if (verbose)
			pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "base %x - limit %x", base, limit);
		base = (uint32_t)(start_addr >> 32);
		limit = (uint32_t)(end_addr >> 32);
		PCI_write_config32 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Prefetchable_Base_Upper32),
			1, &base);
		PCI_write_config32 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Prefetchable_Limit_Upper32),
			1, &limit);
		}
	if (PCI_read_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Command),
		1, &base)) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Read config error", __FUNCTION__);
		return (-1);
		}
	base |= (PCI_COMMAND_MEM_ENABLE | PCI_COMMAND_MASTER_ENABLE);
	PCI_write_config16 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Command),
		1, &base);
	return (0);
}

/**************************************************************************/
/* If a bus structure has not been allocated for this bus, then allocate  */
/* one and fill it with the information regarding this bus. ie. Memory    */
/* and prefetchable memory ranges as well as I/O ranges.                  */
/* If this is a PCI-PCI bridge, then setup the bus number registers.      */
/**************************************************************************/

static	int		configure_bridge (uint8_t bus, uint8_t devfunc, uint8_t sec_bus)

{
uint64_t		qword;
uint64_t		cpu_start, cpu_end;
uint32_t		dword, dword1;
uint16_t		word, word1;
uint8_t			byte;
BUSMAP			*map;


	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: bus %d - devfunc %x", __FUNCTION__, bus, devfunc);
	if ((map = find_bus (sec_bus)) == NULL) {
		if ((map = alloc_bus (bus, sec_bus)) == NULL)
			return (-1);
		}
#if	0
	if (pci_bridge_setup (bus, devfunc)) {
		bridge_dll_load (bus, devfunc);
		}
#endif

	if ((PCI_read_config8 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, Header_Type), 1, (char *)&byte) != PCI_SUCCESS))
		return (-1);
	byte &= 0x7f;
	if (byte == 0) {	/* Header type 0 */
		map->io_start = io_pci_low;
		map->io_end = io_pci_high;
		map->mem_start = mem_pci_low;
		map->mem_end = mem_pci_high;
		return (PCI_SUCCESS);
		}

	PCI_read_config32 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, Primary_Bus_Number), 1, &dword);
	dword = PCI_PRIMARY_BUS_INSERT (dword, bus);
	dword = PCI_SECONDARY_BUS_INSERT (dword, sec_bus);
	dword = PCI_SUBORDINATE_BUS_INSERT (dword, 0xff);	//Allow config. cycles to propagate
	PCI_write_config32 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, Primary_Bus_Number), 1, &dword);
	PCI_read_config16 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, Command), 1, &word);
	word |= PCI_COMMAND_MASTER_ENABLE;
	PCI_write_config16 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, Command), 1, &word);
	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: Bus %x - Devfn %x - dword %x",
			__FUNCTION__, bus, devfunc, dword);

	if (byte == 1) {	/* Header type 1 = PCI-PCI bridge */
		if ((PCI_read_config16 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, IO_Base), 1, (char *)&word) != PCI_SUCCESS))
			return (-1);
		word1 = 0;
		if (word & 0x0f) {		//32-bit I/O addressing
			if ((PCI_read_config16 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, IO_Base_Upper16), 1, (char *)&word1) != PCI_SUCCESS))
				return (-1);
			}
		map->io_start = ((uint64_t)(word1 << 16) | ((uint64_t)(word & 0xf0) << 8));
		word1 = 0;
		if (word & 0x0f00) {		//32-bit I/O addressing
			if ((PCI_read_config16 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, IO_Limit_Upper16), 1, (char *)&word1) != PCI_SUCCESS))
				return (-1);
			}
		map->io_end = (((uint64_t)(word1 << 16) | (uint64_t)(word & 0xf000)) + 0xfff);
		if (verbose > 2)
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: bus %d iostart %llx - ioend %llx",
				__FUNCTION__, bus, map->io_start, map->io_end);
		if (map->io_end < map->io_start) {		// I/O not used
			map->io_start = map->io_end = 0;
			}
		else {
			if (check_range (map->io_start, map->io_end - map->io_start, PCI_IO_SPACE, bus)) {
				map->io_start = map->io_end = 0;
				word = 0xff;
				PCI_write_config16 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, IO_Base), 1, (char *)&word);
				}
			}

		if ((PCI_read_config32 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, Memory_Base), 1, (char *)&dword) != PCI_SUCCESS))
			return (-1);
		map->mem_start = (uint64_t) (dword & 0xffff) << 16;
		map->mem_end = (uint64_t) ((dword & 0xfff00000) + 0xfffff);
		if (verbose > 2)
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: bus %d memstart %llx - memend %llx",
				__FUNCTION__, bus, map->mem_start, map->mem_end);
		if (map->mem_end < map->mem_start) {	// Memory not used
			map->mem_start = map->mem_end = 0;
			}
		else {
			if (check_range (map->mem_start, map->mem_end - map->mem_start, PCI_MEM_SPACE, bus)) {
				map->mem_start = map->mem_end = 0;
				dword = 0xffff;
				PCI_write_config32 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, Memory_Base), 1, (char *)&dword);
				}
			}

		if ((PCI_read_config32 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, Prefetchable_Memory_Base), 1, (char *)&dword) != PCI_SUCCESS))
			return (-1);
		qword = 0;
		if (dword & 0x0f) {		//64-bit addressing
			if ((PCI_read_config32 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, Prefetchable_Base_Upper32), 1, (char *)&dword1) != PCI_SUCCESS))
				return (-1);
			qword = (uint64_t) dword1;
			}
		map->prefetch_mem_start = (uint64_t) ((qword << 32) | ((dword & 0xfff0) << 16));
		qword = 0;
		if (dword & 0xf0000) {	//64-bit addressing
			if ((PCI_read_config32 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, Prefetchable_Limit_Upper32), 1, (char *)&dword1) != PCI_SUCCESS))
				return (-1);
			qword = (uint64_t) dword1;
			}
		map->prefetch_mem_end = (uint64_t) (((qword << 32) | (dword & 0xfff00000)) + 0xfffff);
		if (verbose > 2)
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: bus %d prefstart %llx - prefend %llx",
				__FUNCTION__, bus, map->prefetch_mem_start, map->prefetch_mem_end);
		if (map->prefetch_mem_end < map->prefetch_mem_start) {	// Memory not used
			map->prefetch_mem_start = map->prefetch_mem_end = 0;
			}
		else {
			if (check_range (map->prefetch_mem_start, map->prefetch_mem_end - map->prefetch_mem_start, PCI_MEM_PREFETCH, bus)) {
				map->prefetch_mem_start = map->prefetch_mem_end = 0;
				dword = 0xffff;
				PCI_write_config32 (bus, devfunc, offsetof (struct _pci_bridge_config_regs, Prefetchable_Memory_Base), 1, (char *)&dword);
				}
			}

		if (map->io_start || map->io_end) {
			(*pci_drivers->entry->map_addr) (ll_handle, map->io_start, &cpu_start, PCI_IO_SPACE | PCI_MAP_ADDR_CPU);
			(*pci_drivers->entry->map_addr) (ll_handle, map->io_end, &cpu_end, PCI_IO_SPACE | PCI_MAP_ADDR_CPU);
			reserve_resource (cpu_start, cpu_end, RSRCDBMGR_IO_PORT, sec_bus, 0);
			}

		if (map->mem_start || map->mem_end) {
			(*pci_drivers->entry->map_addr) (ll_handle, map->mem_start, &cpu_start, PCI_MEM_SPACE | PCI_MAP_ADDR_CPU);
			(*pci_drivers->entry->map_addr) (ll_handle, map->mem_end, &cpu_end, PCI_MEM_SPACE | PCI_MAP_ADDR_CPU);
			reserve_resource (cpu_start, cpu_end, RSRCDBMGR_PCI_MEMORY, sec_bus, 0);
			}

		if (map->prefetch_mem_start || map->prefetch_mem_end) {
			(*pci_drivers->entry->map_addr) (ll_handle, map->prefetch_mem_start, &cpu_start, PCI_MEM_SPACE | PCI_MAP_ADDR_CPU);
			(*pci_drivers->entry->map_addr) (ll_handle, map->prefetch_mem_end, &cpu_end, PCI_MEM_SPACE | PCI_MAP_ADDR_CPU);
			reserve_resource (cpu_start, cpu_end, RSRCDBMGR_PCI_MEMORY, sec_bus, 1);
			}
		}

	return (PCI_SUCCESS);
}

/**************************************************************************/
/* Setup the primary, secondary and subordinate bus number registers of   */
/* CardBus controllers.                                                   */
/**************************************************************************/

static	int		setup_cardbus_bridge (uint8_t Bus, uint8_t DevFn, Device *Dev, uint8_t secbus)

{
uint8_t			tmp;
BUSMAP			*map, *map1;
uint32_t		dword;


	PCI_read_config32 (Bus, DevFn, CARDBUS_PRI_BUS, 1, &dword);
	dword = PCI_PRIMARY_BUS_INSERT (dword, Bus);
	dword = PCI_SECONDARY_BUS_INSERT (dword, secbus);
	dword = PCI_SUBORDINATE_BUS_INSERT (dword, secbus);
	PCI_write_config32 (Bus, DevFn, CARDBUS_PRI_BUS, 1, &dword);
	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "%s: Bus %d - lastbus %d - dword %x",
			__FUNCTION__, Bus, lastbus, dword);

	if (scan_windows (Bus, DevFn, Dev) == -1) {
		return (-1);
		}

	if (PCI_read_config8 (Bus, DevFn, offsetof(struct _pci_config_regs, Interrupt_Pin), 1, &tmp) != PCI_SUCCESS) {
		return (-1);
		}
	Dev->IntPin = tmp;

	PCI_read_config32 (Bus, DevFn, offsetof(struct _pci_cardbus_config_regs, Subsystem_Vendor_Id), 1, (char*)&dword);
	Dev->SubVendorId = dword & 0xFFFF;
	Dev->SubDeviceId = dword >> 16;

	if ((map = find_bus (secbus)) == NULL) {
		if ((map = alloc_bus (Bus, secbus)) == NULL)
			return (-1);
		}

	if (Dev->bmap == NULL)		//Link the Device and BUSMAP structures
		Dev->bmap = map;
	if (map->bdev == NULL)
		map->bdev = Dev;
	Dev->Persistent = 1;

	if (Bus) {
		if ((map1 = find_bus (Bus)) == NULL) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to find Bus %d", __FUNCTION__, Bus);
			}
		else {
			map->mem_start = map1->mem_start;
			map->mem_end = map1->mem_end;
			map->io_start = map1->io_start;
			map->io_end = map1->io_end;
			map->prefetch_mem_start = map1->prefetch_mem_start;
			map->prefetch_mem_end = map1->prefetch_mem_end;
			}

		/* If the CardBus controller is on a bus other than zero, make sure that
		   the PCI-PCI bridge above it has memory and I/O apertures programmed.  */

		if (map->mem_start == 0) {
			map1->mem_reqd += MEG1;
			}
		if (map->io_start == 0) {
			map1->io_reqd += KB4;
			}
		}
	return (0);
}

/**************************************************************************/
/* Traverse backwards through the BUSMAP structures.                      */
/**************************************************************************/

static	int			traverse_back (uint8_t sub_bus)

{
Device			*dev;
BUSMAP			*map;
uint32_t		dword;
uint8_t			devfn, byte;
uint8_t			bus, sbus, sub;
int				i;
_pci_bridge_t	*br;

	sbus = cur_scan_bus;
	if ((map = find_bus (sbus)) == NULL) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Bus %d not found!", __FUNCTION__, sbus);
		return (-1);
		}
	dev = map->bdev;
	bus = dev->BusNumber;
	devfn = dev->DevFuncNumber;
	if ((PCI_read_config8 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Header_Type), 1, (char *)&byte) != PCI_SUCCESS)) {
		return (-1);
		}
	byte &= 0x7f;
	if (byte == 1) {	/* This is a PCI-PCI bridge */
		PCI_read_config32 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Primary_Bus_Number), 1, &dword);
		sub = (uint8_t) ((dword & 0xff0000) >> 16);
		dword = PCI_PRIMARY_BUS_INSERT (dword, bus);
		dword = PCI_SECONDARY_BUS_INSERT (dword, sbus);
		if (map->next == NULL) {
			dword = PCI_SUBORDINATE_BUS_INSERT (dword, sbus);
			map->subordinate_bus = sbus;
			}
		else {
			dword = PCI_SUBORDINATE_BUS_INSERT (dword, sub_bus);
			map->subordinate_bus = sub_bus;
			}
		PCI_write_config32 (bus, devfn, offsetof (struct _pci_bridge_config_regs, Primary_Bus_Number), 1, &dword);
		if (verbose > 2) {
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: Bus %x - Devfn %x - dword %x",
				__FUNCTION__, bus, devfn, dword);
			}
		for (i = 0, br = pci_bridges; i < num_pci_bridges; i++) {
			if (br->bus == bus && br->devfn == devfn) {
				if (br->sec_bus != sbus)
					br->sec_bus = sbus;
				if (br->sub_bus < sub_bus)
					br->sub_bus = sub_bus;
				if (br->sub_bus == 0xff)
					br->sub_bus = sub_bus;
				break;
				}
			br = br->next;
			if (br == NULL)
				break;
			}
		}
	cur_scan_bus = map->primary_bus;
	if (cur_scan_bus == 0) {
		endbus = 0;
		}
	while (map->prev) {
		map = map->prev;
		dev = map->bdev;
		if (dev == NULL) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: dev is NULL!", __FUNCTION__);
			break;
			}
		if (dev->BusNumber == bus)
			continue;
		if (map->subordinate_bus < sub_bus) {
			map->subordinate_bus = sub_bus;
			}
		}
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* Allocate a memory or I/O resource, depending on the size and flags     */
/* passed.                                                                */
/**************************************************************************/

static	int		alloc_resource (uint64_t address, uint32_t size, uint32_t flags, uint8_t bus, uint8_t pref)

{
rsrc_request_t	rreq;

	if (! address || ! size)
		return (EOK);
	memset ((char *) &rreq, 0, sizeof (rreq));
	if (make_name (flags, bus, pref))
		return (EINVAL);
	rreq.start = address;
	rreq.end = address + size;
	rreq.length = size;
	rreq.name = res_name;
	rreq.flags = (flags | RSRCDBMGR_FLAG_RANGE | RSRCDBMGR_FLAG_NAME);

	if (rsrcdbmgr_attach (&rreq, 1) == -1) {
		if (verbose > 2) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to seed resource Addr %llx - Size %x - Name %s",
				__FUNCTION__, address, size, res_name);
			}
		return (ENOMEM);
		}
//	pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Alloc resource %llx - Size %x - Flags %x", address, size, flags);
	return (EOK);
}

/**************************************************************************/
/* Power down a PCI device                                                */
/**************************************************************************/

static	int	power_down_device (uint8_t bus, uint8_t devfunc, Device *dev)

{
uint8_t		cap_ptr = dev->PmCap;
uint16_t	cmd, sav_cmd, pwr = dev->PmState;

	if (PCI_read_config16 (bus, devfunc, offsetof(struct _pci_config_regs, Command),
		1, &sav_cmd) != PCI_SUCCESS)
		return (-1);
	cmd = sav_cmd & ~0x7;
	if (PCI_write_config16 (bus, devfunc, offsetof(struct _pci_config_regs, Command),
		1, &cmd) != PCI_SUCCESS)
		return (-1);
	if (PCI_write_config16 (bus, devfunc, cap_ptr + 4, 1, &pwr) != PCI_SUCCESS)
		return (-1);
	if (PCI_write_config16 (bus, devfunc, offsetof(struct _pci_config_regs, Command),
		1, &sav_cmd) != PCI_SUCCESS)
		return (-1);
	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Powered down bus %x - devfunc %x", bus, devfunc);
	return (EOK);
}

/**************************************************************************/
/* Power up a PCI device                                                  */
/**************************************************************************/

static	int	power_up_device (uint8_t bus, uint8_t devfunc, Device *dev)

{
uint8_t		cap_ptr = dev->PmCap;
uint16_t	cmd, sav_cmd, pwr = dev->PmState;
uint32_t	pci_regs[20];

	if (pwr & 0x3) {
		if (PCI_read_config16 (bus, devfunc, offsetof(struct _pci_config_regs, Command),
			1, &sav_cmd) != PCI_SUCCESS)
			return (-1);
		cmd = sav_cmd & ~0x7;
		if (PCI_write_config16 (bus, devfunc, offsetof(struct _pci_config_regs, Command),
			1, &cmd) != PCI_SUCCESS)
			return (-1);
		if (PCI_read_config32 (bus, devfunc, 0, 20, pci_regs) != PCI_SUCCESS)
			return (-1);
		pwr &= ~3;
		if (PCI_write_config16 (bus, devfunc, cap_ptr + 4, 1, &pwr) != PCI_SUCCESS)
			return (-1);
		delay (20);
		if (PCI_write_config32 (bus, devfunc, 0, 20, pci_regs) != PCI_SUCCESS)
			return (-1);
		if (PCI_write_config16 (bus, devfunc, offsetof(struct _pci_config_regs, Command),
			1, &sav_cmd) != PCI_SUCCESS)
			return (-1);
		if (verbose > 2)
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Powered up bus %x - devfunc %x", bus, devfunc);
		}
	return (EOK);
}

/**************************************************************************/
/* Determine the size of memory/io window                                 */
/**************************************************************************/

static uint64_t	window_size (uint8_t bus, uint8_t devfn, uint32_t Offset, uint32_t type, uint32_t reg_64)

{
uint32_t	cur_addr [2], tmp1, tmp_addr;
uint64_t	size, tmp, tmp2;

	cur_addr [1] = tmp_addr = 0xffffffff;
	PCI_read_config32 (bus, devfn, Offset, 1, (char *)&cur_addr [0]);
	if (reg_64) {
		PCI_read_config32 (bus, devfn, Offset + 4, 1, (char *)&cur_addr [1]);
		}
	tmp1 = 0xffffffff;
	PCI_write_config32 (bus, devfn, Offset, 1, (char *)&tmp1);
	if (reg_64) {
		PCI_write_config32 (bus, devfn, Offset + 4, 1, (char *)&tmp1);
		}
	PCI_read_config32 (bus, devfn, Offset, 1, (char *)&tmp1);
	tmp = tmp1;
	if (reg_64) {
		PCI_read_config32 (bus, devfn, Offset + 4, 1, (char *)&tmp_addr);
		tmp2 = tmp_addr;
		tmp |= (tmp2 << 32);
		}
	switch( type ) {
		case PCI_IO_SPACE:
			tmp = PCI_IO_ADDR (tmp);
			break;
		case PCI_MEM_SPACE:
			tmp = PCI_MEM_ADDR (tmp);
			break;
		case PCI_ROM_SPACE:
			tmp = PCI_ROM_ADDR (tmp);
			break;
		}
	if (tmp) {
		if (tmp < 0x10000)
			tmp |= 0xffff0000;
		if (tmp < 0x100000000ULL)
			tmp |= 0xffffffff00000000ULL;
		}
	if (reg_64) {
		if (!(tmp & 0xffffff0000000000ULL)) {	/* Check for 40-bit device */
			tmp |= 0xffffff0000000000ULL;
			}
		}

/* Some stupid chips are pre-programmed with a base address, so default */
	if (tmp1 && cur_addr [0] && tmp_addr == cur_addr [1]) {
		tmp_addr = 0;
		PCI_write_config32 (bus, devfn, Offset, 1, (char *)&tmp_addr);
		PCI_read_config32 (bus, devfn, Offset, 1, (char *)&tmp_addr);
		}
	PCI_write_config32 (bus, devfn, Offset, 1, (char *)&cur_addr [0]);
	if (reg_64) {
		PCI_write_config32 (bus, devfn, Offset + 4, 1, (char *)&cur_addr [1]);
		}
	if (tmp_addr == tmp1)
		size = DEFAULT_SIZE;
	else
		size = (~tmp) + 1;
	return (size);
}

/**************************************************************************/
/* scan base registers                                                    */
/**************************************************************************/

static int	scan_windows (uint8_t bus, uint8_t devfn, Device *device)

{
int			i, reg_64, device_enabled;
int			num_regs = 6;
uint32_t	Offset;
uint32_t	dword, class;
uint64_t	addr64, size;
uint16_t	oldcr;
struct		BaseRegister *BaseReg;
BUSMAP		*map;

	if (verbose > 2) {
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: bus %d - devfn %x", __FUNCTION__, bus, devfn);
		}
	if ((map = find_bus (bus)) == NULL) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to find bus", __FUNCTION__);
		return (-1);
		}

	class = (device->ClassCode & 0xff0000);
	// disable
	oldcr = off_pci (bus, devfn);
	device_enabled = (oldcr & (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE));
	if (! program_display_device) {
		oldcr |= (class == PCI_CLASS_DISPLAY || class == PCI_CLASS_BRIDGE ||
				class == PCI_CLASS_PROCESSOR) ? PCI_COMMAND_MASTER_ENABLE : 0;
		}

	if ((device->HeaderType & 0x7f) == 2) {	/* CardBus device */
		num_regs = 1;
		}
	else {
		if ((device->HeaderType & 0x7f) == 1) {	/* PCI-PCI bridge */
			num_regs = 2;
			}
		}

	// probe
	for (i = 0; i < num_regs; i++) {
		BaseReg = &device->BaseReg [i];
		Offset = offsetof(struct _pci_config_regs, Base_Address_Regs [i]);
		PCI_read_config32 (bus, devfn, Offset, 1, (char *)&dword);
		if (PCI_IS_IO (dword)) {
			size = window_size (bus, devfn, Offset, PCI_IO_SPACE, 0);
			size &= 0xffffU;	/* I/O cannot be more than 64K */
			if (!size)
				continue;
			if (size == 0xffffU) {
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Rejecting I/O size %llx - window %d", size, i);
				continue;
				}
			BaseReg->Size = size;
			map->io_reqd += size;
			BaseReg->Flags |= PCI_IO_SPACE;
			if (check_range ((uint64_t) (dword & ~3), (uint64_t) size, BaseReg->Flags, bus)) {
				pci_slogf (_SLOGC_PCI, _SLOG_WARNING, "Range check failed (IO) - Dev %x - Vend %x - Class %x - Addr %x - Size %llx",
					device->DeviceId, device->VendorId, device->ClassCode, dword & ~3, size);
				if (class != PCI_CLASS_STORAGE && class != PCI_CLASS_DISPLAY) {
					device_enabled = 0;		//Not in valid range
					}
				}
			if (verbose)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "IO %x %llx", PCI_IO_ADDR (dword), size);
			BaseReg->Address = device_enabled ? dword : (dword &= 3);	//Leave reserved bits
			if (! device_enabled || !(dword & ~3)) {
				PCI_write_config32 (bus, devfn, Offset, 1, (char *)&dword);
				}
			else {
				uint64_t Address;

				(*pci_drivers->entry->map_addr) (ll_handle, BaseReg->Address, &Address, (uint32_t) BaseReg->Flags | PCI_MAP_ADDR_CPU);

				if (alloc_resource ((uint32_t) PCI_IO_ADDR (Address), (uint32_t) size, RSRCDBMGR_IO_PORT, bus, 0)) {
					pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Alloc failed %llx - Size %llx",
						__FUNCTION__, Address, size);
					}
				BaseReg->Address = Address;
				BaseReg->Flags |= PCI_DEVICE_ENABLED;
				}
			}
		else {
			reg_64 = 0;
			if ((dword & 0x6) == 0x4) {		//64-bit address
				uint32_t	tmp = 0;
				PCI_read_config32 (bus, devfn, Offset + 4, 1, (char *)&tmp);
				size = (uint64_t) tmp;
				addr64 = (size << 32) | dword;
				reg_64 = 1;
				i++;
				}
			else {
				addr64 = (uint64_t) dword;
				}
			if ((size = window_size (bus, devfn, Offset, PCI_MEM_SPACE, reg_64)) < 8) {
				continue;
				}
			BaseReg->Size = size;
			BaseReg->Flags |= PCI_MEM_SPACE;
			if (addr64 & 0x08) {		//Test for prefetchable
				BaseReg->Flags |= PCI_MEM_PREFETCH;
				map->pref_reqd += size;
				}
			else
				map->mem_reqd += size;
			if (check_range ((addr64 & ~0x0f), (uint64_t) size, BaseReg->Flags, bus)) {
				pci_slogf (_SLOGC_PCI, _SLOG_WARNING, "Range check failed (MEM) - Dev %x - Vend %x - Class %x - Addr %llx - Size %llx",
					device->DeviceId, device->VendorId, device->ClassCode, addr64 & ~0x0f, size);
				if (class != PCI_CLASS_STORAGE && class != PCI_CLASS_DISPLAY &&
					class != PCI_CLASS_PROCESSOR) {
					device_enabled = 0;		//Not in valid range
					}
				}
			if (verbose)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "MEM %llx %llx %s", PCI_MEM_ADDR (addr64), size, addr64 & 0x08 ? "pref" : "");
			BaseReg->Address = device_enabled ? addr64 : (addr64 &= 0x0f);
			if (! device_enabled) {
				PCI_write_config32 (bus, devfn, Offset, 1, (char *)&addr64);
				if (reg_64) {
					addr64 >>= 32;
					PCI_write_config32 (bus, devfn, Offset + 4, 1, (char *)&addr64);
					}
				}
			else {
				uint64_t Address;

				(*pci_drivers->entry->map_addr) (ll_handle, addr64, &Address, (uint32_t) BaseReg->Flags | PCI_MAP_ADDR_CPU);

				if (alloc_resource (PCI_MEM_ADDR (Address), (uint32_t) size, RSRCDBMGR_PCI_MEMORY, bus,
					(BaseReg->Flags & PCI_MEM_PREFETCH) ? 1 : 0)) {
					pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Alloc failed %llx - Size %llx",
						__FUNCTION__, Address, size);
//					BaseReg->Address = 0;
//					BaseReg->Flags &= ~PCI_DEVICE_ENABLED;
//					dword = 0;
//					PCI_write_config32 (bus, devfn, Offset, 1, (char *)&dword);
					}
//				else {
					BaseReg->Address = Address;
					BaseReg->Flags |= PCI_DEVICE_ENABLED;
					}
				}
			}
//		}
	// enable
	if (num_regs < 6) {
		on_pci (bus, devfn, oldcr);
		return (PCI_SUCCESS);
		}

	Offset = offsetof (struct _pci_config_regs, ROM_Base_Address);
	PCI_read_config32 (bus, devfn, Offset, 1, (char *)&dword);
	BaseReg = &device->ROMBase;
	if ((size = window_size (bus, devfn, Offset, PCI_MEM_SPACE, 0)) >= 8) {
		BaseReg->Address = device_enabled ? dword : (dword &= 0x7fe);
		BaseReg->Size = size;
		map->mem_reqd += size;
		BaseReg->Flags |= PCI_ROM_SPACE;
		if (check_range ((uint64_t) (dword & ~1), (uint64_t) size, BaseReg->Flags, bus)) {
			if (verbose)
				pci_slogf (_SLOGC_PCI, _SLOG_WARNING, "Range check failed (ROM) - Dev %x - Vend %x - Class %x - Addr %x - Size %llx",
					device->DeviceId, device->VendorId, device->ClassCode, dword & ~1, size);
			}
		if (verbose)
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "ROM %x - %llx", PCI_ROM_ADDR (dword), size);
		}
	if (!device_enabled) {
		dword &= 0x7fe;		//Leave reserved bits
		PCI_write_config32 (bus, devfn, Offset, 1, (char *)&dword);
		}
	else {
		uint64_t Address;

		(*pci_drivers->entry->map_addr) (ll_handle, BaseReg->Address, &Address, (uint32_t) BaseReg->Flags | PCI_MAP_ADDR_CPU);

// This must also be fixed!!!
//		if (alloc_resource (Address, (uint32_t) size, (RSRCDBMGR_MEMORY | RSRCDBMGR_PCI_MEMORY))) {
//			return (-1);
//			}
		alloc_resource (Address, (uint32_t) size, (RSRCDBMGR_MEMORY | RSRCDBMGR_PCI_MEMORY), bus,
			(BaseReg->Flags & PCI_MEM_PREFETCH) ? 1 : 0);

		BaseReg->Flags |= PCI_DEVICE_ENABLED;
		on_pci (bus, devfn, oldcr);
		if (verbose) {
			for (i = 0; i < 6; i++) {
				BaseReg = &device->BaseReg [i];
				if (BaseReg->Flags) {
					if (BaseReg->Flags & PCI_IO_SPACE)
						pci_slogf (_SLOGC_PCI, _SLOG_INFO, "IO %llx %llx - Flags %x", PCI_IO_ADDR (BaseReg->Address), BaseReg->Size, BaseReg->Flags);
					else
						if (BaseReg->Flags & PCI_MEM_SPACE)
							pci_slogf (_SLOGC_PCI, _SLOG_INFO, "MEM %llx %llx - Flags %x", PCI_MEM_ADDR (BaseReg->Address), BaseReg->Size, BaseReg->Flags);
						else
							pci_slogf (_SLOGC_PCI, _SLOG_INFO, "ROM %llx %llx - Flags %x", PCI_ROM_ADDR (BaseReg->Address), BaseReg->Size, BaseReg->Flags);
					}
				}
			}
		}
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* scan given device, store config                                        */
/**************************************************************************/

static int	scan_device (uint8_t bus, uint8_t SecBus, uint8_t devfn)

{
uint32_t 	dword;
uint8_t 	byte, hdr_type, func;
uint8_t		sec, sub;
int			i, index;
uint16_t	vendor, deviceid, status;
uint32_t	class, subclass;
Device		*device, *Dev;
pci_bus_t	*pcib;
BUSMAP		*map;

	if (!(Dev = calloc (1, sizeof (*Dev))))
		return (-1);

	func = (devfn & 0x07);
	device = DeviceHead;
	if (verbose)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: Bus %d - Device %d - Function %d - SecBus %d",
			__FUNCTION__, bus, devfn >> 3, devfn & 0x7, SecBus);
	Dev->BusNumber = bus;
	Dev->DevFuncNumber = devfn;
	// vendor, device
	PCI_read_config32 (bus, devfn, offsetof(struct _pci_config_regs, Vendor_ID), 1, (char*)&dword);
	Dev->VendorId = vendor = dword & 0xFFFF;
	Dev->DeviceId = deviceid = dword >> 16;

	if (device) {
		index = 0;
		while (device) {
			if (device->VendorId == vendor && device->DeviceId == deviceid) {
				index = device->Index + 1;
				}
			device = device->next;
			}
		}
	else
		index = 0;

	Dev->Index = index;
	// class code
	PCI_read_config32 (bus, devfn, offsetof(struct _pci_config_regs, Revision_ID), 1, (char*)&dword);
	Dev->RevisionId = (uchar_t) dword & 0xff;
	dword >>= 8;
	Dev->ClassCode = dword;
	if (verbose)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Vendor %x Device %x Index %d ClassCode %x", vendor, deviceid, index, dword);

	if (Dev->ClassCode == (PCI_CLASS_BRIDGE | PCI_SUBCLASS_BRIDGE_CARDBUS)) {
		if (func < 2) {
			PCI_read_config32 (bus, devfn, offsetof(struct _pci_cardbus_config_regs, Pci_Bus_Num), 1, (char*)&dword);
			sec = (uint8_t) ((dword & 0xff00) >> 8);
			sub = (uint8_t) ((dword & 0xff0000) >> 16);
			if (sec > 0 && sec <= scan_buses) {
				cardbus = SecBus = sec;
				}
			if (bus && !cardbus) {
				SecBus = bus + 1 + func;
				}
			if (cardbus) {
				++cardbus;
				}
			else {
				cardbus = SecBus;
				}
			endbus = cardbus;
			}
		}
	if ((map = find_bus (SecBus)) == NULL) {
		if ((map = alloc_bus (bus, SecBus)) == NULL) {
			free (Dev);
			return (-1);
			}
		}
	class = (Dev->ClassCode & 0xff0000);
	subclass = (Dev->ClassCode & 0xff00);

	pcib = map->pci_bus;
	if (Dev->bmap == NULL)		//Link the Device and BUSMAP structures
		Dev->bmap = map;
	if (map->bdev == NULL)
		map->bdev = Dev;

	if (DeviceHead == NULL) {
		DeviceHead = Dev;
		DeviceTail = Dev;
		}
	else {
		DeviceTail->next = Dev;
		DeviceTail = Dev;
		}
	numdevices++;

	pcib->ndev++;

	// header type
	PCI_read_config8 (bus, devfn, offsetof (struct _pci_config_regs, Header_Type), 1, (char *) &hdr_type);
	Dev->HeaderType = hdr_type;

	if (Dev->ClassCode == (PCI_CLASS_BRIDGE | PCI_SUBCLASS_BRIDGE_CARDBUS)) {
		if (func < 2) {
			setup_cardbus_bridge (bus, devfn, Dev, cardbus);
			}
		}

	if ((Dev->ClassCode & 0xffff00) == PCI_CLASS_BRIDGE) {
		if (pci_ex_size && pciex_space == NULL) {
			if ((pciex_space = mmap_device_memory (NULL, pci_ex_size,
				PROT_READ | PROT_WRITE | PROT_NOCACHE, MAP_SHARED, pci_ex_bar)) == (uint8_t *) MAP_FAILED) {
				pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: PCIEX map failed", __FUNCTION__);
				pciex_space = NULL;
				}
			pcib->pciex_addr = (void *) pciex_space;
			pcib->pciex_size = pci_ex_size;
			}
		/* If the pciex_space was not set by the server, then see if the low-level */
		/* code will return an address and size */
		if ((*pci_drivers->entry->cnfg_bridge) (ll_handle, bus, devfn, pcib) != PCI_SUCCESS) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to configure bridge", __FUNCTION__);
			return (-1);
			}
		if (pcib->pciex_addr != NULL) {
			pciex_space = (uint8_t *) pcib->pciex_addr;
			pci_ex_size = pcib->pciex_size;
			}
		if (verbose && pciex_space != NULL) {
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "PCIEX base %p - size %x", pciex_space, pci_ex_size);
			}
		}

	// pci status
	PCI_read_config16 (bus, devfn, offsetof (struct _pci_config_regs, Status), 1, (char *) &status);
	if ((status & PCI_STATUS_BACKTOBACK_OKAY) == 0)
		pcib->fast_b2b = 0;
	if ((status & PCI_STATUS_66MHZ_OKAY) == 0)
		pcib->freq66 = 0;
	i = (status & PCI_STATUS_DEVSEL_MASK) >> PCI_STATUS_DEVSEL_SHIFT;
	if (i > pcib->devsel)
		pcib->devsel = i;

	PCI_read_config8 (bus, devfn, offsetof (struct _pci_config_regs, Min_Gnt), 1, (char *) &byte);
	Dev->Min_Gnt = byte;
	PCI_read_config8 (bus, devfn, offsetof (struct _pci_config_regs, Max_Lat), 1, (char *) &byte);
	Dev->Max_Lat = byte;
    if (Dev->Min_Gnt != 0 || Dev->Max_Lat != 0) {
		/* find largest minimum grant time of all devices */
		if (Dev->Min_Gnt != 0 && Dev->Min_Gnt > pcib->Min_Gnt)
			pcib->Min_Gnt = Dev->Min_Gnt;
	
		/* find smallest maximum latency time of all devices */
		if (Dev->Max_Lat != 0 && Dev->Max_Lat < pcib->Max_Lat)
		    pcib->Max_Lat = Dev->Max_Lat;
	
		/* subtract our minimum on-bus time per second from bus bandwidth */
		pcib->bandwidth -= Dev->Min_Gnt * 132000000 / (Dev->Min_Gnt + Dev->Max_Lat);
		if (verbose) {
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Min_Gnt = 0x%x - Max_Lat = 0x%x - Bandwidth = 0x%x", Dev->Min_Gnt, Dev->Max_Lat, pcib->bandwidth);
			}
		}

	/* Check the capabilities */
	
	PCI_read_config16 (bus, devfn, offsetof (struct _pci_config_regs, Status), 1, (char *) &status);
	if (status & PCI_STATUS_CAPABILITIES) {
		if (check_capabilities (bus, devfn, Dev))
			return (-1);
		}

	// type 0 header only ie. not a pci bridge
	// Stop fetching for PCI_SUBCLASS_BRIDGE_OTHER

//	if (class == PCI_CLASS_PROCESSOR)
//		return (PCI_SUCCESS);

	/* The check for bridge type other is required for some targets, as it causes them to fail. */
	if (((hdr_type & 0x7F) != 0x00) || ((class == PCI_CLASS_BRIDGE) &&
	       	(subclass != PCI_SUBCLASS_BRIDGE_PCMCIA) &&
	    (! enum_xtrans_bridge || (subclass != PCI_SUBCLASS_BRIDGE_OTHER)))) {
		pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "%s exiting", __FUNCTION__);	
		return (PCI_SUCCESS);
		}

	if ((class == PCI_CLASS_BRIDGE) && (subclass == PCI_SUBCLASS_BRIDGE_OTHER)) {
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Forcing enumeration of bridge type OTHER : %d", bus);
		}

	/* Exit if host bridge type */
	if (((hdr_type & 0x7f) == 0) && (class == PCI_CLASS_BRIDGE) &&
		(subclass == PCI_SUBCLASS_BRIDGE_HOST)) {
		return (PCI_SUCCESS);
		}

	/* base registers */
	scan_windows (bus, devfn, Dev);

	PCI_read_config32 (bus, devfn, offsetof(struct _pci_config_regs, Sub_Vendor_ID), 1, (char*)&dword);
	Dev->SubVendorId = dword & 0xFFFF;
	Dev->SubDeviceId = dword >> 16;

	// irq
	if (map->bdev->ClassCode == (PCI_CLASS_BRIDGE | PCI_SUBCLASS_BRIDGE_CARDBUS)) {
		Dev->IrqNumber = map->bdev->IrqNumber;
		byte = (uint8_t) Dev->IrqNumber;
		PCI_write_config8 (bus, devfn, offsetof(struct _pci_config_regs, Interrupt_Line), 1, (char *) &byte);
		}
	PCI_read_config8 (bus, devfn, offsetof(struct _pci_config_regs, Interrupt_Pin), 1, (char *) &byte);
	if (byte == 0x0) {
		Dev->IrqNumber = 0xffffffff;
		Dev->IntPin = 0;
		}
	else {
		Dev->IntPin = byte;
		PCI_read_config8 (bus, devfn, offsetof(struct _pci_config_regs, Interrupt_Line), 1, (char *) &byte);
		Dev->IrqNumber = (byte == 0xff) ? 0xffffffff : byte;
#ifndef	__PPC__
		if (byte != 0xff) {
			if (pci_reserve_irq ((uint32_t) byte)) {
				pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to reserve IRQ %d",
					__FUNCTION__, byte);
				}
			}
#endif
		}
	PCI_read_config8 (bus, devfn, offsetof(struct _pci_config_regs, Latency_Timer), 1, (char*)&byte);
	if (!byte || byte > 0x40) {		//Plug the latency timer if not initialised
		byte = 0x20;	//Default value
		PCI_write_config8 (bus, devfn, offsetof(struct _pci_config_regs, Latency_Timer), 1, (char*)&byte);
		}
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* This routine scans through all the PCI buses setting up the PCI-PCI    */
/* bridges.                                                               */
/**************************************************************************/

static	int		setup_pci2pci_bridges (uint8_t bus)

{
int				DevNumber, devfn, NumFuncs;
int				FuncNumber, i, found = 0;
uint32_t		dword, dword1;
uint8_t			byte, lb = 0;
_pci_bridge_t	*br, *br_pre;

	if (verbose > 2) {
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: %d - Lastbus = %d", __FUNCTION__, bus, lastbus);
		}
	for (DevNumber = 0; DevNumber < 32; DevNumber++) {
		NumFuncs = 1;
		for (FuncNumber = 0; FuncNumber < NumFuncs; FuncNumber++) {
			devfn = (DevNumber << 3) | FuncNumber;
			cur_scan_bus = bus;
			if ((PCI_read_config32 (bus, devfn, offsetof(struct _pci_config_regs, Revision_ID), 1, (char *)&dword) != PCI_SUCCESS))
				break;
			dword &= 0xffffff00;
			if (dword == 0 || dword == 0xffffff00)
				continue;
			dword >>= 8;
			if (PCI_read_config8 (bus, devfn, offsetof(struct _pci_config_regs, Header_Type), 1, (char *)&byte) != PCI_SUCCESS)
				break;
			if (FuncNumber == 0) {
				if ((PCI_read_config32 (bus, devfn, offsetof(struct _pci_config_regs, Vendor_ID), 1, (char *)&dword1) != PCI_SUCCESS)) {
					break;
					}
				if (dword1 == 0xffffffff) {
					if (verbose > 2) {
						pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Bus %d - devfn 0x%x - Vendor %x", __FUNCTION__, bus, devfn, dword1);
						}
					continue;
					}
				if (byte & 0x80)
					NumFuncs = 8;
				}
			byte &= 0x7f;
			if ((dword & 0xff0000) != (PCI_CLASS_BRIDGE))
				continue;
			if (byte == 1 || byte == 2) {
				found = 1;

				if (byte == 1) {	/* PCI-PCI header type */
					for (i = 0, br = br_pre = pci_bridges; i < num_pci_bridges; i++) {
						if (br->bus == bus && br->devfn == devfn) {
							if (br->sec_bus)
								lb = br->sec_bus;
							else
								lb = lastbus;
							break;
							}
						br_pre = br;
						br = br->next;
						if (br == NULL)
							break;
						}
					if (i >= num_pci_bridges || br == NULL) {
						if (br == NULL)
							br = br_pre;
						if ((br->next = calloc (1, sizeof (_pci_bridge_t))) == NULL) {
							pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to allocate bridge structure", __FUNCTION__);
							return (0);
							}
						br = br->next;
						br->bus = bus;
						br->devfn = devfn;
						br->class = dword;
						num_pci_bridges++;
						lb = lastbus;
						}
					configure_bridge (bus, devfn, lb);
					}
				else {
					if (PCI_read_config8 (bus, devfn, offsetof(struct _pci_cardbus_config_regs, Cardbus_Bus_Num), 1, (char *)&lb) != PCI_SUCCESS)
						break;
					if (! lb) {
						if (cur_scan_bus) {
							lb = (FuncNumber < 2) ? cur_scan_bus + 1 : cardbus;
							}
						else {
							lb = lastbus;
							}
						}
					}

				if (scan_device (bus, lb, devfn) != PCI_SUCCESS) {
					return (-1);
					}
				bridge_count++;

				if (!setup_pci2pci_bridges (lb)) {
					if (traverse_back (endbus)) {
						return (0);
						}
					}
				found = 0;
				}
			}
		if (verbose > 3)
			pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "%s - last_bus %d - scan_buses %d - lb %d", __FUNCTION__, lastbus, scan_buses, lb);
		if (lastbus >= scan_buses && bridge_count == num_pci_bridges && DevNumber >= 32)
			break;
		}
	return (found);
}

/**************************************************************************/
/* Find a host bridge. This routine assumes that the first device found   */
/* is the host bridge chip and sets up the host_bridge_class variable     */
/* with the class of this device.                                         */
/**************************************************************************/

static	int		find_host_bridge (uint32_t class)

{
uint8_t				bus, dev, func, lb;
uint8_t				devfn, numfunc, byte;
int					status, found;
uint32_t			dword;

	for (dev = bus = 0, found = 0; dev < 32; dev++) {
		numfunc = 1;
		for (func = 0; func < numfunc; func++) {
			devfn = (dev << 3) | func;
			if (func == 0) {
				if (PCI_read_config8 (bus, devfn, offsetof(struct _pci_config_regs, Header_Type), 1, (char *)&byte) != PCI_SUCCESS)
					break;
				if (byte & 0x80)
					numfunc = 8;
				}
			if ((status = PCI_read_config32 (bus, devfn, offsetof(struct _pci_config_regs, Revision_ID), 1, (char *)&dword)) != PCI_SUCCESS) {
				pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Read config status %x", __FUNCTION__, status);
//				return (-1);
				break;
				}
			dword &= 0xffffff00;
			if (dword == 0 || dword == 0xffffff00)
				continue;
			dword >>= 8;
			if ((dword & 0xffff00) == class || class == 0xffffffff) {
				if ((byte & 0x7f) != 0 && (class == PCI_CLASS_BRIDGE)) {	/* Some bridges have an incorrect class code. */
					continue;
					}
				if (verbose > 2) {
					pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Found host bridge bus %x - devfn %x - class %x", bus, devfn, dword);
					}
				if (found && (class == 0xffffffff && dword != host_bridge_class)) {
					continue;
					}
				if (configure_bridge (bus, devfn, lastbus) != PCI_SUCCESS) {		//Configure the host/pci bridge first
					return (-1);
					}
				lb = lastbus ? lastbus - 1 : lastbus;
				if (scan_device (bus, lb, devfn) != PCI_SUCCESS) {
					return (-1);
					}
				endbus = setup_pci2pci_bridges (lb);
				if (host_bridge_class == 0xffffffff) {
					host_bridge_class = dword;
					}
				found = 1;
				num_host_bridges++;
				}
			}
		}
	if (verbose && found) {
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "No. Host bridges %d - Class %x", num_host_bridges, host_bridge_class);
		}
	return (found ? PCI_SUCCESS : -1);
}

/**************************************************************************/
/* A debug function to scan the pci bus for all devices.                  */
/**************************************************************************/

void	find_all (void)

{
int		bus, dev, func, numfunc, devfunc;
int		vend, device;
uint32_t	dword;
uint8_t		byte;

	for (bus = 0; bus <= scan_buses; bus++) {
		for (dev = 0; dev < 32; dev++) {
			numfunc = 1;
			for (func = 0; func < numfunc; func++) {
				devfunc = (dev << 3) | func;
				if ((PCI_read_config32 (bus, devfunc, offsetof(struct _pci_config_regs, Revision_ID), 1, (char *)&dword) != PCI_SUCCESS))
					continue;
//					break;
				dword &= 0xffffff00;
				if (dword == 0 || dword == 0xffffff00)
					continue;
				dword >>= 8;
				if (func == 0) {
					if (PCI_read_config8 (bus, devfunc, offsetof(struct _pci_config_regs, Header_Type), 1, (char *)&byte) != PCI_SUCCESS)
						break;
					if (byte & 0x80)
						numfunc = 8;
					}
				PCI_read_config32 (bus, devfunc, offsetof(struct _pci_config_regs, Vendor_ID), 1, (char*)&dword);
				vend = dword & 0xFFFF;
				device = dword >> 16;
				PCI_read_config32 (bus, devfunc, offsetof(struct _pci_config_regs, Revision_ID), 1, (char*)&dword);
				dword >>= 8;
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Bus %d - Dev %d - Func %d - Vend %x - Device %x - Class %x",
					bus, dev, func, vend, device, dword);
				}
			}
		}
}

/**************************************************************************/
/*                                                                        */
/**************************************************************************/

void	dump_bridge (int bus, int devfn)

{
struct	_pci_bridge_config_regs	regs;
uint32_t	*p;
int			i;

	PCI_read_config32 (bus, devfn, 0, 0x40, (char*)&regs);
	for (i = 0, p = (uint32_t *) &regs; i < 0x40; i += 4) {
		pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "%x - 0x%08x", i, *p++);
		}
}

/**************************************************************************/
/* This routine finds all PCI/PCI bridges and sets the secondary and      */
/* subordinate buses to zero, except if the bus contains a video          */
/* controller.                                                            */
/**************************************************************************/

static	int		find_bridges (void)

{
int				bus, dev, func, numfunc, devfunc;
int				bus_idx = 0, disp_bus = -1;
uint32_t		class;
uint32_t		dword;
uint8_t			byte;
uint8_t			pri, sec, sub;
_pci_bridge_t	*br, *br1;

	for (bus = 0; bus < scan_buses; bus++) {
		for (dev = 0; dev < 32; dev++) {
			numfunc = 1;
			for (func = 0; func < numfunc; func++) {
				devfunc = (dev << 3) | func;
				PCI_read_config32 (bus, devfunc, offsetof(struct _pci_bridge_config_regs, Vendor_ID), 1, (char*)&dword);
				if (dword == 0xffffffff) {
					continue;
					}
				if ((PCI_read_config32 (bus, devfunc, offsetof(struct _pci_config_regs, Revision_ID), 1, (char *)&dword) != PCI_SUCCESS))
					break;
				dword &= 0xffffff00;
				if (dword == 0 || dword == 0xffffff00)
					continue;
				dword >>= 8;
				if (PCI_read_config8 (bus, devfunc, offsetof(struct _pci_config_regs, Header_Type), 1, (char *)&byte) != PCI_SUCCESS)
					break;
				if (func == 0) {
					if (byte & 0x80)
						numfunc = 8;
					}
				class = (dword & 0xffff00);
				if ((class & 0xff0000) == PCI_CLASS_DISPLAY)
					disp_bus = bus;
				if ((byte & 0x7f) != 1) {	/* Header Type */
					if ((byte & 0x7f) == 2) {
						PCI_read_config32 (bus, devfunc, offsetof(struct _pci_cardbus_config_regs, Pci_Bus_Num), 1, (char*)&dword);
						pri = (uint8_t) (dword & 0xff);
						sec = (uint8_t) ((dword & 0xff00) >> 8);
						sub = (uint8_t) ((dword & 0xff0000) >> 16);
						if (verbose > 2) {
							pci_slogf (_SLOGC_PCI, _SLOG_INFO, "CardBus Bridge Bus %d - Dev %d - Func %d - Pri %d - Sec %d - Sub %d",
								bus, dev, func, pri, sec, sub);
							}
						}
					continue;
					}
				PCI_read_config32 (bus, devfunc, offsetof(struct _pci_bridge_config_regs, Primary_Bus_Number), 1, (char*)&dword);
				pri = (uint8_t) (dword & 0xff);
				sec = (uint8_t) ((dword & 0xff00) >> 8);
				sub = (uint8_t) ((dword & 0xff0000) >> 16);
				if (verbose > 2) {
					pci_slogf (_SLOGC_PCI, _SLOG_INFO, "PCI Bridge Bus %d - Dev %d - Func %d - Pri %d - Sec %d - Sub %d",
						bus, dev, func, pri, sec, sub);
					}
				if ((br = calloc (1, sizeof (_pci_bridge_t))) == NULL) {
					pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to allocate bridge structure", __FUNCTION__);
					return (-1);
					}
				br->bus = bus;
				br->devfn = devfunc;
				br->sec_bus = sec;
				br->sub_bus = sub;
				br->class = class;
				if (sub >= scan_buses) {
					scan_buses = (sub >= 0xfd) ? sub : sub + 2;
					if (verbose > 2)
						pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s: scan_buses set to %d", __FUNCTION__, scan_buses);
					}
				if (pci_bridges == NULL)
					pci_bridges = br;
				else {
					br1 = pci_bridges;
					while (br1->next)
						br1 = br1->next;
					br1->next = br;
					}
				bus_idx++;
				if (verbose > 3)
					dump_bridge (bus, devfunc);
				}
			}
		}
	num_pci_bridges = bus_idx;

	return (0);
}

/**************************************************************************/
/* Scan all the PCI buses, setting up the PCI-PCI bridges and devices     */
/* below these bridges.                                                   */
/**************************************************************************/

static int		scan_all (void)

{
int				DevNumber, FuncNumber, devfn;
int				NumFuncs, lb;
uint32_t		dword;
uint8_t			byte, bus = 0;
_pci_bridge_t	*br;

	numdevices = 0;

	if (verbose > 3) {
		find_all ();
		}

	if (find_bridges ())
		return (-1);

	if (find_host_bridge (PCI_CLASS_BRIDGE) != PCI_SUCCESS) {
		if (find_host_bridge (0xffffffff) != PCI_SUCCESS) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: No host bridges found", __FUNCTION__);
			return (-1);
			}
		}

	for (bus = 0; bus < scan_buses; bus++) {
		for (DevNumber = 0; DevNumber < 32; DevNumber++) {
			NumFuncs = 1;
			for (FuncNumber = 0; FuncNumber < NumFuncs; FuncNumber++) {
				devfn = (DevNumber << 3) | FuncNumber;
				if ((PCI_read_config32 (bus, devfn, offsetof(struct _pci_config_regs, Vendor_ID), 1, (char *)&dword) != PCI_SUCCESS))
					continue;
//					break;
				if (dword == 0 || dword == 0xffffffff)
					continue;
				if (FuncNumber == 0) {
					if (PCI_read_config8 (bus, devfn, offsetof(struct _pci_config_regs, Header_Type), 1, (char *)&byte) != PCI_SUCCESS)
						break;
					if (byte & 0x80)
						NumFuncs = 8;
					}
				if ((PCI_read_config32 (bus, devfn, offsetof(struct _pci_config_regs, Revision_ID), 1, (char *)&dword) != PCI_SUCCESS))
//					break;
					continue;
				dword &= 0xffffff00;
				switch (dword) {
					case	0:
						if (class_override & 1)
							continue;
						break;
					case	0xffffff00:
						if (class_override & 2)
							continue;
						break;
					default:
						break;
					}
//				if (!class_override) {
//					if (dword == 0 || dword == 0xffffff00)
//						continue;
//					}
				dword >>= 8;
				dword &= 0xffff00;
				if (dword == (PCI_CLASS_BRIDGE) || dword == (PCI_CLASS_BRIDGE | PCI_SUBCLASS_BRIDGE_PCI) ||
					dword == (PCI_CLASS_BRIDGE | PCI_SUBCLASS_BRIDGE_CARDBUS)) {
					continue;		//Bridges have already been setup
					}

				if (scan_device (bus, bus, devfn) != PCI_SUCCESS) {
					return (-1);
					}
				}
			}
		}
	if (num_pci_bridges) {
		for (lb = 0, br = pci_bridges; lb < num_pci_bridges; lb++) {
			if (br->sub_bus > lastbus)
				lastbus = br->sub_bus;
			if (verbose)
				pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "PCI Bridge Bus %d - DevFn %x - Sec %d - Sub %d",
					br->bus, br->devfn, br->sec_bus, br->sub_bus);
			br = br->next;
			if (br == NULL)
				break;
			}
		}

	return (PCI_SUCCESS);
}

/**************************************************************************/
/* Rescan the PCI buses for devices that have been inserted/removed.      */
/**************************************************************************/

static	int		rescan (void)

{
typedef struct {
	uint8_t		bus;
	uint8_t		devfn;
	} RESCAN;
uint8_t		DevNumber, FuncNumber, devfn;
int			NumFuncs, found, num_dev, count, resptr;
uint32_t	dword;
uint8_t		byte, bus;
Device		*nextdev, *prev = NULL;
RESCAN		*res, *resp;
struct 		BaseRegister *BaseReg;
int			i;

	num_dev = numdevices;
	count = resptr = found = 0;
	if ((res = calloc (100, sizeof (RESCAN))) == NULL)
		return (-1);
	resp = res;
	for (bus = 0; bus <= scan_buses; bus++) {
		for (DevNumber = 0; DevNumber < 32; DevNumber++) {
			NumFuncs = 1;
			for (FuncNumber = 0; FuncNumber < NumFuncs; FuncNumber++) {
				devfn = (DevNumber << 3) | FuncNumber;
				if ((PCI_read_config32 (bus, devfn, offsetof(struct _pci_config_regs, Vendor_ID), 1, (char *)&dword) != PCI_SUCCESS))
					break;
				if (dword == 0 || dword == 0xffffffff)
					continue;
				if (FuncNumber == 0) {
					if (PCI_read_config8 (bus, devfn, offsetof(struct _pci_config_regs, Header_Type), 1, (char *)&byte) != PCI_SUCCESS)
						break;
					if (byte & 0x80)
						NumFuncs = 8;
					}
				nextdev = DeviceHead;
				found = 0;
				do {
					if (nextdev->BusNumber == bus && nextdev->DevFuncNumber == devfn) {
						resp->bus = bus;
						resp++->devfn = devfn;
						resptr++;
						found = 1;
						break;
						}
				} while ((nextdev = nextdev->next));
				count++;
				if (found)
					continue;
				if ((PCI_read_config32 (bus, devfn, offsetof(struct _pci_config_regs, Revision_ID), 1, (char *)&dword) != PCI_SUCCESS))
					break;
				dword &= 0xffffff00;
				if (dword == 0 || dword == 0xffffff00)
					break;
				dword >>= 8;
				dword &= 0xffff00;
				if (dword == (PCI_CLASS_BRIDGE) || dword == (PCI_CLASS_BRIDGE | PCI_SUBCLASS_BRIDGE_PCI)) {
					continue;		//Bridges have already been setup
					}

				if (scan_device (bus, bus, devfn) != PCI_SUCCESS) {
					free (res);
					return (-1);
					}
				}
			}
		}
	if (count < num_dev) {		//Something has been removed!
		numdevices = count;
		nextdev = DeviceHead;
		do {
			for (count = found = 0, resp = res; count < resptr; count++, resp++) {
				if (nextdev->BusNumber == resp->bus && nextdev->DevFuncNumber == resp->devfn) {
					found = 1;
					break;
					}
				}
			if (! found) {
				if (prev == NULL) {
					DeviceHead = nextdev->next;
					}
				else {
					prev->next = nextdev->next;
					}
				for (i = 0; i < 6; i++) {	//Free the resources
					BaseReg = &nextdev->BaseReg [i];
					if (BaseReg->Address)
						BaseReg->Address &= PCI_IO_ADDR (BaseReg->Address) ? 0x3 : 0x0f;
					}
				BaseReg = &nextdev->ROMBase;
				if (BaseReg->Address)
					BaseReg->Address &= 0x7fe;

				// Don't free CardBus interrupts
				if (nextdev->bmap->bdev->ClassCode != (PCI_CLASS_BRIDGE | PCI_SUBCLASS_BRIDGE_CARDBUS)) {
					if (nextdev->IrqNumber != 0 && nextdev->IrqNumber != 0xffffffff) {
						pci_free_irq (nextdev->IrqNumber);
						}
					}
				if (nextdev->io_alloc.Address) {
					pci_free_resource (&nextdev->io_alloc, nextdev->BusNumber);
					}
				if (nextdev->mem_alloc.Address) {
					pci_free_resource (&nextdev->mem_alloc, nextdev->BusNumber);
					}
				if (nextdev->pref_alloc.Address) {
					pci_free_resource (&nextdev->pref_alloc, nextdev->BusNumber);
					}
				if (nextdev->next == NULL) {	//Last item on queue
					DeviceTail = prev;
					free (nextdev);
					break;
					}
				else {
					free (nextdev);
					nextdev = prev;
					continue;
					}
				}
			prev = nextdev;
		} while ((nextdev = nextdev->next));
		}
	free (res);
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* Search through the Device list looking for a particular class of       */
/* device at a specific index.                                            */
/**************************************************************************/

static	Device	*search_class (uint32_t class, uint32_t idx)

{
Device 		*device = DeviceHead;
int			oidx = 0;

	while (device) {
		if (device->ClassCode == class) {
			if (oidx == idx) {
				return (device);
				}
			else {
				oidx++;
				}
			}
		device = device->next;
		}
	return (NULL);
}

/**************************************************************************/
/* Search through the Device list looking for a particular vendor and     */
/* device id at a specific index.                                         */
/**************************************************************************/

static	Device	*search_vendor (uint32_t vendor, uint32_t vendor_mask, uint32_t idx)

{
Device 		*dev_ptr = DeviceHead;

	while (dev_ptr) {
		if ((dev_ptr->VendorId & vendor_mask) == vendor && dev_ptr->Index == idx) {
			return (dev_ptr);
			}
		dev_ptr = dev_ptr->next;
		}
	return (NULL);
}

/**************************************************************************/
/* Search through the Device list looking for a particular vendor and     */
/* device id at a specific index.                                         */
/**************************************************************************/

static	Device	*search_venddev (uint32_t vendor, uint32_t device, uint32_t vendor_mask, uint32_t device_mask, uint32_t idx)

{
Device 		*dev_ptr = DeviceHead;

	while (dev_ptr) {
		if ((dev_ptr->VendorId & vendor_mask) == vendor && (dev_ptr->DeviceId & device_mask) == device && dev_ptr->Index == idx) {
			return (dev_ptr);
			}
		dev_ptr = dev_ptr->next;
		}
	return (NULL);
}

/**************************************************************************/
/* Search through the Device list looking for a particular bus number     */
/* and devfuncnumber.                                                     */
/**************************************************************************/

static	Device	*search_busdev (uint8_t bus, uint8_t devfunc)

{
Device 		*device = DeviceHead;

	while (device) {
		if (device->BusNumber == bus && device->DevFuncNumber == devfunc) {
			return (device);
			}
		device = device->next;
		}
	return (NULL);
}

/**************************************************************************/
/* Return the number of devices found by a previous scan of the bus.      */
/**************************************************************************/

/* configuration calls */


static	int		PCI_GetVersion (uint16_t *Version, uint16_t *NumSysDevices)

{
	*Version = version;
	*NumSysDevices = numdevices;
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* Search through the Device list looking for a device that matches       */
/* DeviceId, VendorId as well as Index. If the device is found, return    */
/* the busnumber and devfuncnumber.                                       */
/**************************************************************************/

static	int		PCI_find_device (uint32_t DeviceId, uint32_t VendorId, uint32_t Index,
							uint8_t *bus, uint8_t *devfn)

{
Device 		*device;

	if ((device = search_venddev (VendorId, DeviceId, 0xffff, 0xffff, Index)) == NULL) {
		return (PCI_DEVICE_NOT_FOUND);
		}
	*bus = device->BusNumber;
	*devfn = device->DevFuncNumber;
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* Search the Device list looking for a class of device (ClassCode)       */
/* at a specific Index. If found, return the busnumber and devfuncnumber. */
/**************************************************************************/

static	int		PCI_find_class (uint32_t ClassCode, uint32_t Index, uint8_t *bus,
							uint8_t *devfn)

{
Device 		*device;

	if (bus == NULL || devfn == NULL) {
		return (PCI_UNSUPPORTED_FUNCT);
		}
	if ((device = search_class (ClassCode, Index)) == NULL) {
		return (PCI_DEVICE_NOT_FOUND);
		}
	*bus = device->BusNumber;
	*devfn = device->DevFuncNumber;
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* This routine checks to see whether an IRQ can be programmed.           */
/* The code first saves the old irq and then writes another irq and reads */
/* it back. If the source and destination irqs agree, then the irq is     */
/* programmable.                                                          */
/**************************************************************************/

int		irq_ok (uint8_t bus, uint8_t devfn)

{
uint8_t		old_irq, tmp [2];

	if (PCI_read_config8 (bus, devfn, offsetof (struct _pci_config_regs, Interrupt_Line), 1, &old_irq) != PCI_SUCCESS) {
		return (0);
		}
	if (old_irq != 0xff && old_irq < 16 && old_irq != 0) {
		return (1);					//IRQ is OK
		}
	tmp [0] = 0x09;
	if (PCI_write_config8 (bus, devfn, offsetof (struct _pci_config_regs, Interrupt_Line), 1, &tmp [0]) != PCI_SUCCESS) {
		return (0);
		}
	if (PCI_read_config8 (bus, devfn, offsetof (struct _pci_config_regs, Interrupt_Line), 1, &tmp [1]) != PCI_SUCCESS) {
		return (0);
		}
	if (PCI_write_config8 (bus, devfn, offsetof (struct _pci_config_regs, Interrupt_Line), 1, &old_irq) != PCI_SUCCESS) {
		return (0);
		}
	return ((tmp [0] == tmp [1]) ? 1 : 0);	//IRQ is OK if tmp [0] == tmp [1]
}

/**************************************************************************/
/* This routine checks to see whether the interrupt pin of a non-zero     */
/* function is the same as function zero.                                 */
/**************************************************************************/

int		same_int (uint8_t bus, uint8_t devfn)

{
uint8_t		int_pin, int_pin1;

	if (PCI_read_config8 (bus, devfn, offsetof (struct _pci_config_regs, Interrupt_Pin), 1, &int_pin) != PCI_SUCCESS) {
		return (0);
		}
	if (PCI_read_config8 (bus, (devfn & 0xf8), offsetof (struct _pci_config_regs, Interrupt_Pin), 1, &int_pin1) != PCI_SUCCESS) {
		return (0);
		}
	return ((int_pin == int_pin1) ? 1 : 0);
}

/**************************************************************************/
/* Return true is this device is not a bridge, else false.                */
/**************************************************************************/

int		is_not_bridge (uint8_t bus, uint8_t devfn)

{
uint32_t	dword;

	if ((PCI_read_config32 (bus, (devfn & 0xf8), offsetof (struct _pci_bridge_config_regs, Revision_ID), 1, (char *)&dword) != PCI_SUCCESS))
		return (-1);
	dword >>= 8;
	if ((dword & 0xff0000) == (PCI_CLASS_BRIDGE)) {
		return (0);
		}
	return (1);
}

/**************************************************************************/
/* Translate addresses from CPU space to PCI space.                       */
/**************************************************************************/

int		PCI_translation( void *ll_handle, struct pci_dev_info *ConfigBuffer )

{
uint64_t	iaddr, oaddr;

	iaddr = 0;
	if ((*pci_drivers->entry->map_addr)(ll_handle, iaddr, &oaddr, PCI_IO_SPACE | PCI_MAP_ADDR_PCI) != EOK)
		pci_slogf (_SLOGC_PCI, _SLOG_WARNING, "Translate I/O failed");
	else
		ConfigBuffer->CpuIoTranslation = -oaddr;
	if ((*pci_drivers->entry->map_addr)(ll_handle, iaddr, &oaddr, PCI_MEM_SPACE | PCI_MAP_ADDR_PCI) != EOK)
		pci_slogf (_SLOGC_PCI, _SLOG_WARNING, "Translate Mem failed");
	else
		ConfigBuffer->CpuMemTranslation = -oaddr;
	if ((*pci_drivers->entry->map_addr)(ll_handle, iaddr, &oaddr, PCI_ISA_SPACE | PCI_MAP_ADDR_PCI) != EOK)
		pci_slogf (_SLOGC_PCI, _SLOG_WARNING, "Translate ISA failed");
	else
		ConfigBuffer->CpuIsaTranslation = -oaddr;
	if ((*pci_drivers->entry->map_addr)(ll_handle, iaddr, &oaddr, PCI_BMSTR_SPACE | PCI_MAP_ADDR_PCI) != EOK)
		pci_slogf (_SLOGC_PCI, _SLOG_WARNING, "Translate Bmstr failed");
	else
		ConfigBuffer->CpuBmstrTranslation = oaddr;

	return( 0 );
}

/**************************************************************************/
/* Validate the handle                                                    */
/**************************************************************************/

static	Device	*validate_handle (Device *handle)

{
Device 		*dev_ptr = DeviceHead;

	while (dev_ptr) {
		if (dev_ptr == handle)
			return (handle);
		dev_ptr = dev_ptr->next;
		}
	return (NULL);
}

/**************************************************************************/
/* Fill base register config information                                  */
/**************************************************************************/

static	void	fill_base (struct pci_dev_info *ConfigBuffer, Device *dev_ptr)

{
struct		BaseRegister *BaseReg;
int			i;
uint64_t	Address;

	for (i = 0; i < 6; i++) {
		BaseReg = &dev_ptr->BaseReg [i];
		if (! BaseReg->Size)
			continue;
		(*pci_drivers->entry->map_addr) (ll_handle, BaseReg->Address, &Address, (uint32_t) (BaseReg->Flags | PCI_MAP_ADDR_PCI));
		ConfigBuffer->PciBaseAddress [i] = Address;
		ConfigBuffer->CpuBaseAddress [i] = BaseReg->Address;
		ConfigBuffer->BaseAddressSize [i] = BaseReg->Size;
		}
	BaseReg = &dev_ptr->ROMBase;
	if (BaseReg->Flags & PCI_ROM_SPACE) {
		(*pci_drivers->entry->map_addr) (ll_handle, BaseReg->Address, &Address, (uint32_t) (BaseReg->Flags | PCI_MAP_ADDR_PCI));
		ConfigBuffer->PciRom = Address;
		ConfigBuffer->CpuRom = BaseReg->Address;
		ConfigBuffer->RomSize = (uint32_t) BaseReg->Size;
		}
	PCI_translation (ll_handle, ConfigBuffer);
}

/**************************************************************************/
/* Fill basic config information                                          */
/**************************************************************************/

static	void	fill_config (struct pci_dev_info *ConfigBuffer, Device *dev_ptr)

{
uint8_t		intline;

	ConfigBuffer->DeviceId = dev_ptr->DeviceId;
	ConfigBuffer->VendorId = dev_ptr->VendorId;
	ConfigBuffer->Class = dev_ptr->ClassCode;
	ConfigBuffer->BusNumber = dev_ptr->BusNumber;
	ConfigBuffer->DevFunc = dev_ptr->DevFuncNumber;
	ConfigBuffer->Revision = dev_ptr->RevisionId;
	ConfigBuffer->SubsystemId = dev_ptr->SubDeviceId;
	ConfigBuffer->SubsystemVendorId = dev_ptr->SubVendorId;
	PCI_read_config8 (dev_ptr->BusNumber, dev_ptr->DevFuncNumber, offsetof (struct _pci_config_regs, Interrupt_Line), 1, &intline);
	if (intline != dev_ptr->IrqNumber) {
		dev_ptr->IrqNumber = intline;
		}
	ConfigBuffer->Irq = dev_ptr->IrqNumber;
}

/**************************************************************************/
/* Make a check of the attach flags                                       */
/**************************************************************************/

static	int		check_flags (Device *dev_ptr, uint32_t flags)

{
	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "AttachFlags %x - flags %x", dev_ptr->AttachFlags, flags);
	if ((dev_ptr->AttachFlags & PCI_HW_INIT) == 0)
		return ((flags & PCI_HW_INIT) ? 1 : 0);
	if ((dev_ptr->AttachFlags ^ flags) & PCI_HW_INIT) {
		if (hw_override) {
			return (-1);
			}
		return (1);
		}
	return (0);
}

/**************************************************************************/
/* Calculate the amount of memory and/or I/O required for a device.       */
/* If this is not a show_pci command, then allocate the memory and I/O.   */
/* CardBus devices need to have resources allocated from their            */
/* controller's primary bus.                                              */
/**************************************************************************/

static	int		addup_resources (Device *dev_ptr, uint32_t flags, int show_pci,
								int cardbus_device, int next_attach)

{
uint8_t		bus, a_bus;
BUSMAP		*map;
Device		*tmp_dev;
int			i, device_enabled = 0;
int			status, reg_idx = 0;
uint32_t	io_size, mem_size, align, pref_align;
uint32_t	pref_size, flg;
uint64_t	pref_addr, mem_addr, io_addr;
struct		BaseRegister *BaseReg, alloc_io_reg, alloc_mem_reg;
struct		{
			uint64_t	size;
			int			reg;
			} reg_sort [7];

	bus = dev_ptr->BusNumber;
	a_bus = bus;
	if (bus) {
		if ((map = find_bus (bus)) != NULL) {
			tmp_dev = map->bdev;
			a_bus = ((tmp_dev->HeaderType & 2) ? map->primary_bus : bus);	/* CardBus controller? */
			if (verbose)
				pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "%s: Header Type %x",
					__FUNCTION__, tmp_dev->HeaderType);
			}
		}
	io_size = mem_size = pref_size = align = 0;
	mem_addr = io_addr = pref_addr = 0;
	pref_align = 0;

	for (i = 0, flg = PCI_INIT_BASE0; i < (cardbus_device ? 1 : 6); i++, flg <<= 1) {
		if (!(flags & flg))				 //Test user enable flags
			continue;
		BaseReg = &dev_ptr->BaseReg [i];
		if (!BaseReg->Flags)
			continue;
		reg_sort [reg_idx].size = BaseReg->Size;
		reg_sort [reg_idx++].reg = i;
		device_enabled |= (BaseReg->Flags & PCI_DEVICE_ENABLED);
		if (BaseReg->Address < ((BaseReg->Flags & PCI_IO_SPACE) ? 4 : 16))
			device_enabled = 0;			//Temporary fix!!!!!
		if (! device_enabled && ! show_pci && ! next_attach) {
			if (BaseReg->Flags & PCI_IO_SPACE) {
				io_size += ((BaseReg->Size < 256) ? 256 : BaseReg->Size);
				}
			else {		//Must be memory
				if (BaseReg->Flags & PCI_MEM_PREFETCH) {
					pref_size += BaseReg->Size;
					if (BaseReg->Size > pref_align)
						pref_align = BaseReg->Size;
					}
				else {
					if (BaseReg->Size > align) {
						align = BaseReg->Size;
						}
					mem_size += BaseReg->Size;
					}
				}
			}
		}
	if (flags & PCI_INIT_ROM) {
		BaseReg = &dev_ptr->ROMBase;
		if (BaseReg->Flags & PCI_ROM_SPACE && ! cardbus_device) {
			int		d1 = (BaseReg->Flags & PCI_DEVICE_ENABLED);
			if (BaseReg->Address < 2)
				d1 = 0;
			if (! d1 && ! show_pci && ! next_attach) {
				if (BaseReg->Size > align) {
					align = BaseReg->Size;
					}
				mem_size += BaseReg->Size;
				reg_sort [reg_idx].size = BaseReg->Size;
				reg_sort [reg_idx++].reg = 6;
				}
			}
		}

	if (io_size) {
		memset ((char *) &alloc_io_reg, 0, sizeof (struct BaseRegister));
		alloc_io_reg.Flags = PCI_IO_SPACE;
		alloc_io_reg.Size = io_size;
		if ((status = pci_alloc_resource (&alloc_io_reg, 0, a_bus)) != PCI_SUCCESS) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Alloc I/O failed", __FUNCTION__);
			return (status);
			}
		io_addr = alloc_io_reg.Address;
		dev_ptr->io_alloc.Address = io_addr;
		dev_ptr->io_alloc.Size = io_size;
		dev_ptr->io_alloc.Flags = PCI_IO_SPACE;
		}

	if (mem_size) {
		memset ((char *) &alloc_mem_reg, 0, sizeof (struct BaseRegister));
		alloc_mem_reg.Flags = PCI_MEM_SPACE;
		alloc_mem_reg.Size = mem_size;
		if ((status = pci_alloc_resource (&alloc_mem_reg, align, a_bus)) != PCI_SUCCESS) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Alloc mem failed", __FUNCTION__);
			return (status);
			}
		mem_addr = alloc_mem_reg.Address;
		dev_ptr->mem_alloc.Address = mem_addr;
		dev_ptr->mem_alloc.Size = mem_size;
		dev_ptr->mem_alloc.Flags = PCI_MEM_SPACE;
		}

	if (pref_size) {
		memset ((char *) &alloc_mem_reg, 0, sizeof (struct BaseRegister));
		alloc_mem_reg.Flags = PCI_MEM_PREFETCH;
		alloc_mem_reg.Size = pref_size;
		if ((status = pci_alloc_resource (&alloc_mem_reg, pref_align, a_bus)) != PCI_SUCCESS) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Alloc pref failed", __FUNCTION__);
			return (status);
			}
		pref_addr = alloc_mem_reg.Address;
		dev_ptr->pref_alloc.Address = pref_addr;
		dev_ptr->pref_alloc.Size = pref_size;
		dev_ptr->pref_alloc.Flags = PCI_MEM_PREFETCH;
		}

	//Sort the BaseRegs in descending order	
	while (1) {
		int			treg, swap = 0;
		uint64_t	tsize;
		for (i = 0; i < reg_idx - 1; i++) {
			if (reg_sort [i].size < reg_sort [i + 1].size) {
				treg = reg_sort [i].reg;
				tsize = reg_sort [i].size;
				reg_sort [i].size = reg_sort [i + 1].size;
				reg_sort [i].reg = reg_sort [i + 1].reg;
				reg_sort [i + 1].size = tsize;
				reg_sort [i + 1].reg = treg;
				swap = 1;
				}
			}
		if (! swap)
			break;
		}


	if ((mem_size || io_size || pref_size) && ! show_pci) {
		for (i = 0; i < reg_idx; i++) {
			int		reg = reg_sort [i].reg;
			if (reg == 6)
				BaseReg = &dev_ptr ->ROMBase;
			else
				BaseReg = &dev_ptr->BaseReg [reg];
			if (verbose)
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Reg %d - Size %x", reg, BaseReg->Size);
			if (BaseReg->Address < ((BaseReg->Flags & PCI_IO_SPACE) ? 4 : 16) && BaseReg->Size) {
				if (BaseReg->Flags & PCI_IO_SPACE) {
					BaseReg->Address |= io_addr;	//Keep flags
					if (cardbus_device)
						BaseReg->Flags |= PCI_DEVICE_ENABLED;
					io_addr += (BaseReg->Size < 256) ? 256 : BaseReg->Size;
					io_size -= (BaseReg->Size < 256) ? 256 : BaseReg->Size;
					}
				else {
					if (BaseReg->Flags & PCI_MEM_PREFETCH) {
						BaseReg->Address |= pref_addr;	//Keep flags
						if (cardbus_device)
							BaseReg->Flags |= PCI_DEVICE_ENABLED;
						pref_addr += BaseReg->Size;
						pref_size -= BaseReg->Size;
						}
					else {
						BaseReg->Address |= mem_addr;	//Keep flags
						if (cardbus_device)
							BaseReg->Flags |= PCI_DEVICE_ENABLED;
						mem_addr += BaseReg->Size;
						mem_size -= BaseReg->Size;
						}
					}
				}
			}
		}
	return (PCI_SUCCESS);
}

/**************************************************************************/
/* get config of logical device                                           */
/**************************************************************************/

static	Device	*PCI_attach_device (void *handle, uint32_t flags, uint16_t idx,
						struct pci_dev_info *ConfigBuffer, DEV_LIST *list,
						int *local_errno)

{
Device 		*dev_ptr;
BUSMAP		*map;
struct		BaseRegister *BaseReg;
uint64_t	Address;
uint32_t	ll_Address;
uint16_t	vendor, device, pci_flg;
uint16_t	vendor_mask, device_mask;
int			i, status, device_enabled = 0;
int			show_pci = 0, next_attach = 0;
int			cardbus_device = 0, re_attach = 0;
uint32_t	flg;
uint32_t	irqlist [SERV_MAX_IRQS];
uint8_t		BusNumber, bus;
uint8_t		DevFuncNumber, devfn;
int			mgr_attach;

	*local_errno = EOK;
	mgr_attach = (flags & PCI_MGR_ATTACH) ? 1 : 0;
	if (! (flags & (PCI_INIT_ALL | PCI_INIT_ROM))) {	//Special case for show_pci. Display all information but don't enable device
		show_pci = 1;
		}
	if (verbose > 2) {
		pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "%s: handle %p - flags %x - idx %x", __FUNCTION__, handle, flags, idx);
		pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "%s: vendor %x - device %x", __FUNCTION__, ConfigBuffer->VendorId, ConfigBuffer->DeviceId);
		}
	if (handle) {
		if ((dev_ptr = validate_handle ((Device *) handle)) == NULL) {
			if (verbose)
				pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Invalid handle", __FUNCTION__);
			*local_errno = EINVAL;
			return (NULL);
			}
		if ((status = check_flags (dev_ptr, flags)) == -1) {
			if (verbose > 2)
				pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "Path1");
			*local_errno = EINVAL;
			return (NULL);
			}
		else {
			if (status) {
				if (verbose > 2)
					pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "Path2");
				pci_flg = off_pci (dev_ptr->BusNumber, dev_ptr->DevFuncNumber);
				device_enabled = re_attach = 1;
				dev_ptr->LockFlg++;			/* set device as locked */
				list->lockflg++;
				}
			else {
				if (verbose > 2)
					pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "Path3");
				fill_config (ConfigBuffer, dev_ptr);
				fill_base (ConfigBuffer, dev_ptr);
				dev_ptr->LockFlg++;			/* set device as locked */
				list->lockflg++;
				for (i = 0; i < NUM_DEVICES; i++) {
					if (list->dev_list [i] == dev_ptr) {
						break;
						}
					if (list->dev_list [i] == NULL) {
						list->dev_list [i] = dev_ptr;
						list->num_devices++;
						break;
						}
					}
				return (dev_ptr);
				}
			}
		}
	else {
		device = ConfigBuffer->DeviceId;
		vendor = ConfigBuffer->VendorId;

		switch (flags & PCI_SEARCH_MASK) {
			case	PCI_SEARCH_VEND:
				vendor_mask = 0xffff;
				if (flags & PCI_MASK_VENDDEV) {
					vendor_mask = ConfigBuffer->SubsystemVendorId;
					}
				dev_ptr = search_vendor (vendor, vendor_mask, idx);
				break;
			case	PCI_SEARCH_VENDEV:
				vendor_mask = device_mask = 0xffff;
				if (flags & PCI_MASK_VENDDEV) {
					vendor_mask = ConfigBuffer->SubsystemVendorId;
					device_mask = ConfigBuffer->SubsystemId;
					}
				dev_ptr = search_venddev (vendor, device, vendor_mask, device_mask, idx);
				break;
			case	PCI_SEARCH_CLASS:
				dev_ptr = search_class (ConfigBuffer->Class, idx);
				break;
			case	PCI_SEARCH_BUSDEV:
				dev_ptr = search_busdev (ConfigBuffer->BusNumber, ConfigBuffer->DevFunc);
				break;
			default:
				if (device || vendor) {
					vendor_mask = device_mask = 0xffff;
					if (flags & PCI_MASK_VENDDEV) {
						vendor_mask = ConfigBuffer->SubsystemVendorId;
						device_mask = ConfigBuffer->SubsystemId;
						}
					dev_ptr = search_venddev (vendor, device, vendor_mask, device_mask, idx);
					}
				else {
					if (ConfigBuffer->Class) {
						dev_ptr = search_class (ConfigBuffer->Class, idx);
						}
					else {
						dev_ptr = search_busdev (ConfigBuffer->BusNumber, ConfigBuffer->DevFunc);
						}
					}
				}

		if (dev_ptr == NULL) {
			*local_errno = ENODEV;
			if (verbose > 2)
				pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: No device", __FUNCTION__);
			return (NULL);
			}
		else {
			if (dev_ptr->LockFlg) {
				if (!mgr_attach && !show_pci) {
					if ((dev_ptr->AttachFlags & PCI_SHARE)) {
						if (!(flags & PCI_SHARE)) {
							*local_errno = EBUSY;
							}
						else {
							next_attach = 1;
							}
						}
					else {
						*local_errno = EBUSY;
						}
					}
				else {
					next_attach = 1;
					}
					
				}
			}
		}
	if (*local_errno != EOK) {
		if (verbose > 2)
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Busy", __FUNCTION__);
		return (NULL);
		}

	if (! dev_ptr->MgrAttach) {
		dev_ptr->MgrAttach = (uchar_t) mgr_attach;
		}
	else {
		next_attach = 1;
		}

	BusNumber = dev_ptr->BusNumber;
	DevFuncNumber = dev_ptr->DevFuncNumber;
	fill_config (ConfigBuffer, dev_ptr);
	last_device = dev_ptr;			/* Cache the last device info */
	clast_bus = BusNumber;
	clast_devfn = (uint8_t) DevFuncNumber;

	if (dev_ptr->ClassCode == (PCI_CLASS_BRIDGE | PCI_SUBCLASS_BRIDGE_CARDBUS)) {
		cardbus_device = 1;
		}

	if (show_pci) {
		flags = PCI_INIT_ALL | PCI_INIT_ROM | flags;
		}
	else {
		if (dev_ptr->PmState & 0x03) {
			if (power_up_device (BusNumber, DevFuncNumber, dev_ptr)) {
				*local_errno = EIO;
				return (NULL);
				}
			}
		}

/* First add up the size of all active registers, so that one */
/* chunk of IO and/or Memory space can be allocated. */

	if ((*local_errno = addup_resources (dev_ptr, flags, show_pci, cardbus_device, next_attach)) != PCI_SUCCESS) {
		return (NULL);
		}

	for (i = 0, flg = PCI_INIT_BASE0, pci_flg = 0; i < (cardbus_device ? 1 : 6); i++, flg <<= 1) {
		if (!(flags & flg))				 //Test user enable flags
			continue;
		BaseReg = &dev_ptr->BaseReg [i];
		if (! BaseReg->Size)
			continue;
		(*pci_drivers->entry->map_addr) (ll_handle, BaseReg->Address, &Address, (uint32_t) (BaseReg->Flags | PCI_MAP_ADDR_PCI));
		if (BaseReg->Flags & PCI_IO_SPACE) {
			ConfigBuffer->PciBaseAddress [i] = Address;
			ConfigBuffer->CpuBaseAddress [i] = BaseReg->Address;
			ConfigBuffer->BaseAddressSize [i] = (uint32_t) BaseReg->Size;
			pci_flg |= PCI_ISPACEEN;
			}
		else if ((BaseReg->Flags & PCI_MEM_SPACE) || (BaseReg->Flags & PCI_MEM_PREFETCH)) {
			ConfigBuffer->PciBaseAddress [i] = Address;
			ConfigBuffer->CpuBaseAddress [i] = BaseReg->Address;
			ConfigBuffer->BaseAddressSize [i] = (uint32_t) BaseReg->Size;
			pci_flg |= PCI_MSPACEEN;
			}
		if (! show_pci) {
			ll_Address = (uint32_t) Address;
			if (PCI_write_config32 (BusNumber, DevFuncNumber, offsetof (struct _pci_config_regs, Base_Address_Regs [i]), 1, &ll_Address) != PCI_SUCCESS) {
				*local_errno = ENXIO;
				if (verbose > 2)
					pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Write failed %d", __FUNCTION__, i);
				return (NULL);
				}
			}
		}
	BaseReg = &dev_ptr->ROMBase;
	if ((BaseReg->Flags & PCI_ROM_SPACE) && (flags & PCI_INIT_ROM) && !cardbus_device) {
		(*pci_drivers->entry->map_addr) (ll_handle, BaseReg->Address, &Address, (uint32_t) (BaseReg->Flags | PCI_MAP_ADDR_PCI));
		ConfigBuffer->PciRom = Address;
		ConfigBuffer->CpuRom = BaseReg->Address;
		ConfigBuffer->RomSize = (uint32_t) BaseReg->Size;
		if (! show_pci) {
			ll_Address = ((uint32_t) Address | 1);	//Set address decode enable
			if (PCI_write_config32 (BusNumber, DevFuncNumber, offsetof (struct _pci_config_regs, ROM_Base_Address), 1, &ll_Address) != PCI_SUCCESS) {
				*local_errno = ENXIO;
				if (verbose > 2)
					pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Write failed ROM", __FUNCTION__);
				return (NULL);
				}
			}
		pci_flg |= PCI_MSPACEEN;
		}

	if (show_pci || next_attach) {
		ConfigBuffer->Irq = dev_ptr->IrqNumber;
		}
	else {
		int		num_irq = 0;

		ConfigBuffer->Irq = 0xff;
		if (dev_ptr->IntPin && (flags & PCI_INIT_IRQ) && irq_ok (BusNumber, DevFuncNumber)) {
			bus = BusNumber;
			devfn = DevFuncNumber;
			if (devfn & 0x07 && dev_ptr->IntPin == 1 && is_not_bridge (bus, devfn)
				&& same_int (bus, devfn)) {	//Functions on same IntPin
			/* Changed this from a read_config to avail_irq, as interrupts on */
			/* some boards are larger than 8-bits. */
				num_irq = SERV_MAX_IRQS;
				if ((*pci_drivers->entry->avail_irq) (ll_handle, bus, (devfn & 0xf8), irqlist, (uint32_t *)&num_irq) != PCI_SUCCESS) {
					*local_errno = ENXIO;
					if (verbose > 2)
						pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: No irq", __FUNCTION__);
					return (NULL);
					}
				dev_ptr->IrqNumber = irqlist [0];
				}
			else {
				if (dev_ptr->bmap && ! cardbus_device) {
					// For CardBus devices, the IRQ has to be programmed into the CardBus controller, not the card.
					if (dev_ptr->bmap->bdev->ClassCode == (PCI_CLASS_BRIDGE | PCI_SUBCLASS_BRIDGE_CARDBUS)) {
						bus = dev_ptr->bmap->bdev->BusNumber;
						devfn = dev_ptr->bmap->bdev->DevFuncNumber;
						cardbus_device = 2;
						}
					}
				num_irq = SERV_MAX_IRQS;
				if ((*pci_drivers->entry->avail_irq) (ll_handle, bus, devfn, irqlist, (uint32_t *)&num_irq) != PCI_SUCCESS) {
					*local_errno = ENXIO;
					if (verbose > 2)
						pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: No irq2", __FUNCTION__);
					return (NULL);
					}
				if (cardbus_device == 2 && dev_ptr->bmap->bdev->IrqNumber) {
					dev_ptr->IrqNumber = dev_ptr->bmap->bdev->IrqNumber;
					}
				else {
					if (! next_attach && num_irq) {
						if (! (dev_ptr->IrqNumber = pci_alloc_irq (irqlist, num_irq, bus))) {
							if (num_irq != 1) {		//If the allocate failed on a list, big problem!
								*local_errno = ENXIO;
								if (verbose > 2)
									pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Alloc IRQ failed", __FUNCTION__);
								return (NULL);
								}
							else {
								dev_ptr->IrqNumber = irqlist [0];
								}
							}
						else {
							if (cardbus_device == 2) {
								dev_ptr->bmap->bdev->IrqNumber = dev_ptr->IrqNumber;
								}
							}
						}
					}
				}
			ConfigBuffer->Irq = dev_ptr->IrqNumber;
#ifdef	__X86__
			if (map_irqs && dev_ptr->IrqNumber != 0xffffffff && num_irq > 1) {
#else
			if (map_irqs && dev_ptr->IrqNumber != 0xffffffff) {
#endif
				if ((status = (*pci_drivers->entry->map_irq) (ll_handle, bus, devfn,
					dev_ptr->IrqNumber, dev_ptr->IntPin + 9, PCI_MAP_IRQ)) != PCI_SUCCESS) {
					if (verbose > 2)
						pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Map IRQ %x failed %x!",
							__FUNCTION__, dev_ptr->IrqNumber, status);
					}
				}
				
			{
			uint8_t		irq = (uint8_t) dev_ptr->IrqNumber;
			if (PCI_write_config8 (bus, devfn, offsetof (struct _pci_config_regs, Interrupt_Line), 1, &irq) != PCI_SUCCESS) {
				*local_errno = ENXIO;
				if (verbose > 2)
					pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Write IRQ failed", __FUNCTION__);
				return (NULL);
				}
			}
			}
		}

	PCI_translation (ll_handle, ConfigBuffer);

	if (flags & PCI_MASTER_ENABLE)
		pci_flg |= PCI_MASTEREN;

	if ((dev_ptr->ClassCode & 0xff0000) == PCI_CLASS_DISPLAY) {
		uint16_t	cr;
		PCI_read_config16 (BusNumber, DevFuncNumber, offsetof (struct _pci_config_regs, Command), 1, (char *)&cr);
		pci_flg |= cr;
		pci_flg |= PCI_COMMAND_IO_ENABLE;		//Always enable I/O on video adapters
		}

	if (! show_pci) {
		on_pci (BusNumber, DevFuncNumber, pci_flg);
		if (! dev_ptr->AttachFlags) {
			dev_ptr->AttachFlags |= flags;	/*Save the flag settings */
			if (! mgr_attach && ! re_attach) {
				dev_ptr->LockFlg++;			/* set device as locked */
				list->lockflg++;
				}
			if (!dev_ptr->Persistent) {
				if (flags & PCI_PERSIST) {
					dev_ptr->Persistent = 1;
					for (i = 0; i < 6; i++) {
						BaseReg = &dev_ptr->BaseReg [i];
						if (BaseReg->Flags)
							BaseReg->Flags |= PCI_DEVICE_ENABLED;
						}
					BaseReg = &dev_ptr->ROMBase;
					if (BaseReg->Flags)
						BaseReg->Flags |= PCI_DEVICE_ENABLED;
					}
				}
			}
		else {
			dev_ptr->AttachFlags |= flags;
			dev_ptr->LockFlg++;			/* set device as locked */
			list->lockflg++;
			}
		if (! list->num_devices) {
			list->dev_list [0] = dev_ptr;
			}
		else {
			for (i = 0; i < NUM_DEVICES; i++) {
				if (list->dev_list [i] == dev_ptr) {
					break;
					}
				if (list->dev_list [i] == NULL) {
					list->dev_list [i] = dev_ptr;
					break;
					}
				}
			}
		list->num_devices++;
		}
	else {		// I really don't think this is necessary!
		if (! re_attach) {
			dev_ptr->LockFlg++;			/* set device as locked */
			list->lockflg++;
			}
		dev_ptr->AttachFlags |= flags;
		if (handle)
			on_pci (BusNumber, DevFuncNumber, pci_flg);
		}
	if (flags & PCI_BUS_INFO) {
		if ((map = find_bus (BusNumber)) != NULL) {
			ConfigBuffer->BusIoStart = map->io_start;
			ConfigBuffer->BusIoEnd = map->io_end;
			ConfigBuffer->BusMemStart = map->mem_start;
			ConfigBuffer->BusMemEnd = map->mem_end;
			if (verbose > 2) {
				pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "I/O Start %llx - Mem Start %llx", map->io_start, map->mem_start);
				}
			}
		}

	return (dev_ptr);
}

/**************************************************************************/
/* Release all the resources associated with a particular device.         */
/**************************************************************************/

static	int		release_resources (Device *device)

{
struct 		BaseRegister *BaseReg;
uint32_t	address = 0;
int			i, enabled = 0;

/* This next line was commented out to fix a problem with the MyCable box */
#ifdef	__X86__
	enabled = ((device->ClassCode & 0xff0000) == PCI_CLASS_DISPLAY) ? 1 : 0;
#endif
	for (i = 0; i < 6; i++) {
		BaseReg = &device->BaseReg [i];
		enabled |= BaseReg->Flags & PCI_DEVICE_ENABLED;
		if (BaseReg->Address && !enabled) {
			PCI_write_config32 (device->BusNumber, device->DevFuncNumber,
				offsetof (struct _pci_config_regs, Base_Address_Regs [i]), 1, (char *)&address);
			BaseReg->Address &= PCI_IS_IO (BaseReg->Address) ? 0x3 : 0x0f;
			}
		}
	BaseReg = &device->ROMBase;
	enabled |= BaseReg->Flags & PCI_DEVICE_ENABLED;
	if (BaseReg->Address && !enabled) {
		PCI_write_config32 (device->BusNumber, device->DevFuncNumber,
			offsetof (struct _pci_config_regs, ROM_Base_Address), 1, (char *)&address);
		BaseReg->Address &= 0x7fe;
		}

	if (device->io_alloc.Address) {
		pci_free_resource (&device->io_alloc, device->BusNumber);
		}
	if (device->mem_alloc.Address) {
		pci_free_resource (&device->mem_alloc, device->BusNumber);
		}
	if (device->pref_alloc.Address) {
		pci_free_resource (&device->pref_alloc, device->BusNumber);
		}

	// Don't release the IRQ if this device is attached to a CardBus controller
	if (device->bmap->bdev->ClassCode == (PCI_CLASS_BRIDGE | PCI_SUBCLASS_BRIDGE_CARDBUS)) {
		return (1);		//Leave device enabled
		}

	if (device->IrqNumber != 0 && device->IrqNumber != 0xffffffff) {
		pci_free_irq (device->IrqNumber);
		}
	return (enabled);
}

/**************************************************************************/
/* release config of device                                               */
/**************************************************************************/

static	int		PCI_detach_device (void *handle, DEV_LIST *list, int close_flag)

{
Device		*device = DeviceHead;
int			i, status, enabled = 1;
uint32_t	flags = 0;

	status = PCI_DEVICE_NOT_FOUND;
	while (device) {
		if (device == (Device *) handle) {
			if (device->LockFlg) {
				if (list && list->lockflg) {
					if (close_flag)
						i = (list->lockflg == device->LockFlg) ? list->lockflg : 1;
					else
						i = 1;
					device->LockFlg -= i;
					list->lockflg -= i;
					}
				else {
					device->LockFlg--;
					}
				}
			else {
				if (device->MgrAttach) {
					device->MgrAttach = 0;
					}
				}
			flags = device->AttachFlags & (PCI_INIT_ALL | PCI_INIT_ROM);
			if (! device->Persistent && flags && ! device->LockFlg && ! device->MgrAttach) {
				enabled = release_resources (device);
				}
//			if (list && ! list->lockflg) {
			if (list) {
				for (i = 0; i < NUM_DEVICES; i++) {
					if (list->dev_list [i] == device) {
						list->dev_list [i] = NULL;
						list->num_devices--;
						}
					}
				}
			status = PCI_SUCCESS;
			break;
			}
		device = device->next;
		}
	if (status == PCI_SUCCESS && ! device->LockFlg && ! device->MgrAttach) {
		device->AttachFlags = 0;	//Clear attach flags if last detach
		}

	if (status == PCI_SUCCESS) {
		uint32_t	class = device->ClassCode & 0xff0000;

		if (class != PCI_CLASS_DISPLAY && class != PCI_CLASS_BRIDGE &&
			class != PCI_CLASS_PROCESSOR) {
			if (status == PCI_SUCCESS && !device->Persistent && !enabled) {
				off_pci (device->BusNumber, device->DevFuncNumber);
				}
			if ((device->PmState & 0x03) && !device->Persistent && flags)
				power_down_device (device->BusNumber, device->DevFuncNumber, device);
			}
		}
	return (status);
}

/**************************************************************************/
/* Setup I/O apertures in PCI-PCI bridges.                                */
/**************************************************************************/

static	int		setup_pci_io (uint64_t io_size)

{
rsrc_request_t	rreq;
int				allocd = 0, i, j;
uint64_t		map_addr, size;
uint8_t			bus, devfn;
BUSMAP			*map;
Device			*dev;
_pci_bridge_t	*br = pci_bridges;

	memset ((char *) &rreq, 0, sizeof (rreq));
	rreq.length = io_size;
	rreq.align = KB4;
	rreq.flags = RSRCDBMGR_IO_PORT | RSRCDBMGR_FLAG_ALIGN | RSRCDBMGR_FLAG_TOPDOWN;
	for (i = 0; i < num_pci_bridges; i++, br = br->next) {
		j = br->sec_bus;
		if ((map = find_bus (j)) == NULL) {
			return (-1);
			}
		if (map->io_reqd == 0 && map->io_start == 0)
			continue;
		if (map->io_start || map->io_end) {	/* Bridge already setup */
			continue;
			}
		if (! allocd) {
			if (rsrcdbmgr_attach (&rreq, 1) != EOK) {
				pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to allocate I/O - Start %llx - End %llx",
					__FUNCTION__, rreq.start, rreq.end);
				return (-1);
				}
			(*pci_drivers->entry->map_addr) (ll_handle, rreq.start, &map_addr, PCI_IO_SPACE | PCI_MAP_ADDR_PCI);
			if (verbose)
				pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "CPU I/O %llx - PCI I/O %llx", rreq.start, map_addr);
			allocd = 1;
			}
		size = map->io_reqd;
		while (map) {
			dev = map->bdev;
			if (dev->ClassCode == host_bridge_class)
				break;
			bus = dev->BusNumber;
			devfn = dev->DevFuncNumber;
			pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "Bridge Bus %x - devfunc %x", bus, devfn);
			if (map->io_start == 0) {
				map->io_start = map_addr;
				map->io_end = map_addr + (size - KB4 + (KB4 - 1));
				}
			else {
				if (map_addr > map->io_start) {
					map->io_end = map_addr + (size - KB4 + (KB4 - 1));
					}
				else {
					map->io_start = map_addr;
					}
				}

			if (reserve_resource (rreq.start, rreq.end, RSRCDBMGR_IO_PORT, map->secondary_bus, 0)) {
				break;
				}

			if (configure_bridge_io (bus, devfn, map->io_start, map->io_end)) {
				return (-1);
				}
			map = map->prev;
			}
		map_addr += size;
		}
	if (allocd) {
		if (rsrcdbmgr_detach (&rreq, 1))
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: I/O detach failed", __FUNCTION__);
		}

	return (0);
}

/**************************************************************************/
/* Setup prefetchable memory apertures in PCI-PCI bridges.                */
/**************************************************************************/

static	int		setup_pci_pref (uint64_t pref_size)

{
rsrc_request_t	rreq;
int				allocd = 0, i, j;
uint64_t		map_addr, size;
uint8_t			bus, devfn;
BUSMAP			*map;
Device			*dev;
_pci_bridge_t	*br = pci_bridges;

	memset ((char *) &rreq, 0, sizeof (rreq));
	rreq.length = pref_size;
	rreq.align = MEG1;
	rreq.flags = RSRCDBMGR_PCI_MEMORY | RSRCDBMGR_FLAG_ALIGN | RSRCDBMGR_FLAG_TOPDOWN;
	for (i = 0; i < num_pci_bridges; i++, br = br->next) {
		j = br->sec_bus;
		if ((map = find_bus (j)) == NULL)
			return (-1);
		if (map->pref_reqd == 0 && map->prefetch_mem_start == 0)
			continue;
		if (map->prefetch_mem_start || map->prefetch_mem_end) {	/* Bridge already setup */
			continue;
			}
		if (! allocd) {
			if (rsrcdbmgr_attach (&rreq, 1) != EOK) {
				return (-1);
				}
			(*pci_drivers->entry->map_addr) (ll_handle, rreq.start, &map_addr, PCI_MEM_SPACE | PCI_MAP_ADDR_PCI);
			if (verbose)
				pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "CPU memory %llx - PCI memory %llx", rreq.start, map_addr);
			allocd = 1;
			}
		size = map->pref_reqd;
		while (map) {
			dev = map->bdev;
			if (dev->ClassCode == host_bridge_class)
				break;
			bus = dev->BusNumber;
			devfn = dev->DevFuncNumber;
			if (verbose)
				pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1,
					"Bridge Bus %x - devfunc %x - Pref start %llx - end %llx", bus, devfn, map->prefetch_mem_start, map->prefetch_mem_end);
			if (map->prefetch_mem_start == 0) {
				map->prefetch_mem_start = map_addr;
				map->prefetch_mem_end = map_addr + (size - MEG1 + (MEG1 - 1));
				}
			else {
				if (map_addr > map->prefetch_mem_start) {
					map->prefetch_mem_end = map_addr + (size - MEG1 + (MEG1 - 1));
					}
				else {
					map->prefetch_mem_start = map_addr;
					}
				}

			if (reserve_resource (rreq.start, rreq.end, RSRCDBMGR_PCI_MEMORY, map->secondary_bus, 1)) {
				break;
				}

			if (configure_bridge_pref (bus, devfn, map->prefetch_mem_start, map->prefetch_mem_end)) {
				return (-1);
				}
			map = map->prev;
			}
		map_addr += size;
		}
	if (allocd) {
		if (rsrcdbmgr_detach (&rreq, 1))
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Pref detach failed", __FUNCTION__);
		}

	return (0);
}


/**************************************************************************/
/* Setup memory apertures in PCI-PCI bridges.                             */
/**************************************************************************/

static	int		setup_pci_mem (uint64_t mem_size)

{
rsrc_request_t	rreq;
int				allocd = 0, i, j;
uint64_t		map_addr, size;
uint8_t			bus, devfn;
BUSMAP			*map;
Device			*dev;
_pci_bridge_t	*br = pci_bridges;

	memset ((char *) &rreq, 0, sizeof (rreq));
	rreq.length = mem_size;
	rreq.align = MEG1;
	rreq.flags = RSRCDBMGR_PCI_MEMORY | RSRCDBMGR_FLAG_ALIGN | RSRCDBMGR_FLAG_TOPDOWN;
	for (i = 0; i < num_pci_bridges; i++, br = br->next) {
		j = br->sec_bus;
		if ((map = find_bus (j)) == NULL)
			return (-1);
		if (map->mem_reqd == 0 && map->mem_start == 0)
			continue;
		if (map->mem_start || map->mem_end) {	/* Bridge already setup */
			continue;
			}
		if (! allocd) {
			if (rsrcdbmgr_attach (&rreq, 1) != EOK) {
				return (-1);
				}
			(*pci_drivers->entry->map_addr) (ll_handle, rreq.start, &map_addr, PCI_MEM_SPACE | PCI_MAP_ADDR_PCI);
			if (verbose)
				pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "CPU memory %llx - PCI memory %llx", rreq.start, map_addr);
			allocd = 1;
			}
		size = map->mem_reqd;
		while (map) {
			dev = map->bdev;
			if (dev->ClassCode == host_bridge_class)
				break;
			bus = dev->BusNumber;
			devfn = dev->DevFuncNumber;
			pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "Bridge Bus %x - devfunc %x", bus, devfn);
			if (map->mem_start == 0) {
				map->mem_start = map_addr;
				map->mem_end = map_addr + (size - MEG1 + (MEG1 - 1));
				}
			else {
				if (map_addr > map->mem_start) {
					map->mem_end = map_addr + (size - MEG1 + (MEG1 - 1));
					}
				else {
					map->mem_start = map_addr;
					}
				}
			if (reserve_resource (rreq.start, rreq.end, RSRCDBMGR_PCI_MEMORY, map->secondary_bus, 0)) {
				break;
				}

			if (configure_bridge_mem (bus, devfn, map->mem_start, map->mem_end)) {
				return (-1);
				}
			map = map->prev;
			}
		map_addr += size;
		}
	if (allocd) {
		if (rsrcdbmgr_detach (&rreq, 1))
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Mem detach failed", __FUNCTION__);
		}

	return (0);
}

/**************************************************************************/
/* Initialise all variables and scan the PCI bus(ses) for devices.        */
/**************************************************************************/

static	int		PCI_Startup (void)

{
int			i, status;
Device		*device;
pci_bus_t	*bus;
BUSMAP		*map, *peer;
uint64_t	io_size, mem_size, pref_size;
uint64_t	tot_mem_size, tot_mem_reqd;
#ifdef	__X86__
uint32_t	tmp;

	(*pci_drivers->entry->version) (ll_handle, &tmp, &version, &hardware);
#else
	version = 0x210;
	hardware = 0;
#endif

	lastbus = endbus = cardbus = 0;
	numdevices = 0;
	listcount = 0;
	clast_bus = clast_devfn = 0xff;
	DeviceHead = DeviceTail = NULL;
	io_size = mem_size = pref_size = 0;

	find_ranges ();

	if (scan_all () != PCI_SUCCESS || lastbus == 0) {		//No PCI found!
		return (-1);
		}

	/* Scan through all the devices and configure the bridges */

	for (i = 1; i < lastbus; i++) {
		if ((map = find_bus (i)) == NULL)
			continue;
		if (map->io_reqd) {
			if (map->io_reqd < KB4) {
				io_size += KB4;
				map->io_reqd = KB4;
				}
			else {
				if (map->io_reqd % KB4) {
					map->io_reqd += KB4;
					map->io_reqd &= ~(KB4 - 1);
					}
				}
			io_size += map->io_reqd;
			}
		if (map->mem_reqd) {
			if (map->mem_reqd < MEG1) {
				mem_size += MEG1;
				map->mem_reqd = MEG1;
				}
			else {
				if (map->mem_reqd % MEG1) {
					map->mem_reqd += MEG1;
					map->mem_reqd &= ~(MEG1 - 1);
					}
				}
			mem_size += map->mem_reqd;
			}
		if (map->pref_reqd) {
			if (map->pref_reqd < MEG1) {
				pref_size += MEG1;
				map->pref_reqd = MEG1;
				}
			else {
				if (map->pref_reqd % MEG1) {
					map->pref_reqd += MEG1;
					map->pref_reqd &= ~(MEG1 - 1);
					}
				}
			pref_size += map->pref_reqd;
			}
		}
	if (verbose > 2)
		pci_slogf (_SLOGC_PCI, _SLOG_DEBUG1, "I/O reqd %llx - Mem reqd %llx - Pref reqd %llx",
			io_size, mem_size, pref_size);

	tot_mem_size = mem_cpu_high - mem_cpu_low;
	tot_mem_reqd = mem_size + pref_size;
	if (tot_mem_reqd > tot_mem_size) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "Memory required (%llx) is > memory size (%llx)", tot_mem_reqd, tot_mem_size);
		}

	if (io_size) {
		if (setup_pci_io (io_size))
			return (-1);
		}

	if (mem_size) {
		if (setup_pci_mem (mem_size))
			return (-1);
		}

	if (pref_size) {
		if (setup_pci_pref (pref_size))
			return (-1);
		}

	device = DeviceHead;
	while (device) {
		if (((device->ClassCode & 0xff0000) == PCI_CLASS_BRIDGE) && (device->DevFuncNumber & 7) == 0) {
			if ((map = find_bus (device->bmap->secondary_bus)) == NULL)
				return (-1);
			bus = map->pci_bus;
			if (verbose > 2) {
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Busmap Pri %x - Sec %x - Sub %x", map->primary_bus,
					map->secondary_bus, map->subordinate_bus);
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "I/O Start %llx - I/O End %llx", map->io_start,
					map->io_end);
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Mem Start %llx - Mem End %llx", map->mem_start,
					map->mem_end);
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "I/O %llx - Mem %llx - Pref %llx", map->io_reqd,
					map->mem_reqd, map->pref_reqd);
				}
			if ((status = (*pci_drivers->entry->cnfg_bridge) (ll_handle,
				device->BusNumber, device->DevFuncNumber, bus)) != PCI_SUCCESS) {
				pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to configure bridge", __FUNCTION__);
				return (status);
				}
			}
		device = device->next;
		}

	if (verbose) {
		map = bus_map;
		while (map) {
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Map %p - Next %p - Prev %p - Peer %p", 
				map, map->next, map->prev, map->peer);
			pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Prim %x - Sec %x - Sub %x",
				map->primary_bus, map->secondary_bus, map->subordinate_bus);
			if (map->peer) {
				peer = map->peer;
				while (peer) {
					pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Peer %p - Next %p - Prev %p - Peer %p", 
						peer, peer->next, peer->prev, peer->peer);
					pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Prim %x - Sec %x - Sub %x",
						peer->primary_bus, peer->secondary_bus, peer->subordinate_bus);
					peer = peer->peer;
					}
				}
			map = map->next;
			}
		}

	return (PCI_SUCCESS);
}

/**************************************************************************/
/* Allocate an ocb for this open, attach a device list buffer to it.      */
/**************************************************************************/

IOFUNC_OCB_T	*PCI_OcbCalloc (resmgr_context_t *ctp, iofunc_attr_t *attr)

{
DEV_LIST		*ocb;

	if (verbose)
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "io_open for id = %x", ctp->rcvid);

	if ((ocb = calloc (1, sizeof (*ocb))) == NULL) {
		if (verbose) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: couldn't allocate %d bytes", __FUNCTION__, sizeof (*ocb));
			}
		return (NULL);
		}

	return (ocb);
}

/**************************************************************************/
/* Free the ocb allocated above as well as the device list buffer.        */
/**************************************************************************/

void	PCI_OcbFree (IOFUNC_OCB_T *list)

{
int		i;

	if (verbose) {
		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "io_close_ocb");
		}

	for (i = 0; i < NUM_DEVICES; i++) {
		if (list->dev_list [i]) {
			PCI_detach_device (list->dev_list [i], list, 1);
			}
		}

	free (list);
}

/**************************************************************************/
/* Handle user I/O messages.                                              */
/**************************************************************************/

int		PCI_IoMsg (resmgr_context_t *ctp, io_msg_t *msg, IOFUNC_OCB_T *ocb, iofunc_attr_t *attr)

{
union _pci_data *data = ((void *) (sizeof (msg->i) + sizeof (struct _pci_reply_hdr) + (char *)(&msg->i)));
uint32_t		t1 = 0, t2 = 0, t3 = 0;
uint8_t			bus = 0, devfn = 0;
int				ret, reply_length = 0;
int				local_errno, type;
Device			*handle, *device = DeviceHead;
int				(*rout)(uint8_t, uint8_t, uint32_t, uint32_t, void *);
struct			_pci_message	msg_reply;

	if (msg->i.mgrid != _IOMGR_PCI)
		return (ENOSYS);

	local_errno = EOK;
	memcpy ((char *) &msg_reply, (char *) &msg->i, msg->i.combine_len);
//	if (verbose > 2)
//		pci_slogf (_SLOGC_PCI, _SLOG_INFO, "Message type %d - id = %x", msg->i.subtype, ctp->rcvid);
	switch (msg->i.subtype) {
		case	IOM_PCI_READ_CONFIG_BYTE:
			ret = PCI_read_config8 (data->config.bus, data->config.devfunc, data->config.offset, data->config.count, (char *)&msg_reply.msg.config.buffer);
			reply_length += sizeof (struct _pci_config);
			break;
		case	IOM_PCI_READ_CONFIG_WORD:
			ret = PCI_read_config16 (data->config.bus, data->config.devfunc, data->config.offset, data->config.count, (char *)&msg_reply.msg.config.buffer);
			reply_length += sizeof (struct _pci_config);
			break;
		case	IOM_PCI_READ_CONFIG_DWORD:
			ret = PCI_read_config32 (data->config.bus, data->config.devfunc, data->config.offset, data->config.count, (char *)&msg_reply.msg.config.buffer);
			reply_length += sizeof (struct _pci_config);
			break;
		case	IOM_PCI_WRITE_CONFIG_BYTE:
			ret = PCI_write_config8 (data->config.bus, data->config.devfunc, data->config.offset, data->config.count, (char*)&data->config.buffer);
			break;
		case	IOM_PCI_WRITE_CONFIG_WORD:
			ret = PCI_write_config16 (data->config.bus, data->config.devfunc, data->config.offset, data->config.count, (char*)&data->config.buffer);
			break;
		case	IOM_PCI_WRITE_CONFIG_DWORD:
			ret = PCI_write_config32 (data->config.bus, data->config.devfunc, data->config.offset, data->config.count, (char*)&data->config.buffer);
			break;
		case	IOM_PCI_READ_CONFIG_HANDLE:
		case	IOM_PCI_WRITE_CONFIG_HANDLE:
			type = msg->i.subtype == IOM_PCI_READ_CONFIG_HANDLE ? 0 : 1;
			handle = (Device *) data->config_hdl.handle;
			rout = NULL;
			ret = PCI_DEVICE_NOT_FOUND;
			while (device) {
				if (device == handle) {
					bus = device->BusNumber;
					devfn = device->DevFuncNumber;
					switch (data->config_hdl.size) {
						case	1:		//Byte
							rout = type ? PCI_write_config8 : PCI_read_config8;
							break;
						case	2:		//Word
							rout = type ? PCI_write_config16 : PCI_read_config16;
							break;
						case	4:		//DWord
							rout = type ? PCI_write_config32 : PCI_read_config32;
							break;
						default:
							rout = NULL;
							break;
						}
					if (rout) {
						ret = (*rout)(bus, devfn, data->config_hdl.offset,
							data->config_hdl.count, (char *)&msg_reply.msg.config_hdl.buffer);
						reply_length += sizeof (struct _pci_config_hdl);
						}
					break;
					}
				device = device->next;
				}
			break;
		case	IOM_PCI_FIND_DEVICE:
			ret = PCI_find_device (data->device.deviceid, data->device.vendorid, data->device.index, &bus, &devfn);
			msg_reply.msg.device.busnum = bus;
			msg_reply.msg.device.devfuncnum = devfn;
			reply_length += sizeof (struct _pci_device);
			break;
		case	IOM_PCI_FIND_CLASS:
			ret = PCI_find_class (data->class.class, data->class.index, &bus, &devfn);
			msg_reply.msg.class.busnum = bus;
			msg_reply.msg.class.devfuncnum = devfn;
			reply_length += sizeof (struct _pci_class);
			break;
		case	IOM_PCI_BIOS_PRESENT:
			ret = pci_bios_present (&t1, &t2, &t3);
			msg_reply.msg.present.lastbus = t1;
			msg_reply.msg.present.version = t2;
			msg_reply.msg.present.hardware = t3;
			reply_length += sizeof (struct _pci_present);
			break;
		case	IOM_PCI_MAP_ADDRESS: {
			uint64_t	addr;

			ret = (*pci_drivers->entry->map_addr) (ll_handle, data->map.pciaddr, &addr, data->map.flags);
			msg_reply.msg.map.cpuaddr = addr;
			reply_length += sizeof (struct _pci_map);
			}
			break;
		case	IOM_PCI_GET_VERSION:
			ret = PCI_GetVersion (&msg_reply.msg.version.version, &msg_reply.msg.version.numdevices);
			reply_length += sizeof (struct _pci_version);
			break;
		case	IOM_PCI_ATTACH_DEVICE:
			msg_reply.msg.lock.handle = PCI_attach_device (data->lock.handle,
				data->lock.flags, data->lock.idx, &msg_reply.msg.lock.configbuffer,
				ocb, &local_errno);
			reply_length += msg_reply.msg.lock.handle ? sizeof (struct _pci_attach) : 0;
			ret = reply_length ? PCI_SUCCESS : PCI_DEVICE_NOT_FOUND;
			break;
		case	IOM_PCI_DETACH_DEVICE:
			ret = PCI_detach_device (data->unlock.handle, ocb, 0);
			break;
		case	IOM_PCI_MAP_IRQ:
			ret = (*pci_drivers->entry->map_irq) (ll_handle, data->map_irq.bus,
					data->map_irq.devfunc, data->map_irq.intno, data->map_irq.intpin,
					PCI_MAP_IRQ);
			break;
		case	IOM_PCI_RESCAN_BUS:
			ret = rescan ();
			break;
		case	IOM_PCI_IRQ_ROUTING_OPTIONS:
			ret = (*pci_drivers->entry->routing_options) ((struct _pci_route_buffer *) &msg_reply.msg.route_opt.buf_size,
					(uint16_t *) &msg_reply.msg.route_opt.irq_info);
			reply_length += (ret == PCI_SUCCESS) ? sizeof (struct _pci_route_opt) : 0;
			break;
		default:
			local_errno = ENOSYS;
			ret = PCI_SUCCESS;
			break;
		}
	if (local_errno != EOK)
		return (local_errno);
	_RESMGR_STATUS (ctp, local_errno);
	msg_reply.rep_hdr.reply_status = ret;
	reply_length += sizeof (struct _pci_reply_hdr);
	msg_reply.rep_hdr.reply_length = reply_length;
	msg_reply.msg_hdr.i.combine_len = reply_length + sizeof (io_msg_t);
	SETIOV (ctp->iov, &msg_reply, reply_length + sizeof (io_msg_t));
	MsgReplyv (ctp->rcvid, ret, ctp->iov, 1);
	return (_RESMGR_NOREPLY);
}

/**************************************************************************/
/*                                                                        */
/**************************************************************************/

int		pci_dll_load (char *options, char *optarg)

{
void			*d;
char			dllpath [_POSIX_PATH_MAX + 1];
pdrvr_entry_t	*entry;
pdrvr_t			*pdrvr;

const struct dll_list	*l;

	if (options == NULL && optarg == NULL) {
		for (l = dll_list; l->fname != NULL; ++l) {
			if (l->fname) {
				sprintf (dllpath, "%s", l->fname);
				break;
				}
			}
		}
	else {
		if (strchr (optarg, '/') != NULL) {
			strcpy (dllpath, optarg);
			}
		else {
			sprintf (dllpath, "pci-%s.so", optarg);
			}
		}

	if ((pdrvr = calloc (1, sizeof (pdrvr_t)))) {
	  if ((d = pci_dlopen (dllpath, 0))) {
	    if ((entry = (void *) pci_dlsym (d, "pci_entry"))) {
				if (!(entry->attach)(options, &pdrvr->hdl)) {
					ll_handle = pdrvr->hdl;
					if (pci_drivers) {
						pdrvr->next = pci_drivers;
						pci_drivers->next = pdrvr;
						}
					pci_drivers = pdrvr;
					pdrvr->entry = entry;
					return (EOK);
					}
				}
			else {
				pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s dlsym failed %s", progname, optarg);
				}
			pci_dlclose (d);
			}
		else {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s unable to load %s", progname, dllpath);
			}
		}
	else {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s Unable to allocate PCI device memory", progname);
		}
		
	return (ENODEV);
}

/**************************************************************************/
/*                                                                        */
/**************************************************************************/

void	pci_signal_handler (int signo)

{
pdrvr_t		*pdrvr;

	if (signo == SIGTERM) {
		// for each driver call detach
		pdrvr = pci_drivers;
		for (; pdrvr; pdrvr = pdrvr->next) {
			if (pdrvr->entry->detach) {
				pci_slogf (_SLOGC_PCI, _SLOG_INFO, "%s shutting down pci %lx", progname, (ulong_t) pdrvr->hdl);
				pdrvr->entry->detach (pdrvr->hdl);
				}
			}
		exit (0);
		}
}

/**************************************************************************/
/*                                                                        */
/**************************************************************************/

void	pci_resmgr_loop (resmgr_context_t *ctp)

{
	while(1) {
		if ((resmgr_block (ctp)) == NULL) {
			pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: block error", __FUNCTION__);
			exit (EXIT_FAILURE);
			}
		resmgr_handler (ctp);
		}
}

/**************************************************************************/
/*                                                                        */
/**************************************************************************/

int		main (int argc, char **argv)

{
int				c, dll_loaded = 0;
iofunc_attr_t	io_attr;
resmgr_connect_funcs_t connect_funcs;
dispatch_t 		*dpp;
resmgr_attr_t 	resmgr_attr;
resmgr_context_t *ctp;
iofunc_mount_t	io_mount;
iofunc_funcs_t	io_ocb_funcs;

	pci_drivers = NULL;
	scan_buses = MAX_BUSES;

	// Enable IO capability.
	if (ThreadCtl (_NTO_TCTL_IO, NULL) == -1) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s ThreadCtl", progname);
		return (EPERM);
		}

	if((dpp = dispatch_create()) == NULL) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s: Unable to allocate dispatch context.",argv[0]);
		exit (EXIT_FAILURE);
		}

	memset(&resmgr_attr, 0, sizeof resmgr_attr);
	resmgr_attr.nparts_max = 20;
	resmgr_attr.msg_max_size = 0;

	// process args	

	while (optind < argc) {
		if ((c = getopt (argc, argv, "Bb:c:d:hmpvx")) == -1) {
			optind++;
			continue;
			}

		switch (c) {
			case	'b':
				scan_buses = atoi (optarg);
				break;
			case	'B':
				enum_xtrans_bridge = 1;
				break;

			case	'c':
				class_override = atoi (optarg);
				if ((class_override & 3) == 0) {
					pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "-c value must be 1, 2, or 3");
					exit (EXIT_FAILURE);
					}
				break;

			case	'd':
				pci_dll_load (argv [optind], optarg);
				dll_loaded = 1;
				break;

			case	'h':
				hw_override = 1;
				break;

			case	'm':
				map_irqs ^= 1;		//Flip the flag
				break;
			case	'p':
				program_display_device = 1;
				break;
			case	'v':
				verbose++;
				break;

			case	'x':
				dont_touch = 1;
				break;

			default:
				pci_slogf (_SLOGC_PCI, _SLOG_WARNING, "%s unknown option", progname);
				break;
			}
		}

	if (! dll_loaded)
		pci_dll_load (NULL, NULL);

	if (!pci_drivers) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s No dll loaded!", progname);
		exit (EXIT_FAILURE);
		}

	memset ((char *) &io_mount, 0, sizeof (io_mount));
	memset ((char *) &io_ocb_funcs, 0, sizeof (iofunc_funcs_t));
	io_ocb_funcs.nfuncs = _IOFUNC_NFUNCS;
	io_ocb_funcs.ocb_calloc = &PCI_OcbCalloc;
	io_ocb_funcs.ocb_free = &PCI_OcbFree;
	io_mount.funcs = &io_ocb_funcs;
	iofunc_attr_init (&io_attr, 0600 | S_IFCHR, NULL, NULL);
	io_attr.mount = &io_mount;
	iofunc_func_init (_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);
	io_funcs.msg = (void *) &PCI_IoMsg;
	
	// initialize configuration manager
	if (PCI_Startup () != PCI_SUCCESS) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "%s PCI_Startup failed!", progname);
		exit (EXIT_FAILURE);
		}

	if(resmgr_attach (dpp, &resmgr_attr, "/dev/pci", _FTYPE_ANY, 0, &connect_funcs, &io_funcs, &io_attr) == -1) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "resmgr_pathname_attach failed");
		exit (EXIT_FAILURE);
		}

	if((ctp = resmgr_context_alloc(dpp)) == NULL) {
		pci_slogf (_SLOGC_PCI, _SLOG_ERROR, "Could not allocate resmgr context");
		exit (EXIT_FAILURE);
		}

#if 0 	/* For debugging, keep around stdout/stderr */
	procmgr_daemon(EXIT_SUCCESS, PROCMGR_DAEMON_NOCLOSE);
#else
	procmgr_daemon(EXIT_SUCCESS, PROCMGR_DAEMON_NODEVNULL | PROCMGR_DAEMON_NOCLOSE);
#endif

	signal (SIGTERM, pci_signal_handler);
	pci_resmgr_loop (ctp);

	exit (EXIT_SUCCESS);
}

__SRCVERSION( "$URL: http://svn/product/tags/internal/bsp/nto650/ti-j5-evm/1.0.0/latest/hardware/pci/server.c $ $Rev: 655782 $" );
