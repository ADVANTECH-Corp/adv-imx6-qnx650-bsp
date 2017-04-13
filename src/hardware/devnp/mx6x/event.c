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


#include "mx6q.h"


//
// this is the rx interrupt handler which directly masks off 
// the hardware interrupt at the nic, and should be a bit faster
//
const struct sigevent *
mx6q_isr_rx(void *arg, int iid)
{
	mx6q_dev_t		*mx6q;
	struct _iopkt_inter	*ient;
	volatile uint32_t	*base;


	mx6q	= arg;
	ient = &mx6q->inter;
	base = mx6q->reg;


	/*
	 * We have to make sure the interrupt is masked regardless
	 * of our on_list status.  This is because of a window where
	 * a shared (spurious) interrupt comes after on_list
	 * is knocked down but before the enable() callout is made.
	 * If enable() then happened to run after we masked, we
	 * could end up on the list without the interrupt masked
	 * which would cause the kernel more than a little grief
	 * if one of our real interrupts then came in.
	 *
	 * This window doesn't exist when using kermask since the
	 * interrupt isn't unmasked until all the enable()s run
	 * (mask count is tracked by kernel).
	 */

	*(base + MX6Q_IMASK) = 0xffffffff;

	return interrupt_queue(mx6q->iopkt, ient);
}


// 
// this is rx interrupt handler when using the kermask method
//
const struct sigevent *
mx6q_isr_rx_kermask(void *arg, int iid)
{
	mx6q_dev_t		*mx6q;
	struct _iopkt_inter	*ient;

	mx6q	= arg;
	ient = &mx6q->inter;


	mx6q->iid = iid;

	InterruptMask(mx6q->cfg.irq[0], iid);

	return interrupt_queue(mx6q->iopkt, ient);
}

int
mx6q_enable_rx_kermask(void *arg)
{
	mx6q_dev_t *mx6q = arg;

	InterruptUnmask(mx6q->cfg.irq[0], mx6q->iid);

	return 0;
}

int
mx6q_enable_rx(void *arg)
{
	mx6q_dev_t  		*mx6q= arg;
	volatile uint32_t  	*base	= mx6q->reg;

	*(base + MX6Q_IMASK) = (IMASK_TBIEN | IMASK_TFIEN | IMASK_RBIEN | IMASK_RFIEN | IMASK_BREN | IMASK_EBERREN | 
				IMASK_XFUNEN | IMASK_XFERREN | IMASK_RFERREN | IMASK_TS_AVAIL | IMASK_TS_TIMER);

	return 0;
}


//
// all three hardware interrupt sources are handled here
//
int
mx6q_process_interrupt(void *arg, struct nw_work_thread *wtp)
{
	mx6q_dev_t	*mx6q	= arg;
	uint32_t    	*base 		= mx6q->reg;
	uint32_t    	ievent;

	for (;;) {

		// read interrupt cause bits
		ievent = *(base + MX6Q_IEVENT);  

		if (mx6q->cfg.verbose > 6) {
			log(LOG_ERR, "%s(): ievent 0x%X\n", __FUNCTION__, ievent);
		}

		if (!ievent) {
			break;
		}

		*(base + MX6Q_IEVENT) = ievent;	

		if ((ievent & (IEVENT_RFINT | IEVENT_RXB)) != 0) {
			mx6q_receive(mx6q, wtp);
		} else if ((ievent & IEVENT_TS_AVAIL) != 0) {
			mx6q_transmit_complete(mx6q);
		} else if ((ievent & IEVENT_TS_TIMER) != 0) {
		    mx6q->rtc++;
		}
	}


	return 1;
}


__SRCVERSION( "$URL$ $REV$" )
