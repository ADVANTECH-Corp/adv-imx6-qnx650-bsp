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







/*
 *
 */

#include <dlfcn.h>

struct dll_syms {
	char	*symname;
	void	*addr;
};

#ifdef DLL_bios
#include "dl_bios.h"
#endif

#ifdef DLL_biosmsi
#include "dl_bios_msi.h"
#endif

#ifdef DLL_biosv2
#include "dl_bios_v2.h"
#endif

#ifdef DLL_biosv2jpl
#include "dl_bios_v2_jpl.h"
#endif

#ifdef DLL_pegasos
#include "dl_pegasos.h"
#endif

#ifdef DLL_cpci6750
#include "dl_cpci6750.h"
#endif

#ifdef DLL_cpc700
#include "dl_cpc700.h"
#endif

#ifdef DLL_raven
#include "dl_raven.h"
#endif

#ifdef DLL_hawk
#include "dl_hawk.h"
#endif

#ifdef DLL_harrier
#include "dl_harrier.h"
#endif

#ifdef DLL_hermosa
#include "dl_hermosa.h"
#endif

#ifdef DLL_mbx
#include "dl_mbx.h"
#endif

#ifdef DLL_tundra860
#include "dl_tundra860.h"
#endif

#ifdef DLL_nec4121
#include "dl_nec4121.h"
#endif

#ifdef DLL_discovery
#include "dl_discovery.h"
#endif

#ifdef DLL_gt64260
#include "dl_gt64260.h"
#endif

#ifdef DLL_copperhead
#include "dl_copperhead.h"
#endif

#ifdef DLL_artesyn750fx
#include "dl_artesyn750fx.h"
#endif

#ifdef DLL_atlantis
#include "dl_atlantis.h"
#endif

#ifdef DLL_katana750i
#include "dl_katana750i.h"
#endif

#ifdef DLL_mvp
#include "dl_mvp.h"
#endif

#ifdef DLL_mtx600
#include "dl_mtx600.h"
#endif

#ifdef DLL_nile
#include "dl_nile.h"
#endif

#ifdef DLL_mpc106
#include "dl_mpc106.h"
#endif

#ifdef DLL_yellowknife
#include "dl_yellowknife.h"
#endif

#ifdef DLL_sandpoint
#include "dl_sandpoint.h"
#endif

#ifdef DLL_jace5
#include "dl_jace5.h"
#endif

#ifdef DLL_ppc405
#include "dl_ppc405.h"
#endif

#ifdef DLL_ppc440rb
#include "dl_ppc440rb.h"
#endif

#ifdef DLL_ppc460ex
#include "dl_ppc460ex.h"
#endif

#ifdef DLL_kilauea
#include "dl_kilauea.h"
#endif

#ifdef DLL_artesyn440
#include "dl_artesyn440.h"
#endif

#ifdef DLL_integrator
#include "dl_integrator.h"
#endif

#ifdef DLL_ixp1200
#include "dl_ixp1200.h"
#endif

#ifdef DLL_ixp2400
#include "dl_ixp2400.h"
#endif

#ifdef DLL_ixp23xx
#include "dl_ixp23xx.h"
#endif

#ifdef DLL_ixc1100
#include "dl_ixc1100.h"
#endif

#ifdef DLL_shasta
#include "dl_shasta.h"
#endif

#ifdef DLL_brh
#include "dl_brh.h"
#endif

#ifdef DLL_mpc8266
#include "dl_mpc8266.h"
#endif

#ifdef DLL_bcm9125e
#include "dl_bcm9125e.h"
#endif

#ifdef DLL_neptecpcc
#include "dl_neptecpcc.h"
#endif

#ifdef DLL_mgt5200
#include "dl_mgt5200.h"
#endif

#ifdef DLL_haco5200
#include "dl_haco5200.h"
#endif

#ifdef DLL_mpc85xx
#include "dl_mpc85xx.h"
#endif

#ifdef DLL_sim
#include "dl_sim.h"
#endif

#ifdef DLL_mpc83xx
#include "dl_mpc83xx.h"
#endif

#ifdef DLL_bn3700
#include "dl_bn3700.h"
#endif

#ifdef DLL_dmc1000
#include "dl_dmc1000.h"
#endif

#ifdef DLL_poulsbo
#include "dl_poulsbo.h"
#endif

#ifdef DLL_poulsbov2
#include "dl_poulsbo_v2.h"
#endif

#ifdef DLL_nex
#include "dl_nex.h"
#endif

#ifdef DLL_mpc8641
#include "dl_mpc8641.h"
#endif

#ifdef DLL_mpc8347ge
#include "dl_mpc8347-ge.h"
#endif

#ifdef DLL_mpc8572
#include "dl_mpc8572.h"
#endif

#ifdef DLL_p4080
#include "dl_p4080.h"
#endif

#ifdef DLL_mpc8536
#include "dl_mpc8536.h"
#endif

#ifdef DLL_mpc5121
#include "dl_mpc5121.h"
#endif

#ifdef DLL_mpc8313
#include "dl_mpc8313.h"
#endif

#ifdef DLL_mpc8544
#include "dl_mpc8544.h"
#endif

#ifdef DLL_mpc8548
#include "dl_mpc8548.h"
#endif

#ifdef DLL_p2020
#include "dl_p2020.h"
#endif

#ifdef DLL_crownbay
#include "dl_crownbay.h"
#endif

#ifdef DLL_dm814x
#include "dl_dm814x.h"
#endif

#ifdef DLL_imx6x
#include "dl_imx6x.h"
#endif

struct	dll_list {
	char					*fname;
	const struct dll_syms	*syms;
};

static const struct dll_list	dll_list[] = {
#ifdef PCI_BIOS_LIST
									{ PCI_BIOS_LIST },
#endif

#ifdef PCI_BIOS_MSI_LIST
									{ PCI_BIOS_MSI_LIST },
#endif

#ifdef PCI_BIOS_V2_LIST
									{ PCI_BIOS_V2_LIST },
#endif

#ifdef PCI_BIOS_V2_JPL_LIST
									{ PCI_BIOS_V2_JPL_LIST },
#endif

#ifdef PCI_P5064_LIST
									{ PCI_P5064_LIST },
#endif

#ifdef PCI_RAVEN_LIST
									{ PCI_RAVEN_LIST },
#endif

#ifdef PCI_HAWK_LIST
									{ PCI_HAWK_LIST },
#endif

#ifdef PCI_HARRIER_LIST
									{ PCI_HARRIER_LIST },
#endif

#ifdef PCI_DISCOVERY_LIST
									{ PCI_DISCOVERY_LIST },
#endif

#ifdef PCI_GT64260_LIST
									{ PCI_GT64260_LIST },
#endif

#ifdef PCI_COPPERHEAD_LIST
                                    { PCI_COPPERHEAD_LIST },
#endif

#ifdef PCI_ARTESYN750FX_LIST
                                    { PCI_ARTESYN750FX_LIST},
#endif

#ifdef PCI_ATLANTIS_LIST
									{ PCI_ATLANTIS_LIST },
#endif

#ifdef PCI_KATANA750I_LIST
                                    { PCI_KATANA750I_LIST },
#endif

#ifdef PCI_MVP_LIST
                                    { PCI_MVP_LIST },
#endif

#ifdef PCI_HERMOSA_LIST
									{ PCI_HERMOSA_LIST },
#endif

#ifdef PCI_NEC4121_LIST
									{ PCI_NEC4121_LIST },
#endif

#ifdef PCI_MBX_LIST
									{ PCI_MBX_LIST },
#endif

#ifdef PCI_MTX600_LIST
									{ PCI_MTX600_LIST },
#endif

#ifdef PCI_NILE_LIST
									{ PCI_NILE_LIST },
#endif

#ifdef PCI_CPCI6750_LIST
									{ PCI_CPCI6750_LIST },
#endif

#ifdef PCI_CPC700_LIST
									{ PCI_CPC700_LIST },
#endif

#ifdef PCI_MPC106_LIST
									{ PCI_MPC106_LIST },
#endif

#ifdef PCI_YELLOWKNIFE_LIST
									{ PCI_YELLOWKNIFE_LIST },
#endif

#ifdef PCI_SANDPOINT_LIST
									{ PCI_SANDPOINT_LIST },
#endif

#ifdef PCI_JACE5_LIST
									{ PCI_JACE5_LIST },
#endif

#ifdef PCI_PPC405_LIST
									{ PCI_PPC405_LIST },
#endif

#ifdef PCI_PPC440RB_LIST
									{ PCI_PPC440RB_LIST },
#endif

#ifdef PCI_PPC460EX_LIST
									{ PCI_PPC460EX_LIST },
#endif

#ifdef PCI_KILAUEA_LIST
									{ PCI_KILAUEA_LIST },
#endif

#ifdef PCI_ARTESYN440_LIST
									{ PCI_ARTESYN440_LIST },
#endif

#ifdef PCI_INTEGRATOR_LIST
									{ PCI_INTEGRATOR_LIST },
#endif

#ifdef PCI_IXP1200_LIST
									{ PCI_IXP1200_LIST },
#endif

#ifdef PCI_IXP2400_LIST
									{ PCI_IXP2400_LIST },
#endif

#ifdef PCI_IXP23XX_LIST
									{ PCI_IXP23XX_LIST },
#endif

#ifdef PCI_IXC1100_LIST
                                    { PCI_IXC1100_LIST },
#endif

#ifdef PCI_SHASTA_LIST
									{ PCI_SHASTA_LIST },
#endif

#ifdef PCI_BRH_LIST
									{ PCI_BRH_LIST },
#endif

#ifdef PCI_MPC8266_LIST
									{ PCI_MPC8266_LIST },
#endif

#ifdef PCI_BCM9125E_LIST
									{ PCI_BCM9125E_LIST },
#endif

#ifdef PCI_NEPTECPCC_LIST
									{ PCI_NEPTECPCC_LIST },
#endif

#ifdef PCI_MGT5200_LIST
                                    { PCI_MGT5200_LIST },
#endif

#ifdef PCI_HACO5200_LIST
                                    { PCI_HACO5200_LIST },
#endif

#ifdef PCI_MPC85XX_LIST
                                    { PCI_MPC85XX_LIST },
#endif

#ifdef PCI_SIM_LIST
                                    { PCI_SIM_LIST },
#endif

#ifdef PCI_MPC83XX_LIST
                                    { PCI_MPC83XX_LIST },
#endif

#ifdef PCI_MPC8313_LIST
                                    { PCI_MPC8313_LIST },
#endif

#ifdef PCI_BN3700_LIST
                                    { PCI_BN3700_LIST },
#endif

#ifdef PCI_DMC1000_LIST
                                    { PCI_DMC1000_LIST },
#endif

#ifdef PCI_POULSBO_LIST
                                    { PCI_POULSBO_LIST },
#endif

#ifdef PCI_POULSBO_V2_LIST
                                    { PCI_POULSBO_V2_LIST },
#endif

#ifdef PCI_NEX_LIST
                                    { PCI_NEX_LIST },
#endif

#ifdef PCI_MPC8641_LIST
                                    { PCI_MPC8641_LIST },
#endif

#ifdef PCI_MPC8347_GE_LIST
                                    { PCI_MPC8347_GE_LIST },
#endif

#ifdef PCI_MPC8572_LIST
                                    { PCI_MPC8572_LIST },
#endif

#ifdef PCI_MPC8536_LIST
                                    { PCI_MPC8536_LIST },
#endif

#ifdef PCI_MPC5121_LIST
                                    { PCI_MPC5121_LIST },
#endif

#ifdef PCI_MPC8544_LIST
                                    { PCI_MPC8544_LIST },
#endif

#ifdef PCI_MPC8548_LIST
                                    { PCI_MPC8548_LIST },
#endif

#ifdef PCI_P4080_LIST
                                    { PCI_P4080_LIST },
#endif

#ifdef PCI_p2020_LIST
                                    { PCI_p2020_LIST },
#endif

#ifdef PCI_CROWNBAY_LIST
                                    { PCI_CROWNBAY_LIST },
#endif

#ifdef PCI_DM814X_LIST
                                    { PCI_DM814X_LIST },
#endif

#ifdef PCI_IMX6X_LIST
                                    { PCI_IMX6X_LIST },
#endif
									{ NULL, NULL }
								};

void	*pci_dlopen (const char *pathname, int mode);
void	*pci_dlsym (void *handle, const char *name);
int		pci_dlclose (void *handle);

__SRCVERSION( "$URL: http://svn/product/tags/restricted/bsp/nto650/ti-j5-evm/latest/src/hardware/pci/include/dl.h $ $Rev: 655789 $" )
