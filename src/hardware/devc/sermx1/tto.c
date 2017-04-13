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

int
tto(TTYDEV *ttydev, int action, int arg1)
{
    TTYBUF           *bup = &ttydev->obuf;
    DEV_MX1          *dev = (DEV_MX1 *)ttydev;
    uintptr_t        base = dev->base;
    unsigned char    c;
    unsigned         cr1;
#ifdef USE_DMA
    static uint32_t  byte_cnt = 0;
    dma_transfer_t   tinfo;
    dma_addr_t       dma_addr;
#endif
    switch (action) {
        case TTO_STTY:
            ser_stty(dev);
            return 0;

        case TTO_CTRL:
            if (arg1 & _SERCTL_BRK_CHG) {
                cr1 = in32(base + MX1_UART_CR1);

                if (arg1 &_SERCTL_BRK)
                    cr1 |= MX1_UCR1_SNDBRK;
                else
                    cr1 &= ~MX1_UCR1_SNDBRK;

                out32(base + MX1_UART_CR1, cr1);
            }

            /*
             * Modem ctrl
             */
            if (arg1 & _SERCTL_DTR_CHG) {
                cr1 = in32(base + MX1_UART_CR3);

                if (arg1 & _SERCTL_DTR)
                    cr1 |= MX1_UCR3_DSR;
                else
                    cr1 &= ~MX1_UCR3_DSR;

                out32(base + MX1_UART_CR3, cr1);
            }

            /*
             * RTS Control
             */
            if (arg1 & _SERCTL_RTS_CHG) {
                if(!dev->usedma){
                    cr1 = in32(base + MX1_UART_CR2);

                    if (arg1 & _SERCTL_RTS)
                        cr1 |= MX1_UCR2_CTS;
                    else
                        cr1 &= ~MX1_UCR2_CTS;

                    out32(base + MX1_UART_CR2, cr1);
                }
#if USE_DMA
                else
                {
                    if (arg1 & _SERCTL_RTS)
                    {
                        // ibuf has more space. Resume receive
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
                }
#endif
            }
            return 0;

        case TTO_LINESTATUS:
            return ((in32(base + MX1_UART_SR1) & 0xFFFF) | (in32(base + MX1_UART_SR2)) << 16);

        case TTO_DATA:
        case TTO_EVENT:
            break;

        default:
            return 0;
    }

    if(dev->usedma)
    {
#if USE_DMA
        /* DMA Transaction in progress, wait for it to finish */
        if(!(in32(base + MX1_UART_SR2) & MX1_USR2_TXDC)){
            dev->tty.un.s.tx_tmr = 3;        /* Timeout 3 */
            return 0;
        }

        while(bup->cnt > 0 && byte_cnt < dev->tx_dma.xfer_size)
        {
            if (dev->tty.flags & (OHW_PAGED|OSW_PAGED) && !(dev->tty.xflags & OSW_PAGED_OVERRIDE)){
                break;
            }

            if (dev->tty.c_oflag & OPOST || dev->tty.xflags & OSW_PAGED_OVERRIDE)
            {

                /*
                 * Get the next character to print from the output buffer
                 */
                dev_lock(&dev->tty);
                c = tto_getchar(&dev->tty);
                dev_unlock(&dev->tty);
                dev->tx_dma.buf[byte_cnt++] = c;

                /*
                 * Clear the OSW_PAGED_OVERRIDE flag as we only want
                 * one character to be transmitted in this case.
                 */
                if (dev->tty.xflags & OSW_PAGED_OVERRIDE)
                    atomic_clr (&dev->tty.xflags, OSW_PAGED_OVERRIDE);

            }
            else
            {
                int buf_n, first_pass;
                dev_lock(&dev->tty);
                buf_n = min(bup->cnt, dev->tx_dma.xfer_size - byte_cnt);
                first_pass = &bup->buff[bup->size] - bup->tail;
                if(buf_n <= first_pass)
                {
                    memcpy(dev->tx_dma.buf + byte_cnt, bup->tail, buf_n);
                    bup->tail += buf_n;
                    if(bup->tail == &bup->buff[bup->size])
                        bup->tail = bup->buff;
                }
                else
                {
                    memcpy(dev->tx_dma.buf + byte_cnt, bup->tail, first_pass);
                    memcpy(dev->tx_dma.buf + byte_cnt + first_pass, bup->buff, buf_n - first_pass);
                    bup->tail = bup->buff + (buf_n - first_pass);
                }
                bup->cnt -= buf_n;
                byte_cnt += buf_n;
                dev_unlock (&dev->tty);
            }
        }
        dev_lock(&dev->tty);
        if(byte_cnt && !(dev->tty.flags & (OHW_PAGED | OSW_PAGED)))
        {
            /* Configure DMA buffer address and transfer size */
            memset(&tinfo, 0, sizeof(tinfo));
            tinfo.xfer_bytes = dma_addr.len = byte_cnt;
            dma_addr.paddr = dev->tx_dma.phys_addr;
            tinfo.src_addrs = &dma_addr;
            tinfo.dst_addrs = NULL;
            tinfo.xfer_unit_size = 8;
            tinfo.src_fragments = 1;

            dev->sdmafuncs.setup_xfer(dev->tx_dma.dma_chn, &tinfo);
            dev->tty.un.s.tx_tmr = 3;        /* Timeout 3 */
            dev->sdmafuncs.xfer_start(dev->tx_dma.dma_chn);

            byte_cnt = 0;
        }
        dev_unlock(&dev->tty);
#endif
    }
    else
    {
        while (bup->cnt > 0 && (in32(base + MX1_UART_SR1) & MX1_USR1_TRDY))
        {
            /*
             * If the OSW_PAGED_OVERRIDE flag is set then allow
             * transmit of character even if output is suspended via
             * the OSW_PAGED flag. This flag implies that the next
             * character in the obuf is a software flow control
             * charater (STOP/START).
             * Note: tx_inject sets it up so that the contol
             *       character is at the start (tail) of the buffer.
             */
            if (dev->tty.flags & (OHW_PAGED|OSW_PAGED) && !(dev->tty.xflags & OSW_PAGED_OVERRIDE))
                break;

            /*
             * Get the next character to print from the output buffer
             */
            dev_lock(&dev->tty);
            c=tto_getchar(&dev->tty);
            dev_unlock(&dev->tty);

            dev->tty.un.s.tx_tmr = 3;        /* Timeout 3 */
            out32(base + MX1_UART_TXDATA, c);

            /* Clear the OSW_PAGED_OVERRIDE flag as we only want
             * one character to be transmitted in this case.
             */
            if (dev->tty.xflags & OSW_PAGED_OVERRIDE)
            {
                atomic_clr(&dev->tty.xflags, OSW_PAGED_OVERRIDE);
                break;
            }
        }
        if (!(dev->tty.flags & (OHW_PAGED|OSW_PAGED)) && bup->cnt) {

            cr1 = in32(base + MX1_UART_CR1);
            out32(base + MX1_UART_CR1, cr1 | MX1_UCR1_TRDYEN);
        }
    }

    return (tto_checkclients(&dev->tty));
}

void ser_stty(DEV_MX1 *dev)
{
    uintptr_t    base = dev->base;
    unsigned    tmp, cr2, clk, rfdiv, fcr, bir, cr4;
    uint32_t timeout;

    /*
     * Check hardware flow control setting
     * NOTE: On this hardware CTS is the output and RTS is the input
     */
    if ((dev->tty.c_cflag & OHFLOW) && (dev->cr2 & MX1_UCR2_IRTS))
    {
        dev->cr2 = in32(base + MX1_UART_CR2);
        dev->cr2 &= ~(MX1_UCR2_IRTS);    /* Transmit only when RTS is asserted */
        if(dev->usedma) // enable auto cts
            dev->cr2 |= MX1_UCR2_CTSC;
        out32(base + MX1_UART_CR2, dev->cr2 | MX1_UCR2_SRST);

        /* In case we exit early, enable the RTS delta interrupt now */
        out32(base + MX1_UART_CR1, in32(base + MX1_UART_CR1) | MX1_UCR1_RTSDEN);
    }
    else if (!(dev->tty.c_cflag & OHFLOW) && !(dev->cr2 & MX1_UCR2_IRTS))
    {
        dev->cr2 = in32(base + MX1_UART_CR2);
        dev->cr2 |= (MX1_UCR2_IRTS | MX1_UCR2_CTS);    /* Ignore RTS and assert CTS */
        if(dev->usedma) // clear auto cts
            dev->cr2 &= ~(MX1_UCR2_CTSC);
        out32(base + MX1_UART_CR2, dev->cr2 | MX1_UCR2_SRST);

        /* Disable the RTS Delta interrupt */
        out32(base + MX1_UART_CR1, in32(base + MX1_UART_CR1) & ~MX1_UCR1_RTSDEN);
    }

    dev->cr2 = cr2 = in32(base + MX1_UART_CR2);
    /*
     * Calculate baud rate divisor, data size, stop bits and parity
     */
    clk = dev->clk;

    if(dev->tty.baud == 4000000)
    {
        rfdiv = 5; // divide by 1
    }
    else
    {
        rfdiv = clk / 16000000;
        if (rfdiv > 6) {
            rfdiv = 6;
            clk /= 7;
        } else if (rfdiv > 0) {
            clk /= rfdiv;
            rfdiv = 6 - rfdiv;
        } else
            rfdiv = 5;
    }

    fcr = (dev->fifo & 0xFC3F) | (rfdiv << 7);
    bir = dev->tty.baud * 16 / (clk / 10000) - 1;

    switch (dev->tty.c_cflag & CSIZE) {
        case CS8:
            cr2 |= MX1_UCR2_WS;
            break;

        case CS7:
            cr2 &= ~MX1_UCR2_WS;
            break;
    }

    if (dev->tty.c_cflag & CSTOPB)
        cr2 |= MX1_UCR2_STPB;
    else
        cr2 &= ~MX1_UCR2_STPB;

    cr2 &= ~(MX1_UCR2_PREN | MX1_UCR2_PROE);
    if (dev->tty.c_cflag & PARENB) {
        cr2 |= MX1_UCR2_PREN;
        if (dev->tty.c_cflag & PARODD)
            cr2 |= MX1_UCR2_PROE;
    }

    if ((dev->fcr == fcr) && (dev->cr2 == cr2) && (dev->bir == bir))
        return;

    dev->fcr = fcr;
    dev->cr2 = cr2;
    dev->bir = bir;

    /*
     * Wait for Tx FIFO and shift register empty if the UART is enabled
     */
    timeout = 100000;
    if ((in32(base + MX1_UART_CR1) & (MX1_UCR1_UARTCLKEN|MX1_UCR1_UARTEN)) == (MX1_UCR1_UARTCLKEN|MX1_UCR1_UARTEN)) {
        if (in32(base + MX1_UART_CR2) & MX1_UCR2_TXEN) {
            while (!(in32(base + MX1_UART_SR2) & MX1_USR2_TXDC) && timeout--)
                ;
        }
    }

    /* Disable UART */
    out32(base + MX1_UART_CR1, MX1_UCR1_UARTCLKEN);

    /* Reset UART */
    out32(base + MX1_UART_CR2, 0);

    for (tmp = 0; tmp < 10; tmp++)
        ;

    out32(base + MX1_UART_CR2, cr2 | MX1_UCR2_SRST);

    if (dev->mx1) {
        out32(base + MX1_UART_CR3, 0);
        /* Reference clock is 16MHz */
        out32(base + MX1_UART_CR4, 0x8000 | MX1_UCR4_REF16 | MX1_UCR4_DREN);
    }
    else {
        /* Program RXD muxed input */
        out32(base + MX1_UART_CR3, 4);
        out32(base + MX1_UART_CR4, 0x8000 | MX1_UCR4_DREN);
    }

    if(dev->usedma)
    {
        cr4 = in32(base + MX1_UART_CR4);
        cr4 &= ~(MX1_UCR4_DREN | MX1_UCR4_CTSTL_MASK);
        cr4 |= (((dev->fifo  & 0x3F)+1) << 10);
        out32(base + MX1_UART_CR4, cr4);
    }

    out32(base + MX1_UART_FCR, fcr);

    /* program ONEMS register for MX21/MX31/MX35 */
    if (!dev->mx1)
    {
           out32(base + MX1_UART_BIPR1, clk / 1000);
    }
    out32(base + MX1_UART_BIR, bir);
    out32(base + MX1_UART_BMR, 9999);

    /* Enable UART and Receiver Ready Interrupt */
    if(dev->usedma)
        out32(base + MX1_UART_CR1, MX53_UART_UCR1_ATDMAEN | MX1_UCR1_UARTEN | MX1_UCR1_RDMAEN | MX1_UCR1_TDMAEN);
    else
        out32(base + MX1_UART_CR1, MX1_UCR1_UARTCLKEN | MX1_UCR1_UARTEN | MX1_UCR1_RRDYEN);


    /* If flow control is enabled then enable the RTS Delta interrupt
     * NOTE: We need to re-enable here after the above UART reset+disable
     *          which clears all interrupts including RTSD
     */
    if (dev->tty.c_cflag & OHFLOW)
        out32(base + MX1_UART_CR1, in32(base + MX1_UART_CR1) | MX1_UCR1_RTSDEN);

    /* Enable Tx/Rx */
    out32(base + MX1_UART_CR2, cr2 | MX1_UCR2_TXEN | MX1_UCR2_RXEN | MX1_UCR2_SRST);
}

int
drain_check (TTYDEV * ttydev, uintptr_t * count)
{
    TTYBUF *bup = &ttydev->obuf;
    DEV_MX1 *dev = (DEV_MX1 *) ttydev;
    uint32_t drain_size = 0;

    if(dev->usedma)
    {
#if USE_DMA
        // if the device has DRAINED, return 1
        if((bup->cnt == 0) && (dev->sdmafuncs.bytes_left(dev->tx_dma.dma_chn) == 0) &&
                (in32(dev->base + MX1_UART_SR2) & MX1_USR2_TXDC))
            return 1;
        // set drain_size
        drain_size = DMA_XFER_SIZE + FIFO_SIZE;
#endif
    }
    else
    {
        // if the device has DRAINED, return 1
        if((bup->cnt == 0) && (in32(dev->base + MX1_UART_SR2) & MX1_USR2_TXDC))
            return 1;
        // set drain_size
        drain_size = FIFO_SIZE;
    }


    // if the device has not DRAINED, set a timer based on 50ms counts
    // wait for the time it takes for one character to be transmitted
    // out the shift register.  We do this dynamically since the
    // baud rate can change.
    if (count != NULL)
        *count =
            (ttydev->baud ==
            0) ? 0 : ((IO_CHAR_DEFAULT_BITSIZE * drain_size) / ttydev->baud) + 1;
    return 0;
}



__SRCVERSION( "$URL: http://svn/product/tags/public/bsp/nto650/freescale-mx6q-sabrelite/latest/src/hardware/devc/sermx1/tto.c $ $Rev: 633202 $" );
