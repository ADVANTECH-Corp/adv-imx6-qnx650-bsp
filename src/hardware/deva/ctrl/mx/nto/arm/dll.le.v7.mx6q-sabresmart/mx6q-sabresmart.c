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

#include <audio_driver.h>
#include "mxssi.h"

/* weilun@adv - start */
#if 0
extern int wm8962_mixer (ado_card_t * card, ado_mixer_t ** mixer, char * args, ado_pcm_t * pcm1);

/******************************
 * Called by audio controller *
 *****************************/
int
codec_mixer (ado_card_t * card, HW_CONTEXT_T * mx)
{
    return (wm8962_mixer (card, &(mx->mixer), mx->mixeropts, mx->pcm1));
}
#else
extern int sgtl5000_mixer (ado_card_t * card, ado_mixer_t ** mixer, char * args, ado_pcm_t * pcm1);

int
codec_mixer (ado_card_t * card, HW_CONTEXT_T * mx)
{
    return (sgtl5000_mixer (card, &(mx->mixer), mx->mixeropts, mx->pcm1));
}
#endif
/* weilun@adv - end */
