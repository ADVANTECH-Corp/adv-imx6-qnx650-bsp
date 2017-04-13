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

/* Default clock values. */
#define MCLK_REF		 16500000 /* This value assumes that mx6q_init_audmux_pins() in startup is configured for 16.5MHz */
#define FVCO_MAXVAL		100000000 /* Maximum value for Fvco: 100Mhz */
#define FVCO_MINVAL		 90000000 /* Minimum value for Fvco: 90Mhz */
#define SYSCLK			 24576000 /* The value we want the PLL to produce for a given MCLK_REF. */

/* I2C 7-bit slave address */
#define WM8962_SLAVE_ADDR	(0x34 >> 1)	/* Device ID from data sheet. */

/*
 * WM8962 CODEC Registers
 */
#define SOFTWARE_RESET		0x0F
#define SW_RESET_PLL		0x7F

#define RINPUT_VOL			0x01 // R1
#define HPOUTL_VOL			0x02 // R2
#define HPOUTR_VOL			0x03 // R3
#define CLOCKING_1			0x04 // R4
#define AUDIO_INTERFACE_0 	0x07 // R7
#define CLOCKING_2			0x08 // R8
#define LDAC_VOLUME			0x0A // R10
#define RDAC_VOLUME			0x0B // R11
#define AUDIO_INTERFACE_2	0x0E // R14
#define ADCL_VOL			0x15 // R21
#define ADCR_VOL			0x16 // R22
#define PWR_MGMT_1			0x19 // R25
#define PWR_MGMT_2			0x1A // R26
#define ADDITIONAL_CTRL_3	0x1B // R27
#define CLOCKING_3			0x1E // R30
#define INPUT_MIXER_CTRL_1	0x1F // R31
#define RIGHT_INPUT_MIX_VOL	0x21 // R33
#define INPUT_MIXER_CTRL_2	0x22 // R34
#define LINPUT_PGA_CONTROL	0x25 // R37
#define RINPUT_PGA_CONTROL	0x26 // R38
#define WRITE_SEQ_CONTROL_1	0x57 // R87
#define WRITE_SEQ_CONTROL_2	0x5a // R90
#define WRITE_SEQ_CONTROL_3	0x5d // R93
#define HEADPHONE_MIXER_1	0x64 // R100
#define HEADPHONE_MIXER_2	0x65 // R101
#define HEADPHONE_MIXER_3	0x66 // R102
#define HEADPHONE_MIXER_4	0x67 // R103
#define ANALOGUE_CLOCKING_1 0x7C // R124
#define ANALOGUE_CLOCKING_2	0x7D // R125
#define ANALOGUE_CLOCKING_3	0x7E // R126
#define PLL_2				0x81 // R129
#define PLL_4				0x83 // R131
#define PLL_13				0x8C // R140
#define PLL_14				0x8D // R141
#define PLL_15				0x8E // R142
#define PLL_16				0x8F // R142
#define PLL_DLL				0x96 // R150
#define ANALOGUE_CLOCKING_4	0x98 // R152 (also called "ANALOGUE CONTROL 4" in datasheet.)
#define GPIO_2				0x201 // R513

/* Right Input Volume */
#define INPGAR_MUTE(x)		((x) << 7)
#define IN_VU				(0x1 << 8)
#define CHIP_REV_MASK		(0x7 << 9)
#define CUST_ID_MASK		(0xF << 11)

#define INPGAR_MUTE_MASK	(0x1 << 7)
#define IN_VU_MASK			(0x1 << 8)

/* HPOUTL VOL */
#define HPOUT_VU			(0x1 << 8) /* Update L & R simultaneously. */
#define HPOUT_ZC			(0x1 << 7) /* Zero Cross Function. */
#define HPOUT_VOL_10DB_ATT	(0x6F << 0) /* -10dB volume */

#define HPOUT_VU_MASK		(0x1 << 8)
#define HPOUT_ZC_MASK		(0x1 << 7)
#define HPOUTL_VOL_MASK		(0x7F << 0)

/* HPOUTR VOL */
#define HPOUTR_VOL_MASK		(0x7F << 0)

/* Audio Interface 0 */
#define FMT_I2S				(0x2 << 0)
#define MSTR				(0x1 << 6)
#define WL_16				(0x00)

#define WL_MASK				(0x3 << 2)
#define MSTR_MASK			(0x1 << 6)
#define FMT_MASK			(0x3 << 0)

/* ADC/DAC Control 1 */
#define DAC_MUTE_SET(x)		((x) << 3)

/* Clocking 2 */
#define CLKREG_OVD			(0x1 << 11)
#define MCLK_SRC_PLL		(0x2 << 9)
#define SYSCLK_ENA			(0x1 << 5)

#define SYSCLK_ENA_MASK		(0x1 << 5)
#define MCLK_SRC_MASK		(0x3 << 9)
#define CLKREG_OVD_MASK		(0x1 << 11)

/* Clocking 3 */
#define OPCLK_DIV_1			(0x00 << 10)
#define BCLK_DIV_8			(0x7 << 0)

#define OPCLK_DIV_MASK		(0x7 << 10)
#define BCLK_DIV_MASK		(0xF << 0)

/* Input Mixer Control 1 */
#define MIXINR_ENA			(0x1 << 0)
#define MIXINL_ENA			(0x1 << 1)
#define MIXINR_MUTE(x)		((x) << 2)

#define MIXINR_ENA_MASK		(0x1 << 0)
#define MIXINL_ENA_MASK		(0x1 << 1)
#define MIXINR_MUTE_MASK	(0x1 << 2)
#define MIXINL_MUTE_MASK	(0x1 << 3)

/* Right Input Mixer Volume */
#define IN3R_MIXINR_VOL_0DB	 	(0x5 << 0)
#define INPGAR_MIXINR_VOL_0DB	(0x0 << 3)

#define INPGAR_MIXINR_VOL_MASK	(0x7 << 3)
#define IN3R_MIXINR_VOL_MASK 	(0x7 << 0)


/* Input Mixer Control 2 */
#define IN3R_TO_MIXINR			(0x1 << 1)
#define INPGAR_TO_MIXINR		(0x1 << 0)

#define INPGAR_TO_MIXINR_MASK	(0x1 << 0)
#define IN3R_TO_MIXINR_MASK		(0x1 << 1)
#define IN2R_TO_MIXINR_MASK		(0x1 << 2)
#define INPGAL_TO_MIXINL_MASK	(0x1 << 3)
#define IN3L_TO_MIXINL_MASK		(0x1 << 4)
#define IN2L_TO_MIXINL_MASK		(0x1 << 5)

/* Input PGA Control */
#define INPGAL_ENA			(0x1 << 4)
#define INPGAR_ENA			(0x1 << 4)
#define IN3R_TO_INPGAR		(0x1 << 1)

#define INPGAR_ENA_MASK		(0x1 << 4)
#define IN1R_TO_INPGAR_MASK	(0x1 << 3)
#define IN2R_TO_INPGAR_MASK	(0x1 << 2)
#define IN3R_TO_INPGAR_MASK	(0x1 << 1)

/* DAC Volume */
#define DAC_VOLUME_0DB		(0xC0)
#define DAC_VU				(0x1 << 8) /* Updates left and right simultaneously. */

#define DAC_VU_MASK			(0x1 << 8)
#define DAC_VOLUME_MASK		(0xFF << 0)

/* Audio Interface 2 */
#define AIF_RATE_32			(0x20)

#define AIF_RATE_MASK		(0x7FF << 0)

/* ADC Volume */
#define ADC_VOL_0DB			(0xC0) /* 0dB volume on left ADC. */
#define ADC_VU				(0x1 << 8)

#define ADC_VOL_MASK		(0xFF << 0)
#define ADC_VU_MASK			(0x1 << 8)

/* PWR_MGMT_1 */
#define MICBIAS_ENA			(0x1 << 1)
#define MICBIAS_SET(x)		((x) << 1)
#define ADCR_ENA			(0x1 << 2)
#define ADCL_ENA			(0x1 << 3)
#define INR_ENA				(0x1 << 4)
#define BIAS_ENA			(0x1 << 6)
#define VMID_SEL_50K_DIV	(0x1 << 7)
#define OPCLK_ENA			(0x1 << 9)
#define DMIC_ENA			(0x1 << 10)

#define MICBIAS_ENA_MASK	(0x1 << 1)
#define ADCR_ENA_MASK		(0x1 << 2)
#define ADCL_ENA_MASK		(0x1 << 3)
#define INR_ENA_MASK		(0x1 << 4)
#define BIAS_ENA_MASK		(0x1 << 6)
#define VMID_SEL_MASK		(0x3 << 7)
#define OPCLK_ENA_MASK		(0x1 << 9)
#define DMIC_ENA_MASK		(0x1 << 10)

/* PWR_MGMT_2 */
#define DACL_ENA			(0x1 << 8)
#define DACR_ENA			(0x1 << 7)
#define HPOUTL_PGA_ENA		(0x1 << 6)
#define HPOUTR_PGA_ENA		(0x1 << 5)
#define HPOUTL_MUTE(x)		((x) << 1)
#define HPOUTR_MUTE(x)		((x) << 0)

#define DACL_ENA_MASK			(0x1 << 8)
#define DACR_ENA_MASK			(0x1 << 7)
#define HPOUTL_PGA_ENA_MASK		(0x1 << 6)
#define HPOUTR_PGA_ENA_MASK		(0x1 << 5)
#define HPOUTL_PGA_MUTE_MASK	(0x1 << 1)
#define HPOUTR_PGA_MUTE_MASK	(0x1 << 0)

/* Additional Control 3 */
#define SAMPLE_INT_MODE		(0x1 << 4)
#define SAMPLE_FRAC_MODE	(0x0 << 4)
#define SAMPLE_RATE_32KHZ	(0x1) // Mode 1 (INT)
#define SAMPLE_RATE_44KHZ	(0x0) // Mode 0 (FRACTIONAL) (44.1Khz)
#define SAMPLE_RATE_48KHZ	(0x0) // Mode 1 (INT)
#define SAMPLE_RATE_96KHZ	(0x6) // Mode 1 (INT)

#define SAMPLE_MODE_MASK	(0x1 << 4)
#define SAMPLE_RATE_MASK	(0x7 << 0)

/* Write Sequencer Control */
#define WSEQ_ENA			(0x1 << 5)
#define WSEQ_ABORT			(0x1 << 8)
#define WSEQ_HP_POWER_UP	(0x80)
#define WSEQ_INPUT_POWER_UP	(0x92)
#define WSEQ_POWER_DOWN		(0x9B)

#define WSEQ_BUSY_MASK		(0x1 << 0)
#define WSEQ_ENA_MASK		(0x1 << 5)

/* Headphone Mixer */
#define HPMIXL_TO_HPOUTL_PGA_MASK	(0x1 << 7)
#define HPMIXR_TO_HPOUTR_PGA_MASK	(0x1 << 7)

#define HPMIXL_MUTE_MASK	(0x1 << 8)
#define HPMIXR_MUTE_MASK	(0x1 << 8)

/* Analogue Clocking */
#define CLKOUT2_SEL_GPIO2	(0x1 << 5)
#define CLKOUT2_OE			(0x1 << 3)

#define CLKOUT2_SEL_MASK	(0x3 << 5)
#define CLKOUT2_OE_MASK		(0x1 << 3)

#define PLL_SYSCLK_DIV_1	(0x00)
#define PLL_SYSCLK_DIV_2	(0x1 << 3)
#define PLL_SYSCLK_DIV_4	(0x2 << 3)

#define PLL3_OUTDIV(x)		(((x) >> 2) << 6) /* x Should only ever be 2 or 4. */

#define PLL3_OUTDIV_MASK	(0x1 << 6)
#define PLL_SYSCLK_DIV_MASK	(0x3 << 3)

/* PLL Registers */
#define PLL3_ENA			(0x1 << 4)

#define PLL_CLK_SRC_MCLK	(0x1 << 1)
#define FLL_TO_PLL3_MCLK	(0x0 << 0)

#define PLL3_FRAC			(0x1 << 6)

#define OSC_MCLK_SRC_MCLK	(0x00)
#define OSC_MCLK_SRC_OSC	(0x01 << 4 )

#define SEQ_ENA_MASK		(0x1 << 1)

#define OSC_ENA_MASK		(0x1 << 7)
#define PLL2_ENA_MASK		(0x1 << 5)
#define PLL3_ENA_MASK		(0x1 << 4)

#define PLL_CLK_SRC_MASK	(0x1 << 1)
#define FLL_TO_PLL3_MASK	(0x1 << 0)

#define PLL3_FRAC_MASK		(0x1 << 6)
#define PLL3_N_MASK			(0x1F << 0)

#define PLL3_K_23_16_MASK	(0xFF << 0)
#define PLL3_K_15_8_MASK	(0xFF << 0)
#define PLL3_K_7_0_MASK		(0xFF << 0)

#define OSC_MCLK_SRC_MASK	(0x1 << 4)

/* GPIO */
#define GPIO2_FN_OPCLK		(0x12 << 0)

#define GPIO2_FN_MASK		(0x1F << 0)

/* Analogue Clocking */
#define CLKOUT5_OE_MASK		(0x1 << 0)

/* Input mux selection */
#define INPUT_MUX_MIC_IN  0

#define MAX_WAIT_TIME 500 	/* Total sleep time is (WAIT_DELAY * MAX_WAIT_TIME) */
#define WAIT_DELAY 25 		/* Time to wait in miliseconds. */

struct wm8962_context;

#define MIXER_CONTEXT_T struct wm8962_context

typedef struct wm8962_context
{
	ado_mixer_delement_t *mic;
	ado_mixer_delement_t *line;

	char                 i2c_num;	/* I2C bus number */
	int                  i2c_fd;	/* I2C device handle */
	int                  mclk;   	/* external SYS_MCLK */
	int                  adr0cs;	/* wm8962 slave address select pin */
	int                  samplerate;
	int                  input_mux;
}
wm8962_context_t;

#endif /* __MIXER_H */
