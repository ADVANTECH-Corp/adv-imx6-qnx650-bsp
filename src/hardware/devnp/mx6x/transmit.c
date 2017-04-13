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


//
// this function is called only if the packet is ridiculously
// fragmented.  If we are lucky, the entire packet will fit into
// one cluster so we manually defrag.  However, if this is a jumbo
// packet, and io-pkt is running without large values for mclbytes
// and pagesize, we are left with no choice but to dup the packet
//
static struct mbuf *
mx6q_defrag(struct mbuf *m)
{
	struct mbuf *m2;

	if (m->m_pkthdr.len > MCLBYTES) { 
		//
		// this is a jumbo packet which just happens to
		// be larger than a single cluster, which means
		// that the normal stuff below wont work.  So, 
		// we try a "deep" copy of this packet, which is
		// time-consuming, but is probably better than		
		// just dropping the packet
		//
		int num_frag;

		m2 = m_dup(m, 0, m->m_pkthdr.len, M_DONTWAIT);
		if (!m2) {
			m_freem(m);
	        return NULL;
		}
		m = m2;  // m is now the defragged packet

		// ensure that dupd packet is not excessively fragmented
		for (num_frag=0, m2=m; m2; num_frag++) {
			m2 = m2->m_next;
		}
		if (num_frag > MX6Q_MAX_FRAGS) {
			// this should never happen
			m_freem(m);
	        return NULL;
		}

		return m;
	}

	// the entire packet should fit into one cluster

    MGET(m2, M_DONTWAIT, MT_DATA);
    if (m2 == NULL) {
		m_freem(m);
        return NULL;
    }

	M_COPY_PKTHDR(m2, m);

    MCLGET(m2, M_DONTWAIT);
    if ((m2->m_flags & M_EXT) == 0) {
		m_freem(m);
        m_freem(m2);
        return NULL;
    }

	// this is NOT paranoid - this can happen with jumbo packets bigger 
	// than a single cluster - should be handled above
	if (m->m_pkthdr.len > m2->m_ext.ext_size) {
		m_freem(m);
        m_freem(m2);
		return NULL;
	}

    m_copydata(m, 0, m->m_pkthdr.len, mtod(m2, caddr_t));
    m2->m_pkthdr.len = m2->m_len = m->m_pkthdr.len;

	m_freem(m);

    return m2;
}

static void
mx6q_kick_tx (void *arg)
{
	mx6q_dev_t *mx6q = arg;

	NW_SIGLOCK(&mx6q->ecom.ec_if.if_snd_ex,
		   mx6q->iopkt);
	mx6q_start(&mx6q->ecom.ec_if);
}

#define DP (unsigned char *)m->m_data

//
//  io-pkt entry point for transmit
//
//  Called with ifp->if_snd_mutex held and IFF_OACTIVE not
//  yet set in ifp->if_flags_tx.
//
void
mx6q_start(struct ifnet *ifp)
{
	mx6q_dev_t		*mx6q = ifp->if_softc;
	struct mbuf		*m;
	struct mbuf		*m2;
	uint32_t        	*base = mx6q->reg;
	mpc_bd_t  		*tx_bd=0;          // compiler
	volatile mpc_bd_t 	*tx_bd_first=0;    // warnings
	int			idx;
	int			num_frag;
	int			num_frag_zero_len;
	int			I;
	int			desc_avail;
	int			extra_descr_used;
	int			ts_needed = 0;

	if ((ifp->if_flags_tx & IFF_RUNNING) == 0) {
		NW_SIGUNLOCK(&ifp->if_snd_ex, mx6q->iopkt);
		return;
	}

	ifp->if_flags_tx |= IFF_OACTIVE;

	for (;;) {

		desc_avail = mx6q->num_tx_descriptors - mx6q->tx_descr_inuse;


		//
		// Ensure that there is enough room in the tx descriptor
		// ring for the most fragmented packet we will attempt to tx
		//
		// Also, every N times we are called, clean up old tx descr
		// where N is the number of tx descr divided by 8 ie shifted
		// right 3 bits.  Under load, this has the effect of cleaning
		// up old descriptors 8 times, each trip around the ring.  
		//
		// Here are some values for N:
		//
		// With the default  256 tx descr, we clean up every  256/8 =  32 calls
		// With the maximum 2048 tx descr, we clean up every 2048/8 = 256 calls
		// With the minimum   32 tx descr, we clean up every   32/8 =   4 calls
		//
		// The bigger the ring, the more mbufs we will let sit in the tx
		// descriptor ring until we harvest them, for maximum performance 
		// under load: we will allow an enormous burst of packets (eg 256)
		// to be transmitted without stopping to smell the flowers
		//
		// The smaller the ring, the fewer mbufs we will let sit in the tx
		// descriptor ring until we harvest them, for minimum mbuf use
		// and resulting minimal memory consumption
		//
		// NB: if no one transmits packets in a long time, the periodic
		// timer (2 seconds) detects this and harvests the txd mbufs
		// out of the tx descriptor ring
		//

		if ((desc_avail < (MX6Q_MAX_FRAGS + MX6Q_UNUSED_DESCR)) ||
		  !(desc_avail % (mx6q->num_tx_descriptors >> 3)) ) {

			// see if we can clean out txd pkts from descr ring
			mx6q_transmit_complete(mx6q);

			// did we free any up?
			desc_avail = mx6q->num_tx_descriptors - mx6q->tx_descr_inuse;

			if (desc_avail < (MX6Q_MAX_FRAGS + MX6Q_UNUSED_DESCR)) {
				// Set a callback to try again later
				callout_msec(&mx6q->tx_callout, 1, mx6q_kick_tx, mx6q);
				// Leave IFF_OACTIVE set so the stack doesn't call us again.
				NW_SIGUNLOCK(&ifp->if_snd_ex, mx6q->iopkt);
				return;
			}
		}

		//
		// ok, now we know that we have room for at least one heavily
		// fragmented packet, get the next mbuf for tx
		//
		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (!m) {
			goto done;  // nothing to tx, we go home now
		}

		// count up mbuf fragments
		for (num_frag=0, num_frag_zero_len=0, m2=m; m2; num_frag++) {
			if (m2->m_len == 0) {
				num_frag_zero_len++;
			}
			m2 = m2->m_next;
		}

		// ridiculously fragmented?
		if (num_frag > MX6Q_MAX_FRAGS) {
			if ((m2 = mx6q_defrag(m)) == NULL) {
				log(LOG_ERR, "%s(): mx6q_defrag() failed", __FUNCTION__);
				mx6q->stats.tx_failed_allocs++;
				ifp->if_oerrors++;
				continue;
			}

			// we have a new best friend
			m = m2;

			// must count mbuf fragments again
			for (num_frag=0, num_frag_zero_len=0, m2=m; m2; num_frag++) {
				if (m2->m_len == 0) {
					num_frag_zero_len++;
				}
				m2 = m2->m_next;
			}
		}
		extra_descr_used = 0;

		// Should we try to do hardare checksumming?  Somewhat surprisingly,
		// there is a noticeable drop in performance when you enable tx hw
		// csum on the 8548, I suspect because of the extra transmit descriptor
		
		if (mx6q->cfg.verbose > 6) {
			log(LOG_ERR, 
			  "%s(): num_frag %d  desc_avail %d  tx_pidx %d  tx_cidx %d  tx_descr_inuse %d num_tx_descriptors %d\n", 
			  __FUNCTION__, num_frag, desc_avail, mx6q->tx_pidx, mx6q->tx_cidx, mx6q->tx_descr_inuse, 
			  mx6q->num_tx_descriptors);
		}

		// dump header of txd packets if user requested it with verbose=8 cmd line option
		if (mx6q->cfg.verbose > 7) {
			int len = m->m_len;
			int I;

			log(LOG_ERR,"Txd dev_idx %d  num_frag %d  first descr bytes %d\n", 
					  mx6q->cfg.device_index, num_frag, len);

			// Debug - print every packet to slog
			unsigned char *p = (unsigned char *)m->m_data;
			for (I=0; I<min(len,64); I++) {
				log(LOG_ERR,"%02X ",*p++);
				if ((I+1)%16 == 0) {
					log(LOG_ERR, "\n");
				}
			}
			if (I < 64)
				log(LOG_ERR,"\n");
		}
		// We have a new frame, clear the timestamp flag
		ts_needed = 0;

		// load up descriptors
		for (I=0, m2=m, idx=mx6q->tx_pidx; I<num_frag; I++, m2=m2->m_next) {

			// stack likes to pass us zero length fragments, which
			// causes the en0 tx to halt - see data sheet.  So, we
			// skip over zero length fragments
			if (m2->m_len == 0) {
				continue;
			}

			// calc pointer to next descriptor we should load
			tx_bd = &mx6q->tx_bd[idx];

			if (tx_bd->status & TXBD_R) {
				//
				// this should NEVER happen - see MX6Q_UNUSED_DESCR.  Reset chip?
				//
				log(LOG_ERR, "%s(): Big Problem: out of sync with nic!!", __FUNCTION__);
				mx6q->stats.un.estats.internal_tx_errors++;
				ifp->if_oerrors++;
				m_freem(m);
				// Set a callback to try again later
				callout_msec(&mx6q->tx_callout, 1, mx6q_kick_tx, mx6q);
				// Leave IFF_OACTIVE set so the stack doesn't call us again.
				NW_SIGUNLOCK(&ifp->if_snd_ex, mx6q->iopkt);
				return;
			}

			uint32_t  mbuf_phys_addr= mbuf_phys(m2);
			uint16_t  mbuf_phys_len	= m2->m_len;

			tx_bd->buffer = mbuf_phys_addr;
			tx_bd->length = mbuf_phys_len;
			CACHE_FLUSH(&mx6q->cachectl, m2->m_data, mbuf_phys_addr, m2->m_len);

			if (I) {
				// this is NOT first descr, set the READY bit
				tx_bd->status |= TXBD_R;
			} else {
				// remember location of first descr, DONT set READY bit
				tx_bd_first = tx_bd;
				if (mx6q_ptp_is_eventmsg(m2, NULL)) {
					// It seems, this is a PTP event message
					ts_needed = 1;
				}
			}
			/* Check if the frame must be time stamped. Only the first
			 * fragment is checked, if it contains PTP header all fragments
			 * of this frame will be time stamped too
			 */
			if (ts_needed) {
				tx_bd->estatus |= TXBD_ESTATUS_TS;
			} else {
				tx_bd->estatus &= ~TXBD_ESTATUS_TS;
			}

			if (mx6q->cfg.verbose > 6) {
				log(LOG_ERR,"normal:      idx %d  len %d  stat 0x%X \n", idx, tx_bd->length, tx_bd->status);
			}

			// we have loaded this descriptor, onto the next
			idx = NEXT_TX(idx);
			if (idx == 0) {
				mx6q_update_stats(mx6q);
			}
		}

		// remember the number of tx descriptors used this time
		mx6q->tx_descr_inuse += (num_frag - num_frag_zero_len + extra_descr_used);

		// remember mbuf pointer for after tx.  For multiple descriptor
		// transmissions, middle and last descriptors have a zero mbuf ptr
		mx6q->tx_pkts[mx6q->tx_pidx] = m;

		// advance producer index to next unused descriptor, using modulo macro above
		mx6q->tx_pidx = idx;
		
		//
		// start transmission of this packet by:
		//
		// 1) setting LAST  bit in last descriptor, and
		// 2) setting READY bit in first descriptor
		//
		// at the completion of the loop, tx_bd_first
		// is set, and tx_bd points to last descr filled
		//
		if ((num_frag == 1) && !extra_descr_used) {  // single descriptor

			// set LAST and READY bits
			tx_bd->status |= TXBD_R | TXBD_L | TXBD_TC;

			if (mx6q->cfg.verbose > 6) {
				log(LOG_ERR,"enable: single: stat 0x%X \n", tx_bd->status);
			}
		} else { // multiple descriptors

			// set LAST bit in last descriptor we loaded
			tx_bd->status |= TXBD_L | TXBD_TC;

			// and set READY bit in first descriptor
			tx_bd_first->status |= TXBD_R;

			if (mx6q->cfg.verbose > 6) {
				log(LOG_ERR,"enable: multi: first stat 0x%X  last stat 0x%X \n", tx_bd_first->status, tx_bd->status);
			}
		}
	 *(base + MX6Q_X_DES_ACTIVE) = X_DES_ACTIVE;
#if NBPFILTER > 0
		// Pass the packet to any BPF listeners
		if (ifp->if_bpf) {
			bpf_mtap(ifp->if_bpf, m);
		}
#endif

	} // for

done:
	ifp->if_flags_tx &= ~IFF_OACTIVE;

	NW_SIGUNLOCK(&ifp->if_snd_ex, mx6q->iopkt);
	return;
}


//
// process completed tx descriptors - call with if_snd_ex
//
void
mx6q_transmit_complete(mx6q_dev_t *mx6q)
{
    int			idx;
    mpc_bd_t		*bd;
    uint16_t		status;
    uint32_t		bdu;
    struct mbuf		*m;
    struct ifnet	*ifp = &mx6q->ecom.ec_if;
    ptpv2hdr_t		*ph = NULL;


    if (mx6q->cfg.verbose > 5) {
	log(LOG_ERR, "%s(): starting: tx_pidx %d  tx_cidx %d  tx_descr_inuse %d", 
	    __FUNCTION__, mx6q->tx_pidx, mx6q->tx_cidx, mx6q->tx_descr_inuse);
    }

    // set flag indicating we have clean out descr ring recently
    mx6q->tx_reaped = 1;

    while (mx6q->tx_pidx != mx6q->tx_cidx) {
	idx = mx6q->tx_cidx;
	bd  = &mx6q->tx_bd[idx];

        status = bd->status;
	bdu = bd->bdu;
        if ((status & TXBD_R) || !(bdu & BD_BDU)) {
	    break; // nic still owns this descriptor
	}

	if (mx6q->cfg.verbose > 6) {
	    log(LOG_ERR,"tx done: idx %d  stat 0x%X", idx, status);
	}

	//
	// only FIRST descriptor will have corresponding mbuf pointer
	//
	if ((m = mx6q->tx_pkts[idx])) {

	    /* Collect any timestamp */
	    if ((bd->estatus & TXBD_ESTATUS_TS) &&
		mx6q_ptp_is_eventmsg(m, &ph)) {
		mx6q_ptp_add_tx_timestamp(mx6q, ph, bd);
	    }

	    m_freem(m);
	    mx6q->tx_pkts[idx] = NULL;
	    ifp->if_opackets++;
	}

	// leave only WRAP bit if was already set
        bd->status = status & TXBD_W;
	bd->estatus = 0;
	bd->bdu = 0;

	// this tx descriptor is available for use now
	mx6q->tx_descr_inuse--;
        mx6q->tx_cidx = NEXT_TX(idx);
	if (mx6q->tx_cidx == 0) {
	    mx6q_update_stats(mx6q);
	}

    }

    if (mx6q->cfg.verbose > 5) {
        log(LOG_ERR, "%s(): ending:   tx_pidx %d  tx_cidx %d  tx_descr_inuse %d",
	    __FUNCTION__, mx6q->tx_pidx, mx6q->tx_cidx, mx6q->tx_descr_inuse);
    }
}


__SRCVERSION( "$URL$ $REV$" )
