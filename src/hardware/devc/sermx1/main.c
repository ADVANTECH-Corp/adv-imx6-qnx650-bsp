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

void   *
ser_term_thread (void *arg)
{
    sigset_t signals;

    if (ThreadCtl(_NTO_TCTL_IO, NULL) == -1)
    {
        fprintf(stderr, "%s: Unable to get I/O privity\n", __FUNCTION__);
        _exit(EXIT_FAILURE);
    }

    sigemptyset (&signals);
    sigaddset (&signals, SIGTERM);
    sigaddset (&signals, SIGINT);
    sigaddset(&signals, SIGBUS);

    sigwaitinfo (&signals, NULL);
    if(devptr->usedma)
    {
#if USE_DMA
        devptr->sdmafuncs.xfer_abort(devptr->rx_dma.dma_chn);
        devptr->sdmafuncs.xfer_abort(devptr->tx_dma.dma_chn);
        devptr->sdmafuncs.channel_release(devptr->rx_dma.dma_chn);
        devptr->sdmafuncs.channel_release(devptr->tx_dma.dma_chn);
        devptr->sdmafuncs.fini();
        my_detach_pulse(&devptr->rx_dma.pulse);
        my_detach_pulse(&devptr->tx_dma.pulse);
#endif
    }

    _exit (0);
}

int
main(int argc, char *argv[])
{
    pthread_attr_t attr;
    sigset_t signals;

    pthread_attr_init (&attr);

    sigfillset (&signals);
    pthread_sigmask (SIG_BLOCK, &signals, NULL);

    pthread_create (NULL, &attr, ser_term_thread, NULL);

    ttyctrl.max_devs = 6;
    ttc(TTC_INIT_PROC, &ttyctrl, 24);

    if (options(argc, argv) == 0) {
        fprintf(stderr, "%s: No serial ports found\n", argv[0]);
        exit(0);
    }

    ttc(TTC_INIT_START, &ttyctrl, 0);

    return 0;
}

__SRCVERSION( "$URL: http://svn/product/tags/public/bsp/nto650/freescale-mx6q-sabrelite/latest/src/hardware/devc/sermx1/main.c $ $Rev: 633202 $" );
