/*
 * $QNXLicenseC:
 * Copyright 2012, QNX Software Systems.
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
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/in.h>
#include <sys/proc.h>

/* Missing header files */
extern int hz;
int ltsleep(volatile const void *ident, int pri, const char *wmesg,
	    int timo, volatile struct simplelock *slock);

#ifndef ETHERTYPE_IEEE1588
#define ETHERTYPE_IEEE1588          0x88f7 // Native Ethernet(PTPv2)
#endif
#define MX6Q_PTP_EMSG_PORT          0x13f  // UDP(319) port for PTP event messages

#define PTP_MSG_SYNC                0x00     // PTP message:: Sync
#define PTP_MSG_DELAY_REQ           0x01     // PTP message:: Delay Request
#define PTP_MSG_PDELAY_REQ          0x02     // PTP message:: Path Delay Request
#define PTP_MSG_PDELAY_RESP         0x03     // PTP message:: Path Delay Response
#define PTP_MSG_DELAY_RESP          0x04     // PTP message:: Delay Response
#define PTP_MSG_ALL_OTHER           0x05     // Other PTP messages

uint32_t mx6q_clock_period; /* in nanoseconds */
uint32_t mx6q_clock_freq;   /* in Hz */

void mx6q_ptp_cal (mx6q_dev_t *mx6q)
{
    volatile uint32_t	*base = mx6q->reg;
    struct timespec	tv_start, tv_end;
    uint32_t		diff, count;
    static int		wait;
    int			timo;
    
    /* Stop the timer */
    *(base + MX6Q_TIMER_CTRLR) = 0;

    /* Reset it */
    *(base + MX6Q_TIMER_CTRLR) = MX6Q_TIMER_CTRL_RESTART;

    /* Clock it directly */
    *(base + MX6Q_TIMER_INCR) = 1;
    *(base + MX6Q_TIMER_CORR) = 0;

    /* Note when we start it */
    clock_gettime(CLOCK_MONOTONIC, &tv_start);
    *(base + MX6Q_TIMER_CTRLR) |= MX6Q_TIMER_CTRL_EN;

    /*
     * 1/5 sec is first that works, smaller results in same time
     * from clock_gettime() due to io-pkt timer tick frequency.
     * We will do 1/2 second as a compromise between accurate
     * calibration and not holding up driver init too much.
     */
    timo = hz / 2;
    ltsleep(&wait, 0, "mx6q_ptp_cal", timo, NULL);

    /* Note when we stop it */
    clock_gettime(CLOCK_MONOTONIC, &tv_end);
    *(base + MX6Q_TIMER_CTRLR) = 0;

    /* Read the timer */
    *(base + MX6Q_TIMER_CTRLR) = MX6Q_TIMER_CTRL_CAPTURE;
    count = *(base + MX6Q_TIMER_VALUER);

    diff = tv_end.tv_nsec - tv_start.tv_nsec;

    if (tv_end.tv_sec > tv_start.tv_sec) {
	diff += 1000 * 1000 * 1000;
    }

    if ((diff > 0) && (count > 0)) {
	mx6q_clock_period = (diff + (count / 2 ))/ count;
    } else {
	log(LOG_ERR, "Timestamp clock frequency unknown, defaulting to 125MHz");
	mx6q_clock_period = 8;
    }
    mx6q_clock_freq = 1000 * 1000 * 1000 / mx6q_clock_period;
}

/*
 * mx6q_ptp_start()
 *
 * Description: This function sets the default values for
 * the counter and resets the timer.
 *
 * Returns: always returns zero
 *
 */
int mx6q_ptp_start (mx6q_dev_t *mx6q)
{

    volatile uint32_t *base = mx6q->reg;

    mx6q_ptp_cal(mx6q);

    // Resets the timer to zero
    *(base + MX6Q_TIMER_CTRLR) = MX6Q_TIMER_CTRL_RESTART;

    // Set the clock period for the timestamping
    *(base + MX6Q_TIMER_INCR) = mx6q_clock_period;

    // Set the periodic events value
    *(base + MX6Q_TIMER_PERR) = MX6Q_TIMER_PER1SEC;

    // Enable periodic events
    *(base + MX6Q_TIMER_CTRLR) = MX6Q_TIMER_CTRL_PEREN;

    if (mx6q->cfg.verbose > 3) {
        log(LOG_ERR, "1588 Clock configuration:");
        log(LOG_ERR, "\t..Clock frequency=%u (Hz)", mx6q_clock_freq);
        log(LOG_ERR, "\t..Clock period=%u (ns. per tick)", mx6q_clock_period);
        log(LOG_ERR, "\t..Event period=%u (ns.)", MX6Q_TIMER_PER1SEC);
        log(LOG_ERR, "Enable 1588 clock counter");
    }
    // Enable timer
    *(base + MX6Q_TIMER_CTRLR) |= MX6Q_TIMER_CTRL_EN;

    return 0;
}


/*
 * mx6q_ptp_stop()
 *
 * Description: This function resets the timer and
 * turns it off.
 *
 * Returns: none
 *
 */
void mx6q_ptp_stop (mx6q_dev_t *mx6q)
{

    volatile uint32_t *base = mx6q->reg;

    // Disable timer
    *(base + MX6Q_TIMER_CTRLR) &= ~MX6Q_TIMER_CTRL_EN;

    // Resets the timer to zero
    *(base + MX6Q_TIMER_CTRLR) = MX6Q_TIMER_CTRL_RESTART;

    if (mx6q->cfg.verbose > 3) {
        log(LOG_ERR, "Disable 1588 clock counter");
    }
}


/*
 * mx6q_ptp_is_eventmsg()
 *
 * Description: This function checks the PTP message.
 *
 * Returns: If the frame contains the PTP event
 * message, returns 1, otherwise 0.
 *
 */
int mx6q_ptp_is_eventmsg (struct mbuf *m, ptpv2hdr_t **ph)
{

    int retval = 0;
    struct ether_header *eh;

    if (m == NULL) {
	log(LOG_ERR, "%s: NULL mbuf", __FUNCTION__);
	return 0;
    }

    eh = (struct ether_header *)m->m_data;

    switch(ntohs(eh->ether_type)) {

    case ETHERTYPE_IEEE1588:
        /* This is a native ethernet frame
         * defined for IEEE1588v2
         */
        if (ph != NULL) {
            *ph = (ptpv2hdr_t *)(m->m_data + ETHER_HDR_LEN);
        }
        retval = 1;
        break;

    case ETHERTYPE_IP:
        {
            struct ip *iph = (struct ip *)(m->m_data + ETHER_HDR_LEN);
            struct udphdr *udph = NULL;

            if (((iph != NULL) &&
                (iph->ip_p == IPPROTO_UDP))) {

                // Get UDP header and check for corresponding packet
                udph = (struct udphdr *)((uint32_t *)iph + iph->ip_hl);
                if ((udph != NULL) &&
                    (ntohs(udph->uh_dport) == MX6Q_PTP_EMSG_PORT) ) {
                    if (ph != NULL) {
                        *ph = (ptpv2hdr_t *)((uint8_t *)udph + sizeof(struct udphdr));
                    }
                    retval = 1;
                }
            }
        }
    	break;

    default:
        // TODO: need to add support for VLAN & IPv6 protocols
    	break;
    }
    return retval;
}


/*
 * mx6q_ptp_add_ts()
 *
 * Description: This function inserts the new timestamp
 * into the existing buffer.
 *
 * Returns: none
 *
 */
void mx6q_ptp_add_ts (mx6q_ptp_timestamp_t *ts, uint32_t rtc,
                     ptpv2hdr_t *ph, mpc_bd_t *bd)
{

    if ((ts != NULL) && (ph != NULL)) {
        unsigned short p_num =
                       ntohs( *((unsigned short *)(ph->sourcePortIdentity + 8)) );

        // Cleaning up the previous timestamp's values
        memset(ts, 0x00, sizeof(mx6q_ptp_timestamp_t));

        // Sequence id of the PTP message
        ts->sequence_id = ntohs(ph->sequenceId);

        // Copy the Clock identity fileds (source port identity and the port number)
        memcpy(ts->sport_identity, ph->sourcePortIdentity, 8);
        memcpy((ts->sport_identity + 8), (uint8_t *)&p_num, 2);

        // Timestamp of the PTP event message
        ts->timestamp.nsec = bd->timestamp;
        ts->timestamp.sec = rtc;  // Real time clock value
    }
}


/*
 * mx6q_ptp_add_rx_timestamp()
 *
 * Description: This function inserts RX timestamp into
 * the corresponding buffer (depends on the message type).
 * If the corresponding buffer already full, the timestamp
 * will be inserted into the start of the buffer
 *
 * Returns: none
 *
 */
void mx6q_ptp_add_rx_timestamp (mx6q_dev_t *mx6q, ptpv2hdr_t *ph, mpc_bd_t *bd)
{

    int *ts_cnt;

    if ((ph == NULL) || (bd == NULL) ||
        ((ph->version & 0x0f) != 0x2)) {
        /* Only PTPv2 currently supported */
        return;
    }

    switch (ph->messageId & 0x0f) {

    case PTP_MSG_SYNC:
        // This is a PTP SYNC message
        ts_cnt = &mx6q->rx_sync_cnt;
        mx6q_ptp_add_ts(&mx6q->rx_sync_ts[mx6q->rx_sync_cnt], mx6q->rtc, ph, bd);
        break;

    case PTP_MSG_DELAY_REQ:
        // This is a PTP DELAY REQUEST message
        ts_cnt = &mx6q->rx_delayreq_cnt;
        mx6q_ptp_add_ts(&mx6q->rx_delayreq_ts[mx6q->rx_delayreq_cnt], mx6q->rtc, ph, bd);
        break;

    case PTP_MSG_PDELAY_REQ:
        // This is a PTP PATH DELAY REQUEST message
        ts_cnt = &mx6q->rx_pdelayresp_cnt;
        mx6q_ptp_add_ts(&mx6q->rx_pdelayreq_ts[mx6q->rx_pdelayresp_cnt], mx6q->rtc, ph, bd);
        break;

    case PTP_MSG_PDELAY_RESP:
        // This is a PTP PATH DELAY RESPONSE message
        ts_cnt = &mx6q->rx_pdelayreq_cnt;
        mx6q_ptp_add_ts(&mx6q->rx_pdelayresp_ts[mx6q->rx_pdelayreq_cnt], mx6q->rtc, ph, bd);
        break;

    default:
        // Unknown message type, get out of here
        return;
    }
    // Checks if the buffer is already full
    if ((*ts_cnt + 1) >= MX6Q_RX_TIMESTAMP_BUF_SZ) {
        *ts_cnt = 0;
    } else {
        *ts_cnt = *ts_cnt + 1;
    }

    if (mx6q->cfg.verbose > 4) {
        log(LOG_ERR, "RX 1588 message:");
        log(LOG_ERR, "\t..message = %u", (ph->messageId & 0x0f));
        log(LOG_ERR, "\t..sequence = %u", ntohs(ph->sequenceId));
        log(LOG_ERR, "\t..timestamp = %u.%u\n", mx6q->rtc, bd->timestamp);
    }
}


/*
 * mx6q_ptp_add_tx_timestamp()
 *
 * Description: This function inserts TX timestamp into
 * the corresponding buffer (depends on the message type).
 * If the corresponding buffer already full, the timestamp
 * will be inserted into the start of the buffer
 *
 * Returns: none
 *
 */
void mx6q_ptp_add_tx_timestamp (mx6q_dev_t *mx6q, ptpv2hdr_t *ph, mpc_bd_t *bd)
{

    int *ts_cnt;

    if ((ph == NULL) || (bd == NULL) ||
        ((ph->version & 0x0f) != 0x2) ) {
        /* Only PTPv2 currently supported */
        return;
    }

    switch(ph->messageId & 0x0f) {

    case PTP_MSG_SYNC:
        // This is a PTP SYNC message
        ts_cnt = &mx6q->tx_sync_cnt;
        mx6q_ptp_add_ts(&mx6q->tx_sync_ts[mx6q->tx_sync_cnt], mx6q->rtc, ph, bd);
        break;

    case PTP_MSG_DELAY_REQ:
        // This is a PTP DELAY REQUEST message
        ts_cnt = &mx6q->tx_delayreq_cnt;
        mx6q_ptp_add_ts(&mx6q->tx_delayreq_ts[mx6q->tx_delayreq_cnt], mx6q->rtc, ph, bd);
        break;

    case PTP_MSG_PDELAY_REQ:
        // This is a PTP PATH DELAY REQUEST message
        ts_cnt = &mx6q->tx_pdelayresp_cnt;
        mx6q_ptp_add_ts(&mx6q->tx_pdelayreq_ts[mx6q->tx_pdelayresp_cnt], mx6q->rtc, ph, bd);
        break;

    case PTP_MSG_PDELAY_RESP:
        // This is a PTP PATH DELAY RESPONSE message
        ts_cnt = &mx6q->tx_pdelayreq_cnt;
        mx6q_ptp_add_ts(&mx6q->tx_pdelayresp_ts[mx6q->tx_pdelayreq_cnt], mx6q->rtc, ph, bd);
        break;

    default:
        // Unknown message type, get out of here
        return;
    }
    // Checks if the buffer is already full
    if ((*ts_cnt + 1) >= MX6Q_TX_TIMESTAMP_BUF_SZ) {
        *ts_cnt = 0;
    } else {
        *ts_cnt = *ts_cnt + 1;
    }
    if (mx6q->cfg.verbose > 4) {
        log(LOG_ERR, "TX 1588 message:");
        log(LOG_ERR, "\t..message = %u", (ph->messageId & 0x0f));
        log(LOG_ERR, "\t..sequence = %u", ntohs(ph->sequenceId));
        log(LOG_ERR, "\t..timestamp = %u.%u\n", mx6q->rtc, bd->timestamp);
    }
}


/*
 * mx6q_ptp_get_ts()
 *
 * Description: This function searches a timestamp
 * according to message type, sequence id and the source port id.
 *
 * Returns: Non zero value if the timestamp has been found. Timestamp will
 * be copyied into the passed structure.
 *
 */
int mx6q_ptp_get_ts (mx6q_ptp_timestamp_t *tsbuff, mx6q_ptp_extts_t *extts,
		     int buffsz)
{

    int i;

    /* TODO: Think about what happend if the frame is going to be
     * transmitted or received, but this not happend yet. */
    if ((tsbuff == NULL) || (extts == NULL) || (buffsz == 0)) {
        return 0;
    }

    for (i = 0; i < buffsz; i++) {
        if ((extts->sequence_id == tsbuff[i].sequence_id)) {
            // We found it!
            extts->ts.nsec = tsbuff[i].timestamp.nsec;
            extts->ts.sec = tsbuff[i].timestamp.sec;
            return 1;
        }
    }
    return 0;
}


/*
 * mx6q_ptp_get_rx_timestamp()
 *
 * Description: This function searches a timestamp in the corresponding RX buffer,
 * according to message type, sequence id and the source port id.
 *
 * Returns: Non zero value if the timestamp has been found. Timestamp will
 * be copyied into the passed structure.
 *
 */
int mx6q_ptp_get_rx_timestamp (mx6q_dev_t *mx6q, mx6q_ptp_extts_t *ts)
{

    if (ts != NULL) {

        switch(ts->msg_type) {

        case PTP_MSG_SYNC:
            // This is a PTP SYNC message
            return mx6q_ptp_get_ts(mx6q->rx_sync_ts, ts,
                                   MX6Q_RX_TIMESTAMP_BUF_SZ);

        case PTP_MSG_DELAY_REQ:
            // This is a PTP DELAY REQUEST message
            return mx6q_ptp_get_ts(mx6q->rx_delayreq_ts, ts,
                                   MX6Q_RX_TIMESTAMP_BUF_SZ);

        case PTP_MSG_PDELAY_REQ:
            // This is a PTP PATH DELAY REQUEST message
            return mx6q_ptp_get_ts(mx6q->rx_pdelayreq_ts, ts,
                                   MX6Q_RX_TIMESTAMP_BUF_SZ);

        case PTP_MSG_PDELAY_RESP:
            // This is a PTP PATH DELAY RESPONSE message
            return mx6q_ptp_get_ts(mx6q->rx_pdelayresp_ts, ts,
                                   MX6Q_RX_TIMESTAMP_BUF_SZ);
        default:
            break;
        }
    }
    return 0;
}


/*
 * mx6q_ptp_get_tx_timestamp()
 *
 * Description: This function searches a timestamp in the corresponding TX buffer,
 * according to message type, sequence id and the source port id.
 *
 * Returns: Non zero value if the timestamp has been found. Timestamp will
 * be copyied into the passed structure.
 *
 */
int mx6q_ptp_get_tx_timestamp (mx6q_dev_t *mx6q, mx6q_ptp_extts_t *ts)
{

    if (ts != NULL) {

        switch(ts->msg_type) {

        case PTP_MSG_SYNC:
            // This is a PTP SYNC message
            return mx6q_ptp_get_ts(mx6q->tx_sync_ts, ts,
                                   MX6Q_TX_TIMESTAMP_BUF_SZ);

        case PTP_MSG_DELAY_REQ:
            // This is a PTP DELAY REQUEST message
            return mx6q_ptp_get_ts(mx6q->tx_delayreq_ts, ts,
                                   MX6Q_TX_TIMESTAMP_BUF_SZ);

        case PTP_MSG_PDELAY_REQ:
            // This is a PTP PATH DELAY REQUEST message
            return mx6q_ptp_get_ts(mx6q->tx_pdelayreq_ts, ts,
                                   MX6Q_TX_TIMESTAMP_BUF_SZ);

        case PTP_MSG_PDELAY_RESP:
            // This is a PTP PATH DELAY RESPONSE message
            return mx6q_ptp_get_ts(mx6q->tx_pdelayresp_ts, ts,
                                   MX6Q_TX_TIMESTAMP_BUF_SZ);
        default:
            break;
        }
    }
    return 0;
}


/*
 * mx6q_ptp_get_cnt()
 *
 * Description: This function returns the current timer value.
 *
 * Returns: none
 *
 */
void mx6q_ptp_get_cnt (mx6q_dev_t *mx6q, mx6q_ptp_time_t *cnt)
{

    volatile uint32_t *base = mx6q->reg;
    uint32_t tmp;

    if (cnt != NULL) {

        // Set capture flag
        tmp = *(base + MX6Q_TIMER_CTRLR);
        tmp |= MX6Q_TIMER_CTRL_CAPTURE;

        *(base + MX6Q_TIMER_CTRLR) = tmp;

        // Capture the timer value
        cnt->nsec = *(base + MX6Q_TIMER_VALUER);
        cnt->sec = mx6q->rtc;
    }
}


/*
 * mx6q_ptp_set_cnt()
 *
 * Description: This function sets the current timer value.
 *
 * Returns: none
 *
 */
void mx6q_ptp_set_cnt (mx6q_dev_t *mx6q, mx6q_ptp_time_t *cnt)
{

    volatile uint32_t *base = mx6q->reg;

    if (cnt != NULL) {

        mx6q->rtc = cnt->sec;
        *(base + MX6Q_TIMER_VALUER) = cnt->nsec;
    }
}


/*
 * mx6q_ptp_set_compensation()
 *
 * Description: Sets the clock compensation.
 *
 * Inputs: offset is correction of nanoseconds in 1 second,
 *         i.e. parts-per-billion.
 *         ops is direction of offset, 1 means slow down.
 *
 * Returns: none
 *
 */
void mx6q_ptp_set_compensation (mx6q_dev_t *mx6q, mx6q_ptp_comp_t *ptc)
{

    volatile uint32_t	*base = mx6q->reg;
    uint32_t		inc, err = 0, min_err = ~0;
    uint32_t		i, inc_cor, cor_period, rem_offset;
    uint32_t		test_period, test_offset;

    if (ptc == NULL) {
        log(LOG_ERR, "%s: NULL parameter", __FUNCTION__);
        return;
    }

    if (ptc->offset == 0) {
        /* Reset back to default */
	*(base + MX6Q_TIMER_CORR) = 0;
	*(base + MX6Q_TIMER_INCR) = mx6q_clock_period;
	return;
    }

    /* Allow a frequency swing of up to half our clock */
    if (ptc->offset / mx6q_clock_freq > (mx6q_clock_freq / 2)) {
	log(LOG_ERR, "%s: offset %d too big, ignoring",
	    __FUNCTION__, ptc->offset);
	return;
    }

    /* Deal with any major correction by updating the base increment */
    inc = mx6q_clock_period;
    if (ptc->offset >= mx6q_clock_freq) {
	if (ptc->ops) {
	    inc += ptc->offset / mx6q_clock_freq;
	} else {
	    inc -= ptc->offset / mx6q_clock_freq;
	}
    }

    /* Find the best correction factor */
    rem_offset = ptc->offset - (ptc->offset / mx6q_clock_freq);
    inc_cor = inc;
    cor_period = 0;
    for (i = 1; i <= inc; i++) {
	test_period = (mx6q_clock_freq * i) / rem_offset;
	test_offset = (mx6q_clock_freq * i) / test_period;

	/* Integer division so never too small */
	err = test_offset - ptc->offset;
	if (err == 0) {
	    inc_cor = i;
	    cor_period = test_period;
	    break;
	} else if (err < min_err) {
	    inc_cor = i;
	    cor_period = test_period;
	    min_err = err;
	}
    }

    if (ptc->ops) {
	inc_cor = inc + inc_cor;
    } else {
	inc_cor = inc - inc_cor;
    }
    inc |= inc_cor << MX6Q_TIMER_INCR_CORR_OFF;

    *(base + MX6Q_TIMER_INCR) = inc;
    *(base + MX6Q_TIMER_CORR) = cor_period;
} 

/*
 * mx6q_ptp_ioctl()
 *
 * Description: This is a PTP IO control function
 *
 * Returns: Non zero value if the error has been occured.
 *
 */
int mx6q_ptp_ioctl (mx6q_dev_t *mx6q, struct ifdrv *ifd)
{
    mx6q_ptp_extts_t	ts;
    int			vm_flags, err = 0;
    struct proc		*p;

    if (ifd != NULL) {
        switch(ifd->ifd_cmd) {

        case PTP_GET_RX_TIMESTAMP:
        case PTP_GET_TX_TIMESTAMP:
            {
                if (ifd->ifd_len == sizeof(mx6q_ptp_extts_t)) {
		  /* Save any VM_NOCTXT for later copyout */
		    p = curproc;
		    vm_flags = p->p_vmspace.vm_flags;
                    if (!copyin((((uint8_t *)ifd) + sizeof(struct ifdrv)),
				&ts, sizeof(mx6q_ptp_extts_t))) {

			/* Restore it */
			p->p_vmspace.vm_flags = vm_flags;

                        if (ifd->ifd_cmd == PTP_GET_RX_TIMESTAMP) {
                            err = mx6q_ptp_get_rx_timestamp(mx6q, &ts);
                        } else {
                            err = mx6q_ptp_get_tx_timestamp(mx6q, &ts);
                        }
                        if (err) {
                            copyout(&ts, (((uint8_t *)ifd) + sizeof(struct ifdrv)),
                                    sizeof(mx6q_ptp_extts_t));
                        }
                        return (err)? EOK : ENOENT;
                    }
                }
                return EINVAL;
            }

        case PTP_GET_TIME:
            {
                if (ifd->ifd_len == sizeof(mx6q_ptp_time_t)) {

                    mx6q_ptp_time_t c;

                    mx6q_ptp_get_cnt(mx6q, &c);
                    copyout(&c, (((uint8_t *)ifd) + sizeof(struct ifdrv)),
                            sizeof(mx6q_ptp_time_t));
                    if (mx6q->cfg.verbose > 4) {
                        log(LOG_ERR, "%s(): 1588 Clock time: %u.%u\n", __FUNCTION__,
                            c.sec, c.nsec);
                    }
                    return EOK;
                }
                return EINVAL;
            }

        case PTP_SET_TIME:
            {
                if (ifd->ifd_len == sizeof(mx6q_ptp_time_t)) {

                    mx6q_ptp_time_t c;

                    if (!copyin((((uint8_t *)ifd) + sizeof(struct ifdrv)),
                                &c, sizeof(mx6q_ptp_time_t))) {
                        mx6q_ptp_set_cnt(mx6q, &c);
                        if (mx6q->cfg.verbose > 4) {
                            log(LOG_ERR, "%s(): Set %u.%u as 1588 Clock time\n", __FUNCTION__,
                                c.sec, c.nsec);
                        }
                        return EOK;
                    }
                }
                return EINVAL;
            }

        case PTP_SET_COMPENSATION:
            {
                if (ifd->ifd_len == sizeof(mx6q_ptp_comp_t)) {

                    mx6q_ptp_comp_t ptc;

                    if (!copyin((((uint8_t *)ifd) + sizeof(struct ifdrv)),
                                &ptc, sizeof(mx6q_ptp_comp_t))) {
                        mx6q_ptp_set_compensation(mx6q, &ptc);
                        return EOK;
                    }
                }
                return EINVAL;
            }

        case PTP_GET_COMPENSATION:
            /* TODO: Not yet implemented */
            return ENOTSUP;

        default:
            break;
        }
    }
    return EINVAL;
}

__SRCVERSION( "$URL$ $REV$" )
