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

/*
 * Routines to initialize the various hardware subsystems
 * on the i.MX6Q Sabre-Smart
 */

#include "startup.h"
#include "board.h"

/* weilun@adv - begin */
//#define CONFIG_MACH_MX6Q_RSB_4410
//#define CONFIG_MACH_MX6Q_ROM_5420
//#define CONFIG_MACH_MX6Q_ROM_7420
/* weilun@adv - end */


#define MX6Q_PAD_SETTINGS_I2C	(PAD_CTL_SRE_FAST | PAD_CTL_ODE_ENABLE | PAD_CTL_PKE_ENABLE | \
							PAD_CTL_PUE_PULL | PAD_CTL_DSE_40_OHM | PAD_CTL_PUS_100K_PU | \
							PAD_CTL_HYS_ENABLE | PAD_CTL_SPEED_MEDIUM)

#define MX6Q_PAD_SETTINGS_USDHC	(PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PULL | PAD_CTL_PUS_47K_PU | \
								PAD_CTL_SPEED_LOW | PAD_CTL_DSE_80_OHM | PAD_CTL_SRE_FAST | \
								PAD_CTL_HYS_ENABLE)


#define MX6Q_PAD_SETTINGS_USDHC_CDWP	(PAD_CTL_PKE_DISABLE | PAD_CTL_SPEED_LOW | \
										PAD_CTL_DSE_DISABLE | PAD_CTL_HYS_ENABLE)

#define MX6Q_PAD_SETTINGS_ECSPI (PAD_CTL_SRE_FAST | PAD_CTL_SPEED_MEDIUM | PAD_CTL_DSE_40_OHM | PAD_CTL_HYS_ENABLE)

#define MX6Q_PAD_SETTINGS_ENET	( PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PULL | PAD_CTL_PUS_100K_PU | \
				PAD_CTL_SPEED_MEDIUM  | PAD_CTL_DSE_40_OHM | PAD_CTL_HYS_ENABLE )

#define MX6Q_PAD_SETTINGS_CLKO (PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PULL | PAD_CTL_PUS_47K_PU | PAD_CTL_SPEED_LOW | \
							PAD_CTL_DSE_80_OHM | PAD_CTL_SRE_FAST | PAD_CTL_HYS_ENABLE)

// No pull-up/down.  Average drive strength
#define MX6Q_PAD_SETTINGS_GPIO_OUT ( PAD_CTL_SPEED_LOW | PAD_CTL_DSE_80_OHM | PAD_CTL_SRE_SLOW )

#define CCM_CCOSR_CLKO1_EN	(0x1 << 7)
#define CCM_CCOSR_CLKO1_DIV_8	(0x7 << 4)
#define CCM_CCOSR_CLKO1_AHB	(0xb << 0)

#define PLL_ENET_CTRL_ENABLE      (1 << 13) 
#define PLL_ENET_CTRL_DIV_SELECT  (3 << 0) 

#define GPIO_LOW		0
#define GPIO_HIGH		1

#define GPIO_IRQ_LEVEL_LOW		0
#define GPIO_IRQ_LEVEL_HIGH		1
#define GPIO_IRQ_EDGE_RISE		2
#define GPIO_IRQ_EDGE_FALL		3


/* look-up table for GPIO port base addresses */
uint32_t gpio_base_addresses[] =
{
		0,		/* GPiMX6 GPIO ranges are numbered 1-7. Zero in invalid. */
		MX6X_GPIO1_BASE,
		MX6X_GPIO2_BASE,
		MX6X_GPIO3_BASE,
		MX6X_GPIO4_BASE,
		MX6X_GPIO5_BASE,
		MX6X_GPIO6_BASE,
		MX6X_GPIO7_BASE
};

/* helper function to drive make a GPIO an output and drive it HIGH */
void gpio_drive_output( int instance, int bit, int state )
{
	uint32_t gpio_base;

	/* safety */
	if ( (instance < 1) || (instance > 7) )
	{
		return;
	}
	if ( (bit < 0) || (bit > 31) )
	{
		return;
	}
	gpio_base = gpio_base_addresses[instance];

	out32(gpio_base + MX6X_GPIO_GDIR, in32(gpio_base + MX6X_GPIO_GDIR) | (0x1 << bit));
	if ( state == 0 )
	{
		out32(gpio_base + MX6X_GPIO_DR, in32(gpio_base + MX6X_GPIO_DR) & ~(0x1 << bit));
	}
	else
	{
		out32(gpio_base + MX6X_GPIO_DR, in32(gpio_base + MX6X_GPIO_DR) | (0x1 << bit));
	}
}

/* helper function to drive make a GPIO an output and drive it HIGH */
void gpio_select_input( int instance, int bit )
{
	uint32_t gpio_base;

	/* safety */
	if ( (instance < 1) || (instance > 7) )
	{
		return;
	}
	if ( (bit < 0) || (bit > 31) )
	{
		return;
	}
	gpio_base = gpio_base_addresses[instance];

	out32(gpio_base + MX6X_GPIO_GDIR, in32(gpio_base + MX6X_GPIO_GDIR) & ~(0x1 << bit));
}

void gpio_set_irq_mode( int instance, int bit, int irq_mode )
{
	uint32_t tmp;
	uint32_t gpio_base;

	/* safety */
	if ( (instance < 1) || (instance > 7) )
	{
		return;
	}
	if ( (bit < 0) || (bit > 31) )
	{
		return;
	}
	gpio_base = gpio_base_addresses[instance];

	/* there are two bits per IRQ, so two registers for each bank */
	if ( bit < 16 )
	{
		bit = bit << 1;
		tmp = in32(gpio_base + MX6X_GPIO_ICR1);
		tmp &= ~(0x2 << bit);
		tmp |= irq_mode << bit;
		out32(gpio_base + MX6X_GPIO_ICR1, tmp );
	}
	else
	{
		bit = (bit - 16) << 1;
		tmp = in32(gpio_base + MX6X_GPIO_ICR2);
		tmp &= ~(0x2 << bit);
		tmp |= irq_mode << bit;
		out32(gpio_base + MX6X_GPIO_ICR2, tmp );
	}
}

/* weilun@adv - start */
#define MX6Q_PAD_SETTINGS_UART (PAD_CTL_HYS_ENABLE | PAD_CTL_PKE_ENABLE | \
								PAD_CTL_PUS_100K_PU | PAD_CTL_SPEED_MEDIUM | \
								PAD_CTL_DSE_40_OHM | PAD_CTL_SRE_FAST | PAD_CTL_PUE_PULL)

void mx6q_init_mtouch(void)
{
	/* FIXME: There is something wrong with QNX BSP. */
	/* The pinmux of UART4 is configured in uboot, not here */
#if 0
	/*
	 * MTOUCH_UART_RXD CSI0-DAT13 
	 */
	pinmux_set_swmux(SWMUX_CSI0_DAT13, MUX_CTL_MUX_MODE_ALT3);
	pinmux_set_padcfg(SWPAD_CSI0_DAT13, MX6Q_PAD_SETTINGS_UART);
	pinmux_set_input(SWINPUT_UART4_IPP_UART_RXD_MUX, 0x1);
	
	/*
	 * MTOUCH_UART_CTS CSI0-DAT17
	 */
	pinmux_set_swmux(SWMUX_CSI0_DAT17, MUX_CTL_MUX_MODE_ALT3);
	pinmux_set_padcfg(SWPAD_CSI0_DAT17, MX6Q_PAD_SETTINGS_UART);

	/*
	 * MTOUCH_UART_TXD CSI0_DAT12
	 */
	pinmux_set_swmux(SWMUX_CSI0_DAT12, MUX_CTL_MUX_MODE_ALT3);
	pinmux_set_padcfg(SWPAD_CSI0_DAT12, MX6Q_PAD_SETTINGS_UART);

	/*
	 * MTOUCH_UART_RTS CSI0_DAT16
	 */
	pinmux_set_swmux(SWMUX_CSI0_DAT16, MUX_CTL_MUX_MODE_ALT3);
	pinmux_set_padcfg(SWPAD_CSI0_DAT16, MX6Q_PAD_SETTINGS_UART);
	pinmux_set_input(SWINPUT_UART4_IPP_UART_RTS_B, 0x0);
#endif

#if 0
	/* debug - begin */
	{

		#include <arm/mx6x.h>
		#include <arm/mx1.h>
		//#define UART1_BASE 0x2020000
		//#define UART2_BASE 0x21e8000
		//#define UART3_BASE 0x21ec000
		//#define UART4_BASE 0x21f0000

		mx6x_usleep(500000);
		//int data = 0;
		unsigned int reg1 = in32(MX6X_UART1_BASE + MX1_UART_SR2);
		unsigned int reg2 = in32(MX6X_UART2_BASE + MX1_UART_SR2);
		unsigned int reg3 = in32(MX6X_UART3_BASE + MX1_UART_SR2);
		unsigned int reg4 = in32(MX6X_UART4_BASE + MX1_UART_SR2);
		unsigned int reg1_1 = in32(MX6X_IOMUXC_BASE + 0x920);
		unsigned int reg4_1 = in32(MX6X_IOMUXC_BASE + 0x938);

		//if (MXC_UART_SR2 & MXC_USR2_RDR)
		//	data = 1;
		//else
		//	data = 0;
		kprintf("weilun@adv debug serial_UART1_SR2 = 0x%x\n", reg1);
		kprintf("weilun@adv debug serial_UART1_SELECT_INPUT = 0x%x\n\n", reg1_1);
		kprintf("weilun@adv debug serial_UART2_SR2 = 0x%x\n", reg2);
		kprintf("weilun@adv debug serial_UART3_SR2 = 0x%x\n", reg3);
		kprintf("weilun@adv debug serial_UART4_SR2 = 0x%x\n\n", reg4);
		kprintf("weilun@adv debug serial_UART4_SELECT_INPUT = 0x%x\n\n", reg4_1);

	}
	/* debug - end  */
#endif
}
/* weilun@adv - end */

void mx6q_init_i2c1(void)
{
	/* I2C1  SCL */
	pinmux_set_swmux(SWMUX_CSI0_DAT9, MUX_CTL_MUX_MODE_ALT4 | MUX_CTL_SION);
	pinmux_set_padcfg(SWPAD_CSI0_DAT9, MX6Q_PAD_SETTINGS_I2C);
	pinmux_set_input(SWINPUT_I2C1_IPP_SCL_IN, 0x1);

	/* I2C1  SDA */
	pinmux_set_swmux(SWMUX_CSI0_DAT8, MUX_CTL_MUX_MODE_ALT4 | MUX_CTL_SION);
	pinmux_set_padcfg(SWPAD_CSI0_DAT8, MX6Q_PAD_SETTINGS_I2C);
	pinmux_set_input(SWINPUT_I2C1_IPP_SDA_IN, 0x1);
}

void mx6q_init_i2c2(void)
{
	/* I2C2  SCL */
	pinmux_set_swmux(SWMUX_KEY_COL3, MUX_CTL_MUX_MODE_ALT4 | MUX_CTL_SION);
	pinmux_set_padcfg(SWPAD_KEY_COL3, MX6Q_PAD_SETTINGS_I2C);
	pinmux_set_input(SWINPUT_I2C2_IPP_SCL_IN, 0x1);

	/* I2C2  SDA */
	pinmux_set_swmux(SWMUX_KEY_ROW3, MUX_CTL_MUX_MODE_ALT4 | MUX_CTL_SION);
	pinmux_set_padcfg(SWPAD_KEY_ROW3, MX6Q_PAD_SETTINGS_I2C);
	pinmux_set_input(SWINPUT_I2C2_IPP_SDA_IN, 0x1);
}

void mx6q_init_i2c3(void)
{
	/* I2C3 SCL */
	pinmux_set_swmux(SWMUX_GPIO_3, MUX_CTL_MUX_MODE_ALT2 | MUX_CTL_SION);
	pinmux_set_padcfg(SWPAD_GPIO_3, MX6Q_PAD_SETTINGS_I2C);
	pinmux_set_input(SWINPUT_I2C3_IPP_SCL_IN, 0x1);

	/* I2C3  SDA */
	pinmux_set_swmux(SWMUX_GPIO_6, MUX_CTL_MUX_MODE_ALT2 | MUX_CTL_SION);
	pinmux_set_padcfg(SWPAD_GPIO_6, MX6Q_PAD_SETTINGS_I2C);
	pinmux_set_input(SWINPUT_I2C3_IPP_SDA_IN, 0x1);
}

void mx6q_init_audmux_pins(void)
{
	// CCM CLKO pin muxing
	pinmux_set_swmux(SWMUX_GPIO_0, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_GPIO_0, MX6Q_PAD_SETTINGS_CLKO);

	// AUDMUX pin muxing
    // Note: the AUDMUX3 pins are not daisy chained

    // AUD4_RXD
	pinmux_set_swmux(SWMUX_CSI0_DAT7, MUX_CTL_MUX_MODE_ALT4);

    // AUD4_TXC
	pinmux_set_swmux(SWMUX_CSI0_DAT4, MUX_CTL_MUX_MODE_ALT4);

    // AUD4_TXD
	pinmux_set_swmux(SWMUX_CSI0_DAT5, MUX_CTL_MUX_MODE_ALT4);

    // AUD4_TXFS
	pinmux_set_swmux(SWMUX_CSI0_DAT6, MUX_CTL_MUX_MODE_ALT4);

	// Configure CLKO to produce 16.5MHz master clock. If you modify this, you will also have to adjust the FLL N.K value for the audio codec.
	out32(MX6X_CCM_BASE + MX6X_CCM_CCOSR, CCM_CCOSR_CLKO1_EN | CCM_CCOSR_CLKO1_AHB | CCM_CCOSR_CLKO1_DIV_8);
}

void mx6q_init_enet(void)
{
	// RGMII MDIO - transfers control info between MAC and PHY
	pinmux_set_swmux(SWMUX_ENET_MDIO, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_input(SWINPUT_ENET_IPP_IND_MAC0_MDIO,0);

	// RGMII MDC - output from MAC to PHY, provides clock reference for MDIO
	pinmux_set_swmux(SWMUX_ENET_MDC, MUX_CTL_MUX_MODE_ALT1);

	// RGMII TXC - output from MAC, provides clock used by RGMII_TXD[3:0], RGMII_TX_CTL
	pinmux_set_swmux(SWMUX_RGMII_TXC, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_TXC, MX6Q_PAD_SETTINGS_ENET);

	// RGMII TXD[3:0] - Transmit Data Output
	pinmux_set_swmux(SWMUX_RGMII_TD0, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_TD0, MX6Q_PAD_SETTINGS_ENET);

	pinmux_set_swmux(SWMUX_RGMII_TD1, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_TD1, MX6Q_PAD_SETTINGS_ENET);

	pinmux_set_swmux(SWMUX_RGMII_TD2, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_TD2, MX6Q_PAD_SETTINGS_ENET);

	pinmux_set_swmux(SWMUX_RGMII_TD3, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_TD3, MX6Q_PAD_SETTINGS_ENET);

	// RGMII TX_CTL - contains TXEN on TXC rising edge, TXEN XOR TXERR on TXC falling edge
	pinmux_set_swmux(SWMUX_RGMII_TX_CTL, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_TX_CTL, MX6Q_PAD_SETTINGS_ENET);

	// set ENET_REF_CLK to mux mode 1 - TX_CLK, this is a 125MHz input which is driven by the PHY
	pinmux_set_swmux(SWMUX_ENET_REF_CLK, MUX_CTL_MUX_MODE_ALT1);	

	pinmux_set_swmux(SWMUX_RGMII_RXC, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_RXC, MX6Q_PAD_SETTINGS_ENET);
	pinmux_set_input(SWINPUT_ENET_IPP_IND_MAC0_RXCLK,0);

	pinmux_set_swmux(SWMUX_RGMII_RD0, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_RD0, MX6Q_PAD_SETTINGS_ENET);
	pinmux_set_input(SWINPUT_ENET_IPP_IND_MAC_RXDATA_0,0);

	pinmux_set_swmux(SWMUX_RGMII_RD1, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_RD1, MX6Q_PAD_SETTINGS_ENET);
	pinmux_set_input(SWINPUT_ENET_IPP_IND_MAC_RXDATA_1,0);

	pinmux_set_swmux(SWMUX_RGMII_RD2, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_RD2, MX6Q_PAD_SETTINGS_ENET);
	pinmux_set_input(SWINPUT_ENET_IPP_IND_MAC_RXDATA_2,0);

	pinmux_set_swmux(SWMUX_RGMII_RD3, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_RD3, MX6Q_PAD_SETTINGS_ENET);
	pinmux_set_input(SWINPUT_ENET_IPP_IND_MAC_RXDATA_3,0);

	pinmux_set_swmux(SWMUX_RGMII_RX_CTL, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_RGMII_RX_CTL, MX6Q_PAD_SETTINGS_ENET);
	pinmux_set_input(SWINPUT_ENET_IPP_IND_MAC0_RXEN,0);

	// TX EN set to Wake-on-LAN
	pinmux_set_swmux(SWMUX_ENET_TX_EN, MUX_CTL_MUX_MODE_ALT1);

	// RGMII reset (under GPIO control)
	pinmux_set_swmux(SWMUX_ENET_CRS_DV, MUX_CTL_MUX_MODE_ALT5);
	gpio_drive_output( 1, 25, GPIO_LOW );
	mx6x_usleep(10000);
	gpio_drive_output( 1, 25, GPIO_HIGH );
}

void mx6q_init_usdhc_v3(void)
{
	/*
	 * SD 2
	 */

	/* SD2 CLK */
	pinmux_set_swmux(SWMUX_SD2_CLK, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD2_CLK, MX6Q_PAD_SETTINGS_USDHC);

	/* SD2 CMD */
	pinmux_set_swmux(SWMUX_SD2_CMD, MUX_CTL_MUX_MODE_ALT0 | MUX_CTL_SION);
	pinmux_set_padcfg(SWPAD_SD2_CMD, MX6Q_PAD_SETTINGS_USDHC);

	/* SD2 DAT0 */
	pinmux_set_swmux(SWMUX_SD2_DAT0, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD2_DAT0, MX6Q_PAD_SETTINGS_USDHC);

	/* SD2 DAT1 */
	pinmux_set_swmux(SWMUX_SD2_DAT1, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD2_DAT1, MX6Q_PAD_SETTINGS_USDHC);

	/* SD2 DAT2 */
	pinmux_set_swmux(SWMUX_SD2_DAT2, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD2_DAT2, MX6Q_PAD_SETTINGS_USDHC);

	/* SD2 DAT3 */
	pinmux_set_swmux(SWMUX_SD2_DAT3, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD2_DAT3, MX6Q_PAD_SETTINGS_USDHC);

	/* SD2 DAT4 */
	pinmux_set_swmux(SWMUX_NANDF_D4, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_NANDF_D4, MX6Q_PAD_SETTINGS_USDHC);

	/* SD2 DAT5 */
	pinmux_set_swmux(SWMUX_NANDF_D5, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_NANDF_D5, MX6Q_PAD_SETTINGS_USDHC);

	/* SD2 DAT6 */
	pinmux_set_swmux(SWMUX_SD3_DAT6, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_NANDF_D6, MX6Q_PAD_SETTINGS_USDHC);

	/* SD2 DAT7 */
	pinmux_set_swmux(SWMUX_SD3_DAT7, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_NANDF_D7, MX6Q_PAD_SETTINGS_USDHC);

	/* SD2 Write Protect - configure GPIO2[3] as an input */
	pinmux_set_swmux(SWMUX_NANDF_D3, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWPAD_NANDF_D3, MX6Q_PAD_SETTINGS_USDHC_CDWP);
	gpio_select_input( 2, 3 );

	/* SD2 Card Detect - configure GPIO2[2] as an input */
	pinmux_set_swmux(SWMUX_NANDF_D2, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWPAD_NANDF_D2, MX6Q_PAD_SETTINGS_USDHC_CDWP);
	gpio_select_input( 2, 2 );

	/*
	 * SD 3
	 */

	/* SD3 CLK */
	pinmux_set_swmux(SWMUX_SD3_CLK, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD3_CLK, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 CMD */
	pinmux_set_swmux(SWMUX_SD3_CMD, MUX_CTL_MUX_MODE_ALT0 | MUX_CTL_SION);
	pinmux_set_padcfg(SWPAD_SD3_CMD, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT0 */
	pinmux_set_swmux(SWMUX_SD3_DAT0, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD3_DAT0, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT1 */
	pinmux_set_swmux(SWMUX_SD3_DAT1, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD3_DAT1, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT2 */
	pinmux_set_swmux(SWMUX_SD3_DAT2, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD3_DAT2, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT3 */
	pinmux_set_swmux(SWMUX_SD3_DAT3, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD3_DAT3, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT4 */
	pinmux_set_swmux(SWMUX_SD3_DAT4, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD3_DAT4, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT5 */
	pinmux_set_swmux(SWMUX_SD3_DAT5, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD3_DAT5, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT6 */
	pinmux_set_swmux(SWMUX_SD3_DAT6, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD3_DAT6, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 DAT7 */
	pinmux_set_swmux(SWMUX_SD3_DAT7, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_SD3_DAT7, MX6Q_PAD_SETTINGS_USDHC);

	/* SD3 Write Protect - configure GPIO2[1] as an input */
	pinmux_set_swmux(SWMUX_NANDF_D1, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWPAD_NANDF_D1, MX6Q_PAD_SETTINGS_USDHC_CDWP);
	gpio_select_input( 2, 1 );

	/* SD3 Card Detect - configure GPIO2[0] as an input */
	pinmux_set_swmux(SWMUX_NANDF_D0, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWPAD_NANDF_D0, MX6Q_PAD_SETTINGS_USDHC_CDWP);
	gpio_select_input( 2, 0 );
}

void mx6q_init_ecspi(void)
{
	/* weilun@adv 12/10/2015: fix LVDS panel issue - begin */
	/* modified for SPI 2/8/2016 - begin */
#if 0
	/* SPI SCLK */
	pinmux_set_swmux(SWMUX_KEY_COL0, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_KEY_COL0, MX6Q_PAD_SETTINGS_ECSPI);
	pinmux_set_input(SWINPUT_ECSPI1_IPP_CSPI_CLK, 0x02); /* Mode ALT0 */

	/* SPI MISO */
	pinmux_set_swmux(SWMUX_KEY_COL1, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_KEY_COL1, MX6Q_PAD_SETTINGS_ECSPI);
	pinmux_set_input(SWINPUT_ECSPI1_IPP_IND_MISO, 0x02); /* Mode ALT0 */

	/* SPI MOSI */
	pinmux_set_swmux(SWMUX_KEY_ROW0, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_KEY_ROW0, MX6Q_PAD_SETTINGS_ECSPI);
	pinmux_set_input(SWINPUT_ECSPI1_IPP_IND_MOSI, 0x02); /* Mode ALT0 */

	/* Select mux mode ALT0 for SS0 */
	pinmux_set_swmux(SWMUX_KEY_ROW1, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_padcfg(SWPAD_KEY_ROW1, MX6Q_PAD_SETTINGS_ECSPI);
	pinmux_set_input(SWINPUT_ECSPI1_IPP_IND_SS_B_0, 0x02); /* Mode ALT0 */
#else

	/* SPI SCLK */
	pinmux_set_swmux(SWMUX_EIM_D16, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_EIM_D16, MX6Q_PAD_SETTINGS_ECSPI);
	pinmux_set_input(SWINPUT_ECSPI1_IPP_CSPI_CLK, 0x0); /* Mode ALT1 */

	/* SPI MISO */
	pinmux_set_swmux(SWMUX_EIM_D17, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_EIM_D17, MX6Q_PAD_SETTINGS_ECSPI);
	pinmux_set_input(SWINPUT_ECSPI1_IPP_IND_MISO, 0x0); /* Mode ALT1 */

	/* SPI MOSI */
	pinmux_set_swmux(SWMUX_EIM_D18, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_EIM_D18, MX6Q_PAD_SETTINGS_ECSPI);
	pinmux_set_input(SWINPUT_ECSPI1_IPP_IND_MOSI, 0x0); /* Mode ALT1 */

	/* Select mux mode ALT0 for SS0 */

/* ROM-5420 */
#if defined(CONFIG_MACH_MX6Q_ROM_5420) || defined(CONFIG_MACH_MX6Q_RSB_4410)
	pinmux_set_swmux(SWMUX_EIM_EB2, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_EIM_EB2, MX6Q_PAD_SETTINGS_ECSPI);
	pinmux_set_input(SWINPUT_ECSPI1_IPP_IND_SS_B_0, 0x0); /* Mode ALT1 */
#endif

/* ROM-7420 */
#if defined(CONFIG_MACH_MX6Q_ROM_7420)
	pinmux_set_swmux(SWMUX_EIM_D19, MUX_CTL_MUX_MODE_ALT1);
	pinmux_set_padcfg(SWPAD_EIM_D19, MX6Q_PAD_SETTINGS_ECSPI);
	pinmux_set_input(SWINPUT_ECSPI1_IPP_IND_SS_B_1, 0x0); /* Mode ALT1 */
	//pinmux_set_input(SWINPUT_ECSPI1_IPP_IND_SS_B_0, 0x0); /* Mode ALT1 */
#endif


#endif
	/* weilun@adv 12/10/2015: fix LVDS panel issue - end */
	/* modified for SPI 2/8/2016 - end */
}

void mx6q_init_lvds(void)
{
	/*weilun@adv - fixed RSB4410 LVDS issue - begin */
#if 0
	/* Enable PWM - Used to configure backlight brightness (GPIO1[18]) */
	pinmux_set_swmux(SWMUX_SD1_CMD, MUX_CTL_MUX_MODE_ALT5);
	
	/* 
	 * Configure GPIO controlling PWM as an output and drive the GPIO high.  In this case PWM is always high, meaning
	 * a 100% duty cycle, a lower duty cycle could be used to decrease the brightness of the display. 
	 */
	gpio_drive_output( 1, 18, GPIO_HIGH );

	/* Set pad [NANDF_ALE] to GPIO_6[7] for CAP_TCH_INT0 touchscreen interrupts */
	pinmux_set_swmux(SWMUX_NANDF_ALE, MUX_CTL_MUX_MODE_ALT5);
	gpio_select_input( 6, 7 );

	/* Set pad [NANDF_CLE] to GPIO_6[8] for CAP_TCH_INT1 touchscreen interrupts */
	pinmux_set_swmux(SWMUX_NANDF_CLE, MUX_CTL_MUX_MODE_ALT5);
	gpio_select_input( 6, 8 );
#else

/* ROM-5420 && RSB-4410 */
#if defined(CONFIG_MACH_MX6Q_ROM_5420) || defined(CONFIG_MACH_MX6Q_RSB_4410)
#if defined(CONFIG_MACH_MX6Q_RSB_4410)
	/* Enable PWM - Used to configure backlight brightness (GPIO9) */
	pinmux_set_swmux(SWMUX_GPIO_9, MUX_CTL_MUX_MODE_ALT4);
#endif
	pinmux_set_swmux(SWMUX_KEY_COL0, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWMUX_KEY_COL0, MX6Q_PAD_SETTINGS_GPIO_OUT);
	//mx6x_set_gpio_output(MX6X_GPIO4_BASE, 6, GPIO_HIGH);
	gpio_drive_output( 4, 6, GPIO_HIGH );

	pinmux_set_swmux(SWMUX_KEY_ROW0, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWMUX_KEY_ROW0, MX6Q_PAD_SETTINGS_GPIO_OUT);
	//mx6x_set_gpio_output(MX6X_GPIO4_BASE, 7, GPIO_HIGH);
	gpio_drive_output( 4, 7, GPIO_HIGH );
#endif

/* ROM-7420 */
#if defined(CONFIG_MACH_MX6Q_ROM_7420)
	/* Enable PWM */
	pinmux_set_swmux(SWMUX_SD1_DAT3, MUX_CTL_MUX_MODE_ALT3);
	
	/* LVDS_VDD_EN */
	pinmux_set_swmux(SWMUX_NANDF_CLE, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWMUX_NANDF_CLE, MX6Q_PAD_SETTINGS_GPIO_OUT);
	gpio_drive_output( 6, 7, GPIO_HIGH );

	/* LVDS_BKLT_EN */
	pinmux_set_swmux(SWMUX_NANDF_WP_B, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWMUX_NANDF_WP_B, MX6Q_PAD_SETTINGS_GPIO_OUT);
	gpio_drive_output( 6, 9, GPIO_HIGH );
#endif

#endif
	/*weilun@adv - fixed RSB4410 LVDS issue - begin */

}

void mx6q_init_lcd_panel(void)
{
	/* IPU1 Display Interface 0 clock */
	pinmux_set_swmux(SWMUX_DI0_DISP_CLK, MUX_CTL_MUX_MODE_ALT0);

	/* LCD EN */
	pinmux_set_swmux(SWMUX_DI0_PIN15, MUX_CTL_MUX_MODE_ALT0);

	/* LCD HSYNC */
	pinmux_set_swmux(SWMUX_DI0_PIN2, MUX_CTL_MUX_MODE_ALT0);

	/* LCD VSYNC */
	pinmux_set_swmux(SWMUX_DI0_PIN3, MUX_CTL_MUX_MODE_ALT0);

	/* Data Lines */
	pinmux_set_swmux(SWMUX_DISP0_DAT0, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT1, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT2, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT3, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT4, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT5, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT6, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT7, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT8, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT9, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT10, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT11, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT12, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT13, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT14, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT15, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT16, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT17, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT18, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT19, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT20, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT21, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT22, MUX_CTL_MUX_MODE_ALT0);
	pinmux_set_swmux(SWMUX_DISP0_DAT23, MUX_CTL_MUX_MODE_ALT0);

	/* Configure pin as GPIO1_30 (Power Enable)
	 * Force LCD_EN (ENET_TXD0) HIGH to enable LCD */
	pinmux_set_swmux(SWMUX_ENET_TXD0, MUX_CTL_MUX_MODE_ALT5);
	gpio_drive_output( 1, 30, GPIO_HIGH );

	/* Note: the touchscreen is configured by [mx6q_init_sensors] */
}

/*
 * Initialise all the GPIO lines used to control the mini PCIe slot
 *
 * - MPCIE_3V3
 *   PCIE_PWR_EN = EIM_D19(ALT5) = GPIO[19] of instance: gpio3
 *
 * - WAKE# => PCIE_WAKE_B => CSI0_DATA_EN
 * - W_DISABLE# => PCIE_DIS_B => KEY_COL4
 * - PERST# => PCIE_RST_B => SPDIF_OUT => GPIO_17
 *
 * I2C bus 3 connects to the PCIe SMB channel
 * - SMB_CLK = PCIe_SMB_CLK
 * - SMB_DATA = PCIe_SMB_DATA
 */
void mx6q_init_pcie( void )
{
	/* Disable power to PCIe device by driving a GPIO low.
	 * The driver will enable power itself at the right time
	 * MPCIE_3V3
	 * PCIE_PWR_EN = EIM_D19(ALT5) = GPIO[19] of instance: gpio3 */


/* ROM-7420 */
#if defined(CONFIG_MACH_MX6Q_ROM_7420)
	pinmux_set_padcfg(SWPAD_EIM_D20, MX6Q_PAD_SETTINGS_GPIO_OUT);
	gpio_drive_output( 3, 20, GPIO_LOW );
#else
	pinmux_set_swmux(SWMUX_EIM_D19, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWPAD_EIM_D19, MX6Q_PAD_SETTINGS_GPIO_OUT);
	gpio_drive_output( 3, 19, GPIO_LOW );
#endif

	/* Wake-up line is an input
	 * WAKE# => PCIE_WAKE_B => CSI0_DATA_EN(ALT5) = GPIO[20] of instance: gpio5. */
	pinmux_set_swmux(SWMUX_CSI0_DATA_EN, MUX_CTL_MUX_MODE_ALT5);
	gpio_select_input( 5, 20 );

	/* Assert disable by driving LOW
	 * W_DISABLE# => PCIE_DIS_B => KEY_COL4(ALT5) = GPIO[14] of instance: gpio4. */
	pinmux_set_swmux(SWMUX_KEY_COL4, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWPAD_KEY_COL4, MX6Q_PAD_SETTINGS_GPIO_OUT);
	gpio_drive_output( 4, 14, GPIO_LOW );

	/* De-assert reset by driving HIGH
	 * PERST# => PCIE_RST_B => SPDIF_OUT => GPIO_17(ALT5) = GPIO[12] of instance: gpio7. */
	pinmux_set_swmux(SWMUX_GPIO_17, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWPAD_GPIO_17, MX6Q_PAD_SETTINGS_GPIO_OUT);
	gpio_drive_output( 7, 12, GPIO_HIGH );
}


/* The Sabre Smart is fitted with various sensors:
 * - 3 axis accelerometer  I2C_1 = MMA8451
 * - 3 axis magentometer   I2C_3 = MAG3110
 * - ambient light sensor  I2C_3 = ISL29023
 * - touchscreen           I2C_2 = MAX11800
 *
 * Each of these can generate an interrupt to the iMX6
 * over a GPIO line.  This function sets up the pinmuxes
 * for the interrupt lines.
 */
void mx6q_init_sensors(void)
{
	/* Accelerometer ACCL_INT_IN */
	pinmux_set_swmux(SWMUX_SD1_CMD, MUX_CTL_MUX_MODE_ALT5);
	
	/* Ambient Light Sensor ALS_INT */
	pinmux_set_swmux(SWMUX_EIM_DA9, MUX_CTL_MUX_MODE_ALT5);

	/* Compass COMP_INT */
	pinmux_set_swmux(SWMUX_EIM_D16, MUX_CTL_MUX_MODE_ALT5);

	/* Touchscreen TS_INT : EIM_D26 pin as GPIO_3[26] for touchscreen IRQ */
	pinmux_set_swmux(SWMUX_EIM_D26, MUX_CTL_MUX_MODE_ALT5);
	pinmux_set_padcfg(SWPAD_EIM_D26, PAD_CTL_PKE_DISABLE | PAD_CTL_SPEED_LOW | PAD_CTL_DSE_DISABLE );
	gpio_select_input( 3, 26 );
	gpio_set_irq_mode( 3, 26, GPIO_IRQ_LEVEL_LOW );
}


/*
 * Enable various external power domains on the SabreSmart:
 * - AUD_1V8, AUD_3V3
 *   CODEC_PWR_EN = KEY_COL2(ALT5) = GPIO[10] of instance: gpio4
 *     - Audio CODEC
 *
 * - SEN_1V8, SEN_3V3
 *   SENSOR_PWR_EN = EIM_EB3(ALT5) = GPIO[31] of instance: gpio2
 *     - Accelerometer
 *     - Magnetometer
 *     - Ambient Light Sensor
 *     - Barometer
 *
 * - AUX_5V
 *   AUX_5V_EN = NANDF_RB0(ALT5) = GPIO[10] of instance: gpio6
 *     - SATA
 *     - LVDS_0 (note: LVDS_1 is powered by PMIC_5V)
 *     - CAN 1
 *
 * - Pin 31 of J11
 *   DISP_PWR_EN = NANDF_CS1(ALT5) = GPIO[14] of instance: gpio6
 *
 * - Pin 79 of J504
 *   DISP0_PWR_EN = ENET_TXD0(ALT5) = GPIO[30] of instance: gpio1
 *
 * - Pin 1 of J503 (LVDS_0 output)
 *   CABC_EN0 = NANDF_CS2(ALT5) = GPIO[15] of instance: gpio6
 *
 * - Pin 1 of of J502 (LVDS_1 output)
 *   CABC_EN1 = NANDF_CS3(ALT5) = GPIO[16] of instance: gpio6.
 *
 * - GPS_3V15, GPS_1V5
 *   GPS_PWREN = EIM_DA0(ALT5) = GPIO[0] of instance: gpio3.
 *
 * =================================================================
 * Note: These items are initialised in [init_usb.c]
 *
 * - USB_OTG_VBUS
 *   USB_OTG_PWR_EN = EIM_D22(ALT5) = GPIO[22] of instance: gpio3
 *      - Power to iMX6 USB OTG controller
 *
 * - USB_H1_VBUS
 *   USB_H1_PWR_EN = ENET_TXD1(ALT5) = GPIO[29] of instance: gpio1
 *      - Power to iMX6 USB Host controller H1 for PCIe slot
 *
 */
void mx6q_init_external_power_rails(void)
{
	/* Note: The Sensors and Audio CODEC must be powered before we
	 * can communicate on I2C busses 1 and 3! */

	/* Enable power to Audio CODEC by driving a GPIO high.
	 * CODEC_PWR_EN = KEY_COL2(ALT5) = GPIO[10] of instance: gpio4. */
	pinmux_set_swmux(SWMUX_KEY_COL2, MUX_CTL_MUX_MODE_ALT5);
	gpio_drive_output( 4, 10, GPIO_HIGH );

	/* Enable power to all sensors by driving a GPIO high.
	 * SENSOR_PWR_EN = EIM_EB3(ALT5) = GPIO[31] of instance: gpio2. */
	pinmux_set_swmux(SWMUX_EIM_EB3, MUX_CTL_MUX_MODE_ALT5);
	gpio_drive_output( 2, 31, GPIO_HIGH );

	/* Enable Auxiliary 5V supply of J11 by driving a GPIO high.
	 * AUX_5V
	 * AUX_5V_EN = NANDF_RB0(ALT5) = GPIO[10] of instance: gpio6
	 *  - SATA
	 *  - LVDS_0 (note: LVDS_1 is powered by PMIC_5V)
	 *  - CAN 1 */
	pinmux_set_swmux(SWMUX_NANDF_RB0, MUX_CTL_MUX_MODE_ALT5);
	gpio_drive_output( 6, 10, GPIO_HIGH );

	/* Enable power to Pin 31 of J11 by driving a GPIO high.
	 * DISP_PWR_EN = NANDF_CS1(ALT5) = GPIO[14] of instance: gpio6 */
	pinmux_set_swmux(SWMUX_NANDF_CS1, MUX_CTL_MUX_MODE_ALT5);
	gpio_drive_output( 6, 14, GPIO_HIGH );

	/* Enable power to Pin 79 of J504 by driving a GPIO high.
	 * DISP0_PWR_EN = ENET_TXD0(ALT5) = GPIO[30] of instance: gpio1 */
	pinmux_set_swmux(SWMUX_ENET_TXD0, MUX_CTL_MUX_MODE_ALT5);
	gpio_drive_output( 1, 30, GPIO_HIGH );

	/* Enable power to Pin 1 of J503 (LVDS_0 output) by driving a GPIO high.
	 * CABC_EN0 = NANDF_CS2(ALT5) = GPIO[15] of instance: gpio6 */
	pinmux_set_swmux(SWMUX_NANDF_CS2, MUX_CTL_MUX_MODE_ALT5);
	gpio_drive_output( 6, 15, GPIO_HIGH );

	/* Enable power to Pin 1 of of J502 (LVDS_1 output) by driving a GPIO high.
	 * CABC_EN1 = NANDF_CS3(ALT5) = GPIO[16] of instance: gpio6. */
	pinmux_set_swmux(SWMUX_NANDF_CS3, MUX_CTL_MUX_MODE_ALT5);
	gpio_drive_output( 6, 16, GPIO_HIGH );

	/* Enable power to GPS by driving a GPIO high.
	 * GPS_3V15, GPS_1V5
	 * GPS_PWREN = EIM_DA0(ALT5) = GPIO[0] of instance: gpio3. */
	pinmux_set_swmux(SWMUX_EIM_DA0, MUX_CTL_MUX_MODE_ALT5);
	gpio_drive_output( 3, 0, GPIO_HIGH );

	/* Note: PCIe power is controlled by [mx6q_init_pcie] */
}



void mx6q_init_displays(void)
{
	mx6q_init_lcd_panel();
	mx6q_init_lvds();
}

__SRCVERSION("$URL$ $Rev$");

