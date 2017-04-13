/*
 * $QNXLicenseC: 
 * Copyright 2012, QNX Software Systems.  
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

#include "startup.h"
#include <arm/mx6x.h>
#include "board.h"

#define MX6X_CCM_CBCMR_GPU3D_SHADER_SEL_594M_PFD (2 << MX6X_CCM_CBCMR_GPU3D_SHADER_CLK_SEL_OFFSET)
#define MX6X_CCM_CBCMR_GPU3D_CORE_SEL_MMDC_CH0   (0 << MX6X_CCM_CBCMR_GPU3D_CORE_CLK_SEL_OFFSET)

/*
 * Gate clocks to unused components.
 */
void mx6x_init_clocks()
{
	/* Enable all clocks in CCGR0 */
	out32(MX6X_CCM_BASE + MX6X_CCM_CCGR0, MX6X_CCM_CCGR0_RESET);

	/* Disable ESAI and unused ECSPI ports */
    out32(MX6X_CCM_BASE + MX6X_CCM_CCGR1, MX6X_CCM_CCGR1_RESET
#ifdef MX6X_DISABLE_CLOCK_CCGR1
		  & ~( MX6X_DISABLE_CLOCK_CCGR1 )
#endif /* MX6X_DISABLE_CLOCK_CCGR1 */
		  );

    out32(MX6X_CCM_BASE + MX6X_CCM_CCGR2, MX6X_CCM_CCGR2_RESET 
#ifdef MX6X_DISABLE_CLOCK_CCGR2
		  & ~( MX6X_DISABLE_CLOCK_CCGR2 )
#endif /* MX6X_DISABLE_CLOCK_CCGR2 */
		  ); 
    
	/* Disable IPU2 */
    out32(MX6X_CCM_BASE + MX6X_CCM_CCGR3, MX6X_CCM_CCGR3_RESET
#ifdef MX6X_DISABLE_CLOCK_CCGR3
		  & ~( MX6X_DISABLE_CLOCK_CCGR3 )
#endif /* MX6X_DISABLE_CLOCK_CCGR3 */
		  );

	/* Disable PCIE */
    out32(MX6X_CCM_BASE + MX6X_CCM_CCGR4, MX6X_CCM_CCGR4_RESET
#ifdef MX6X_DISABLE_CLOCK_CCGR4
		  & ~( MX6X_DISABLE_CLOCK_CCGR4 )
#endif /* MX6X_DISABLE_CLOCK_CCGR4 */		  
		  );
	
    out32(MX6X_CCM_BASE + MX6X_CCM_CCGR5, MX6X_CCM_CCGR5_RESET
#ifdef MX6X_DISABLE_CLOCK_CCGR5
		  & ~( MX6X_DISABLE_CLOCK_CCGR5 )
#endif /* MX6X_DISABLE_CLOCK_CCGR5 */
		  );
	
    out32(MX6X_CCM_BASE + MX6X_CCM_CCGR6, MX6X_CCM_CCGR6_RESET
#ifdef MX6X_DISABLE_CLOCK_CCGR6
		  & ~( MX6X_DISABLE_CLOCK_CCGR6 )
#endif /* MX6X_DISABLE_CLOCK_CCGR6 */		  
		  );
    
    out32(MX6X_CCM_BASE + MX6X_CCM_CCGR7, MX6X_CCM_CCGR7_RESET
#ifdef MX6X_DISABLE_CLOCK_CCGR7
		  & ~( MX6X_DISABLE_CLOCK_CCGR7 )
#endif /* MX6X_DISABLE_CLOCK_CCGR7 */		  
		  );
}


/*
 * Init Vivante GC2000 3D GPU shader, core clocks:
 * shader clock: 594 MHz
 * core clock:   528 MHz
 */
void mx6x_init_gpu3D(void)
{
    int32_t tmp;

    // gate GPU3D clocks before modifying divisors
    out32(MX6X_CCM_BASE + MX6X_CCM_CCGR1,
		  in32(MX6X_CCM_BASE + MX6X_CCM_CCGR1) & ~(MX6X_CCGR1_GPU3D_ENABLE));
    
    // set gc2000 shader parent clock to 594 MHz PFD clock. or GC350??
    tmp = in32(MX6X_CCM_BASE + MX6X_CCM_CBCMR) &
		  ~MX6X_CCM_CBCMR_GPU3D_SHADER_CLK_SEL_MASK;
    tmp |= MX6X_CCM_CBCMR_GPU3D_SHADER_SEL_594M_PFD;
    out32(MX6X_CCM_BASE + MX6X_CCM_CBCMR, tmp);
    
    // set gc2000 shader clock divisor to 1 (bit value 000)
    tmp = in32(MX6X_CCM_BASE + MX6X_CCM_CBCMR) &
		  ~MX6X_CCM_CBCMR_GPU3D_SHADER_PODF_MASK;
    out32(MX6X_CCM_BASE + MX6X_CCM_CBCMR, tmp);

    /* set gc2000 core parent clock to multi port
	 * DRAM/DDR controller (mmdc). */
    /* mmdc clock is derived from periph_main_clock which is derived
	 * from pll2_sw_clk, which is 528MHz
	 */
    tmp = in32(MX6X_CCM_BASE + MX6X_CCM_CBCMR) &
		  ~MX6X_CCM_CBCMR_GPU3D_CORE_CLK_SEL_MASK;
    tmp |= MX6X_CCM_CBCMR_GPU3D_CORE_SEL_MMDC_CH0;
    out32(MX6X_CCM_BASE + MX6X_CCM_CBCMR, tmp);

    // set gc2000 core clock divisor to 1 (bit value 000)
    tmp = in32(MX6X_CCM_BASE + MX6X_CCM_CBCMR) &
		  ~MX6X_CCM_CBCMR_GPU3D_CORE_PODF_MASK;
    out32(MX6X_CCM_BASE + MX6X_CCM_CBCMR, tmp);
    
    // re-enable GPU3D clocks
    out32(MX6X_CCM_BASE + MX6X_CCM_CCGR1,
		  in32(MX6X_CCM_BASE + MX6X_CCM_CCGR1) | MX6X_CCGR1_GPU3D_ENABLE);
}

__SRCVERSION( "$URL$ $Rev$" )
