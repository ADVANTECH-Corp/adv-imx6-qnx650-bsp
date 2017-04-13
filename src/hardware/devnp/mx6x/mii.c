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

//
// drvr lib callback to read phy register
//
uint16_t    
mx6q_mii_read (void *handle, uint8_t phy_add, uint8_t reg_add)
{
    mx6q_dev_t       *mx6q = (mx6q_dev_t *) handle;
    volatile uint32_t   *base = mx6q->reg;
    int                 timeout = MPC_TIMEOUT;
    uint32_t            val;

	*(base + MX6Q_IEVENT) |= IEVENT_MII;
        val = ((1 << 30) | (0x2 << 28) | (phy_add << 23) | (reg_add << 18) | (2 << 16));
        *(base + MX6Q_MII_DATA) = val;

        while (timeout--) {
                if (*(base + MX6Q_IEVENT) & IEVENT_MII) {
                        *(base + MX6Q_IEVENT) |= IEVENT_MII;
	                       break;
                 }
                nanospin_ns (10000);
                }
        return ((timeout <= 0) ? 0 : (*(base + MX6Q_MII_DATA) & 0xffff));
}


//
// drvr lib callback to write phy register
//
void    
mx6q_mii_write (void *handle, uint8_t phy_add, uint8_t reg_add, uint16_t data)

{
    mx6q_dev_t       *mx6q = (mx6q_dev_t *) handle;
    volatile uint32_t   *base = mx6q->reg;
    int                 timeout = MPC_TIMEOUT;
	uint32_t        phy_data;

	*(base + MX6Q_IEVENT) |= IEVENT_MII;
        phy_data = ((1 << 30) | (0x1 << 28) | (phy_add << 23) | (reg_add << 18) | (2 << 16) | data);
        *(base + MX6Q_MII_DATA) = phy_data;
        while (timeout--) {
                if (*(base + MX6Q_IEVENT) & IEVENT_MII) {
                        *(base + MX6Q_IEVENT) |= IEVENT_MII;
                        break;
                        }
                nanospin_ns (10000);
                }
}


//
// drvr lib callback when PHY link state changes
//
void
mx6q_mii_callback(void *handle, uchar_t phy, uchar_t newstate)
{
	mx6q_dev_t		*mx6q = handle;
	nic_config_t		*cfg  = &mx6q->cfg;
	char			*s;
	int			i;
	int			mode;
	struct ifnet		*ifp = &mx6q->ecom.ec_if;
	volatile uint32_t	*base = mx6q->reg;
	int			phy_idx = cfg->phy_addr;
	uint16_t		advert, lpadvert;
	
	switch(newstate) {
	case MDI_LINK_UP:
		if ((i = MDI_GetActiveMedia(mx6q->mdi, cfg->phy_addr, &mode)) != MDI_LINK_UP) {
			log(LOG_INFO, "%s(): MDI_GetActiveMedia() failed: %x", __FUNCTION__, i);
			mode = 0;  // force default case below - all MDI_ macros are non-zero
		}

		switch(mode) {
		case MDI_10bTFD:
			s = "10 BaseT Full Duplex";
			cfg->duplex = 1;
			cfg->media_rate = 10 * 1000L;
			*(base + MX6Q_ECNTRL) &= ~ECNTRL_ETH_SPEED;
			break;
		case MDI_10bT:
			s = "10 BaseT Half Duplex";
			cfg->duplex = 0;
			cfg->media_rate = 10 * 1000L;
			*(base + MX6Q_ECNTRL) &= ~ECNTRL_ETH_SPEED;
			break;
		case MDI_100bTFD:
			s = "100 BaseT Full Duplex";
			cfg->duplex = 1;
			cfg->media_rate = 100 * 1000L;
			*(base + MX6Q_ECNTRL) &= ~ECNTRL_ETH_SPEED;
			break;
		case MDI_100bT: 
			s = "100 BaseT Half Duplex";
			cfg->duplex = 0;
			cfg->media_rate = 100 * 1000L;
			*(base + MX6Q_ECNTRL) &= ~ECNTRL_ETH_SPEED;
			break;
		case MDI_100bT4:
			s = "100 BaseT4";
			cfg->duplex = 0;
			cfg->media_rate = 100 * 1000L;
			*(base + MX6Q_ECNTRL) &= ~ECNTRL_ETH_SPEED;
			break;
		case MDI_1000bT:
			s = "1000 BaseT Half Duplex";
			cfg->duplex = 0;
			cfg->media_rate = 1000 * 1000L;
			*(base + MX6Q_ECNTRL) |= ECNTRL_ETH_SPEED;
			break;
		case MDI_1000bTFD:
			s = "1000 BaseT Full Duplex";
			cfg->duplex = 1;
			cfg->media_rate = 1000 * 1000L;
			*(base + MX6Q_ECNTRL) |= ECNTRL_ETH_SPEED;
			break;
		default:
			log(LOG_INFO,"%s(): unknown link mode 0x%X",__FUNCTION__,mode);
			s = "Unknown";
			cfg->duplex = 0;
			cfg->media_rate = 0L;
			break;
		}

	// immediately set new speed and duplex in nic config registers
	mx6q_speeduplex(mx6q);

	if (mx6q->flow == -1) {
	    /* Flow control was autoneg'd, set what we got in the MAC */
	    advert = mx6q_mii_read(mx6q, phy_idx, MDI_ANAR);
	    lpadvert = mx6q_mii_read(mx6q, phy_idx, MDI_ANLPAR);
	    if (advert & MDI_FLOW) {
		if (lpadvert & MDI_FLOW) {
		    /* Enable Tx and Rx of Pause */
		    *(base + MX6Q_R_SECTION_EMPTY_ADDR) = 0x82;
		    *(base + MX6Q_R_CNTRL) |= RCNTRL_FCE;
		} else if ((advert & MDI_FLOW_ASYM) &&
			   (lpadvert & MDI_FLOW_ASYM)) {
		    /* Enable Rx of Pause */
		    *(base + MX6Q_R_SECTION_EMPTY_ADDR) = 0;
		    *(base + MX6Q_R_CNTRL) |= RCNTRL_FCE;

		} else {
		    /* Disable all pause */
		    *(base + MX6Q_R_SECTION_EMPTY_ADDR) = 0;
		    *(base + MX6Q_R_CNTRL) &= ~RCNTRL_FCE;
		}
	    } else if ((advert & MDI_FLOW_ASYM) &&
		       (lpadvert & MDI_FLOW) &&
		       (lpadvert & MDI_FLOW_ASYM)) {
		/* Enable Tx of Pause */
		*(base + MX6Q_R_SECTION_EMPTY_ADDR) = 0x82;
		*(base + MX6Q_R_CNTRL) &= ~RCNTRL_FCE;
	    } else {
		/* Disable all pause */
		*(base + MX6Q_R_SECTION_EMPTY_ADDR) = 0;
		*(base + MX6Q_R_CNTRL) &= ~RCNTRL_FCE;
	    }
	}

	if (cfg->media_rate) {
		cfg->flags &= ~NIC_FLAG_LINK_DOWN;
		if (cfg->verbose) {
			log(LOG_INFO, "%s(): link up lan %d idx %d (%s)",
				__FUNCTION__, cfg->lan, cfg->device_index, s);
		}
		if_link_state_change(ifp, LINK_STATE_UP);
		if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
			// Packets were still in the queue when the link
			// went down, call start to get them moving again
			NW_SIGLOCK(&ifp->if_snd_ex, mx6q->iopkt);
			mx6q_start(ifp);
		}
		break;
	} else {
		// fall through to link down handling
	}

	case MDI_LINK_DOWN:
		cfg->media_rate = cfg->duplex = -1;
		cfg->flags |= NIC_FLAG_LINK_DOWN;

		if (cfg->verbose) {
			log(LOG_INFO,
				"%s(): Link down lan %d idx %d, calling MDI_AutoNegotiate()",
				__FUNCTION__, cfg->lan, cfg->device_index);
		}
		MDI_AutoNegotiate(mx6q->mdi, cfg->phy_addr, NoWait);
		if_link_state_change(ifp, LINK_STATE_DOWN);
		break;

	default:
		log(LOG_ERR, "%s(): idx %d: Unknown link state 0x%X",	__FUNCTION__, cfg->device_index, newstate);
		break;
	}
}


//
// periodically called by stack to probe phy state
// and to clean out tx descriptor ring
//
void
mx6q_MDI_MonitorPhy (void *arg)
{
	mx6q_dev_t		*mx6q	= arg;
	nic_config_t		*cfg  		= &mx6q->cfg;
	struct ifnet 		*ifp		= &mx6q->ecom.ec_if;

	//
	// we will probe the PHY if:
	//   the user has forced it from the cmd line, or
	//   we have not rxd any packets since the last time we ran, or
	//   the link is considered down
	//
	if (mx6q->probe_phy ||
	  !mx6q->rxd_pkts   ||
	  cfg->media_rate <= 0 ||		
	  cfg->flags & NIC_FLAG_LINK_DOWN) {
		if (cfg->verbose > 4) {
			log(LOG_ERR, "%s(): calling MDI_MonitorPhy()\n",  __FUNCTION__);
		}
		//
		// directly call drvr lib to probe PHY link state which in turn
		// will call mx6q_mii_callback() above if link state changes
		//
		MDI_MonitorPhy(mx6q->mdi);  

	} else {
		if (cfg->verbose > 4) {
			log(LOG_ERR, "%s(): NOT calling MDI_MonitorPhy()\n",  __FUNCTION__);
		}
	}
	mx6q->rxd_pkts = 0;  // reset for next time we are called

	//
	// Clean out the tx descriptor ring if it has not
	// been done by the start routine in the last 2 seconds
	//
	if (!mx6q->tx_reaped) {	
		NW_SIGLOCK(&ifp->if_snd_ex, mx6q->iopkt);

		mx6q_transmit_complete(mx6q);
		
		NW_SIGUNLOCK(&ifp->if_snd_ex, mx6q->iopkt);
	}
	mx6q->tx_reaped = 0;  // reset for next time we are called


	// restart timer to call us again in 2 seconds
	callout_msec(&mx6q->mii_callout, 2 * 1000, mx6q_MDI_MonitorPhy, mx6q);
}

int mx6_sabrelite_get_phy_addr(mx6q_dev_t *mx6q)
{
	int		status;
	nic_config_t	*cfg = &mx6q->cfg;
	int		phy_idx = cfg->phy_addr;

	if (mx6q->mdi) {
		MDI_DeRegister(&mx6q->mdi);   // repetitively called by if_init
	}

	status = MDI_Register_Extended (mx6q, mx6q_mii_write, mx6q_mii_read,
	mx6q_mii_callback, (mdi_t **)&mx6q->mdi, NULL, 0, 0);
	if (status != MDI_SUCCESS) {
		log(LOG_ERR, "%s(): MDI_Register_Extended() failed: %d", __FUNCTION__ ,status);
		mx6q->mdi = NULL;
		return -1;
	}

	callout_init(&mx6q->mii_callout);

	// Get PHY address
	for(phy_idx = 0; phy_idx < 32; phy_idx++) {
		if (MDI_FindPhy(mx6q->mdi, phy_idx) == MDI_SUCCESS && 
			MDI_InitPhy(mx6q->mdi, phy_idx) == MDI_SUCCESS) {

				if (cfg->verbose) {
					log(LOG_ERR, "%s(): PHY found at address %d", __FUNCTION__, phy_idx);
				}
			MDI_ResetPhy(mx6q->mdi, phy_idx, WaitBusy);
			break;
		}
		// If PHY is not detected then exit
		if (phy_idx == 31)
			return -1;
	}

	if (mx6q->force_advertise == 0) {    // the user has forced the link down
		MDI_IsolatePhy(mx6q->mdi, phy_idx);
		MDI_DisableMonitor(mx6q->mdi);  // neuter MDI_MonitorPhy()
		return phy_idx;
	} else {  // forced or auto-neg  
		// in case the user previously forced the link
		// down, bring it back up again
		MDI_DeIsolatePhy(mx6q->mdi, phy_idx);
		delay(10);
	}
	return phy_idx;

}

int mx6_sabrelite_phy_init(mx6q_dev_t *mx6q)
{
	nic_config_t	*cfg	= &mx6q->cfg;
	int		phy_idx	= cfg->phy_addr;
	
	// master mode (when possible), 1000 Base-T capable
	mx6q_mii_write(mx6q, phy_idx, 0x9, 0x0f00);

	// min rx data delay
	mx6q_mii_write(mx6q, phy_idx, 0xb, 0x8105);
	mx6q_mii_write(mx6q, phy_idx, 0xc, 0x0000);

	// max rx/tx clock delay, min rx/tx control delay
	mx6q_mii_write(mx6q, phy_idx, 0xb, 0x8104);
	mx6q_mii_write(mx6q, phy_idx, 0xc, 0xf0f0);
	mx6q_mii_write(mx6q, phy_idx, 0xb, 0x104);
	
	return 0;
}

int mx6_sabreauto_rework(mx6q_dev_t *mx6q)
{
	nic_config_t        *cfg 		= &mx6q->cfg;
	int                 phy_idx 	= cfg->phy_addr;
	unsigned short val;

	/* To enable AR8031 ouput a 125MHz clk from CLK_25M */
	mx6q_mii_write(mx6q, phy_idx, 0xd, 0x7);
	mx6q_mii_write(mx6q, phy_idx, 0xe, 0x8016);
	mx6q_mii_write(mx6q, phy_idx, 0xd, 0x4007);
	val = mx6q_mii_read(mx6q, phy_idx, 0xe);

	val &= 0xffe3;
	val |= 0x18;
	mx6q_mii_write(mx6q, phy_idx, 0xe, val);

	/* introduce tx clock delay */
	mx6q_mii_write(mx6q, phy_idx, 0x1d, 0x5);
	val = mx6q_mii_read(mx6q, phy_idx, 0x1e);
	val |= 0x0100;
	mx6q_mii_write(mx6q, phy_idx, 0x1e, val);

	/*
	 * Disable SmartEEE
	 * The Tx delay can mean late pause and bad timestamps.
	 */
	mx6q_mii_write(mx6q, phy_idx, 0xd, 0x3);
	mx6q_mii_write(mx6q, phy_idx, 0xe, 0x805d);
	mx6q_mii_write(mx6q, phy_idx, 0xd, 0x4003);
	val = mx6q_mii_read(mx6q, phy_idx, 0xe);
	val &= ~(1 << 8);
	mx6q_mii_write(mx6q, phy_idx, 0xe, val);

	/* As above for EEE (802.3az) */
	mx6q_mii_write(mx6q, phy_idx, 0xd, 0x7);
	mx6q_mii_write(mx6q, phy_idx, 0xe, 0x3c);
	mx6q_mii_write(mx6q, phy_idx, 0xd, 0x4007);
	mx6q_mii_write(mx6q, phy_idx, 0xd,0);

	return 0;
}


//
// called from mx6q_init()
//
void
mx6q_init_phy(mx6q_dev_t *mx6q)
{
	nic_config_t	*cfg		= &mx6q->cfg;
	int		phy_idx		= cfg->phy_addr;
	uint32_t	*base		= mx6q->reg;
	int		an_capable;
	uint16_t	reg ,mscr,mscr1;
	int		i;

	if (cfg->verbose) {
		log(LOG_ERR, "%s(): media_rate: %d, duplex: %d, PHY: %d",
		__FUNCTION__, cfg->media_rate, cfg->duplex, phy_idx);
	}

	// read PHY's Basic Mode Status Register to determine if PHY
	// is capable of auto negotiation
	an_capable = mx6q_mii_read(mx6q, phy_idx, MDI_BMSR) & BMSR_AN_ABILITY;

	//
	// if the user has specified the speed or duplex
	// or if the phy cannot auto-negotiate ...
	//
	if (mx6q->force_advertise != -1 || !an_capable) {
	
		reg = mx6q_mii_read(mx6q, phy_idx, MDI_BMCR);

		reg &= ~(BMCR_RESTART_AN | BMCR_SPEED_100 | BMCR_SPEED_1000 | BMCR_FULL_DUPLEX);

		if (an_capable && mx6q->force_advertise != 0) {
			/*
			 * If we force the speed, but the link partner
			 * is autonegotiating, there is a greater chance
			 * that everything will work if we advertise with
			 * the speed that we are forcing to.
			 */
			MDI_SetAdvert(mx6q->mdi, phy_idx, mx6q->force_advertise);

			reg |= BMCR_RESTART_AN | BMCR_AN_ENABLE;

			if (cfg->verbose) {
				log(LOG_ERR, "%s(): restricted autonegotiate (%dMbps only)",
					__FUNCTION__, cfg->media_rate / 1000);
			}
		}
		else {
			reg &= ~BMCR_AN_ENABLE;

			if (cfg->verbose) {
				log(LOG_ERR, "%s(): forcing the link", __FUNCTION__);
			}
		}

		mscr = 0;
		reg |= BMCR_FULL_DUPLEX;
		switch (cfg->media_rate) {
		case (10 * 1000):
			// 10/100 Mbps mode as opposed to 1000Mbps mode
			*(base + MX6Q_ECNTRL) &= ~ECNTRL_ETH_SPEED;

			// 10Mbps operation
			*(base + MX6Q_R_CNTRL) |= RCNTRL_RMII_10T;
			an_capable = (cfg->duplex > 0) ? ANAR_10bTFD : ANAR_10bT;
			break;
		case (100 * 1000):
			// 10/100 Mbps mode as opposed to 1000Mbps mode
			*(base + MX6Q_ECNTRL) &= ~ECNTRL_ETH_SPEED;

			// 100Mbps operation
			*(base + MX6Q_R_CNTRL) &= ~RCNTRL_RMII_10T;
			
			reg |= BMCR_SPEED_100;
			an_capable = (cfg->duplex > 0) ? ANAR_100bTFD : ANAR_100bT;
			break;
		case (1000 * 1000):
			if (cfg->media_rate == 1000*1000)
				*(base + MX6Q_ECNTRL) |= ECNTRL_ETH_SPEED;
				reg |= BMCR_SPEED_1000;
				an_capable = 0;
				mscr = (cfg->duplex > 0) ? MSCR_ADV_1000bTFD : MSCR_ADV_1000bT;
				break;
		default:
				log(LOG_ERR, "%s(): Unknown link speed\n", __FUNCTION__);
				break;
		}
		mx6q_mii_write(mx6q, phy_idx, MDI_ANAR, an_capable);

		mscr1 = mx6q_mii_read(mx6q, phy_idx, MDI_MSCR);
		// ensure PHY only advertises 1000Mbps speeds if 1000Mbps is specified
		mscr1 &= ~0x300;
		mscr1 |= mscr;
		mx6q_mii_write(mx6q, phy_idx, MDI_MSCR, mscr1);

		if (cfg->duplex > 0) {
			// full duplex enable
			*(base + MX6Q_X_CNTRL) |= XCNTRL_FDEN;

			// receive path operates independently of transmit (i.e. full duplex)
			*(base + MX6Q_R_CNTRL) &= ~RCNTRL_DRT;
		}

		switch (mx6q->flow) {
		case IFM_FLOW:
		    /* Enable Tx and Rx of Pause */
		    *(base + MX6Q_R_SECTION_EMPTY_ADDR) = 0x82;
		    *(base + MX6Q_R_CNTRL) |= RCNTRL_FCE;
		    break;
		case IFM_ETH_TXPAUSE:
		    /* Enable Tx of Pause */
		    *(base + MX6Q_R_SECTION_EMPTY_ADDR) = 0x82;
		    *(base + MX6Q_R_CNTRL) &= ~RCNTRL_FCE;
		    break;
		case IFM_ETH_RXPAUSE:
		    /* Enable Rx of Pause */
		    *(base + MX6Q_R_SECTION_EMPTY_ADDR) = 0;
		    *(base + MX6Q_R_CNTRL) |= RCNTRL_FCE;
		    break;
		default:
		    /* Disable all pause */
		    *(base + MX6Q_R_SECTION_EMPTY_ADDR) = 0;
		    *(base + MX6Q_R_CNTRL) &= ~RCNTRL_FCE;
		}

		if (reg & BMCR_AN_ENABLE) {
			if ((i = MDI_EnableMonitor(mx6q->mdi, 1)) != MDI_SUCCESS) {
				log(LOG_ERR, "%s(): MDI_EnableMonitor(forced) failed for phy %d: %d", __FUNCTION__, phy_idx, i);
			}
		}

	} 
	else {  // normal auto-negotiation mode
		
		cfg->flags |= NIC_FLAG_LINK_DOWN;
		/* Enable Pause in autoneg */
		MDI_GetMediaCapable(mx6q->mdi, phy_idx, &i);
		i |= MDI_FLOW | MDI_FLOW_ASYM;
		MDI_SetAdvert(mx6q->mdi, phy_idx, i);
		if ((i = MDI_AutoNegotiate(mx6q->mdi, phy_idx, NoWait)) != MDI_SUCCESS) {
			log(LOG_ERR, "%s(): MDI_AutoNegotiate() failed for phy %d: %d", __FUNCTION__, phy_idx, i);
		}

		if ((i = MDI_EnableMonitor(mx6q->mdi, 1)) != MDI_SUCCESS) {
			log(LOG_ERR, "%s(): MDI_EnableMonitor(auto) failed for phy %d: %d", __FUNCTION__, phy_idx, i);
		}
	}

	cfg->connector = NIC_CONNECTOR_MII;
	
}

__SRCVERSION( "$URL$ $REV$" )
