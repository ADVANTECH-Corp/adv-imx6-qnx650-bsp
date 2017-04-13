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


#include "mxecspi.h"
#include "mx51.h"
#include "mx51_iomux.h"
#include <stdbool.h>
#include <stddef.h>

enum opt_index {BASE, IRQ, CLOCK, LOOPBACK, WAITSTATE, BURST, GPIOCSBASE, GPIOCS0, GPIOCS1, GPIOCS2, GPIOCS3, END};
static char *mx51_opts[] = {
	[BASE]		=	"base",			/* Base address for this CSPI controller */
	[IRQ]		=	"irq",			/* IRQ for this CSPI intereface */
	[CLOCK]		=	"clock",		/* CSPI clock */
	[LOOPBACK]	=	"loopback",     /*loopback interface for test purposes*/
	[WAITSTATE]	=	"waitstate",    /* waitstates between xfers */
	[BURST]		=	"burst",
	[GPIOCSBASE]=	"gpiocsbase",	/* gpio base address of bank of gpios used as
									 * chipselects.
									 * if SPI_MODE_CSHOLD_HIGH is to be used the corresponding
									 * chipselect should be configured as gpio
									 */
	[GPIOCS0]	=	"gpiocs0",		/* gpio pin number to be used as chipselect 0*/
	[GPIOCS1]	=	"gpiocs1",
	[GPIOCS2]	=	"gpiocs2",
	[GPIOCS3]	=	"gpiocs3",
	[END]		=	NULL
};

spi_funcs_t spi_drv_entry = {
	sizeof(spi_funcs_t),
	mx51_init,		/* init() */
	mx51_dinit,		/* fini() */
	mx51_drvinfo,	/* drvinfo() */
	mx51_devinfo,	/* devinfo() */
	mx51_setcfg,	/* setcfg() */
	mx51_xfer,		/* xfer() */
	NULL			/* dma_xfer() */
};

/*
 * Note:
 * The devices listed are just examples, users should change
 * this according to their own hardware spec.
 */
static spi_devinfo_t devlist[4] = {
	{
		0x00,				// Device ID, for SS0
		"CSPI-DEV0",		// Description
		{ 
			16,		        // data length 16bit, MSB
			5000000			// Clock rate 5M
		},
	},
	{
		0x01,				// Device ID, for SS1
		"CSPI-DEV1",		// Description
		{ 
			16,		        // data length 16bit, MSB
			5000000			// Clock rate 5M
		},
	},
	{
		0x02,				// Device ID, for SS2
		"CSPI-DEV2",		// Description
		{ 
			16,		        // data length 16bit, MSB
			5000000			// Clock rate 5M
		},
	},
	{
		0x03,				// Device ID, for SS3
		"CSPI-DEV3",		// Description
		{ 
			16,		        // data length 16bit, MSB
			5000000			// Clock rate 5M
		},
	}
};

static uint32_t devctrl[4] = {0,0,0,0};
static uint32_t devcfg[4] = {0,0,0,0};

static const uintptr_t gpio_pbase[NUM_GPIO] = {
	MX51_GPIO1_BASE, MX51_GPIO2_BASE, MX51_GPIO3_BASE, MX51_GPIO4_BASE };

/* These errata structures are related to i.MX51 erratum ENGcm09397
 * (see mx51_chipsel).  For each slave select output, they describe
 * the GPIO that shares its pin.
 *
 * Note that only ecspi1[0] has been tested.
 **/
#define NUM_SPI_ERRATA 2
#define NUM_SLAVE_SELECT 4
struct ss_errata_info {
	ptrdiff_t mux;
	int mode_spi;
	int mode_gpio;
	int gpio_instance;
	int gpio_pin;
};
struct spi_errata_info {
	uintptr_t spi_pbase;
	struct ss_errata_info ss[NUM_SLAVE_SELECT];
};
static const struct spi_errata_info errata_info[NUM_SPI_ERRATA] = {
	[0] = {  // ecspi1
		.spi_pbase = MX51_ECSPI1_BASE,
		.ss = {
			[0] = {
				.mux = SWMUX_CSPI1_SS0,
				.mode_spi = MUX_CTL_MUX_MODE_ALT0,
				.mode_gpio = MUX_CTL_MUX_MODE_ALT3,
				.gpio_instance = GPIO4,
				.gpio_pin = 24,
			},
			[1] = {
				.mux = SWMUX_CSPI1_SS1,
				.mode_spi = MUX_CTL_MUX_MODE_ALT0,
				.mode_gpio = MUX_CTL_MUX_MODE_ALT3,
				.gpio_instance = GPIO4,
				.gpio_pin = 25,
			},
			[2] = {
				.mux = SWMUX_DI1_PIN11,
				.mode_spi = MUX_CTL_MUX_MODE_ALT7,
				.mode_gpio = MUX_CTL_MUX_MODE_ALT4,
				.gpio_instance = GPIO3,
				.gpio_pin = 0,
			},
			[3] = {
				.mux = SWMUX_USBH1_DATA7,
				.mode_spi = MUX_CTL_MUX_MODE_ALT1,
				.mode_gpio = MUX_CTL_MUX_MODE_ALT2,
				.gpio_instance = GPIO1,
				.gpio_pin = 18,
			},
		},
	},
	[1] = {  // ecspi2
		.spi_pbase = MX51_ECSPI2_BASE,
		.ss = {
			[0] = {
				.mux = SWMUX_NANDF_RDY_INT,
				.mode_spi = MUX_CTL_MUX_MODE_ALT2,
				.mode_gpio = MUX_CTL_MUX_MODE_ALT3,
				.gpio_instance = GPIO3,
				.gpio_pin = 24,
			},
			[1] = {
				// This signal could be routed to NANDF_RB0 instead.
				// We may want to check this in the future.
				.mux = SWMUX_NANDF_D12,
				.mode_spi = MUX_CTL_MUX_MODE_ALT2,
				.mode_gpio = MUX_CTL_MUX_MODE_ALT3,
				.gpio_instance = GPIO3,
				.gpio_pin = 28,
			},
			[2] = {
				.mux = SWMUX_NANDF_D13,
				.mode_spi = MUX_CTL_MUX_MODE_ALT2,
				.mode_gpio = MUX_CTL_MUX_MODE_ALT3,
				.gpio_instance = GPIO3,
				.gpio_pin = 27,
			},
			[3] = {
				// Could be USBH1_DATA7 instead (same pin as ecspi1 ss3).
				.mux = SWMUX_NANDF_D14,
				.mode_spi = MUX_CTL_MUX_MODE_ALT2,
				.mode_gpio = MUX_CTL_MUX_MODE_ALT3,
				.gpio_instance = GPIO3,
				.gpio_pin = 26,
			},
		},
	},
};


static int mx51_options(mx51_cspi_t *dev, char *optstring)
{
	int		opt, rc = 0, err = EOK;
	char	*options, *freeptr, *c, *value;

	if (optstring == NULL)
		return 0;

	freeptr = options = strdup(optstring);

	while (options && *options != '\0') {
		c = options;
		if ((opt = getsubopt(&options, mx51_opts, &value)) == -1)
			goto error;

		switch (opt) {
			case BASE:
				dev->pbase = strtoul(value, 0, 0); 
				continue;
			case IRQ:
				dev->irq   = strtoul(value, 0, 0);
				continue;
			case CLOCK:
				dev->clock = strtoul(value, 0, 0);
				continue;
			case LOOPBACK:
				dev->loopback = value ? strtoul(value, 0, 0) : 1;
				continue;
			case WAITSTATE:
				dev->waitstates = strtoul(value, 0, 0);
				continue;
			case BURST:
				dev->burst = value ? strtoul(value, 0, 0) : 1;
				continue;
			case GPIOCSBASE:
				dev->gpiocs.vbase = strtoul(value, 0, 0);
				continue;
			case GPIOCS0:
				dev->gpiocs.gpio[0] = strtoul(value, 0, 0) | GPIOCS_EN;
				continue;
			case GPIOCS1:
				dev->gpiocs.gpio[1] = strtoul(value, 0, 0) | GPIOCS_EN;
				continue;
			case GPIOCS2:
				dev->gpiocs.gpio[2] = strtoul(value, 0, 0) | GPIOCS_EN;
				continue;
			case GPIOCS3:
				dev->gpiocs.gpio[3] = strtoul(value, 0, 0) | GPIOCS_EN;
				continue;
		}
error:
		fprintf(stderr, "mx51: unknown option %s", c);
		err = EINVAL;
		rc = -1;
	}

	free(freeptr);

	return rc;
}

static void recalculate_devcfg(mx51_cspi_t *dev)
{
	int i;
	for (i = 0; i < 4; i++) {
		devctrl[i] = mx51_cfg(dev, &devlist[i].cfg);

		/* set SCLK polarity, 0 = active high, 1 = active low */
		if (devlist[i].cfg.mode & SPI_MODE_CKPOL_HIGH){
			devcfg[i] &= ~(1 << (i + CSPI_CONFIGREG_POL_MASK));
		} else {
			devcfg[i] |= (1 << (i + CSPI_CONFIGREG_POL_MASK));
		}
		
		/* set SCLK phase, if flag SPI_MODE_CKPHASE_HALF is set
		 * then specify a half SCLK phase difference between SCLK
		 * and MISO/MOSI */
		if (devlist[i].cfg.mode & SPI_MODE_CKPHASE_HALF) {
			devcfg[i] &= ~(1 << (i + CSPI_CONFIGREG_PHA_MASK));
		} else {
			devcfg[i] |= (1 << (i + CSPI_CONFIGREG_PHA_MASK));
		}
		
		/* set slave select (SS) polarity, 0 = active low, 1 = active high
		 * note that SCLK, SS polarity bit settings are different */
		if (devlist[i].cfg.mode & SPI_MODE_CSPOL_HIGH) {
			devcfg[i] |= (1 << (i + CSPI_CONFIGREG_SSPOL_MASK));
		} else {
			devcfg[i] &= ~(1 << (i + CSPI_CONFIGREG_SSPOL_MASK));
		}
		
		devcfg[i] &= ~( (1 << (i + 8) | (1 << (i + 16)) | (1 << (i + 20))) );
	}
}

void *mx51_init(void *hdl, char *options)
{
	mx51_cspi_t	*dev;
	uintptr_t	base;
	int			i;
	uint32_t	periodreg = 0, gpioval = 0;
    
	dev = calloc(1, sizeof(mx51_cspi_t));

	if (dev == NULL)
		return NULL;

	//Set defaults
	dev->pbase = MX51_ECSPI1_BASE;
	dev->irq   = MX51_ECSPI1_IRQ;
	dev->clock = 66500000;			/* 66.5 MHz CSPI clock */
	dev->loopback = 0;
	dev->waitstates = 0;
	dev->burst = 1;
	dev->errata = NULL;
	// initialise _vbase values so mx51_dinit won't try to free
	// them unless we've actually mapped them
	dev->iomuxc_vbase = (uintptr_t)MAP_FAILED;
	for (i = 0; i < NUM_GPIO; ++i)
		dev->gpio_vbase[i] = (uintptr_t)MAP_FAILED;
		
	if (mx51_options(dev, options))
		goto fail0;

	/* Determine whether this is ecspi1 or ecspi2 from the base address.
	 * mx51_chipsel uses dev->errata to work around a hardware bug. */
	for (i = 0; i < NUM_SPI_ERRATA; ++i) {
		if (dev->pbase == errata_info[i].spi_pbase) {
			dev->errata = &errata_info[i];
			break;
		}
	}

	/*
	 * Map in SPI registers
	 */
	if ((base = mmap_device_io(MX51_ECSPI1_SIZE, dev->pbase)) == (uintptr_t)MAP_FAILED)
		goto fail0;

	if (dev->gpiocs.vbase) {
		dev->gpiocs.vbase = mmap_device_io(MX53_GPIO_SZ, dev->gpiocs.vbase);
		if(dev->gpiocs.vbase == MAP_DEVICE_FAILED) {

			goto fail1;
		}
	}

	dev->vbase = base;

	if (dev->errata)
	{
		dev->iomuxc_vbase = mmap_device_io(MX51_IOMUXC_SIZE, MX51_IOMUXC_BASE);
		if (dev->iomuxc_vbase == (uintptr_t)MAP_FAILED)
			goto fail1;

		for (i = 0; i < NUM_GPIO; ++i) {
			dev->gpio_vbase[i] = mmap_device_io(
					MX51_GPIO_SIZE, gpio_pbase[i]);
			if (dev->gpio_vbase[i] == (uintptr_t)MAP_FAILED)
				goto fail1;
		}
	}
  
	out32(  base + MX51_CSPI_CONTROLREG, 
		in32(base + MX51_CSPI_CONTROLREG) |
				CSPI_CONTROLREG_PRE_DVDR | CSPI_CONTROLREG_ENABLE);	/* Enable SPI all chans in Master mode */
    
	if (dev->loopback == 1) {
		out32(base + MX51_CSPI_TESTREG, CSPI_TESTREG_LOOPBACK);
	}
   
	// set wait states between xfers
	periodreg |= dev->waitstates & CSPI_PERIODREG_SP_MASK;
	out32(base + MX51_CSPI_PERIODREG, periodreg);
            
	/*
	 * Calculate all device configuration here
	 */

	recalculate_devcfg(dev);

	/* set gpios used as cs to high state (de-assert chip select) & direction as output */
	if(dev->gpiocs.vbase) {
		for(i = 0; i < 4; i++) {
			gpioval |= 1 << (dev->gpiocs.gpio[i] & MSK_GPIO);
		}
		out32(dev->gpiocs.vbase + IMX53_GPIO_GDIR, in32(dev->gpiocs.vbase + IMX53_GPIO_GDIR) | gpioval);
		out32(dev->gpiocs.vbase + IMX53_GPIO_DR, in32(dev->gpiocs.vbase + IMX53_GPIO_DR) | gpioval);
	}		

	/*
	 * Attach SPI interrupt
	 */
	if (mx51_attach_intr(dev))
		goto fail2;
  
	dev->spi.hdl = hdl;

   	return dev;

fail2:
	if(dev->gpiocs.vbase)
		munmap_device_io(dev->gpiocs.vbase, MX53_GPIO_SZ);
fail1:
	for (i = 0; i < NUM_GPIO; ++i) {
		if (dev->gpio_vbase[i] != (uintptr_t)MAP_FAILED)
			munmap_device_io(dev->gpio_vbase[i], MX51_GPIO_SIZE);
	}
	if (dev->iomuxc_vbase != (uintptr_t)MAP_FAILED)
		munmap_device_io(dev->iomuxc_vbase, MX51_IOMUXC_SIZE);
	munmap_device_io(dev->vbase, MX51_ECSPI1_SIZE);
fail0:
	free(dev);
	return NULL;
}

void mx51_dinit(void *hdl)
{
	mx51_cspi_t	*dev = hdl;
	int i;
	/*
	 * unmap the register, detach the interrupt
	 */
	InterruptDetach(dev->iid);
	ConnectDetach(dev->coid);
	ChannelDestroy(dev->chid);

	/*
	 * Disable SPI
	 */
	out32( dev->vbase + MX51_CSPI_CONTROLREG,
               in32( dev->vbase + MX51_CSPI_CONTROLREG) & ~CSPI_CONTROLREG_ENABLE);

	for (i = 0; i < NUM_GPIO; ++i) {
		if (dev->gpio_vbase[i] != (uintptr_t)MAP_FAILED)
			munmap_device_io(dev->gpio_vbase[i], MX51_GPIO_SIZE);
	}
	if (dev->iomuxc_vbase != (uintptr_t)MAP_FAILED)
		munmap_device_io(dev->iomuxc_vbase, MX51_IOMUXC_SIZE);
	munmap_device_io(dev->vbase, MX51_ECSPI1_SIZE);
	if(dev->gpiocs.vbase)
		munmap_device_io(dev->gpiocs.vbase, MX53_GPIO_SZ);
	free(hdl);
}

int mx51_drvinfo(void *hdl, spi_drvinfo_t *info)
{

	info->version = (SPI_VERSION_MAJOR << SPI_VERMAJOR_SHIFT) | (SPI_VERSION_MINOR << SPI_VERMINOR_SHIFT) | (SPI_REVISION << SPI_VERREV_SHIFT);
	strcpy(info->name, "MX51 ECSPI");
	info->feature = 0;
	return (EOK);
}

int mx51_setcfg(void *hdl, uint16_t device, spi_cfg_t *cfg)
{
	uint32_t	control;
	mx51_cspi_t	*dev = hdl;
	
	if (device > 3)
		return (EINVAL);

	memcpy(&devlist[device].cfg, cfg, sizeof(spi_cfg_t));

	control = mx51_cfg(hdl, &devlist[device].cfg);

	devctrl[device] = control;
	recalculate_devcfg(dev);
	if( (cfg->mode & SPI_MODE_CSHOLD_HIGH) && dev->gpiocs.vbase && 
							(dev->gpiocs.gpio[device] & GPIOCS_EN) ) {
		out32(dev->gpiocs.vbase + IMX53_GPIO_DR, in32(dev->gpiocs.vbase + IMX53_GPIO_DR)
   											 & ~(1 << (dev->gpiocs.gpio[device] & MSK_GPIO)));
   		dev->gpiocs.gpio[device] |= GPIOCS_CSHOLD;
	} else if (dev->gpiocs.vbase && (dev->gpiocs.gpio[device] & GPIOCS_EN)) {
		out32(dev->gpiocs.vbase + IMX53_GPIO_DR, in32(dev->gpiocs.vbase + IMX53_GPIO_DR)
   												| (1 << (dev->gpiocs.gpio[device] & MSK_GPIO)));
   		dev->gpiocs.gpio[device] &= ~GPIOCS_CSHOLD;
	}

	return (EOK);
}

int mx51_devinfo(void *hdl, uint32_t device, spi_devinfo_t *info)
{
	int		dev = device & SPI_DEV_ID_MASK;

	if (device & SPI_DEV_DEFAULT) {
		/*
		 * Info of this device
		 */
		if (dev >= 0 && dev <= 3)
			memcpy(info, &devlist[dev], sizeof(spi_devinfo_t));
		else
			return (EINVAL);
	}
	else {
		/*
		 * Info of next device 
		 */
		if (dev == SPI_DEV_ID_NONE)
			dev = -1;
		dev++;		// get next device number		
		if (dev < 4)
			memcpy(info, &devlist[dev], sizeof(spi_devinfo_t));
		else
			return (EINVAL);
	}

	return (EOK);
}

static void mx51_chipsel(mx51_cspi_t *dev, uint32_t id, bool active)
{
	/* Workaround for i.MX51 erratum ENGcm09397:
	 *  "When the SSB_POL is set to '1' the associated slave select does
	 *   not automatically return to an inactive state after the data
	 *   transaction is complete."
	 *
	 * When inactive, we configure the pin as GPIO with output low.
	 */
	
	const struct ss_errata_info *ss;
	if (!dev->errata || id >= NUM_SLAVE_SELECT)
		return;
	
	ss = &dev->errata->ss[id];
	if (active) {
		pinmux_set_swmux(dev->iomuxc_vbase, ss->mux, ss->mode_spi);
	} else if (devlist[id].cfg.mode & SPI_MODE_CSPOL_HIGH) {
		uintptr_t gpio_vbase = dev->gpio_vbase[ss->gpio_instance];
		uint32_t gpio_bit = (1 << ss->gpio_pin);
		
		pinmux_set_swmux(dev->iomuxc_vbase, ss->mux, ss->mode_gpio);
		out32(gpio_vbase + MX51_GPIO_GDIR,
				in32(gpio_vbase + MX51_GPIO_GDIR) | gpio_bit);
		out32(gpio_vbase + MX51_GPIO_DR,
				in32(gpio_vbase + MX51_GPIO_DR) & ~gpio_bit);
	}
}

void *mx51_xfer(void *hdl, uint32_t device, uint8_t *buf, int *len)
{
	mx51_cspi_t	*dev = hdl;
	uintptr_t	base = dev->vbase;
	uint32_t	burst, id, i = 0;
	uint32_t	ctrl, tmp, data, cfg;
	

	id = device & SPI_DEV_ID_MASK;

	if (id > 3) {
		*len = -1;
		return buf;
	}

	dev->xlen = *len;
	dev->rlen = 0;
	dev->tlen = 0;
	dev->pbuf = buf;
	dev->dlen = ((devlist[id].cfg.mode & SPI_MODE_CHAR_LEN_MASK) + 7) >> 3;

	// Estimate transfer time in us... The calculated dtime is only used for
	// the timeout, so it doesn't have to be that accurate.  At higher clock
	// rates, a calcuated dtime of 0 would mess-up the timeout calculation, so
	// round up to 1 us
	dev->dtime = dev->dlen * (8 + dev->waitstates) * 1000 * 1000 / devlist[id].cfg.clock_rate;

	/* Disable Interrupts */
	out32(base + MX51_CSPI_INTREG, 0x0);

	if (dev->dtime == 0)
		dev->dtime = 1;

	ctrl =  devctrl[id]                     | 
            (id << CSPI_CONTROLREG_CSEL_POS)    |
            (0x1 << (CSPI_CONTROLREG_CH_MODE_POS + id))	| 
            CSPI_CONTROLREG_ENABLE;

	cfg =  devcfg[id];
	(void)mx51_chipsel(dev, id, true);
    tmp = in32(base + MX51_CSPI_CONTROLREG) & (~CSPI_CONTROLREG_CSEL_MASK);
	out32(  base + MX51_CSPI_CONTROLREG, ctrl | tmp);	/* Enable SPI */
	out32(  base + MX51_CSPI_CONFIGREG, cfg);

	/* clean up the RXFIFO */
	while (in32(base + MX51_CSPI_TESTREG) & CSPI_TESTREG_RXCNT_MASK)
		in32(base + MX51_CSPI_RXDATA);

	if (dev->burst) {               // for SPI burst transmit mode
                if (dev->xlen >= MX51_CSPI_BURST_MAX) {

			burst = MX51_CSPI_BURST_MAX * 8 - 1;
                } else {
                        burst = dev->xlen * 8 - 1;

                        /* check if the xlen not aligne on word size */
                        switch (tmp = dev->xlen % 4) {
                                case 1:   // only write 8 bits in TXFIFO
                                        if (1 == dev->dlen) {
                                                out32(base + MX51_CSPI_TXDATA, buf[dev->tlen]);
                                                dev->tlen++;
                                        } else
                                                fprintf(stderr, "mx51_xfer: Unexpected tranfer length. \n");
                                        break;
                                case 2:   // only write 16 bits in TXFIFO
                                        if (1 == dev->dlen) {
                                                tmp = (buf[dev->tlen] << 8) | (buf[dev->tlen + 1]);
                                                out32(base + MX51_CSPI_TXDATA, tmp);
                                                dev->tlen += 2;

                                        } else if (2 == dev->dlen) {
                                                tmp = *(uint16_t *)(&buf[dev->tlen]);
                                                out32(base + MX51_CSPI_TXDATA, tmp);
                                                dev->tlen += 2;
                                        }else
                                                fprintf(stderr, "mx51_xfer: Unexpected tranfer length. .2\n");
                                        break;
                                case 3:   // only write 24 bits in TXFIFO
                                        if (1 == dev->dlen) {
                                                tmp = (buf[dev->tlen] << 16) | (buf[dev->tlen + 1] << 8) | (buf[dev->tlen + 2]);
                                                out32(base + MX51_CSPI_TXDATA, tmp);
                                                dev->tlen += 3;
                                        } else
                                                fprintf(stderr, "mx51_xfer: Unexpected tranfer length. .3\n");
                                        break;
                                default:
                                        break;
                        }
                }

		while ((dev->xlen > dev->tlen) && (i < 8)) {    // write rest of data to TXFIFO
                        switch (dev->dlen) {
                                case 1:
                                        data = (buf[dev->tlen] << 24) | (buf[dev->tlen + 1] << 16)
                                                        | (buf[dev->tlen + 2] << 8) | (buf[dev->tlen + 3]);
                                        break;
                                case 2:
                                        data = (*(uint16_t *)(&buf[dev->tlen]) << 16)
                                                        | (*(uint16_t *)(&buf[dev->tlen + 2]));
                                        break;
                                case 4:
                                        data = *(uint32_t *)(&buf[dev->tlen]);
                                        break;
                                default:
                                        fprintf(stderr, "mx51_xfer: Unsupport word length.\n");
                                        data = 0;
                                        break;
                        }
                        out32(base + MX51_CSPI_TXDATA, data);
                        dev->tlen += 4;
                        i++;
                }

		cfg &= ~(0x1 << (id + CSPI_CONFIGREG_SSCTL_POS));
		ctrl = in32(base + MX51_CSPI_CONTROLREG);
		ctrl = (ctrl & ~CSPI_CONTROLREG_BCNT_MASK) | (burst << CSPI_CONTROLREG_BCNT_POS);
		out32(base + MX51_CSPI_CONTROLREG, ctrl);
		out32(  base + MX51_CSPI_CONFIGREG, cfg);

	} else {
		for (i = 0; i < 8; i++) {

			if (dev->tlen >= dev->xlen)
				break;

			switch (dev->dlen) {
				case 1:
					out32(base + MX51_CSPI_TXDATA, buf[dev->tlen]);
					dev->tlen++;
					break;
				case 2:
					out32(base + MX51_CSPI_TXDATA, *(uint16_t *)(&buf[dev->tlen]));
					dev->tlen += 2;
					break;
				case 3:
				case 4:
					out32(base + MX51_CSPI_TXDATA, *(uint32_t *)(&buf[dev->tlen]));
					dev->tlen += 4;
					break;
			}
		}
	}

	// enable tx complete interrupt and receive ready FIFO interrupt
	out32(base + MX51_CSPI_INTREG, CSPI_INTREG_TCEN );
   /* if cs controller by gpio , assert cs */
   if(dev->gpiocs.vbase && (dev->gpiocs.gpio[id] & GPIOCS_EN)) {

   		out32(dev->gpiocs.vbase + IMX53_GPIO_DR, in32(dev->gpiocs.vbase + IMX53_GPIO_DR)
   											 & ~(1 << (dev->gpiocs.gpio[id] & MSK_GPIO)));
   	}
	/* Start exchange */
	out32(  base + MX51_CSPI_CONTROLREG, in32(base + MX51_CSPI_CONTROLREG) | CSPI_CONTROLREG_XCH);
	
	/*
	 * Wait for exchange to finish
	 */
          
	if (mx51_wait(dev, dev->xlen)) {
		fprintf(stderr, "spi-mx51ecspi: XFER Timeout!!!");
		dev->rlen = -1;
	}
	/* if cs controller by gpio & CS_HOLD flag is not set, then de-assert cs */
   if(dev->gpiocs.vbase && (dev->gpiocs.gpio[id] & GPIOCS_EN) &&
   										!(dev->gpiocs.gpio[id] & GPIOCS_CSHOLD)) {
   		out32(dev->gpiocs.vbase + IMX53_GPIO_DR, in32(dev->gpiocs.vbase + IMX53_GPIO_DR)
   												| (1 << (dev->gpiocs.gpio[id] & MSK_GPIO)));

   	}
	
        if (dev->burst) {               // for SPI burst transmit mode
                burst = (dev->xlen / MX51_CSPI_BURST_MAX ) * MX51_CSPI_BURST_MAX;

                switch (tmp = dev->xlen % 4) {
                        case 1:   // ignore first 3 bytes in RXFIFO
                                memcpy(&buf[burst], &buf[burst + 3], dev->xlen - burst);
                                dev->rlen = dev->rlen - 3;
                                break;

                        case 2:   // ignore first 2 bytes in RXFIFO
                                memcpy(&buf[burst], &buf[burst + 2], dev->xlen - burst);
                                dev->rlen = dev->rlen - 2;
                                break;

                        case 3:   // ignore first 1 bytes in RXFIFO
                                memcpy(&buf[burst], &buf[burst + 1], dev->xlen - burst);
                                dev->rlen = dev->rlen - 1;
                                break;

                        default:
                                break;
                }
        }
	(void)mx51_chipsel(dev, id, false);
	*len = dev->rlen;

	return buf;
}


__SRCVERSION("$URL$ $Rev$");

