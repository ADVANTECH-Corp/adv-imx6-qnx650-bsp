/*
 * $QNXLicenseC:
 * Copyright 2007, 2008, QNX Software Systems.
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







#include "externs.h"

#define    MX1_RXERR    (MX1_URXD_ERR | MX1_URXD_OVERRUN | MX1_URXD_FRMERR | MX1_URXD_BRK | MX1_URXD_PRERR)

#if USE_DMA
/*
 * TX Pulse Handler - Gets notified once TX DMA is done
 */
void mx53_tx_pulse_hdlr(DEV_MX1 *dev, struct sigevent *event)
{
    int    status = 0;

    dev->sdmafuncs.xfer_complete(dev->tx_dma.dma_chn);

    dev->tty.un.s.tx_tmr = 0;
    /* Send event to io-char, tto() will be processed at thread time */
    atomic_set(&dev->tty.flags, EVENT_TTO);
    status |= 1;

    if (status)
    {
        iochar_send_event (&dev->tty);
    }
}

/*
 * RX Pulse Handler - Get notified once RX DMA is complete
 */
void mx53_rx_pulse_hdlr(DEV_MX1 *dev, struct sigevent *event)
{
    uintptr_t    base = dev->base;
    int error = 0;
    uint32_t sr1, sr2;
    dma_transfer_t tinfo;
    dma_addr_t dma_addr;

    dev->rx_dma.bytes_read = dev->sdmafuncs.bytes_left(dev->rx_dma.dma_chn);
    error = dev->sdmafuncs.xfer_complete(dev->rx_dma.dma_chn);
    dev->rx_dma.key = 0;
    dev->rx_dma.status = 0;

    if(error)
    {
        atomic_set(&dev->tty.flags, OBAND_DATA);

        sr1 = in32(base + MX1_UART_SR1);
        sr2 = in32(base + MX1_UART_SR2);

        if(sr2 & MX1_USR2_BRCD)
            dev->rx_dma.key |= TTI_BREAK;
        else if(sr1 & MX1_USR1_FRAMERR)
            dev->rx_dma.key |= TTI_FRAME;
        else if(sr1 & MX1_USR1_PARITYERR)
            dev->rx_dma.key |= TTI_PARITY;
        else if(sr2 & MX1_USR2_ORE)
            dev->rx_dma.key |= TTI_OVERRUN;
    }
    if((dev->tty.ibuf.size - dev->tty.ibuf.cnt) < dev->rx_dma.bytes_read)
    {
        /* no enough spage in ibuf. Return!
         * This is okay because auto-cts is enabled and another transfer will
         * get scheduled once we receive the signal in tto.c
         */
        return;
    }
    if(dev->rx_dma.buffer0)
    {
        dev->rx_dma.status |= tti2(&dev->tty, dev->rx_dma.buf, dev->rx_dma.bytes_read, dev->rx_dma.key);
        memset(&tinfo, 0, sizeof(tinfo));
        tinfo.xfer_bytes = dma_addr.len = dev->rx_dma.xfer_size;
        dma_addr.paddr = dev->rx_dma.phys_addr + dev->rx_dma.xfer_size;
        tinfo.dst_addrs = &dma_addr;
        tinfo.src_addrs= NULL;
        tinfo.xfer_unit_size = 8;
        tinfo.dst_fragments = 1;

        dev->sdmafuncs.setup_xfer(dev->rx_dma.dma_chn, &tinfo);
        dev->sdmafuncs.xfer_start(dev->rx_dma.dma_chn);
    }
    else
    {
        dev->rx_dma.status |= tti2(&dev->tty, dev->rx_dma.buf + dev->rx_dma.xfer_size, dev->rx_dma.bytes_read, dev->rx_dma.key);
        memset(&tinfo, 0, sizeof(tinfo));
        tinfo.xfer_bytes = dma_addr.len = dev->rx_dma.xfer_size;
        dma_addr.paddr = dev->rx_dma.phys_addr;
        tinfo.dst_addrs = &dma_addr;
        tinfo.src_addrs= NULL;
        tinfo.xfer_unit_size = 8;
        tinfo.dst_fragments = 1;

        dev->sdmafuncs.setup_xfer(dev->rx_dma.dma_chn, &tinfo);
        dev->sdmafuncs.xfer_start(dev->rx_dma.dma_chn);
    }
    dev->rx_dma.buffer0 ^= 1;

    if (dev->rx_dma.status)
    {
        iochar_send_event (&dev->tty);
    }
}
#endif
static inline int ms_interrupt(DEV_MX1 *dev)
{
    int status = 0;
    uintptr_t       base = dev->base;
    int sr1;

    sr1 = in32(base + MX1_UART_SR1);
    out32(base + MX1_UART_SR1, MX1_USR1_RTSD);
    if (!dev->usedma && dev->tty.c_cflag & OHFLOW){
        status |= tti(&dev->tty,
                (sr1 & MX1_USR1_RTSS) ? TTI_OHW_CONT : TTI_OHW_STOP);
    }

    return (status);
}

static inline int tx_interrupt(DEV_MX1 *dev)
{
    int    status = 0;
    uintptr_t        base = dev->base;
    int cr1;

    cr1 = in32(base + MX1_UART_CR1);
    out32(base + MX1_UART_CR1, cr1 & ~MX1_UCR1_TRDYEN);

    dev->tty.un.s.tx_tmr = 0;
    /* Send event to io-char, tto() will be processed at thread time */
    atomic_set(&dev->tty.flags, EVENT_TTO);
    status |= 1;

    return (status);
}

static inline int rx_interrupt(DEV_MX1 *dev)
{
    int            status = 0;
    unsigned       key, rxdata;
    uintptr_t      base = dev->base;
    while (in32(base + MX1_UART_SR2) & MX1_USR2_RDR)
    {
        /*
         * Read next character from FIFO
         */
        rxdata = in32(base + MX1_UART_RXDATA);
        key = rxdata & 0xFF;
        if (rxdata & MX1_RXERR)
        {
            /*
             * Save error as out-of-band data which can be read via devctl()
             */
            dev->tty.oband_data |= rxdata;
            atomic_set(&dev->tty.flags, OBAND_DATA);

            if (rxdata & MX1_URXD_BRK)
                key |= TTI_BREAK;
            else if (rxdata & MX1_URXD_FRMERR)
                key |= TTI_FRAME;
            else if (rxdata & MX1_URXD_PRERR)
                key |= TTI_PARITY;
            else if (rxdata & MX1_URXD_OVERRUN)
                key |= TTI_OVERRUN;
        }
        status |= tti(&dev->tty, key);
    }
    return status;
}

static inline int do_interrupt(DEV_MX1 *dev, int id)
{
    int sts=0;

    if(!dev->usedma) // do not need to process tx and rx_interrupt in DMA mode
    {
        sts = rx_interrupt(dev);
        if (in32(dev->base + MX1_UART_SR1) & MX1_USR1_TRDY)
            sts |= tx_interrupt(dev);
    }

    if (in32(dev->base + MX1_UART_SR1) & MX1_USR1_RTSD)
        sts |= ms_interrupt(dev);

    return sts;
}

/*
 * Serial interrupt handler
 */
static const struct sigevent * ser_intr(void *area, int id)
{
    DEV_MX1    *dev = area;

    if (do_interrupt(dev,id) && (dev->tty.flags & EVENT_QUEUED) == 0) {
        dev_lock(&ttyctrl);
        ttyctrl.event_queue[ttyctrl.num_events++] = &dev->tty;
        atomic_set(&dev->tty.flags, EVENT_QUEUED);
        dev_unlock(&ttyctrl);
        return &ttyctrl.event;
    }

    return 0;
}

int
interrupt_event_handler (message_context_t * msgctp, int code, unsigned flags, void *handle)
{
    uint32_t status;
    DEV_MX1 *dev = (DEV_MX1 *) handle;

    status = do_interrupt (dev, dev->iid[0]);

    if (status)
    {
        iochar_send_event (&dev->tty);
    }

    InterruptUnmask (dev->intr[0], dev->iid[0]);
    return (EOK);
}

void
ser_attach_intr(DEV_MX1 *dev)
{
    struct sigevent event;
    if(dev->isr)
    {
        dev->iid[0] = InterruptAttach(dev->intr[0], ser_intr, dev, 0, _NTO_INTR_FLAGS_TRK_MSK);
        if (dev->intr[1] != -1)
            dev->iid[1] = InterruptAttach(dev->intr[1], ser_intr, dev, 0, _NTO_INTR_FLAGS_TRK_MSK);
    }
    else
    {
        // Associate a pulse which will call the event handler.
        if ((event.sigev_code =
                pulse_attach (ttyctrl.dpp, MSG_FLAG_ALLOC_PULSE, 0, &interrupt_event_handler,
                    dev)) == -1)
        {
            fprintf (stderr, "Unable to attach event pulse.%s\n", strerror(errno));
            return;
        }

        /* Init the pulse for interrupt event */
        event.sigev_notify = SIGEV_PULSE;
        event.sigev_coid = ttyctrl.coid;
        event.sigev_priority = 63;                               /* service interrupts at a higher priority then client requests */
        event.sigev_value.sival_int = 0;

        dev->iid[0] = InterruptAttachEvent (dev->intr[0], &event, _NTO_INTR_FLAGS_TRK_MSK);
        if (dev->iid[0] == -1)
            fprintf (stderr, "UUnable to attach InterruptEvent. %s\n", strerror(errno));
    }
}


#if 0
void
ser_detach_intr(DEV_MX1 *dev)
{
    uintptr_t    base = dev->base;

    /* Disable UART */
    out32(base + MX1_UART_CR1, 0x04);
    out32(base + MX1_UART_CR2, 0x00);

    InterruptDetach(dev->iid[0]);
    dev->intr[0] = -1;
    if (dev->intr[1] != -1) {
        InterruptDetach(dev->iid[1]);
        dev->intr[1] = -1;
    }

}
#endif

__SRCVERSION( "$URL: http://svn/product/tags/public/bsp/nto650/freescale-mx6q-sabrelite/latest/src/hardware/devc/sermx1/intr.c $ $Rev: 633202 $" );
