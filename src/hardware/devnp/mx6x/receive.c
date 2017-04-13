/*
 * $QNXLicenseC: 
 * Copyright 2010, QNX Software Systems.  
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


#include "bpfilter.h"

#include "mx6q.h"

#if NBPFILTER > 0
#include <net/bpf.h>
#include <net/bpfdesc.h>
#endif

extern void dump_mib(mx6q_dev_t *mx6q);

static void 
mx6q_add_pkt (mx6q_dev_t *mx6q, struct mbuf *new, int idx)
{
    	mpc_bd_t	*bd		= &mx6q->rx_bd[idx];
   	 uint16_t  	status	= RXBD_E;
	off64_t    	phys    = pool_phys(new->m_data, new->m_ext.ext_page);
	CACHE_INVAL(&mx6q->cachectl, new->m_data, phys, new->m_ext.ext_size);

	// set wrap bit if on last rx descriptor
	if (idx == mx6q->num_rx_descriptors -1) {
		status |= RXBD_W;
	}

	// remember mbuf pointer for this rx descriptor
	mx6q->rx_pkts[idx] = new;

	// stuff rx descriptor
    	bd->buffer = (uint32_t) phys;
    	bd->length = 0;
   	bd->status = status;
}



#define DP (unsigned char *)rpkt->m_data

void
mx6q_receive (mx6q_dev_t *mx6q, struct nw_work_thread *wtp)
{
	struct mbuf		*rpkt;
	struct mbuf		*new;
	ptpv2hdr_t		*ph;
	int      		this_idx;
    	uint16_t  		status;
    	uint16_t  		length;
    	mpc_bd_t  		*rx_bd;
	struct ifnet		*ifp = &mx6q->ecom.ec_if;
	volatile uint32_t	*base = mx6q->reg;

	// probe phy optimization - rx pkt activity
	mx6q->rxd_pkts = 1;  

	for(;;) {

		if (mx6q->cfg.verbose > 5) {
			log(LOG_ERR, "%s(): rx_cidx %d\n", __FUNCTION__, mx6q->rx_cidx);
		}

		// is there an rxd packet at the next descriptor?
		this_idx	= mx6q->rx_cidx;
		rx_bd		= &mx6q->rx_bd[this_idx];
		status		= rx_bd->status;
		if (status & RXBD_E) {
			break;
		}
		// always includes 4 byte crc and on en0 
		length = (rx_bd->length & 0x7FF) - 4; 

		// pull rxd packet out of corresponding queue
		rpkt = mx6q->rx_pkts[this_idx];		
	        mx6q->rx_pkts[this_idx] = NULL;

		// update rx descriptor consumer index for next loop iteration
        	mx6q->rx_cidx = NEXT_RX(this_idx);
		if (mx6q->rx_cidx == 0) {
			mx6q_update_stats(mx6q);
		}

		// any problems with this rxd packet?
	if (status & RXBD_ERR) {
			// give old packet back to nic
        	mx6q_add_pkt(mx6q, rpkt, this_idx);  
			log(LOG_ERR, "%s(): status RXBD_ERR 0x%X", __FUNCTION__, status);
			ifp->if_ierrors++;
            	continue;
        }
		
		// get an empty mbuf 
		new = m_getcl_wtp(M_DONTWAIT, MT_DATA, M_PKTHDR, wtp);
		if (new == NULL) {
			// give old mbuf back to nic
			mx6q_add_pkt(mx6q, rpkt, this_idx);
			log(LOG_ERR, "%s(): mbuf alloc failed!", __FUNCTION__);
			mx6q->stats.rx_failed_allocs++;
			ifp->if_ierrors++;
				continue;
		}

		// give new mbuf to nic - modifies what rx_bd points to!!
		mx6q_add_pkt(mx6q, new, this_idx);

		// dump frag if user requested it with verbose=8 cmd line option
		if (mx6q->cfg.verbose > 7) {
			unsigned char *p = (unsigned char *)rpkt->m_data;
			int len = length;
			int I;
		
			log(LOG_ERR,"Rxd dev_idx %d  bytes %d\n", mx6q->cfg.device_index, len);

			for (I=0; I<min(len,80); I++) {
				log(LOG_ERR,"%02X ",*p++);
				if ((I+1)%16 == 0) {
					log(LOG_ERR,"\n");
				}
			}
			if (I < 64) {
				log(LOG_ERR,"\n");
			}
		}


		// update rpkt for this rxd fragment
		rpkt->m_pkthdr.rcvif = ifp;
		rpkt->m_len = length;  // if last of fragmented, corrected below
		rpkt->m_next = 0;

		// early-out high runner single packet
		if (status &  RXBD_L) {
			rpkt->m_pkthdr.len = rpkt->m_len;
			goto pass_it_up;
		}

pass_it_up:

#if NBPFILTER > 0
			/* Pass this up to any BPF listeners. */
			if (ifp->if_bpf) {
				bpf_mtap(ifp->if_bpf, rpkt);
			}
#endif
			if (mx6q_ptp_is_eventmsg(rpkt, &ph)) {
			    mx6q_ptp_add_rx_timestamp(mx6q, ph, rx_bd);
			}

			// finally, pass this received pkt up
			ifp->if_ipackets++;
			(*ifp->if_input)(ifp, rpkt);

	} // for

	// If descriptors were full we've now cleared space, restart receive
	*(base + MX6Q_R_DES_ACTIVE) = R_DES_ACTIVE;

} // mx6q_receive()


__SRCVERSION( "$URL$ $REV$" )
