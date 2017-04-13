/*
 * $QNXLicenseC:
 * Copyright 2008, QNX Software Systems.
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

#ifndef __MIXER_H
#define __MIXER_H

/*
 * SGTL5000 CODEC Registers
 */
#define CHIP_ID			0x00
#define CHIP_DIG_POWER		0x02
#define CHIP_CLK_CTRL		0x04
#define CHIP_I2S_CTRL		0x06
#define CHIP_SSS_CTRL		0x0a
#define CHIP_ADCDAC_CTRL	0x0e
#define CHIP_DAC_VOL		0x10
#define CHIP_PAD_STRENGTH	0x14
#define CHIP_ANA_ADC_CTRL	0x20
#define CHIP_ANA_HP_CTRL	0x22
#define CHIP_ANA_CTRL		0x24
#define CHIP_LINREG_CTRL	0x26
#define CHIP_REF_CTRL		0x28
#define CHIP_MIC_CTRL		0x2a
#define CHIP_LINE_OUT_CTRL	0x2c
#define CHIP_LINE_OUT_VOL	0x2e
#define CHIP_ANA_POWER		0x30
#define CHIP_PLL_CTRL		0x32
#define CHIP_CLK_TOP_CTRL	0x34
#define CHIP_ANA_STATUS		0x36
#define CHIP_ANA_TEST2		0x3a
#define CHIP_SHORT_CTRL		0x3c
#define DAP_CONTROL		0x100

/* I2C 7-bit slave address */
#define SGTL5000_SLAVE_ADDR_0	0x0a	/* I2C_ADR0_CS=0 */
#define SGTL5000_SLAVE_ADDR_1	0x2a	/* I2C_ADR0_CS=1 */

/* Input mux selection */
#define INPUT_MUX_MIC_IN  0
#define INPUT_MUX_LINE_IN 1

struct sgtl5000_context;

#define MIXER_CONTEXT_T struct sgtl5000_context

#define MAX_STRLEN 50

typedef struct sgtl5000_context
{
	ado_mixer_delement_t *mic;
	ado_mixer_delement_t *line;

	char                 i2c_num;	/* I2C bus number */
	int                  i2c_fd;	/* I2C device handle */
	int                  mclk;   	/* external SYS_MCLK */
	int                  adr0cs;	/* sgtl5000 slave address select pin */
	int                  samplerate;
	int                  input_mux;
}
sgtl5000_context_t;

#endif /* __MIXER_H */
