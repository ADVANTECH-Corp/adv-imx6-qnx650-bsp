/*
 * $QNXLicenseC: 
 * Copyright 2010, 2011 QNX Software Systems.  
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



#include "mx6q.h"
//weilun@adv - begin
#include <fcntl.h>
//weilun@adv - end
#include <device_qnx.h>


static void mx6q_stop(struct ifnet *ifp, int disable);
static void mx6q_destroy(mx6q_dev_t *mx6q, int how);
static void mx6q_reset(mx6q_dev_t *mx6q);
static int mx6q_init(struct ifnet *ifp);
static void mx6q_rx_mbuf_free(mx6q_dev_t *mx6q);

static int mx6q_attach(struct device *, struct device *, void *);
static int mx6q_detach(struct device *, int);

static void mx6q_shutdown(void *);

static void set_phys_addr (mx6q_dev_t *mx6q);
static void get_phys_addr (mx6q_dev_t *mx6q, uchar_t *addr);


struct mx6q_arg {
	void			*dll_hdl;
	char			*options;
	int			idx;
	uint32_t		iobase;
};


CFATTACH_DECL(mx6q,
	sizeof(mx6q_dev_t),
	NULL,
	mx6q_attach,
	mx6q_detach,
	NULL);


#define NIC_PRIORITY            21

static  char *mpc_opts [] = {
	"receive",                // 0 
	"transmit",       // 1
	"kermask",        // 2
	NULL
};



//
// called from mx6q_detect()
//
static int  
mx6q_parse_options (mx6q_dev_t *mx6q, char *options, nic_config_t *cfg)
{
    char    *value, *restore, *c;
    int     opt;
    int     rc = 0;
    int     err = EOK;

   	/* Getting the ENET Base addresss and irq from the Hwinfo Section if available */
	unsigned hwi_off = hwi_find_device("fec", 0);
	if(hwi_off != HWI_NULL_OFF) {
		hwi_tag *tag = hwi_tag_find(hwi_off, HWI_TAG_NAME_location, 0);
			if(tag) {
				mx6q->iobase = tag->location.base;
				cfg->irq[0] = hwitag_find_ivec(hwi_off, NULL);
			}
	} 


    restore = NULL;
    while (options && *options != '\0') {
	c = options;
        restore = strchr(options, ',');
        opt = getsubopt (&options, mpc_opts, &value);

        switch (opt) {
          case  0:
            if (mx6q) {
                mx6q->num_rx_descriptors = strtoul (value, 0, 0);
            }
            break;

          case  1:
            if (mx6q) {
                mx6q->num_tx_descriptors = strtoul (value, 0, 0);
            }
            break;

	  case 2:
            if (mx6q) {
				if (value) {
				    mx6q->kermask = strtoul(value, 0, 0);
				    if (mx6q->kermask) {
				        mx6q->kermask = 1;
					}
				}
            }
			break;
        
	default:
	    if (nic_parse_options (cfg, value) != EOK) {
                log(LOG_ERR, "%s(): unknown option %s", __FUNCTION__, c);
                err = EINVAL;
                rc = -1;
	    }
            break;
        }
        if (restore != NULL)
            *restore = ',';

    }

    errno = err;
    return (rc);
}


//
// convert virtual to physical address
//
static paddr_t 
vtophys (void *addr)
{
	off64_t  offset;

	if (mem_offset64 (addr, NOFD, 1, &offset, 0) == -1) {
		return (-1);
	}
	return (offset);
}


//
// called from mx6q_detect() to allocate resources
//

static int
mx6q_attach(struct device *parent, struct device *self, void *aux)
{
	mx6q_dev_t	*mx6q = (mx6q_dev_t *)self;
	nic_config_t	*cfg = &mx6q->cfg;
	int				rc, idx;
	struct			ifnet *ifp;
	size_t			size;
	char		*options;
	uint32_t	iobase;
	struct _iopkt_self	*iopkt;
	struct mx6q_arg		*mx6q_arg;

	iopkt = iopkt_selfp;
	mx6q_arg = aux;

	options = mx6q_arg->options;
	idx = mx6q_arg->idx;
	iobase = mx6q_arg->iobase;

	mx6q->dev.dv_dll_hdl = mx6q_arg->dll_hdl;
	mx6q->iopkt = iopkt;
	mx6q->iid = -1;

	ifp = &mx6q->ecom.ec_if;
	ifp->if_softc = mx6q;

	if (cfg->verbose > 3) {
		log(LOG_ERR, "%s(): starting: idx %d\n", __FUNCTION__, idx);
	}

	if ((mx6q->sdhook = shutdownhook_establish(mx6q_shutdown, mx6q)) == NULL) {
		return ENOMEM;
	}

	mx6q->iobase = (uintptr_t) iobase;
	cfg->num_irqs = 1;

	/* set some defaults for the command line options */
	cfg->flags = NIC_FLAG_MULTICAST;
	cfg->media_rate = cfg->duplex = -1;
	cfg->priority = IRUPT_PRIO_DEFAULT;
	cfg->iftype = IFT_ETHER;
	cfg->lan = -1;
	strcpy((char *)cfg->uptype, "en");
	//cfg->verbose = 1;  // XXX debug - set verbose=0 for normal output

	mx6q->num_tx_descriptors = DEFAULT_NUM_TX_DESCRIPTORS;
	mx6q->num_rx_descriptors = DEFAULT_NUM_RX_DESCRIPTORS;

	mx6q->probe_phy = 0;
	mx6q->kermask = 1;  // XXX debug

	cfg->device_index = 0xFFFFFFFF;
	if ((rc = mx6q_parse_options(mx6q, options, cfg))) {
		log(LOG_ERR, "%s(): mx6q_parse_options() failed: %d", __FUNCTION__, rc);
		mx6q_destroy(mx6q, 1);
		return rc;
	}
	
	cfg->lan = mx6q->dev.dv_unit;
	cfg->device_index = idx;

	if (cfg->verbose) {
		log(LOG_ERR, "%s(): IF %d: Base register 0x%08X", 
			__FUNCTION__, idx, mx6q->iobase);
		log(LOG_ERR, "%s(): IF %d: IRQ %2d", 
			__FUNCTION__, idx, cfg->irq[0]);

	}

	// set defaults - only used by nicinfo to display mtu
	cfg->mtu = cfg->mru = ETH_MAX_PKT_LEN;

	cfg->media = NIC_MEDIA_802_3;
	cfg->mac_length = ETH_MAC_LEN;

	mx6q->force_advertise = -1;
	mx6q->flow = -1;

	// did user specify either of speed or duplex on the cmd line?
	if ((cfg->media_rate != -1) || (cfg->duplex != -1)) {

		if (cfg->media_rate == -1) {
			log(LOG_ERR, "%s(): must also specify speed when duplex is specified", __FUNCTION__);
			mx6q_destroy(mx6q, 1);
			return EINVAL;
		}
		if (cfg->duplex == -1) {
			log(LOG_ERR, "%s(): must also specify duplex when speed is specified", __FUNCTION__);
			mx6q_destroy(mx6q, 1);
			return EINVAL;
		}

		// we get here, we know both media_rate and duplex are set

		mx6q->flow = 0;
		switch(cfg->media_rate) {
			case 0:			
			mx6q->force_advertise = 0;  // disable link
			break;

			case 10*1000:
			mx6q->force_advertise = cfg->duplex ? MDI_10bTFD : MDI_10bT;
			break;

			case 100*1000:
			mx6q->force_advertise = cfg->duplex ? MDI_100bTFD : MDI_100bT;
			break;

			case 1000*1000:
			mx6q->force_advertise = cfg->duplex ? MDI_1000bTFD : MDI_1000bT;
			break;

			default:
			log(LOG_ERR, "%s(): invalid speed: %d", __FUNCTION__, cfg->media_rate/1000);
			mx6q_destroy(mx6q, 1);
			return EINVAL;
			break;			
		}
	}

	// initialize - until mii callback says we have a link ...
	cfg->flags |= NIC_FLAG_LINK_DOWN;

	if (cfg->num_mem_windows == 0) {
		cfg->num_mem_windows = 1;
	}
	cfg->mem_window_base[0] = mx6q->iobase;
	cfg->mem_window_size[0] = 0x1000;

	strcpy((char *)cfg->device_description, "IMX 6Q ENET");

	mx6q->num_rx_descriptors &= ~3;
	if (mx6q->num_rx_descriptors < MIN_NUM_RX_DESCRIPTORS) {
		mx6q->num_rx_descriptors = MIN_NUM_RX_DESCRIPTORS;
	}
	if (mx6q->num_rx_descriptors > MAX_NUM_RX_DESCRIPTORS) {
		mx6q->num_rx_descriptors = MAX_NUM_RX_DESCRIPTORS;
	}

	mx6q->num_tx_descriptors &= ~3;
	if (mx6q->num_tx_descriptors < MIN_NUM_TX_DESCRIPTORS) {
		mx6q->num_tx_descriptors = MIN_NUM_TX_DESCRIPTORS;
	}
	if (mx6q->num_tx_descriptors > MAX_NUM_TX_DESCRIPTORS) {
		mx6q->num_tx_descriptors = MAX_NUM_TX_DESCRIPTORS;
	}

	cfg->revision = NIC_CONFIG_REVISION;

	mx6q->cachectl.fd = NOFD;

	if (cache_init(0, &mx6q->cachectl, NULL) == -1) {
		rc = errno;
		log(LOG_ERR, "mx6q_detect: cache_init: %d", rc);
		mx6q_destroy(mx6q, 2);
		return rc;
	}

	// map nic registers into virtual memory
	if ((mx6q->reg = mmap_device_memory (NULL, 0x1000, PROT_READ | PROT_WRITE | PROT_NOCACHE,
		MAP_SHARED, mx6q->iobase)) == MAP_FAILED) {
		log(LOG_ERR, "%s(): mmap regs failed: %d", __FUNCTION__, rc);
		mx6q_destroy(mx6q, 2);
		return rc;
	}

	/* default MAC address to current ENET hardware setting (comes from boot loader on first boot) */
	get_phys_addr(mx6q, cfg->permanent_address);
	if (memcmp (cfg->current_address, "\0\0\0\0\0\0", 6) == 0)  {
		memcpy(cfg->current_address, cfg->permanent_address,
		       ETH_MAC_LEN);
	}

	if (cfg->verbose) {
		nic_dump_config(cfg);
	}

	// alloc rx descr ring
	if ((mx6q->rx_bd = mmap (NULL, sizeof (mpc_bd_t) * mx6q->num_rx_descriptors,
		PROT_READ | PROT_WRITE | PROT_NOCACHE , /* MAP_PRIVATE |*/ MAP_ANON | MAP_PHYS, NOFD, 0)) == MAP_FAILED) {
		log(LOG_ERR, "%s(): mmap rxd failed: %d", __FUNCTION__, rc);
		mx6q_destroy(mx6q, 4);
		return rc;
	}

	// alloc tx descr ring
	if ((mx6q->tx_bd = mmap (NULL, sizeof (mpc_bd_t) * mx6q->num_tx_descriptors,
		PROT_READ | PROT_WRITE | PROT_NOCACHE , /*MAP_PRIVATE |*/ MAP_ANON | MAP_PHYS, NOFD, 0)) == MAP_FAILED) {
		log(LOG_ERR, "%s(): mmap txd failed: %d", __FUNCTION__, rc);
		mx6q_destroy(mx6q, 5);
		return rc;
	}

	// alloc mbuf pointer array, corresponding to rx descr ring
	size = sizeof(struct mbuf *) * mx6q->num_rx_descriptors;
	mx6q->rx_pkts = malloc(size, M_DEVBUF, M_NOWAIT);
	if (mx6q->rx_pkts == NULL) {
		rc = ENOBUFS;
		log(LOG_ERR, "%s(): malloc rx_pkts failed", __FUNCTION__);
		mx6q_destroy(mx6q, 6);
		return rc;
	}
	memset(mx6q->rx_pkts, 0x00, size);

	// alloc mbuf pointer array, corresponding to tx descr ring
	size = sizeof(struct mbuf *) * mx6q->num_tx_descriptors;
	mx6q->tx_pkts = malloc(size, M_DEVBUF, M_NOWAIT);
	if (mx6q->tx_pkts == NULL) {
		rc = ENOBUFS;
		log(LOG_ERR, "%s(): malloc tx_pkts failed", __FUNCTION__);
		mx6q_destroy(mx6q, 7);
		return rc;
	}
	memset(mx6q->tx_pkts, 0x00, size);

	// one hardware interrupt on en0
	if ((rc = interrupt_entry_init(&mx6q->inter, 0, NULL,
		cfg->priority)) != EOK) {
		log(LOG_ERR, "%s(): interrupt_entry_init(rx) failed: %d", __FUNCTION__, rc);
		mx6q_destroy(mx6q, 9);
		return rc;
	}

	/* TX timestamp buffers for 'sync', 'delay_req',
	 * 'p_delay_req' and 'p_delay_resp' messages
	 */
	size = sizeof(mx6q_ptp_timestamp_t) * MX6Q_TX_TIMESTAMP_BUF_SZ;
	mx6q->tx_sync_ts       = malloc(size, M_DEVBUF, M_NOWAIT);
	mx6q->tx_delayreq_ts   = malloc(size, M_DEVBUF, M_NOWAIT);
	mx6q->tx_pdelayreq_ts  = malloc(size, M_DEVBUF, M_NOWAIT);
	mx6q->tx_pdelayresp_ts = malloc(size, M_DEVBUF, M_NOWAIT);
	if ((mx6q->tx_sync_ts == NULL) || (mx6q->tx_delayreq_ts == NULL) ||
	    (mx6q->tx_pdelayreq_ts == NULL) || (mx6q->tx_pdelayresp_ts == NULL) ) {
	    rc = ENOBUFS;
	    log(LOG_ERR, "%s(): malloc tx_timestamp failed", __FUNCTION__);
	    mx6q_destroy(mx6q, 10);
	    return rc;
	}
	memset(mx6q->tx_sync_ts, 0x00, size);
	memset(mx6q->tx_delayreq_ts, 0x00, size);
	memset(mx6q->tx_pdelayreq_ts, 0x00, size);
	memset(mx6q->tx_pdelayresp_ts, 0x00, size);

	/* RX timestamp buffers for 'sync', 'delay_req',
	 * 'p_delay_req' and 'p_delay_resp' messages
	 */
	size = sizeof(mx6q_ptp_timestamp_t) * MX6Q_RX_TIMESTAMP_BUF_SZ;
	mx6q->rx_sync_ts       = malloc(size, M_DEVBUF, M_NOWAIT);
	mx6q->rx_delayreq_ts   = malloc(size, M_DEVBUF, M_NOWAIT);
	mx6q->rx_pdelayreq_ts  = malloc(size, M_DEVBUF, M_NOWAIT);
	mx6q->rx_pdelayresp_ts = malloc(size, M_DEVBUF, M_NOWAIT);
	if ((mx6q->rx_sync_ts == NULL) || (mx6q->rx_delayreq_ts == NULL) ||
	    (mx6q->rx_pdelayreq_ts == NULL) || (mx6q->rx_pdelayresp_ts == NULL) ) {
	    rc = ENOBUFS;
	    log(LOG_ERR, "%s(): malloc rx_timestamp failed", __FUNCTION__);
	    mx6q_destroy(mx6q, 11);
	    return rc;
	}
	memset(mx6q->rx_sync_ts, 0x00, size);
	memset(mx6q->rx_delayreq_ts, 0x00, size);
	memset(mx6q->rx_pdelayreq_ts, 0x00, size);
	memset(mx6q->rx_pdelayresp_ts, 0x00, size);

	mx6q->tx_sync_cnt = 0;
	mx6q->tx_delayreq_cnt = 0;
	mx6q->tx_pdelayresp_cnt = 0;
	mx6q->tx_pdelayreq_cnt = 0;

	mx6q->rx_sync_cnt = 0;
	mx6q->rx_delayreq_cnt = 0;
	mx6q->rx_pdelayresp_cnt = 0;
	mx6q->rx_pdelayreq_cnt = 0;

	// hook up so media devctls work
	bsd_mii_initmedia(mx6q);

	// interface setup - entry points, etc
	strcpy(ifp->if_xname, mx6q->dev.dv_xname);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = mx6q_ioctl;
	ifp->if_start = mx6q_start;
//	ifp->if_watchdog = mx6q_watchdog;
	ifp->if_init = mx6q_init;
	ifp->if_stop = mx6q_stop;
	IFQ_SET_READY(&ifp->if_snd);


	if_attach(ifp);
	ether_ifattach(ifp, mx6q->cfg.current_address);

	// rx interrupt can either hit nic or use kermask
	mx6q->inter.func = mx6q_process_interrupt;
	if (mx6q->kermask) {
		mx6q->inter.enable = mx6q_enable_rx_kermask;
		mx6q->isrp = mx6q_isr_rx_kermask;
	} else {
		mx6q->inter.enable = mx6q_enable_rx;
		mx6q->isrp = mx6q_isr_rx;
	}
	mx6q->inter.arg = mx6q;

	callout_init(&mx6q->tx_callout);

	if (cfg->verbose > 3) {
		log(LOG_ERR, "%s(): ending: idx %d\n", __FUNCTION__, idx);
	}

	return EOK;
}


//
// called from mx6q_create_instance() and mx6q_destroy() 
//
static void
mx6q_destroy(mx6q_dev_t *mx6q, int how)
{
	struct ifnet		*ifp;

	ifp = &mx6q->ecom.ec_if;

	/* FALLTHROUGH all of these */
	switch (how) {

	//
	// called from mx6q_destroy()
	//
	case -1:
		/*
		 * Don't init() while we're dying.  Yes it can happen:
		 * ether_ifdetach() calls bridge_ifdetach() which
		 * tries to take us out of promiscuous mode with an
		 * init().
		 */
		mx6q->dying = 1;

		mx6q_stop(ifp, 1);

		mx6q_rx_mbuf_free(mx6q);

		ether_ifdetach(ifp);
		if_detach(ifp);

	//
	// called from mx6q_create_instance()
	//
    case 11:
        if (mx6q->rx_sync_ts != NULL) {
            free(mx6q->rx_sync_ts, M_DEVBUF);
        }
        if (mx6q->rx_delayreq_ts != NULL) {
            free(mx6q->rx_delayreq_ts, M_DEVBUF);
        }
        if (mx6q->rx_pdelayreq_ts != NULL) {
            free(mx6q->rx_pdelayreq_ts, M_DEVBUF);
        }
        if (mx6q->rx_pdelayresp_ts != NULL) {
            free(mx6q->rx_pdelayresp_ts, M_DEVBUF);
        }
    case 10:
        if (mx6q->tx_sync_ts != NULL) {
            free(mx6q->tx_sync_ts, M_DEVBUF);
        }
        if (mx6q->tx_delayreq_ts != NULL) {
            free(mx6q->tx_delayreq_ts, M_DEVBUF);
        }
        if (mx6q->tx_pdelayreq_ts != NULL) {
            free(mx6q->tx_pdelayreq_ts, M_DEVBUF);
        }
        if (mx6q->tx_pdelayresp_ts != NULL) {
            free(mx6q->tx_pdelayresp_ts, M_DEVBUF);
        }
    case 9:
	case 8:
		interrupt_entry_remove(&mx6q->inter, NULL);

	case 7:
		free(mx6q->tx_pkts, M_DEVBUF);

	case 6:
		free(mx6q->rx_pkts, M_DEVBUF);

	case 5:
		munmap(mx6q->tx_bd, sizeof(mpc_bd_t) * mx6q->num_tx_descriptors);

	case 4:
		munmap(mx6q->rx_bd, sizeof(mpc_bd_t) * mx6q->num_rx_descriptors);

	case 3:
		cache_fini(&mx6q->cachectl);

	case 2:
		munmap_device_memory(mx6q->reg, 0x1000);

	case 1:
		shutdownhook_disestablish(mx6q->sdhook);
		break;
	}
}


//
// io-pkt entry point
//

static void
mx6q_shutdown(void *arg)
{
	mx6q_dev_t	*mx6q;

	mx6q = arg;
	
	mx6q_stop(&mx6q->ecom.ec_if, 1);

}

static int
mx6q_detach(struct device *dev, int flags)
{
	mx6q_dev_t	*mx6q;

	mx6q = (mx6q_dev_t *)dev;
	mx6q_destroy(mx6q, -1);

	return EOK;
}


//
// called from mx6q_init() and mx6q_stop()
//
static void    
mx6q_reset (mx6q_dev_t *mx6q)
{
	volatile uint32_t	*base = mx6q->reg;
	uint32_t                    status;
	int                         timeout = MPC_TIMEOUT;

	// mask interrupt in nic register
	*(base + MX6Q_IMASK) = 0;
	
	if  ((*(base + MX6Q_X_CNTRL) & XCNTRL_GTS) == 0) {
		/* Graceful transmit stop and wait for completion. */
		*(base + MX6Q_X_CNTRL) |= XCNTRL_GTS;
		timeout = MPC_TIMEOUT;
		do {
			nanospin_ns (10);
			if (! --timeout)
				break;
			status = *(base + MX6Q_IEVENT);
		} while ((status & IEVENT_GRA) != IEVENT_GRA);

		if (! timeout) {
			log(LOG_ERR, "%s(): DMA GTS stop failed", __FUNCTION__ );
		}
	}

	/* Disable and clear statistics counters */
	*(base + MX6Q_MIB_CONTROL) |= (MIB_DISABLE | MIB_CLEAR);
	*(base + MX6Q_MIB_CONTROL) &= ~MIB_CLEAR;
	mx6q_clear_stats(mx6q);

	/* Disable Rx and Tx */
	*(base + MX6Q_ECNTRL) &= ~(ECNTRL_ETHER_EN | ECNTRL_ENET_OE);

	/* Wait for 9.6KBytes worth of data (worst case ~ 8ms) */
	delay (9);

	/* reset */
	*(base + MX6Q_ECNTRL) = ECNTRL_RESET;
	nanospin_ns (10000);
	*(base + MX6Q_ECNTRL) = 0;              /* Seems like the reset bit doesn't auto clear */

	if (*(base + MX6Q_ECNTRL) & ECNTRL_RESET) {
		log(LOG_ERR, "%s(): Reset bit didn't clear", __FUNCTION__);
	}
	// re program MAC address to PADDR registers
	set_phys_addr(mx6q);
	
	*(base + MX6Q_IEVENT) = 0xffffffff;
}

//
// called from mx6q_init()
//
static void
set_phys_addr (mx6q_dev_t *mx6q)
{
	// Program MAC address provided from command line argument
	uint32_t    *base = mx6q->reg;
	*(base + MX6Q_PADDR1) = (mx6q->cfg.current_address [0] << 24 |
		mx6q->cfg.current_address [1] << 16 | mx6q->cfg.current_address [2] << 8 |
		mx6q->cfg.current_address [3]);
	*(base + MX6Q_PADDR2) = (mx6q->cfg.current_address [4] << 24 |
		mx6q->cfg.current_address [5] << 16 | 0x8808);

}

/* weilun@adv read MAC address from SPI instead of eFUSE - begin */
static int is_zero_ether_addr(const uchar_t *addr)
{
	return !(addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]);
}

static int is_multicast_ether_addr(const uchar_t *addr)
{
	return (0x01 & addr[0]);
}

static int is_valid_ether_addr(const uchar_t * addr)
{
	/* FF:FF:FF:FF:FF:FF is a multicast address so we don't need to
	 * explicitly check for it here. */
	return !is_multicast_ether_addr(addr) && !is_zero_ether_addr(addr);
}
/* weilun@adv read MAC address from SPI instead of eFUSE - end */

/*
 *	Read MAC address from ENET hardware.
 *	Set by bootloader on reset (likely from Fuse registers
 *	but can be overridden).
 */
static void
get_phys_addr (mx6q_dev_t *mx6q, uchar_t *addr)
{
	uint32_t    *base = mx6q->reg;
	uint32_t    mx6q_paddr;

	mx6q_paddr = *(base + MX6Q_PADDR1);
	addr [0] = (mx6q_paddr >> 24) & ~0x01; /* clear multicast bit for sanity's sake */
	addr [1] = mx6q_paddr >> 16;
	addr [2] = mx6q_paddr >> 8;
	addr [3] = mx6q_paddr;
	mx6q_paddr = *(base + MX6Q_PADDR2);
	addr [4] = mx6q_paddr >> 24;
	addr [5] = mx6q_paddr >> 16;

	/* weilun@adv read MAC address from SPI instead of eFUSE - begin */
	uchar_t fs0_addr[ETH_MAC_LEN];
	memset(fs0_addr, 0x00, ETH_MAC_LEN);
	int fs0_fd = open("/dev/fs0", O_RDONLY);
	if (fs0_fd) {
		lseek(fs0_fd, 0xd0000, SEEK_SET);
   		read(fs0_fd, fs0_addr, ETH_MAC_LEN);
		close(fs0_fd);
	}
	else 
		log(LOG_INFO,"weilun@adv can not open /dev/fs0 : get_phys_addr");

	if (is_valid_ether_addr(fs0_addr)) {
		log(LOG_INFO,"weilun@adv get_phys_addr: [%02x:%02x:%02x:%02x:%02x:%02x] fs0 mac addr is used.", fs0_addr[0], fs0_addr[1], fs0_addr[2], fs0_addr[3], fs0_addr[4], fs0_addr[5]);
		memcpy(addr, fs0_addr, ETH_MAC_LEN);
	}
	else {
		fs0_addr[0] = 0x00;
		fs0_addr[1] = 0x11;
		fs0_addr[2] = 0x22;
		fs0_addr[3] = 0x33;
		fs0_addr[4] = 0x44;
		fs0_addr[5] = 0x55;
		log(LOG_INFO,"weilun@adv mac addre is invalid: [%02x:%02x:%02x:%02x:%02x:%02x] default fs0 mac addr is used.", fs0_addr[0], fs0_addr[1], fs0_addr[2], fs0_addr[3], fs0_addr[4], fs0_addr[5]);
		memcpy(addr, fs0_addr, ETH_MAC_LEN);
	}
	/* weilun@adv read MAC address from SPI instead of eFUSE - end */

}

typedef struct _ENET_PHY_INFO
{
	uint32_t  PhyId;
}ENET_PHY_INFO, *PENET_PHY_INFO;

ENET_PHY_INFO Am79c874Info = {
	0x0022561b,

};

ENET_PHY_INFO LAN87xxInfo = {
	0x7c0f1,
};
ENET_PHY_INFO Dp83640Info = {
	0x20005ce1, 
};
ENET_PHY_INFO AR8031Info = {
	0x004dd074,
};
ENET_PHY_INFO KSZ9021Info = {
	0x00221611,
};

ENET_PHY_INFO BCM54616Info = {
	0x03625d12,
};

// Now we  support Am79c874 and SMCS LAN87xx and DP83640

ENET_PHY_INFO *PhyInfo[] = {
	&Am79c874Info,
	&LAN87xxInfo,
	&Dp83640Info,
	&AR8031Info,
	&KSZ9021Info,
	&BCM54616Info,
	NULL
};

typedef enum _ENET_PHY_ID {
    AM79C874           = 0,
    LAN87XX            = 1,
    DP83640            = 2,
    AR8031             = 3,
    KSZ9021            = 4,
    BCM54616           = 5,
    ENET_PHY_MAX_ID
} ENET_PHY_ID;


#define PHY_STAT_10HDX  0x1000  /* 10 Mbit half duplex selected */
#define PHY_STAT_10FDX  0x2000  /* 10 Mbit full duplex selected */ 
#define PHY_STAT_100HDX 0x4000  /* 100 Mbit half duplex selected */
#define PHY_STAT_100FDX 0x8000  /* 100 Mbit full duplex selected */


void DumpMAC(mx6q_dev_t *mx6q)
{
volatile uint32_t	*base = mx6q->reg;

	uint32_t i,j;
	log(LOG_INFO,"--------DumpMAC----------");
	for(i=0,j= (uint32_t)base;i<0x200/4;i++){
		if(!(i%4)){
			log(LOG_INFO,"%08x %08x %08x %08x %08x",j, *((uint32_t*)base + i),*((uint32_t*)base + i+1),*((uint32_t*)base + i+2),*((uint32_t*)base + i+3));
			j+=0x10;
		}
	}
	log(LOG_INFO,"--------DumpMAC END----------\r\n");


//    EdbgOutputDebugString("EIR:0x%08x\r\n",HW_ENET_MAC_EIR_RD(MAC0));    
//    EdbgOutputDebugString("EIMR:0x%08x\r\n",HW_ENET_MAC_EIMR_RD(MAC0));    
//    EdbgOutputDebugString("RDAR:0x%08x\r\n",HW_ENET_MAC_RDAR_RD(MAC0));    
//    EdbgOutputDebugString("TDAR:0x%08x\r\n",HW_ENET_MAC_TDAR_RD(MAC0));    
//    EdbgOutputDebugString("ECR:0x%08x\r\n",HW_ENET_MAC_ECR_RD(MAC0));    
//    EdbgOutputDebugString("MMFR:0x%08x\r\n",HW_ENET_MAC_MMFR_RD(MAC0));    
//    EdbgOutputDebugString("MSCR:0x%08x\r\n",HW_ENET_MAC_MSCR_RD(MAC0));    
//    EdbgOutputDebugString("MIBC:0x%08x\r\n",HW_ENET_MAC_MIBC_RD(MAC0));    
//    EdbgOutputDebugString("RCR:0x%08x\r\n",HW_ENET_MAC_RCR_RD(MAC0));    
//    EdbgOutputDebugString("TCR:0x%08x\r\n",HW_ENET_MAC_TCR_RD(MAC0));    
//    EdbgOutputDebugString("PALR:0x%08x\r\n",HW_ENET_MAC_PALR_RD(MAC0));    
//    EdbgOutputDebugString("PAUR:0x%08x\r\n",HW_ENET_MAC_PAUR_RD(MAC0));    
//    EdbgOutputDebugString("OPD:0x%08x\r\n",HW_ENET_MAC_OPD_RD(MAC0));    
//    EdbgOutputDebugString("IAUR:0x%08x\r\n",HW_ENET_MAC_IAUR_RD(MAC0));    
//    EdbgOutputDebugString("IALR:0x%08x\r\n",HW_ENET_MAC_IALR_RD(MAC0));    
//    EdbgOutputDebugString("GAUR:0x%08x\r\n",HW_ENET_MAC_GAUR_RD(MAC0));    
//    EdbgOutputDebugString("DMA1:0x%08x\r\n",HW_ENET_MAC_ERDSR_RD(MAC0));    
//    EdbgOutputDebugString("DMA2:0x%08x\r\n",HW_ENET_MAC_ETDSR_RD(MAC0));    
//    EdbgOutputDebugString("--------DumpMAC END----------\r\n");    
}


void DumpPhy(mx6q_dev_t *mx6q)
{
	uint32_t i;
	nic_config_t	*cfg	= &mx6q->cfg;
	int		phy_idx	= cfg->phy_addr;


//    TCHAR PhyReg[]={
//        {TEXT("Control")},
//        {TEXT("Status")},
//        {TEXT("Phy1")},
//        {TEXT("Phy2")},
//        {TEXT("Auto-Nego")},
//        {TEXT("Link partner")},
//        {TEXT("Auto-Nego_ex")},
//        {TEXT("Next page")},
//        {TEXT("Link Partner next")},
//        {TEXT("1000 BT ctl")},
//        {TEXT("100 BT stat")},
//        {TEXT("RSVD")},
//        {TEXT("RSVD")},
//        {TEXT("MMD control")},
//        {TEXT("MMD data")},
//        {TEXT("ext status")},
//        {TEXT("func ctl")},
//        {TEXT("phy status")},
//        {TEXT("intr en")},
//        {TEXT("intr stat")},
//        {TEXT("smart speed")},
//    };
	log(LOG_INFO,"--------DumpPhy----------\r\n");
	for(i=0;i<0x20;i+=4)
	{
		log(LOG_INFO,"phy[%d]:0x%x phy[%d]:0x%x phy[%d]:0x%x phy[%d]:0x%x",
			i,mx6q_mii_read(mx6q, phy_idx,i),i+1,mx6q_mii_read(mx6q, phy_idx,i+1),i+2,mx6q_mii_read(mx6q, phy_idx,i+2),i+3,mx6q_mii_read(mx6q, phy_idx,i+3));    
	}
	log(LOG_INFO,"--------DumpPhy END----------\r\n");
}




//
// io-pkt entry point - initialize hardware re-entrantly
//
// N.B.  Must ensure that this function, and everything it calls
// is REALLY ok to call over and over again, because the stack
// will call us MORE THAN ONCE
//
static int
mx6q_init (struct ifnet *ifp)
{
	mx6q_dev_t			*mx6q = ifp->if_softc;
	nic_config_t			*cfg = &mx6q->cfg;
	volatile uint32_t		*base = mx6q->reg;
	int				err, i;
	struct mbuf			*m;
	mpc_bd_t        		*bd;
	uint32_t			MIIPhyId;
	int				phy_idx;
	unsigned short			PhyType;
		if (cfg->verbose > 3) {
		log(LOG_ERR, "%s(): starting: idx %d\n", 
			__FUNCTION__, cfg->device_index);
	}

	if (mx6q->dying) {
		log(LOG_ERR, "%s(): dying", __FUNCTION__);
		return 1;
	}

	// clean up and reset nic
	mx6q_stop(ifp, 0);  

	// Transmitter is now locked out

	// Enable enhanced mode
	*(base + MX6Q_ECNTRL) |= ECNTRL_ENA_1588;

	// init tx descr ring
	for (i=0; i<mx6q->num_tx_descriptors; i++) {

		bd = &mx6q->tx_bd[i];
		if (i == (mx6q->num_tx_descriptors - 1)) {
		        bd->status |= TXBD_W;
		} else {
			bd->status = TXBD_L | TXBD_TC;
		}

		// free any previously txd mbufs
		if ((m = mx6q->tx_pkts[i])) {
			m_freem(m);
			mx6q->tx_pkts[i] = NULL;
		}
		// Enable TXBD update interrupt
		bd->estatus = TXBD_ESTATUS_INT;
	}
	mx6q->tx_pidx = mx6q->tx_cidx = mx6q->tx_descr_inuse = 0;

	// init rx descr ring
	for (i=0; i<mx6q->num_rx_descriptors; i++) {
		bd = &mx6q->rx_bd[i];
		if (i == (mx6q->num_rx_descriptors - 1)) {
			bd->status = RXBD_W | RXBD_E;    /* Wrap indicator */
			} else {
			bd->status = RXBD_E;
		}
		// Enable RXBD update interrupt
		bd->estatus = RXBD_ESTATUS_INT;

		// assign an mbuf to each rx descriptor
		if ((m = mx6q->rx_pkts[i])) {
			// reuse previously allocated mbuf
		} else {
			if ((m = m_gethdr(M_DONTWAIT, MT_DATA)) == NULL) {
				return ENOMEM;
			}
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_freem(m);
				return ENOMEM;
			}
			mx6q->rx_pkts[i] = m;
		}

		bd->buffer =  pool_phys(m->m_data, m->m_ext.ext_page);
		CACHE_INVAL(&mx6q->cachectl, m->m_data, bd->buffer, m->m_ext.ext_size);
		bd->length = 0;
	}
	mx6q->rx_cidx = 0;

	/* check for command line override */
        if (memcmp (cfg->current_address, "\0\0\0\0\0\0", 6) == 0) {
		log(LOG_ERR, "%s():You must specify a MAC address", __FUNCTION__);
		return (-1);
        }

	// write to MX6Q_PADDR1 and MX6Q_PADDR2 from cfg->current_address
	set_phys_addr(mx6q);

	// set addresses of tx, rx descriptor rings
	*(base + MX6Q_X_DES_START)  = vtophys ((void *) mx6q->tx_bd);
	*(base + MX6Q_R_DES_START)  = vtophys ((void *) mx6q->rx_bd);

	// Set transmit FIFO to store and forward
	*(base + MX6Q_X_WMRK) = X_WMRK_STR_FWD;

	// Set maximum receive buffer size
	*(base + MX6Q_R_BUFF_SIZE) = MX6Q_MTU_SIZE;

	// Set Rx FIFO thresholds for Pause generation
	*(base + MX6Q_R_SECTION_FULL_ADDR) = 0x10;
	*(base + MX6Q_R_SECTION_EMPTY_ADDR) = 0x82;
	*(base + MX6Q_R_ALMOST_EMPTY_ADDR) = 0x8;
	*(base + MX6Q_R_ALMOST_FULL_ADDR) = 0x8;

	// Enable Pause
	*(base + MX6Q_R_CNTRL) |= RCNTRL_FCE;
	*(base + MX6Q_OP_PAUSE) = 0xFFF0;

	*(base + MX6Q_IADDR1) = 0x0;
	*(base + MX6Q_IADDR2) = 0x0;
	*(base + MX6Q_GADDR1) = 0x0;
	*(base + MX6Q_GADDR2) = 0x0;

	// Enable and clear MIB Registers
	*(base + MX6Q_MIB_CONTROL) &= ~MIB_DISABLE;
	*(base + MX6Q_MIB_CONTROL) |= MIB_CLEAR;
	*(base + MX6Q_MIB_CONTROL) &= ~MIB_CLEAR;
	mx6q_clear_stats(mx6q);

	//ENET is in RGMII mode 
	*(base + MX6Q_R_CNTRL) |= RCNTRL_RGMII_ENA;

	// As per reference manual this bit should always be 1 - MII/RMII mode
	*(base + MX6Q_R_CNTRL) |= RCNTRL_MII_MODE;

	/* 
	 * i.MX6 Q requires speed register to be 0x11a to fix a clock issue 
	 * TODO, more details from Freescale.
	 */
	*(base + MX6Q_MII_SPEED) = 0x0000011A;

	// Enable little endian
	*(base + MX6Q_ECNTRL) |= ECNTRL_DBSWP;

	// Disable internal loopback
	*(base + MX6Q_R_CNTRL) &= ~RCNTRL_LOOP;

	// MAC automatically inserts MAC address in frames
	*(base + MX6Q_X_CNTRL) |= XCNTRL_TX_ADDR_INS;

	// Full duplex by default
	*(base + MX6Q_X_CNTRL) |= XCNTRL_FDEN;

	// Clear interrupt status
	*(base + MX6Q_IEVENT) = 0xffffffff;

	// get PHY address, register callbacks with MII managment library 
	phy_idx = mx6_sabrelite_get_phy_addr(mx6q);
	if (phy_idx == -1) {
		log(LOG_ERR,"Unable to detect PHY, exiting...");
		return -1;
	}
		
	cfg->phy_addr = phy_idx;

	PhyType = mx6q_mii_read(mx6q, phy_idx, MDI_PHYID_1);

	while (PhyType == 0 || PhyType == 0xFFFF)
	{
		log(LOG_INFO,"Bad MMI read, continue ...");
		log(LOG_INFO,"ENET Init PhyType is 0x%x", PhyType);
	}

	MIIPhyId = (uint32_t) PhyType << 16;

	PhyType = mx6q_mii_read(mx6q, phy_idx, MDI_PHYID_2);

	if (PhyType == 0)
	{
		log(LOG_INFO,"MII_REG_PHYIR2 READ Failed");
	}

	MIIPhyId |=  (PhyType & 0xffff);


	for (i = 0; PhyInfo[i] != NULL; i++)
	{
		if (PhyInfo[i]->PhyId == MIIPhyId)
			break;

		if ((PhyInfo[i]->PhyId & 0xffff0) == (MIIPhyId & 0xffff0)) 
			break;
	}

	if (PhyInfo[i])
	{
		if (cfg->verbose > 3)
			log(LOG_INFO,"The index for the external PHY is %x %x", i,PhyInfo[i]->PhyId);
	}
	else
	{
		log(LOG_INFO,"Failed to get the external PHY, ID 0x%x", MIIPhyId);
	}

	switch (i) {
		case AM79C874:
			// TODO
			if (cfg->verbose > 3)
				log(LOG_INFO,"Detected AM79C874 PHY");
			break;

		case LAN87XX:
			// TODO
			if (cfg->verbose > 3)
				log(LOG_INFO,"Detected LAN87xx PHY");
			break;

		case DP83640:
			// TODO
			log(LOG_INFO,"Detected DP83640 PHY");
			break;

		case AR8031:
			if (cfg->verbose > 3)
				log(LOG_INFO,"Detected Atheros AR8031 PHY");
			mx6_sabreauto_rework(mx6q);
			break;

		case KSZ9021:
			if (cfg->verbose > 3)
				log(LOG_INFO,"Detected Micrel KSZ9021 PHY");
			mx6_sabrelite_phy_init(mx6q);
			// Note that on some boards (such as the i.MX6 Q Sabre-Lite)
			// the KSZ9021 cannot operate at 1000Mbps (use 100Mbps or 10Mbps instead)
			break;

		case BCM54616:
			// TODO
			if (cfg->verbose > 3)
				log(LOG_INFO,"Detected Broadcom 54616 PHY");
			break;
	}

	// Now that PHY specific initialisation is finished, perform auto negotiation
	// or bypass auto-negotiation if user passed command line link settings
	mx6q_init_phy(mx6q);

	// arm callout which detects PHY link state changes
	callout_msec(&mx6q->mii_callout, 2 * 1000, mx6q_MDI_MonitorPhy, mx6q);

	// PHY is now ready, turn on MAC
	*(base + MX6Q_ECNTRL) |= ECNTRL_ETHER_EN;

	// Instruct MAC to poll receiver descriptor ring and process recieve frames
	*(base + MX6Q_R_DES_ACTIVE) = R_DES_ACTIVE;

	// Attach to hardware interrupts
	if (mx6q->iid == -1) {
		if ((err = InterruptAttach_r(mx6q->cfg.irq[0], mx6q->isrp,
		    mx6q, sizeof(*mx6q), _NTO_INTR_FLAGS_TRK_MSK)) < 0) {
			err = -err;
			log(LOG_ERR, "%s(): InterruptAttach_r(rx) failed: %d", __FUNCTION__, err);
			return err;
		}
		mx6q->iid = err;
	}

	*(base + MX6Q_IMASK) = (IMASK_TBIEN | IMASK_TFIEN | IMASK_RBIEN | IMASK_RFIEN | IMASK_BREN | IMASK_EBERREN |
				IMASK_XFUNEN | IMASK_TS_AVAIL | IMASK_TS_TIMER);
	// Start 1588 timer
	mx6q_ptp_start(mx6q);

	NW_SIGLOCK(&ifp->if_snd_ex, mx6q->iopkt);
	ifp->if_flags_tx |= IFF_RUNNING;
	NW_SIGUNLOCK(&ifp->if_snd_ex, mx6q->iopkt);
	ifp->if_flags |= IFF_RUNNING;

	if (cfg->verbose > 3) {
		log(LOG_ERR, "%s(): ending: idx %d\n", 
		  __FUNCTION__, cfg->device_index);
	}
	

	//DumpMAC(mx6q);
	//DumpPhy(mx6q);

	return EOK;
}


//
// called from mx6q_mii_callback() and mx6q_ioctl()
//
void    
mx6q_speeduplex (mx6q_dev_t *mx6q)
{
	nic_config_t        	*cfg		= &mx6q->cfg;
	volatile uint32_t   	*base		= mx6q->reg;

	if (cfg->duplex) {
		*(base + MX6Q_X_CNTRL) |= XCNTRL_FDEN;
		*(base + MX6Q_R_CNTRL) &= ~RCNTRL_DRT;
	}
	else {
		*(base + MX6Q_X_CNTRL) &= ~XCNTRL_FDEN;
		*(base + MX6Q_R_CNTRL) |= RCNTRL_DRT;
	}
}


//
// io-pkt entry point.  Also called from mx6q_destroy()
// and mx6q_init()
//
static void
mx6q_stop(struct ifnet *ifp, int disable)
{
	mx6q_dev_t	*mx6q = ifp->if_softc;
	int		i;
	
	// shut down mii probing
	callout_stop(&mx6q->mii_callout);

	// shutdown tx callback
	callout_stop(&mx6q->tx_callout);

	if (mx6q->mdi) {
		MDI_DeRegister(&mx6q->mdi);
		mx6q->mdi = NULL;
	}

	// stop tx and rx and reset nic
	mx6q_reset(mx6q);

	// Stop 1588 timer
	mx6q_ptp_stop( mx6q );

	/* Lock out the transmit side */
	NW_SIGLOCK(&ifp->if_snd_ex, mx6q->iopkt);
	for (i = 0; i < 10; i++) {
		if ((ifp->if_flags_tx & IFF_OACTIVE) == 0)
			break;
		NW_SIGUNLOCK(&ifp->if_snd_ex, mx6q->iopkt);
		delay(50);
		NW_SIGLOCK(&ifp->if_snd_ex, mx6q->iopkt);
	}

	if (i < 10) {
		ifp->if_flags_tx &= ~IFF_RUNNING;
		NW_SIGUNLOCK(&ifp->if_snd_ex, mx6q->iopkt);
	} else {
		/* Heavy load or bad luck.  Try the big gun. */
		quiesce_all();
		ifp->if_flags_tx &= ~IFF_RUNNING;
		unquiesce_all();
	}

	/*
	 * The only other transmit entry point is now locked out.
	 * All others are only used by the stack thread which is
	 * currently right here.
	 */

	if (mx6q->iid != -1) {
		InterruptDetach(mx6q->iid);
		mx6q->iid = -1;
	}


	/* Mark the interface as down and cancel the watchdog timer. */
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;
}


//
// called from mx6q_destroy()
//
static void
mx6q_rx_mbuf_free(mx6q_dev_t *mx6q)
{
	int		i;
	struct mbuf	*m;

	for (i = 0; i < mx6q->num_rx_descriptors; i++) {
		if ((m = mx6q->rx_pkts[i])) {
			m_freem(m);
			mx6q->rx_pkts[i] = NULL;
		}
	}
}

//
// called from mx6q_entry() in mx6q.c
//
int
mx6q_detect(void *dll_hdl, struct _iopkt_self *iopkt, char *options)
{
	mx6q_dev_t	*mx6q;			// dummy entry used for parsing and detecting only
	nic_config_t	*cfg;
	int				idx=0;
	int				rc=EOK, single;
	uint32_t        iobase = 0;
	struct mx6q_arg  mx6q_arg;
	struct device	*dev;

	if ((mx6q = malloc(sizeof(*mx6q), M_TEMP, M_NOWAIT | M_ZERO)) == NULL) {
		return ENOMEM;
	}
	cfg = &mx6q->cfg;
	cfg->device_index = 0xffffffff;

	/* If device index specified, ignore all other device cfgs. */
		if ((cfg->device_index == idx) || (cfg->device_index == 0xffffffff)) {

			single = 0;
			if (cfg->device_index == idx) {
				/* Configured required device... No need to look for
				 * more. */
				single = 1;
			}

		mx6q_arg.dll_hdl = dll_hdl;
		mx6q_arg.options = options;
		mx6q_arg.idx = idx;
		mx6q_arg.iobase = iobase;

		dev = NULL; /* No parent */
			if ((rc = dev_attach("fec", options, &mx6q_ca, &mx6q_arg, &single, &dev, NULL)) != EOK) {
				log(LOG_ERR, "%s(): syspage dev_attach(%d) failed: %d\n", __FUNCTION__, idx, rc);
				free(mx6q, M_TEMP);
				return (ENODEV);
			}
	}

	free(mx6q, M_TEMP);
	return (rc);
}


__SRCVERSION( "$URL$ $REV$" )
