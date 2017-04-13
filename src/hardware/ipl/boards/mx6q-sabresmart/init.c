/*
 * $QNXLicenseC: 
 * Copyright 2011, QNX Software Systems.  
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

#include "ipl_mx6x.h"
#include <sys/srcversion.h>
#include <arm/mx6x_iomux.h>
#include <hw/inout.h>


#define MX6Q_PAD_SETTINGS_UART (PAD_CTL_HYS_ENABLE | PAD_CTL_PKE_ENABLE | \
                				PAD_CTL_PUS_100K_PU | PAD_CTL_SPEED_MEDIUM | \
                                PAD_CTL_DSE_40_OHM | PAD_CTL_SRE_FAST | PAD_CTL_PUE_PULL)

#define MX6Q_PAD_SETTINGS_USDHC (PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PULL | PAD_CTL_PUS_47K_PU | \
                                PAD_CTL_SPEED_LOW | PAD_CTL_DSE_80_OHM | PAD_CTL_SRE_FAST | \
                                PAD_CTL_HYS_ENABLE)

#define MX6Q_PAD_SETTINGS_USDHC_CDWP	(PAD_CTL_PKE_DISABLE | PAD_CTL_SPEED_LOW | \
										PAD_CTL_DSE_DISABLE | PAD_CTL_HYS_ENABLE)


#define MX6Q_PAD_SETTINGS_ECSPI (PAD_CTL_SRE_FAST | PAD_CTL_SPEED_MEDIUM | PAD_CTL_DSE_40_OHM | PAD_CTL_HYS_ENABLE)

static inline void set_mux_cfg(unsigned int pin, unsigned int value)
{
	out32((MX6X_IOMUXC_BASE + MX6X_IOMUX_SWMUX + (pin * 4)), value);
}

static inline void set_pad_cfg(unsigned int pin, unsigned int value)
{
	out32((MX6X_IOMUXC_BASE + MX6X_IOMUX_SWPAD + (pin * 4)), value);
}

static inline void set_pin_as_input(unsigned int pin, unsigned int value)
{
	out32((MX6X_IOMUXC_BASE + MX6X_IOMUX_SWINPUT + (pin * 4)), value);
}

void init_clocks(void)
{
    /* Restore the default values in the Gate registers */
	out32((MX6X_CCM_BASE + MX6X_CCM_CCGR0), MX6X_CCM_CCGR0_RESET);
	out32((MX6X_CCM_BASE + MX6X_CCM_CCGR1), MX6X_CCM_CCGR1_RESET);
	out32((MX6X_CCM_BASE + MX6X_CCM_CCGR2), MX6X_CCM_CCGR2_RESET);
	out32((MX6X_CCM_BASE + MX6X_CCM_CCGR3), MX6X_CCM_CCGR3_RESET);
	out32((MX6X_CCM_BASE + MX6X_CCM_CCGR4), MX6X_CCM_CCGR4_RESET);
	out32((MX6X_CCM_BASE + MX6X_CCM_CCGR5), MX6X_CCM_CCGR5_RESET);
	out32((MX6X_CCM_BASE + MX6X_CCM_CCGR6), MX6X_CCM_CCGR6_RESET);
	out32((MX6X_CCM_BASE + MX6X_CCM_CCGR7), MX6X_CCM_CCGR7_RESET);

    /* TODO: No documentation about this? */
    unsigned int val = in32(MX6X_ANATOP_BASE + MX6X_ANADIG_REG_CORE);
    val &= ~ANADIG_REG_CORE_REG2_TRG_MASK;
    val |= ((0x14 << 18) & ANADIG_REG_CORE_REG2_TRG_MASK);
	out32(MX6X_ANATOP_BASE + MX6X_ANADIG_REG_CORE, val);
}

void init_pinmux()
{
    /*
     * UART
     */

	/* UART1 RXD - CSIO-DAT11 */
	set_mux_cfg(SWMUX_CSI0_DAT11, MUX_CTL_MUX_MODE_ALT3);
	set_pad_cfg(SWPAD_CSI0_DAT11, MX6Q_PAD_SETTINGS_UART);
	set_pin_as_input(SWINPUT_UART1_IPP_UART_RXD_MUX, 0x1);
	
	/* UART1 TXD - CSIO-DAT10 */
	set_mux_cfg(SWMUX_CSI0_DAT10, MUX_CTL_MUX_MODE_ALT3);
	set_pad_cfg(SWPAD_CSI0_DAT10, MX6Q_PAD_SETTINGS_UART);

	/* UART3 is for the GPS receiver. */
	
	/*
     * USDHC interfaces 
     */

	/* SD3 CLK */
	set_mux_cfg(SWMUX_SD3_CLK, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_SD3_CLK, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 CMD */
	set_mux_cfg(SWMUX_SD3_CMD, MUX_CTL_MUX_MODE_ALT0 | MUX_CTL_SION);
	set_pad_cfg(SWPAD_SD3_CMD, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT0 */
	set_mux_cfg(SWMUX_SD3_DAT0, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_SD3_DAT0, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT1 */
	set_mux_cfg(SWMUX_SD3_DAT1, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_SD3_DAT1, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT2 */
	set_mux_cfg(SWMUX_SD3_DAT2, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_SD3_DAT2, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT3 */
	set_mux_cfg(SWMUX_SD3_DAT3, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_SD3_DAT3, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT4 */
	set_mux_cfg(SWMUX_SD3_DAT4, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_SD3_DAT4, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT5 */
	set_mux_cfg(SWMUX_SD3_DAT5, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_SD3_DAT5, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT6 */
	set_mux_cfg(SWMUX_SD3_DAT6, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_SD3_DAT6, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT7 */
	set_mux_cfg(SWMUX_SD3_DAT7, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_SD3_DAT7, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 Write Protect - configure GPIO2[1] as an input */
	set_mux_cfg(SWMUX_NANDF_D1, MUX_CTL_MUX_MODE_ALT5);
	set_pad_cfg(SWPAD_NANDF_D1, MX6Q_PAD_SETTINGS_USDHC_CDWP);
	out32(MX6X_GPIO2_BASE + MX6X_GPIO_GDIR, in32(MX6X_GPIO2_BASE + MX6X_GPIO_GDIR) & ~(1<<1));

	/* SD3 Card Detect - configure GPIO2[0] as an input */
	set_mux_cfg(SWMUX_NANDF_D0, MUX_CTL_MUX_MODE_ALT5);
	set_pad_cfg(SWPAD_NANDF_D0, MX6Q_PAD_SETTINGS_USDHC_CDWP);
	out32(MX6X_GPIO2_BASE + MX6X_GPIO_GDIR, in32(MX6X_GPIO2_BASE + MX6X_GPIO_GDIR) & ~(1<<0));


	/*
	* SPI
	*/

	/* SPI SCLK */
	set_mux_cfg(SWMUX_KEY_COL0, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_KEY_COL0, MX6Q_PAD_SETTINGS_ECSPI);
	set_pin_as_input(SWINPUT_ECSPI1_IPP_CSPI_CLK, 0x2);

	/* SPI MISO */
	set_mux_cfg(SWMUX_KEY_COL1, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_KEY_COL1, MX6Q_PAD_SETTINGS_ECSPI);
	set_pin_as_input(SWINPUT_ECSPI1_IPP_IND_MISO, 0x2);

	/* SPI MOSI */
	set_mux_cfg(SWMUX_KEY_ROW0, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_KEY_ROW0, MX6Q_PAD_SETTINGS_ECSPI);
	set_pin_as_input(SWINPUT_ECSPI1_IPP_IND_MOSI, 0x2);

	/* Select mux mode ALT0 for SS1 */
	set_mux_cfg(SWMUX_KEY_ROW1, MUX_CTL_MUX_MODE_ALT0);
	set_pad_cfg(SWPAD_KEY_ROW1, MX6Q_PAD_SETTINGS_ECSPI);
	set_pin_as_input(SWINPUT_ECSPI1_IPP_IND_SS_B_0, 0x2);
}

void init_aips()
{
    /*
     * Set all MPROTx to be non-bufferable, trusted for R/W,
     * not forced to user-mode.
     */
	out32(MX6X_AIPS1_CONFIG, 0x77777777);
	out32((MX6X_AIPS1_CONFIG + 0x04), 0x77777777);
	out32((MX6X_AIPS1_CONFIG + 0x40), 0x0);
	out32((MX6X_AIPS1_CONFIG + 0x44), 0x0);
	out32((MX6X_AIPS1_CONFIG + 0x48), 0x0);
	out32((MX6X_AIPS1_CONFIG + 0x4C), 0x0);
	out32((MX6X_AIPS1_CONFIG + 0x50), 0x0);

	out32(MX6X_AIPS2_CONFIG, 0x77777777);
	out32((MX6X_AIPS2_CONFIG + 0x04), 0x77777777);
	out32((MX6X_AIPS2_CONFIG + 0x40), 0x0);
	out32((MX6X_AIPS2_CONFIG + 0x44), 0x0);
	out32((MX6X_AIPS2_CONFIG + 0x48), 0x0);
	out32((MX6X_AIPS2_CONFIG + 0x4C), 0x0);
	out32((MX6X_AIPS2_CONFIG + 0x50), 0x0);
}

