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

#include <audio_driver.h>
#include <string.h>

#include <hw/i2c.h>
#include <stdint.h>
#include "wm8962.h"

#define LOCAL static

/* One playback device, one capture device. */
static int32_t pcm_devices[2] = {
	0, 1
};

static snd_mixer_voice_t stereo_voices[2] = {
	{SND_MIXER_VOICE_LEFT, 0},
	{SND_MIXER_VOICE_RIGHT, 0}
};

/* mindB and maxdB values are all x100 */
static struct snd_mixer_element_volume1_range hp_range[2] = {
	{0x35, 0x7F, -6800, 600},
	{0x35, 0x7F, -6800, 600},
};

/* ADC RAnge */
LOCAL struct snd_mixer_element_volume1_range adc_range[2] = {
	{0, 0xFF, -7100, 2300},
	{0, 0xFF, -7100, 2300},
};



/*********************************************** 
 * read/write interface to wm8962 codec *
 ***********************************************/
/* read codec */
LOCAL uint32_t
codec_read (MIXER_CONTEXT_T * mixer, uint32_t reg)
{ 
    struct {
        i2c_send_t      hdr;
        uint16_t        reg;
    } msgreg;
    struct {
        i2c_recv_t      hdr;
        uint16_t        val;
    } msgval;
    int rbytes;

    msgreg.hdr.slave.addr = WM8962_SLAVE_ADDR;
    msgreg.hdr.slave.fmt  = I2C_ADDRFMT_7BIT;
    msgreg.hdr.len        = 2;
    msgreg.hdr.stop       = 1;
    msgreg.reg            = ((reg & 0xff) << 8) | ((reg & 0xff00) >> 8);

    int ret = devctl(mixer->i2c_fd, DCMD_I2C_SEND, &msgreg,
            sizeof(msgreg), NULL);

    if (ret) {
        ado_error ("WM8962: I2C_READ ADDR failed: %d", ret);
	return (-1);
    }

    msgval.hdr.slave.addr = WM8962_SLAVE_ADDR;
    msgval.hdr.slave.fmt  = I2C_ADDRFMT_7BIT;
    msgval.hdr.len        = 2;
    msgval.hdr.stop       = 1;

    if (devctl(mixer->i2c_fd, DCMD_I2C_RECV, &msgval, 
                        sizeof(msgval), &rbytes)) {
        ado_error ("WM8962: I2C_READ DATA failed.");
	return (-1);
    }

    msgval.val = ((msgval.val & 0xff) << 8) | ((msgval.val & 0xff00) >> 8);

    return (msgval.val);
}

/* write codec */
LOCAL int
codec_write(MIXER_CONTEXT_T * mixer, uint32_t reg, uint32_t val)
{
    struct {
        i2c_send_t      hdr;
        uint16_t        reg;
        uint16_t        val;
    } msg;

    msg.hdr.slave.addr = WM8962_SLAVE_ADDR;
    msg.hdr.slave.fmt  = I2C_ADDRFMT_7BIT;
    msg.hdr.len        = 4;
    msg.hdr.stop       = 1;
    msg.reg            = ((reg & 0xff) << 8) | ((reg & 0xff00) >> 8);
    msg.val            = ((val & 0xff) << 8) | ((val & 0xff00) >> 8);

    if (devctl (mixer->i2c_fd, DCMD_I2C_SEND, &msg, sizeof(msg), NULL))
    {
        ado_error ("WM8962: I2C_WRITE failed.");
    }

    return (EOK);
}

/*
 * Provides a non-destructive way of modifying register contents.
 */
LOCAL int codec_update(MIXER_CONTEXT_T * wm8962, uint32_t reg, uint32_t mask, uint32_t val)
{
	uint32_t current_value = codec_read(wm8962, reg);
	current_value &= ~mask;

	return codec_write(wm8962, reg, current_value | val);
}


/************************************* 
 *  wm8962 codec internal control  *
 *************************************/
/* Headphone mute control. */
LOCAL int32_t
wm8962_hp_mute_control (MIXER_CONTEXT_T * wm8962,
	ado_mixer_delement_t * element, uint8_t set, uint32_t * vol, 
	void *instance_data)
{
	int32_t altered = 0;
	uint16_t pwr_mgmt_2;

    pwr_mgmt_2 = codec_read (wm8962, PWR_MGMT_2);

	if (set)
	{
		/* Only check right headphone mute, l & r should be in sync. */
		altered = ((vol[0] & 1) != (pwr_mgmt_2 & 1));

		if (altered)
		{
			codec_update(wm8962, PWR_MGMT_2, (HPOUTL_PGA_MUTE_MASK | HPOUTR_PGA_MUTE_MASK),
					HPOUTL_MUTE(vol[0] &1) | HPOUTR_MUTE(vol[0] &1));
			codec_update(wm8962, HPOUTL_VOL, HPOUT_VU_MASK, HPOUT_VU);
		}
	}
	else
		vol[0] = (pwr_mgmt_2) & 1;

	return (altered);
}

LOCAL int32_t
wm8962_hp_vol_control (MIXER_CONTEXT_T * wm8962,
	ado_mixer_delement_t * element, uint8_t set, uint32_t * vol, 
	void *instance_data)
{
	int32_t altered = 0;
	uint16_t hpoutl_vol, hpoutr_vol;

    hpoutl_vol = codec_read (wm8962, HPOUTL_VOL);
	hpoutr_vol = codec_read (wm8962, HPOUTR_VOL);

	if (set)
	{
		altered = ((vol[0] != ((hpoutl_vol)& 0x7F))
				|| (vol[1] !=  ((hpoutr_vol) & 0x7F)));

		if (altered)
		{
			codec_update(wm8962, HPOUTL_VOL, HPOUTL_VOL_MASK, (vol[0] & 0x7F));
			codec_update(wm8962, HPOUTR_VOL, HPOUTR_VOL_MASK, (vol[1] & 0x7F));
			codec_update(wm8962, HPOUTL_VOL, HPOUT_VU_MASK, HPOUT_VU);
		}
	}
	else
	{
		vol[0] = (hpoutl_vol & 0x7F);
		vol[1] = (hpoutr_vol & 0x7F);
	}

	return (altered);
}

/*
 * ADC mute control - used for MIC_IN.
 */
LOCAL int32_t
wm8962_adc_mute_control (MIXER_CONTEXT_T * wm8962,
	ado_mixer_delement_t * element, uint8_t set, uint32_t * vol, 
	void *instance_data)
{
	int32_t altered = 0;
	uint16_t inpgar_mute;

	inpgar_mute = codec_read (wm8962, RINPUT_VOL);

	if (set)
	{
		/* Only check right input, left is not wired up. */
		altered = ((vol[0] & 1) != ((inpgar_mute >> 7) & 1));

		if (altered)
		{
			/* Disable MICBIAS to stop noise leaking through amp even when muted. */
			codec_update(wm8962, RINPUT_VOL, (INPGAR_MUTE_MASK | IN_VU_MASK), (INPGAR_MUTE(vol[0] & 1) | IN_VU));
			codec_update(wm8962, PWR_MGMT_1, MICBIAS_ENA_MASK, MICBIAS_SET((vol[0] & 1) ^ 1));
		}
	}
	else
		vol[0] = ((inpgar_mute >> 7)) & 1;

	return (altered);
}

/*
 * Set ADV volume, used for MIC_IN.
 */
LOCAL int32_t
wm8962_adc_vol_control (MIXER_CONTEXT_T * wm8962,
	ado_mixer_delement_t * element, uint8_t set, uint32_t * vol,
	void *instance_data)
{
	int32_t altered = 0;
	uint16_t adcl_vol, adcr_vol;

    adcl_vol = codec_read (wm8962, ADCL_VOL);
    adcr_vol = codec_read (wm8962, ADCR_VOL);

	if (set)
	{
		altered = ((vol[0] != (adcl_vol & 0xFF)) || (vol[0] != (adcr_vol & 0xFF)));

		if (altered)
		{
			codec_update(wm8962, ADCL_VOL, ADC_VOL_MASK, (vol[0] & 0xFF));
			codec_update(wm8962, ADCR_VOL, ADC_VOL_MASK, (vol[0] & 0xFF));
			codec_update(wm8962, ADCR_VOL, ADC_VU_MASK, ADC_VU);
		}
	}
	else
	{
		vol[0] = adcr_vol & 0xFF;
	}

	return (altered);
}

/* Input Mux */
LOCAL int32_t
wm8962_input_mux_control (MIXER_CONTEXT_T * wm8962,
	ado_mixer_delement_t * element, uint8_t set, ado_mixer_delement_t ** inelements, 
	void *instance_data)
{
	/* This could be used to switch between using the digital Mic and analogue MIC_IN.
	 * However, the digital Mic is currently not supported for the sabresmart board.
	 */
	return (EOK);
}

/* Determine if the mic-bias is enabled. */
LOCAL int32_t
wm8962_micbias_get (MIXER_CONTEXT_T * wm8962,
	ado_dswitch_t * dswitch, snd_switch_t * cswitch,
	void *instance_data)
{
	uint32_t data;

	data = (codec_read (wm8962, PWR_MGMT_1) >> 1) & 1;
	cswitch->type = SND_SW_TYPE_BOOLEAN;
	cswitch->value.enable = data ? 1 : 0;

	return (0);
}

/* Set the micbias to a new value if that has been requested. */
LOCAL int32_t
wm8962_micbias_set (MIXER_CONTEXT_T * wm8962,
	ado_dswitch_t * dswitch, snd_switch_t * cswitch,
	void *instance_data)
{
	uint32_t data, bias;
	int32_t altered = 0;

	data = codec_read (wm8962, PWR_MGMT_1);
	bias = (data >> 1) & 1;
	altered = (cswitch->value.enable != (bias ? 1 : 0));

	if (altered)
	{
		codec_update(wm8962, PWR_MGMT_1, MICBIAS_ENA_MASK, bias);
		cswitch->value.enable = bias ? 1 : 0;
	}

	return (altered);
}

/* Soft reset wm8962 codec */
LOCAL int
wm8962_reset (MIXER_CONTEXT_T * wm8962)
{
    /* Data written to reset registers isn't really important. */
    codec_write(wm8962, SOFTWARE_RESET, 0x6243);
    codec_write(wm8962, SW_RESET_PLL, 0x00);

	return (0);
}

/* Wait for a write sequence to finish. */
int wm8962_waitfor(MIXER_CONTEXT_T * wm8962, uint32_t reg, uint32_t mask)
{
	int i;
	int val;

	for(i = 0; i < MAX_WAIT_TIME; i++)
	{
		val = codec_read(wm8962, reg);

		/* If the bits specified in the mask are no longer high. */
		if(!(val & mask))
		{
			ado_debug(DB_LVL_MIXER, "WM8962: waited %dms for write sequence.\n", i*WAIT_DELAY);
			return EOK;
		}
		delay(WAIT_DELAY);
	}
	return -1;
}

/* 
 * Configure the WM8962 internall PLL.
 */
LOCAL int
wm8962_setpll (MIXER_CONTEXT_T * wm8962)
{
    uint32_t fref = wm8962->mclk;
    uint32_t fout = SYSCLK;
    uint32_t pll_outdiv = 4;
    uint32_t pll_n, pll_k_int;
    float nk, fvco, pll_k;

   	/* Calculate N.K */
   	fvco = fout * pll_outdiv;

   	/* Ensure Fvco is in the correct range. */
   	if((fvco < FVCO_MINVAL) || (fvco > FVCO_MAXVAL))
   	{
   		pll_outdiv = 2;
   		fvco = fout * pll_outdiv;

   		if((fvco < FVCO_MINVAL) || (fvco > FVCO_MAXVAL))
   		{
   			ado_error ("WM8962: Fvco not in 90-100MHz range! Adjust PLL_OUTDIV or SYSCLK.");
   			return EINVAL;
   		}
   	}

    nk = (fvco * 2) / fref;
    pll_n = (int) nk;
    pll_k = nk - pll_n;
    pll_k_int = pll_k * 16777216; /* Magic number from datasheet, convert K into integer format. */

    /* As we're using MCLK as the fref, we need to disable SEQ_ENA. */
    codec_update(wm8962, PLL_DLL, SEQ_ENA_MASK, 0x00);

   	/* Configure integer portion. */
   	codec_update(wm8962, PLL_13, (PLL3_FRAC_MASK | PLL3_N_MASK), (PLL3_FRAC | (pll_n << 0)));

   	/* Configure fractional portion. */
   	codec_update(wm8962, PLL_14, PLL3_K_23_16_MASK, ((pll_k_int >> 16) & 0xFF));
   	codec_update(wm8962, PLL_15, PLL3_K_15_8_MASK, ((pll_k_int >> 8) & 0xFF));
   	codec_update(wm8962, PLL_16, PLL3_K_7_0_MASK, ((pll_k_int) & 0xFF));

   	/* Route MCLK to PLL3 */
   	codec_update(wm8962, PLL_4, (PLL_CLK_SRC_MASK | FLL_TO_PLL3_MASK), (PLL_CLK_SRC_MCLK | FLL_TO_PLL3_MCLK));
   	codec_update(wm8962, ANALOGUE_CLOCKING_4, (OSC_MCLK_SRC_MASK), OSC_MCLK_SRC_MCLK);

   	/* Configure appropriate divisors. */
   	codec_update(wm8962, ANALOGUE_CLOCKING_2, (PLL3_OUTDIV_MASK | PLL_SYSCLK_DIV_MASK), (PLL3_OUTDIV(pll_outdiv) | PLL_SYSCLK_DIV_2));

   	/* Enable the PLL now that we've configured everything. */
   	codec_update(wm8962, PLL_2, (PLL3_ENA_MASK), PLL3_ENA);

	return (EOK);
}

/*
 * Power on parts of the codec required for normal operation.
 */
LOCAL int wm8962_init_subsystems(MIXER_CONTEXT_T * wm8962)
{
	/* Power on subsystems. DMIC_ENA must not be on when using MIC_IN as input. */
	codec_update(wm8962, PWR_MGMT_1, (DMIC_ENA_MASK | VMID_SEL_MASK | BIAS_ENA_MASK | INR_ENA_MASK | ADCL_ENA_MASK | ADCR_ENA_MASK | MICBIAS_ENA_MASK),
				(VMID_SEL_50K_DIV | BIAS_ENA | INR_ENA | ADCL_ENA | ADCR_ENA | MICBIAS_ENA));

	/* Configure GPIO for testing on TP11. */
	codec_update(wm8962, PWR_MGMT_1, OPCLK_ENA_MASK, 0x00);
	codec_update(wm8962, GPIO_2, GPIO2_FN_MASK, GPIO2_FN_OPCLK);
	codec_update(wm8962, PWR_MGMT_1, OPCLK_ENA_MASK, OPCLK_ENA);

	/* Enable and Configure Output Paths. */
	codec_update(wm8962, WRITE_SEQ_CONTROL_1, WSEQ_ENA_MASK, WSEQ_ENA);
	codec_write(wm8962, WRITE_SEQ_CONTROL_2, WSEQ_HP_POWER_UP);
	wm8962_waitfor(wm8962, WRITE_SEQ_CONTROL_3, WSEQ_BUSY_MASK );

	codec_update(wm8962, HEADPHONE_MIXER_3, HPMIXL_MUTE_MASK, 0x00 );
	codec_update(wm8962, HEADPHONE_MIXER_4, HPMIXR_MUTE_MASK, 0x00 );

	codec_update(wm8962, HEADPHONE_MIXER_1, HPMIXL_TO_HPOUTL_PGA_MASK, 0x00);
	codec_update(wm8962, HEADPHONE_MIXER_2, HPMIXR_TO_HPOUTR_PGA_MASK, 0x00);

	codec_update(wm8962, LDAC_VOLUME, (DAC_VOLUME_MASK | DAC_VU_MASK), (DAC_VOLUME_0DB | DAC_VU));
	codec_update(wm8962, RDAC_VOLUME, (DAC_VOLUME_MASK | DAC_VU_MASK), (DAC_VOLUME_0DB | DAC_VU));
	codec_update(wm8962, HPOUTL_VOL, (HPOUT_VU_MASK | HPOUTL_VOL_MASK), (HPOUT_VU | HPOUT_VOL_10DB_ATT));
	codec_update(wm8962, HPOUTR_VOL, (HPOUT_VU_MASK | HPOUTR_VOL_MASK), (HPOUT_VU | HPOUT_VOL_10DB_ATT));
	codec_update (wm8962, PWR_MGMT_2, (HPOUTR_PGA_MUTE_MASK | HPOUTL_PGA_MUTE_MASK), 0x00);

	/* Enable Input Path */
	codec_update(wm8962, RINPUT_PGA_CONTROL, (INPGAR_ENA_MASK | IN3R_TO_INPGAR_MASK | IN1R_TO_INPGAR_MASK | IN2R_TO_INPGAR_MASK), (INPGAR_ENA | IN3R_TO_INPGAR));
	codec_update(wm8962, INPUT_MIXER_CTRL_1, (MIXINR_ENA_MASK | MIXINL_ENA_MASK | MIXINR_MUTE_MASK | MIXINL_MUTE_MASK), (MIXINR_ENA | MIXINL_ENA));
	codec_update(wm8962, INPUT_MIXER_CTRL_2, (INPGAR_TO_MIXINR_MASK | IN3R_TO_MIXINR_MASK | IN2R_TO_MIXINR_MASK | INPGAL_TO_MIXINL_MASK | IN3L_TO_MIXINL_MASK | IN2L_TO_MIXINL_MASK),
											INPGAR_TO_MIXINR);
	codec_update(wm8962, RIGHT_INPUT_MIX_VOL, INPGAR_MIXINR_VOL_MASK , INPGAR_MIXINR_VOL_0DB);
	codec_update(wm8962, RINPUT_VOL, (INPGAR_MUTE_MASK | IN_VU_MASK), IN_VU);

	codec_update(wm8962, ADCL_VOL, ADC_VOL_MASK, ADC_VOL_0DB);
	codec_update(wm8962, ADCR_VOL, ADC_VOL_MASK, ADC_VOL_0DB);
	codec_update(wm8962, ADCL_VOL, ADC_VU_MASK, ADC_VU);

	/* Configure WM8962 as I2S master. */
	codec_update(wm8962, AUDIO_INTERFACE_0, MSTR_MASK | FMT_MASK | WL_MASK, MSTR | FMT_I2S | WL_16);

	return EOK;
}

LOCAL int wm8962_init_clocks(MIXER_CONTEXT_T * wm8962, int sample_rate, int sample_mode)
{
	/* Turn off sysclk so we can safely write to registers. */
	codec_update(wm8962, CLOCKING_2, (SYSCLK_ENA_MASK), (0x00));

	/* Ensure we can control all registers. */
	codec_update(wm8962, CLOCKING_2, (CLKREG_OVD_MASK), (CLKREG_OVD));

	/* Turn off the OSC_ENA bit set by CLKREG_OVD, and disable the PLLs. */
	codec_update(wm8962, PLL_2, (OSC_ENA_MASK | PLL2_ENA_MASK | PLL3_ENA_MASK), 0x00);

	/* Set up path to allow observation of SYSCLK at TP11. */
	codec_update(wm8962, ANALOGUE_CLOCKING_1, (CLKOUT2_SEL_MASK), (CLKOUT2_SEL_GPIO2));
	codec_update(wm8962, ANALOGUE_CLOCKING_3, (CLKOUT5_OE_MASK | CLKOUT2_OE_MASK), (CLKOUT2_OE));

	/* Configure, then enable the PLL */
	wm8962_setpll(wm8962);

	/* Indicate which input clk to derive sysclk from. */
	codec_update(wm8962, CLOCKING_2, (MCLK_SRC_MASK), MCLK_SRC_PLL);

	/* Configure GPIO clock speed. */
	codec_update(wm8962, CLOCKING_3, (OPCLK_DIV_MASK), (OPCLK_DIV_1));

	/* Configure sample rate */
	codec_update(wm8962, ADDITIONAL_CTRL_3, (SAMPLE_MODE_MASK | SAMPLE_RATE_MASK), (sample_mode | sample_rate));

	/* Set LRCLK rate for 16 bit audio. */
	codec_update(wm8962, AUDIO_INTERFACE_2, AIF_RATE_MASK, AIF_RATE_32);

	/* Turn on sysclk so as we're done configuring. */
	codec_update(wm8962, CLOCKING_2, (SYSCLK_ENA_MASK | BCLK_DIV_MASK), (SYSCLK_ENA | BCLK_DIV_8));

	return EOK;
}


LOCAL int
wm8962_destroy (MIXER_CONTEXT_T * wm8962)
{
	/* Power down the codec if we're no longer using it. */
    codec_write(wm8962, WRITE_SEQ_CONTROL_2, WSEQ_POWER_DOWN);

	/* disconnect connect between SoC and codec */
	if (wm8962->i2c_fd != -1)
		close (wm8962->i2c_fd);
	
	ado_debug (DB_LVL_MIXER, "Destroying WM8962 mixer");
	ado_free (wm8962);
	
	return (0);
}

LOCAL int32_t
build_wm8962_mixer (MIXER_CONTEXT_T * wm8962, ado_mixer_t * mixer)
{
	/* Output elements */
	ado_mixer_delement_t *hp_mute = NULL, *hp_vol = NULL, *hp_io = NULL;
	ado_mixer_dgroup_t *hp_grp = NULL;

	/* Input elements */
	ado_mixer_delement_t *adc_vol = NULL, *adc_mute = NULL, *adc_pcm = NULL;
	ado_mixer_delement_t *input_mux = NULL;
	ado_mixer_dgroup_t *adc_grp = NULL;

	/* ################# */
	/*  PLAYBACK GROUPS  */
	/* ################# */
	/* WM8962: Headphone Output */
	if ((hp_mute = ado_mixer_element_sw2 (mixer, "HP Mute",
				(void *)wm8962_hp_mute_control, NULL, NULL)) == NULL)
		return (-1);

	if ((hp_vol = ado_mixer_element_volume1 (mixer, "HP Volume",
				2, hp_range, (void *)wm8962_hp_vol_control, NULL, NULL)) == NULL)
		return (-1);

	if (ado_mixer_element_route_add (mixer, hp_mute, hp_vol) != 0)
		return (-1);

	if ((hp_io = ado_mixer_element_io (mixer, "HP OUT", SND_MIXER_ETYPE_OUTPUT, 
				0, 2, stereo_voices)) == NULL)
        	return (-1);

	if (ado_mixer_element_route_add (mixer, hp_vol, hp_io) != 0)
		return (-1);
	
	if ((hp_grp = ado_mixer_playback_group_create (mixer, SND_MIXER_PCM_OUT,
				SND_MIXER_CHN_MASK_STEREO, hp_vol, hp_mute)) == NULL)
		return (-1);

	/* ################ */
	/*  CAPTURE GROUPS  */
	/* ################ */
	/* WM8962: ADC Gain Control */
	if ((adc_vol = ado_mixer_element_volume1 (mixer,
				"ADC Volume", 2, adc_range,
				(void *)wm8962_adc_vol_control, NULL, NULL)) == NULL)
		return (-1);
	
	if ((adc_mute = ado_mixer_element_sw1 (mixer, "ADC Mute", 1,
				(void *)wm8962_adc_mute_control, NULL, NULL)) == NULL)
		return (-1);
	
	if (ado_mixer_element_route_add (mixer, adc_vol, adc_mute) != 0)
		return (-1);

	if ((adc_pcm = ado_mixer_element_pcm1 (mixer, "ADC PCM",
				SND_MIXER_ETYPE_CAPTURE1, 1, &pcm_devices[0])) == NULL)
		return (-1);
	
	if (ado_mixer_element_route_add (mixer, adc_mute, adc_pcm) != 0)
		return (-1);	
	
	if ((adc_grp = ado_mixer_capture_group_create (mixer, SND_MIXER_GRP_IGAIN, 
				SND_MIXER_CHN_MASK_STEREO, adc_vol, adc_mute, NULL, NULL)) == NULL)
		return (-1);

	/* Input Multiplexer */
	if ((input_mux = ado_mixer_element_mux1 (mixer,
				SND_MIXER_ELEMENT_INPUT_MUX, 0, 1,
				(void *)wm8962_input_mux_control, NULL, NULL)) == NULL)
		return (-1);

	/* ####################### */
	/* SWITCHES                */
	/* ####################### */
	if (ado_mixer_switch_new (mixer, "MIC BIAS", SND_SW_TYPE_BYTE,
			0 , (void *)wm8962_micbias_get, (void *)wm8962_micbias_set, NULL, NULL) == NULL)
		return (-1);

	return (0);		
}

static char *wm8962_opts[] = {
#define I2CDEV       0
	"i2cdev",
#define ADR0CS       1
	"adr0cs",
#define MCLK         2
	"mclk",
#define SAMPLERATE   3
	"samplerate", 
#define INPUTMUX     4
	"input_mux",
#define INFO         5
	"info",		/* info must be last in the option list */
	NULL
};

static char *opt_help[] = {
	"=[#]                 : i2c device number, default 0 -> /dev/i2c0",
	"=[#]                 : adr0cs pin selection, 0->i2c addr 0xa, 1->i2c addr 0x2a, default 0",
	"=[#]                 : external SYS_MCLK, default 12288000",
	"=[#]                 : samplerate of codec, default 48000",
	"=[mic_in]            : input mux select, default mic_in",
	"                     : display the mixer options"
};

/* Parse mixer options */
LOCAL int
wm8962_parse_options (MIXER_CONTEXT_T * wm8962, char * args)
{
	char * value = NULL;
	int opt;
	int idx;
	char *p;

	wm8962->i2c_num = 1;
	wm8962->adr0cs = 0;
	wm8962->mclk = MCLK_REF;
	wm8962->samplerate = 48000;
	wm8962->input_mux = INPUT_MUX_MIC_IN;

	while ((p = strchr(args, ':')) != NULL)
		*p = ',';

	while (*args != '\0')
	{
		opt = getsubopt (&args, wm8962_opts, &value);
		switch (opt) {
			case I2CDEV:
				if (value != NULL) 
					wm8962->i2c_num = strtoul (value, NULL, 0);
				break;
			case ADR0CS:
				if (value != NULL) 
					wm8962->adr0cs = strtoul (value, NULL, 0);
				break;
			case MCLK:
				if (value != NULL) 
					wm8962->mclk = strtoul (value, NULL, 0);
				break;
			case SAMPLERATE:
				if (value == NULL) 
					ado_error ("WM8962: Must pass a value to samplerate");
				else
					wm8962->samplerate = strtoul (value, NULL, 0);
				break;
			case INPUTMUX:
				if (value != NULL)
				{
					if (!strcmp(value, "mic_in"))
					{
						wm8962->input_mux = INPUT_MUX_MIC_IN;
					}
					else
					{
						ado_error("WM8962: Driver only currently supports MIC_IN as input.");
					}
				}
				break;
			case INFO:
				idx = 0;
				printf ("WM8962 mixer options\n");

				/* display all the mixer options before "info" */
				while (idx <= opt)
				{
					printf("%10s%s\n", wm8962_opts[idx], opt_help[idx]);
					idx++;
				}
				printf("\nExample: io-audio -d [driver] mixer=i2cdev=1:adr0cs=0:mclk=1650000\n");
				break;
			default:
				ado_error ("WM8962: Unrecognized option \"%s\"", value);
				break;
		}
	}

	return (EOK);
}

/* Called by audio i2s controller */
int 
wm8962_mixer (ado_card_t * card, ado_mixer_t ** mixer, char * args, ado_pcm_t * pcm1)
{
	wm8962_context_t *wm8962;
	int32_t status;
	int rate_set;
	int sample_mode = SAMPLE_INT_MODE;
	char i2c_dev[_POSIX_PATH_MAX];

	if ((wm8962 = (wm8962_context_t *)
		ado_calloc (1, sizeof (wm8962_context_t))) == NULL)
	{
		ado_error ("WM8962: no memory (%s)", strerror (errno));
		return (-1);
	}

	if ((status = ado_mixer_create (card, "wm8962", mixer, (void *)wm8962)) != EOK)
	{
		ado_error ("WM8962: Failed to create mixer", strerror (errno));
		ado_free (wm8962);
		return (status);
	}

	if (wm8962_parse_options (wm8962, args)!=EOK)
	{
		ado_error ("WM8962: Failed to parse mixer options");
		ado_free (wm8962);
		return (-1);
	}

	/* check samplerate option */
	switch (wm8962->samplerate) {
		case 32000: 
			rate_set = SAMPLE_RATE_32KHZ;
			break;
		case 44100: 
			rate_set = SAMPLE_RATE_44KHZ;
			sample_mode = SAMPLE_FRAC_MODE;
			break;
		case 48000: 
			rate_set = SAMPLE_RATE_48KHZ;
			break;
		case 96000: 
			rate_set = SAMPLE_RATE_96KHZ;
			break;
		default:
			slogf (_SLOG_SETCODE (_SLOGC_AUDIO, 0), _SLOG_ERROR, 
				"WM8962: Unsupported audio sample rate. This codec supports 32000, 44100, 48000, 96000 sample rates. Using the default (48000).");
			rate_set = SAMPLE_RATE_48KHZ;
			wm8962->samplerate = SAMPLE_RATE_48KHZ;
	}

	/* establish connection between SoC and wm8962 via I2C resource manager */
	sprintf(i2c_dev, "/dev/i2c%d", wm8962->i2c_num);
	wm8962->i2c_fd = open (i2c_dev, O_RDWR);
	if (wm8962->i2c_fd == -1)
	{
		ado_error ("WM8962 I2C_INIT: Failed to open I2C device %s", i2c_dev);
		ado_free (wm8962);
		return (-1);
	}
	
	if (build_wm8962_mixer (wm8962, *mixer))
	{
		ado_error ("WM8962: Failed to build mixer");
		close (wm8962->i2c_fd);
		ado_free (wm8962);
		return (-1);
	}
	
	wm8962_reset (wm8962);
	wm8962_init_subsystems(wm8962);
	wm8962_init_clocks(wm8962, rate_set, sample_mode);

	ado_mixer_set_reset_func (*mixer, (void *)wm8962_reset);
	ado_mixer_set_destroy_func (*mixer, (void *)wm8962_destroy);

	/* setup mixer controls for playback and capture */
	ado_pcm_chn_mixer (pcm1,
		ADO_PCM_CHANNEL_PLAYBACK, *mixer,
		ado_mixer_find_element (*mixer, SND_MIXER_ETYPE_PLAYBACK1,
			SND_MIXER_PCM_OUT, 0),
		ado_mixer_find_group (*mixer, SND_MIXER_PCM_OUT, 0));

	ado_pcm_chn_mixer (pcm1,
		ADO_PCM_CHANNEL_CAPTURE, *mixer,
		ado_mixer_find_element (*mixer, SND_MIXER_ETYPE_CAPTURE1,
			SND_MIXER_ELEMENT_CAPTURE, 0), 
		ado_mixer_find_group (*mixer, SND_MIXER_GRP_IGAIN, 0));

	/* allow audio chip to support multiple simultaneous streams */
	if (ado_pcm_sw_mix (card, pcm1, *mixer))
	{
		ado_error ("WM8962: Failed to build software mixer");
		close (wm8962->i2c_fd);
		ado_free (wm8962);
		return -1;
	}

	return (0);
}

