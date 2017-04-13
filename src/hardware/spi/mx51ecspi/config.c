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


int mx51_cfg(void *hdl, spi_cfg_t *cfg)
{
	mx51_cspi_t	*mx51 = hdl;
	uint32_t	ctrl, i, div, drate;
    
	if (cfg == NULL)
		return 0;

	ctrl = cfg->mode & SPI_MODE_CHAR_LEN_MASK;

	if (ctrl > 32 || ctrl < 1)
		return 0;

	for (i = 0; i < 8 ; i++) {
		div = 4 << i;
		drate = mx51->clock / div;
        
		// Assign the datarate if calculated <= desired...
		// OR assign lowest possible rate last time through the loop
		if (drate <= cfg->clock_rate || i==7) {
			cfg->clock_rate = drate;
			ctrl = (0x0 << CSPI_CONREG_POSTDIVIDR_POS);
			break;
		}
	}

	switch (cfg->mode & SPI_MODE_RDY_MASK) {
		case SPI_MODE_RDY_EDGE:
			ctrl |= CSPI_CONTROLREG_DRCTL_EDGE << CSPI_CONTROLREG_DRCTL_POS;
			break;
		case SPI_MODE_RDY_LEVEL:
			ctrl |= CSPI_CONTROLREG_DRCTL_LEVEL << CSPI_CONTROLREG_DRCTL_POS;
			break;
	}

	return ctrl;
}

__SRCVERSION("$URL$ $Rev$");

