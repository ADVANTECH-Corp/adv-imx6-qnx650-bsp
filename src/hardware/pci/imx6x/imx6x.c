/*
 * $QNXLicenseC:
 * Copyright 2012, QNX Software Systems. All Rights Reserved.
 *
 * You must obtain a written license from and pay applicable
 * license fees to QNX Software Systems before you may reproduce,
 * modify or distribute this software, or any work that includes
 * all or part of this software.   Free development licenses are
 * available for evaluation and non-commercial purposes.  For more
 * information visit http://licensing.qnx.com or email
 * licensing@qnx.com.
 *
 * This file may contain contributions from others.  Please review
 * this entire file for other proprietary rights or license notices,
 * as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include <arm/mx6x.h>	// Global header for iMX6x SoC
#include "imx6x.h"		// PIC driver for iMX6x
#include <setjmp.h>


pdrvr_entry_t imx6x_entry =
{
	10,
	imx6x_attach,
	imx6x_detach,
	imx6x_cnfg_bridge,
	imx6x_read_cnfg,
	imx6x_write_cnfg,
	imx6x_special_cycle,
	imx6x_map_irq,
	imx6x_avail_irq,
	imx6x_map_addr
	// Strangely we report 10 functions but only have 9.  This is
	// what the reference DM814x PCIe driver does (missing 'routing_options')
};  

#define DB_R1	0x72c

#define STS_CMD_RGSTR	0x4

#define ATU_VIEWPORT_R 				0x900
#define ATU_REGION_CTRL1_R 			0x904
#define ATU_REGION_CTRL2_R 			0x908
#define ATU_REGION_LOWBASE_R 		0x90c
#define ATU_REGION_UPBASE_R 		0x910
#define ATU_REGION_LIMIT_ADDR_R 	0x914
#define ATU_REGION_LOW_TRGT_ADDR_R 	0x918
#define ATU_REGION_UP_TRGT_ADDR_R 	0x91c

#define HW_CCM_ANALOG_PLL_ENET_REG			0xe0
#define HW_CCM_ANALOG_PLL_ENET_SET_REG  	0xe4
#define HW_CCM_ANALOG_PLL_ENET_CLR_REG  	0xe8
#define BM_CCM_ANALOG_PLL_ENET_ENABLE_SATA 	0x00100000
#define BM_CCM_ANALOG_PLL_ENET_ENABLE      	0x00002000
#define BM_CCM_ANALOG_PLL_ENET_POWERDOWN    0x00001000
#define BM_CCM_ANALOG_PLL_ENET_BYPASS       0x00010000
#define BM_CCM_ANALOG_PLL_ENET_ENABLE_PCIE  0x00080000

#define HW_PMU_REG_MISC1_REG		0x160
#define HW_PMU_REG_MISC1_SET_REG	0x164
#define HW_PMU_REG_MISC1_CLR_REG	0x168
#define BM_PMU_REG_MISC1_LVDSCLK1_IBEN      0x00001000
#define BM_PMU_REG_MISC1_LVDSCLK1_OBEN      0x00000400

#define ANATOP_LVDS_CLK1_SRC_PCIE       0xA
#define ANATOP_LVDS_CLK1_SRC_SATA       0xB

#define	LINK_TRAINED		0x11


sigjmp_buf	env;
static int	verbose = 0;


/* =============================================================================
 *  PCI peripheral control functions.  These access the IOMUX_GPR registers
 *  which control features of the PCIe module.
 * ============================================================================= */

typedef struct {
    uint32_t addr;		// field register address
    uint32_t mask;		// field mask
    uint32_t offset;	//field offset
} pcie_iomux_gpr_field_t, *pcie_iomux_gpr_field_p;

typedef enum {
    apps_pm_xmt_pme,
    device_type,
    diag_status_bus_select,
    sys_int,
    apps_pm_xmt_turnoff,
    app_ltssm_enable,
    app_init_rst,
    ref_ssp_en,
    diag_ctrl_bus,
    app_req_entr_l1,
    app_ready_entr_l23,
    app_req_exit_l1,
    app_clk_req_n,
    cfg_l1_clk_removal_en,
    phy_test_powerdown,
    los_level,
    tx_deemph_gen1,
    tx_deemph_gen2_3p5db,
    tx_deemph_gen2_6db,
    tx_swing_full,
    tx_swing_low
} pcie_iomux_gpr_field_type_e;


typedef enum {
    TLP_TYPE_MemRdWr = 0,
    TLP_TYPE_MemRdLk = 1,
    TLP_TYPE_IORdWr = 2,
    TLP_TYPE_CfgRdWr0 = 4,
    TLP_TYPE_CfgRdWr1 = 5
} pcie_tlp_type_e;

typedef enum {
    PCIE_DM_MODE_EP = 0,
    PCIE_DM_MODE_RC = 0x4,
} pcie_dm_mode_e;

typedef enum {
    PCIE_IATU_VIEWPORT_0,
    PCIE_IATU_VIEWPORT_1,
    PCIE_IATU_VIEWPORT_2,
    PCIE_IATU_VIEWPORT_3,

    PCIE_IATU_VIEWPORT_MAX,
} pcie_iatu_vp_e;

/********** IOMUX GPR variables and routines *******************/
static const pcie_iomux_gpr_field_t pcie_iomux_gpr_fields[] = {
    {MX6X_IOMUX_GPR12, (1 << 9), 9},        //    .apps_pm_xmt_pme(iomuxc_gpr12[9]),
    {MX6X_IOMUX_GPR12, (0xf << 12), 12},    //    .device_type(iomuxc_gpr12[15:12]),
    {MX6X_IOMUX_GPR12, (0xf << 17), 17},    //    .diag_status_bus_select(iomuxc_gpr12[20:17]),
    {MX6X_IOMUX_GPR1,  (1 << 14), 14},      //    .sys_int(iomuxc_gpr1[14]),
    {MX6X_IOMUX_GPR12, (1 << 16), 16},      //    .apps_pm_xmt_turnoff(iomuxc_gpr12[16]),
    {MX6X_IOMUX_GPR12, (1 << 10), 10},      //    .app_ltssm_enable(iomuxc_gpr12[10]),
    {MX6X_IOMUX_GPR12, (1 << 11), 11},      //    .app_init_rst(iomuxc_gpr12[11]),
    {MX6X_IOMUX_GPR1,  (1 << 16), 16},      //    .ref_ssp_en(iomuxc_gpr1[16]),
    {MX6X_IOMUX_GPR12, (7 << 21), 21},      //    .diag_ctrl_bus(iomuxc_gpr12[23:21]),
    {MX6X_IOMUX_GPR1,  (1 << 26), 26},      //    .app_req_entr_l1(iomuxc_gpr1_26),
    {MX6X_IOMUX_GPR1,  (1 << 27), 27},      //    .app_ready_entr_l23(iomuxc_gpr1_27),
    {MX6X_IOMUX_GPR1,  (1 << 28), 28},      //    .app_req_exit_l1(iomuxc_gpr1_28),
    {MX6X_IOMUX_GPR1,  (1 << 30), 30},      //    .app_clk_req_n(iomuxc_gpr1_30),
    {MX6X_IOMUX_GPR1,  (1 << 31), 31},      //    .cfg_l1_clk_removal_en(iomuxc_gpr1_31),
    {MX6X_IOMUX_GPR1,  (1 << 18), 18},      //    .test_powerdown(iomuxc_gpr1_18),
    {MX6X_IOMUX_GPR12, (0x1f << 4), 4},     //    .los_level(iomuxc_gpr12[8:4]),
    {MX6X_IOMUX_GPR8,  (0x3f << 0), 0},     //    .pcs_tx_deemph_gen1(iomuxc_gpr8[5:0]),
    {MX6X_IOMUX_GPR8,  (0x3f << 6), 6},     //    .pcs_tx_deemph_gen2_3p5db(iomuxc_gpr8[11:6]),
    {MX6X_IOMUX_GPR8,  (0x3f << 12), 12},   //    .pcs_tx_deemph_gen2_6db(iomuxc_gpr8[17:12]),
    {MX6X_IOMUX_GPR8,  (0x7f << 18), 18},   //    .pcs_tx_swing_full(iomuxc_gpr8[24:18]),
    {MX6X_IOMUX_GPR8,  (0x7f << 25), 25},   //    .pcs_tx_swing_low(iomuxc_gpr8[31:25]),
};

static void pcie_gpr_write_field(imx6x_dev_t *pdev, pcie_iomux_gpr_field_type_e field, uint32_t val)
{
    uint32_t v, addr, mask, offset;

    addr = pdev->iomux_base + pcie_iomux_gpr_fields[field].addr;
    mask = pcie_iomux_gpr_fields[field].mask;
    offset = pcie_iomux_gpr_fields[field].offset;
//    printf( "GPR write mask=0x%08x offset=%d\n", mask, offset );
//    printf( "    phy=0x%08x\n", (addr - pdev->iomux_base) + MX6X_IOMUXC_BASE );

    v = in32(addr);
    v &= ~mask;
    v |= ((val << offset) & mask);
    out32(addr, v);
}


/* wait for the physical link to start */
static int pcie_wait_link_up(imx6x_dev_t *pdev, int wait_ms)
{
    uint32_t val;
    int count;

    // Fix for POR 0000270 : PCI driver hangs when re-started after being slayed
    // We need to allow time for the link to settle before reading the DB_R1 register
    // otherwise the system will hang.
    delay(100);

    count = wait_ms;
    do
    {
        val = in32(pdev->pci_dbi_base + DB_R1) & (0x1 << (36 - 32));  // link is debug bit 36 debug 1 start in bit 32
        delay(1);
        count--;
    } while ( !val && ((count > 0) || (wait_ms == 0)) );

    if (!val)
        return -1;

    return 0;
}


/* =============================================================================
 *  Clock and power helper functions
 * ============================================================================= */

#define GPIO_LOW		0
#define GPIO_HIGH		1

#define GPIO_IRQ_LEVEL_LOW		0
#define GPIO_IRQ_LEVEL_HIGH		1
#define GPIO_IRQ_EDGE_RISE		2
#define GPIO_IRQ_EDGE_FALL		3


/* helper function to drive make a GPIO an output and drive it HIGH */
void gpio_drive_output( imx6x_dev_t *pdev, int instance, int bit, int state )
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
	// Calculate offset for this GPIO bank from start of the GPIO region
	// Note: GPIO banks are numbered starting from 1, not 0!
	gpio_base = (uint32_t)pdev->gpio_base + (MX6X_GPIO_SIZE * (instance-1));
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


// Enable PLL to generate PCIe clock
static void pcie_clk_setup( imx6x_dev_t *pdev, int enable )
{
    uint32_t val;

    if (enable)
    {
		// gate on pci-e clks
		val = in32( pdev->ccm_base + MX6X_CCM_CCGR4 );
		val |= 0x3 << 0;
		out32( pdev->ccm_base + MX6X_CCM_CCGR4, val );

		// clear the powerdown bit
        out32( pdev->ccm_analog_base + HW_CCM_ANALOG_PLL_ENET_CLR_REG,
        		BM_CCM_ANALOG_PLL_ENET_POWERDOWN );

		// enable pll
        out32( pdev->ccm_analog_base + HW_CCM_ANALOG_PLL_ENET_SET_REG,
        		BM_CCM_ANALOG_PLL_ENET_ENABLE );

		// wait until the pll is locked
        do
        {
        	val = in32( pdev->ccm_analog_base + HW_CCM_ANALOG_PLL_ENET_REG );
        	delay(1);
        } while ( (val & (1 << 31)) == 0 );

		// Disable bypass
        out32( pdev->ccm_analog_base + HW_CCM_ANALOG_PLL_ENET_CLR_REG,
        		BM_CCM_ANALOG_PLL_ENET_BYPASS );

		// enable pci-e ref clk
        out32( pdev->ccm_analog_base + HW_CCM_ANALOG_PLL_ENET_SET_REG,
        		BM_CCM_ANALOG_PLL_ENET_ENABLE_PCIE );
    }
    else
    {
		// disable pci-e ref clk
        out32( pdev->ccm_analog_base + HW_CCM_ANALOG_PLL_ENET_CLR_REG,
        		BM_CCM_ANALOG_PLL_ENET_ENABLE_PCIE );

		// Enable bypass
        out32( pdev->ccm_analog_base + HW_CCM_ANALOG_PLL_ENET_SET_REG,
        		BM_CCM_ANALOG_PLL_ENET_BYPASS );

		// disable pll
        out32( pdev->ccm_analog_base + HW_CCM_ANALOG_PLL_ENET_CLR_REG,
        		BM_CCM_ANALOG_PLL_ENET_ENABLE );

		// set the powerdown bit
        out32( pdev->ccm_analog_base + HW_CCM_ANALOG_PLL_ENET_SET_REG,
        		BM_CCM_ANALOG_PLL_ENET_POWERDOWN );

		// gate off pci-e clks
		val = in32( pdev->ccm_base + MX6X_CCM_CCGR4 );
		val &= ~(0x3 << 0);
		out32( pdev->ccm_base + MX6X_CCM_CCGR4, val );
    }
}


static void pcie_card_rst( imx6x_dev_t *pdev )
{
	/* Assert reset by driving LOW
	 * PERST# => PCIE_RST_B => SPDIF_OUT(ALT5) = GPIO[12] of instance: gpio7. */
	gpio_drive_output( pdev, 7, 12, GPIO_LOW );

    // wait for card to fully reset (200ms in Freescale's reference SDK)
	delay(200);

	/* De-assert reset by driving HIGH
	 * PERST# => PCIE_RST_B => SPDIF_OUT(ALT5) = GPIO[12] of instance: gpio7. */
	gpio_drive_output( pdev, 7, 12, GPIO_HIGH );

    // wait for card to drop out of reset
	delay(100);
}

// pcie_enable_extrn_100mhz_clk
static void pcie_card_ref_clk_setup( imx6x_dev_t *pdev, int enable )
{
	uint32_t val;

    if (enable)
    {
        // Disable SATA clock gating used as external reference
        out32( pdev->ccm_analog_base + HW_CCM_ANALOG_PLL_ENET_SET_REG,
        		BM_CCM_ANALOG_PLL_ENET_ENABLE_SATA );

        // Select SATA clock source and switch to output buffer.
        out32( pdev->ccm_analog_base + HW_PMU_REG_MISC1_CLR_REG,
        		BM_PMU_REG_MISC1_LVDSCLK1_IBEN );

        val = in32( pdev->ccm_analog_base + HW_PMU_REG_MISC1_REG );
        val &= ~0x1f;	// mask off bits [4:0]

        // For some reason we have to use the 100MHz SATA clock
        // even though the PCIe spec' says the card must use
        // the same reference clock as the PCIe controller.
        // Maybe the iMX6 datasheet is wrong?
        val |= ANATOP_LVDS_CLK1_SRC_SATA;
        out32( pdev->ccm_analog_base + HW_PMU_REG_MISC1_REG, val );

        out32( pdev->ccm_analog_base + HW_PMU_REG_MISC1_SET_REG,
        		BM_PMU_REG_MISC1_LVDSCLK1_OBEN );
    }
}

static void pcie_card_pwr_setup( imx6x_dev_t *pdev, int enable )
{
    if (enable)
    {
    	/* Enable power to PCIe device by driving a GPIO high.
    	 * MPCIE_3V3
    	 * PCIE_PWR_EN = EIM_D19(ALT5) = GPIO[19] of instance: gpio3 */
    	gpio_drive_output( pdev, 3, 19, GPIO_HIGH );

    	/* De-assert disable by driving HIGH
    	 * W_DISABLE# => PCIE_DIS_B => KEY_COL4(ALT5) = GPIO[14] of instance: gpio4. */
    	gpio_drive_output( pdev, 4, 14, GPIO_HIGH );
    }
    else
    {
    	/* Disable power to PCIe device by driving a GPIO low.
    	 * MPCIE_3V3
    	 * PCIE_PWR_EN = EIM_D19(ALT5) = GPIO[19] of instance: gpio3 */
    	gpio_drive_output( pdev, 3, 19, GPIO_LOW );

    	/* Assert disable by driving LOW
    	 * W_DISABLE# => PCIE_DIS_B => KEY_COL4(ALT5) = GPIO[14] of instance: gpio4. */
    	gpio_drive_output( pdev, 4, 14, GPIO_LOW );
    }
}


/*
 * Map endpoint's space to CPU side.
 *
 *   viewport:            the viewport number of iATU
 *   tlp_type:            the type of the transaction layer package
 *   addr_base_cpu_side:  base address in CPU side
 *   addr_base_pcie_side: base address in PCIE side
 *   size:                the size of the space to be mapped
 *
 *   returns              base address in CPU side
 */
static void pcie_map_space( imx6x_dev_t *pdev, uint32_t viewport, uint32_t tlp_type,
                            uint32_t addr_base_cpu_side, uint32_t addr_base_pcie_side,
                            uint32_t size )
{
	/* Optimise config mappings so we only map if the PCIe target address
	 * (bus,device,function) differs from the previous mapping.
	 */
	if ( tlp_type == TLP_TYPE_CfgRdWr0 )
	{
	    if ( addr_base_pcie_side == pdev->current_cfg_addr )
		{
			return;
		}
		pdev->current_cfg_addr = addr_base_pcie_side;
	}

//	printf( "%s : type=%d cpu=0x%08x pci=0x%08x size=0x%x\n", __FUNCTION__, tlp_type, addr_base_cpu_side, addr_base_pcie_side, size);
    out32( pdev->pci_dbi_base + ATU_VIEWPORT_R, (viewport & 0x0F) | (0 << 31) );
    out32( pdev->pci_dbi_base + ATU_REGION_LOWBASE_R, addr_base_cpu_side );
    out32( pdev->pci_dbi_base + ATU_REGION_UPBASE_R, 0 );
    out32( pdev->pci_dbi_base + ATU_REGION_LIMIT_ADDR_R, addr_base_cpu_side + size - 1 );
    out32( pdev->pci_dbi_base + ATU_REGION_UP_TRGT_ADDR_R, 0 );
    out32( pdev->pci_dbi_base + ATU_REGION_LOW_TRGT_ADDR_R, addr_base_pcie_side );
    out32( pdev->pci_dbi_base + ATU_REGION_CTRL1_R, tlp_type & 0x0F );
    out32( pdev->pci_dbi_base + ATU_REGION_CTRL2_R, ((unsigned int)(1 << 31)) );
}


/* This function establishes public mappings for the IO and MEM
 * PCI address spaces.  It also holds the IRQ numbers.
 *
 * Private mappings for the PCIe standard configuration and application
 * specific registers are created by [imx6x_config]
 */
static	int	imx6x_seed (imx6x_dev_t *pdev)
{
	rsrc_alloc_t	*ralloc;

	// seed memory
	ralloc = &(pdev->ralloc[0]);
	ralloc->start = PCI_MEM_BASE;
	ralloc->end   = PCI_MEM_BASE + PCI_MEM_SIZE - 1;
	ralloc->flags = RSRCDBMGR_PCI_MEMORY | RSRCDBMGR_FLAG_RANGE;

	if ( rsrcdbmgr_create(ralloc, 1) == -1 )
	{
		perror ("Unable to seed resource memory: ");
		return ENOMEM;
	}
	pdev->pci_mem_base = PCI_MEM_BASE;
	pdev->pci_mem_size = PCI_MEM_BASE + PCI_MEM_SIZE - 1;

	// seed I/O
	ralloc = &(pdev->ralloc[1]);
	ralloc->start = PCI_IO_BASE;
	ralloc->end   = PCI_IO_BASE + PCI_IO_SIZE - 1;
	ralloc->flags = RSRCDBMGR_IO_PORT | RSRCDBMGR_FLAG_RANGE;

	if ( rsrcdbmgr_create(ralloc, 1) == -1 )
	{
		perror ("Unable to seed resource I/O: ");
		return ENOMEM;
	}
	pdev->pci_io_base = PCI_IO_BASE;
	pdev->pci_io_size = PCI_IO_BASE + PCI_IO_SIZE - 1;

	// seed irqs
	// note: IRQs are numbered back to front on iMX6
	ralloc = &(pdev->ralloc[2]);
	ralloc->start = PCI_IRQ_D;
	ralloc->end   = PCI_IRQ_A;
	ralloc->flags = RSRCDBMGR_IRQ;

	if ( rsrcdbmgr_create(ralloc, 1) == -1 )
	{
		perror ("Unable to seed resource irqs: ");
		return ENOMEM;
	}
	return (EOK);
}


static	int	imx6x_unseed (imx6x_dev_t *pdev)
{
	rsrc_alloc_t	ralloc;

	// seed memory
	ralloc.start = PCI_MEM_BASE;
	ralloc.end   = PCI_MEM_BASE + PCI_MEM_SIZE - 1;
	ralloc.flags = RSRCDBMGR_PCI_MEMORY | RSRCDBMGR_FLAG_RANGE;

	if ( rsrcdbmgr_destroy(&(pdev->ralloc[0]), 1) == -1 )
	{
		perror ("Unable to unseed resource memory: ");
		return ENOMEM;
	}
	if ( rsrcdbmgr_destroy(&(pdev->ralloc[0]), 1) == -1 )
	{
		perror ("Unable to unseed resource I/O: ");
		return ENOMEM;
	}
	if ( rsrcdbmgr_destroy(&(pdev->ralloc[0]), 1) == -1 )
	{
		perror ("Unable to unseed resource irqs: ");
		return ENOMEM;
	}
	return (EOK);
}


/**************************************************************************/
/* Perform all steps needed to configure the PCIe controller, power up    */
/* the miniPCIe card and bring up the PCIe link between host and device.  */
/*                                                                      */
/**************************************************************************/
static	int	imx6x_config (imx6x_dev_t *pdev)
{
	int         result;
    uint32_t    val;

	// Tell PCI software about the IO and MEM addresses (and IRQs)
    result = imx6x_seed (pdev);
	if ( result != EOK )
		return (result);

	// Map peripherals into our memory space
	// -------------------------------------

	// PCI config area
	pdev->pci_cfg_pbase = PCI_CFG_BASE;
	pdev->pci_cfg_vbase = mmap_device_io(PCI_CFG_SIZE, PCI_CFG_BASE);
	if ( pdev->pci_cfg_vbase == (uintptr_t) MAP_FAILED )
	{
		perror ("Unable to mmap PCI config aera: ");
		return (ENODEV);
	}

	// PCI DBI
	pdev->pci_dbi_base = mmap_device_io(PCI_DBI_SIZE, PCI_DBI_BASE);
	if ( pdev->pci_dbi_base == (uintptr_t) MAP_FAILED )
	{
		perror ("Unable to mmap PCI DBI aera: ");
		return (ENODEV);
	}

	// CCM
	pdev->ccm_base = mmap_device_io(MX6X_CCM_SIZE, MX6X_CCM_BASE);
	if ( pdev->ccm_base == (uintptr_t) MAP_FAILED )
	{
		perror ("Unable to mmap CCM: ");
		return (ENODEV);
	}

	// CCM Analogue + PMU
	pdev->ccm_analog_base = mmap_device_io(MX6X_ANATOP_SIZE, MX6X_ANATOP_BASE);
	if ( pdev->ccm_analog_base == (uintptr_t) MAP_FAILED )
	{
		perror ("Unable to mmap CCM analog: ");
		return (ENODEV);
	}

	// GPIO
	pdev->gpio_base = mmap_device_io((7*MX6X_GPIO_SIZE), MX6X_GPIO1_BASE);
	if ( pdev->gpio_base == (uintptr_t) MAP_FAILED )
	{
		perror ("Unable to mmap GPIO: ");
		return (ENODEV);
	}
//	printf( "gpio phy=0x%08x vir=0x%08x\n", MX6X_GPIO1_BASE, pdev->gpio_base );

	// IOMUX (including GPR registers)
	pdev->iomux_base = mmap_device_io(MX6X_IOMUXC_SIZE, MX6X_IOMUXC_BASE);
	if ( pdev->iomux_base == (uintptr_t) MAP_FAILED )
	{
		perror ("Unable to mmap IOMUX: ");
		return (ENODEV);
	}

	// The system may have been reset by the watchdog timer,
	// leaving the PCIe controller in a random state.  We
	// must completely disable it before reinitialising.
	// ------------------------------------------------------

    // disable link training
    pcie_gpr_write_field(pdev, app_ltssm_enable, 0);
    delay(50);

    // disable ref clk of the phy within i.mx6x
    pcie_gpr_write_field(pdev, ref_ssp_en, 0);
    delay(20);

	// reset the PCIe card
	pcie_card_rst( pdev );

	// disable power to the PCIe slot
	pcie_card_pwr_setup( pdev, 0 );

    // disable clocks to the PCIe block
	pcie_clk_setup( pdev, 0 );
	delay(20);

	// PCIe controller configuration
	// -----------------------------
    pcie_gpr_write_field(pdev, diag_status_bus_select, 0xb);

    // disable link
    pcie_gpr_write_field(pdev, app_ltssm_enable, 0);

    // clear PCIe test power down
    pcie_gpr_write_field(pdev, phy_test_powerdown, 0);

    // configure constant input signal to the pcie ctrl and phy
    pcie_gpr_write_field(pdev, device_type, PCIE_DM_MODE_RC);
    pcie_gpr_write_field(pdev, los_level, 9); //phy Loss-Of-Signal detection level. process dependent.
    pcie_gpr_write_field(pdev, tx_deemph_gen1, 21);   // typical setting for PCIe 1.1 operation - package dependen
    pcie_gpr_write_field(pdev, tx_deemph_gen2_3p5db, 21); // setting for PCI2 2.0 operation with low de-emphasis setting - package dependen
    pcie_gpr_write_field(pdev, tx_deemph_gen2_6db, 32);   // typical setting for PCIe 2.0 operation - package dependen
    pcie_gpr_write_field(pdev, tx_swing_full, 115);   // For the default 1.0V amplitude - package dependent
    pcie_gpr_write_field(pdev, tx_swing_low, 115);    // to support PCIe Mobile Mode

    // enable clocks to the PCIe block
	pcie_clk_setup( pdev, 1 );

	// enable power to the PCIe slot
	pcie_card_pwr_setup( pdev, 1 );

	// wait for the power stabilise
	delay(20);

	// enable external clock to the PCIe card
	pcie_card_ref_clk_setup( pdev, 1 );

	// reset the PCIe card
	pcie_card_rst( pdev );

    // enable ref clk of the phy within i.mx6x
    pcie_gpr_write_field(pdev, ref_ssp_en, 1);

    // wait for a while before accessing the controller's registers.
    delay(1);

    // start link up
    pcie_gpr_write_field(pdev, app_ltssm_enable, 1);

    pdev->link_trained = 0;
    if ( pcie_wait_link_up(pdev, 1000) != 0 )
    {
    	slogf (_SLOGC_PCI, _SLOG_ERROR, "Link training failed");
        return -1;
    }
    pdev->link_trained = LINK_TRAINED;

    // enable bus master, io and memory
    val = in32( pdev->pci_dbi_base + STS_CMD_RGSTR );
    val |= 0x07;
    out32( pdev->pci_dbi_base + STS_CMD_RGSTR, val );


    // Map PCI regions into ARM memory space
    // ----------------------------------------

    // The first cfg mapping will be to PCIe address 0
    // and so we must set the 'previous mapping' another
    // value. pcie_map_space() will then change it to 0.
    pdev->current_cfg_addr = ~0;

    // A default config mapping for bus=0, d=0, func=0
    // This will be replaced during a config read/write
    pcie_map_space( pdev, PCIE_IATU_VIEWPORT_0,
					TLP_TYPE_CfgRdWr0,
					PCI_CFG_BASE, 0, PCI_CFG_SIZE);

    // Memory
    pcie_map_space( pdev, PCIE_IATU_VIEWPORT_1,
					TLP_TYPE_MemRdWr,
					PCI_MEM_BASE, PCI_MEM_BASE, PCI_MEM_SIZE);

    // IO
    pcie_map_space( pdev, PCIE_IATU_VIEWPORT_2,
					TLP_TYPE_IORdWr,
					PCI_IO_BASE, 0, PCI_IO_SIZE);

    // [BA] configure the bridge bus connections:
    // Primary bus     = 0
    // Secondary bus   = 1
    // Subordinate bus = 1 (same as primary)
    imx6x_read_cnfg (pdev, 0, 0, offsetof (struct _pci_bridge_config_regs, Primary_Bus_Number), 4, &val);
    val &= 0xff000000;
    val |= 0x00010100;
	imx6x_write_cnfg (pdev, 0, 0, offsetof (struct _pci_bridge_config_regs, Primary_Bus_Number), 4, val);

    // Set the IO address range to 0x1000-0xffff
	//
	// IO_LIMIT [15:8]
	//   [15:12] = upper nibble of size
	//   [7:0]   = size (0=16 bit, 1=32bit)
	// IO_BASE [7:0]
	//   [15:12] = upper nibble of start address
	//   [7:0]   = size (0=16 bit, 1=32bit)
	imx6x_read_cnfg (pdev, 0, 0, offsetof (struct _pci_bridge_config_regs, IO_Base), 1, &val);
 	val = 0xf010;
	imx6x_write_cnfg (pdev, 0, 0, offsetof (struct _pci_bridge_config_regs, IO_Base), 1, val);

	// PCIe host bridge defaults to legacy IRQs off (0x1ff).
	// We need them set to 0x0, the same as DM814x and P2020
	// because QNX doesn't support Message Signalled Irqs yet.
	imx6x_read_cnfg (pdev, 0, 0, offsetof (struct _pci_bridge_config_regs, Interrupt_Line), 1, &val);
 	val = 0x0000;
	imx6x_write_cnfg (pdev, 0, 0, offsetof (struct _pci_bridge_config_regs, Interrupt_Line), 1, val);

    // Tell the PCI controller where the mapping memory starts
	val = (PCI_MEM_BASE >> 16) & 0xfff0;		// [BA] 12 MSB, 4 LSB are zero
	val |= ((PCI_MEM_BASE + PCI_MEM_SIZE - 1) & 0xfff00000);
	imx6x_write_cnfg (pdev, 0, 0, offsetof (struct _pci_bridge_config_regs, Memory_Base), 1, val);

	return (0);
}


/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
int		imx6x_cnfg_bridge (void *hdl, uint32_t bus, uint32_t devfunc, pci_bus_t *pbus)
{
//	imx6x_dev_t	*pdev = (imx6x_dev_t *)hdl;

	// [BA] try this snippet from P2020 driver..
//	pbus->pciex_addr = pdev->pci_dbi_base;
//	pbus->pciex_size = 0xe00;

	return (EOK);
}


/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
int		imx6x_read_cnfg (void *hdl, unsigned bus, unsigned devfunc, unsigned reg, unsigned width, void *buf)
{
	imx6x_dev_t	*pdev = (imx6x_dev_t *)hdl;
	uint8_t		dev, func;
	uint32_t	addr;

	if (sigsetjmp (env, SIGBUS) == 14)
		return (-1);

	dev = (devfunc >> 3) & 0x1F;
	func = devfunc & 0x07;

	if (bus > 1 || dev > 0 || func > 7)
		return (-1);

	// Local config
	if (bus == 0 && devfunc == 0)
	{
		*((uint32_t *)buf) = in32(pdev->pci_dbi_base + reg);

		if (reg == 0x08)	// Class code
			*((uint32_t *)buf) |= 0x06000000;	// host-pci bridge
//		printf( "read: bus=%d dev=%d func=%d reg=%d width=%d data=0x%08x\n",
//				bus, dev, func, reg, width, *((uint32_t *)buf) );
	}
	else
	{
		// Link must be trained
		if (pdev->link_trained != LINK_TRAINED)
		{
			slogf (_SLOGC_PCI, _SLOG_ERROR, "write: link not trained\n" );
			return (-1);
		}

		/*
		 * The iMX6 PCIe controller does not have a specific CONFIG_SETUP register
		 * where the bus,device,function of a PCI config request are specified.
		 * Instead the controller encodes these details into the 32bit address
		 * of a request from the ARM:
		 *
		 *	  31:24 Bus Number
		 *	  23:19 Device Number
		 *	  18:16 Function Number
		 *	  11:8 Extended Register Number
		 *	  7:2 Register Number
		 *
		 * However... The PCIe controller is only exposed to a 16MB region of
		 * the ARM address space.  So to form the CFG request address properly
		 * we use one of the 4 Address Translation Units (iATU) between the PCIe
		 * controller and the ARM.  This lets us map a 64KB CONFIG region to
		 * point at the address of a specific bus,device,function.
		 * Accesses within this mapped region form the 'register' and
		 * 'extended register' parts of the address.
		 *
		 * See page 4168 of the iMX6 Reference Manual, Rev C.
		 */

		addr = (bus << 24) | (dev << 19) | (func << 16);

		pcie_map_space( pdev, PCIE_IATU_VIEWPORT_0,
						TLP_TYPE_CfgRdWr0,
						PCI_CFG_BASE,	// Location in ARM memory space
						addr,
						PCI_CFG_SIZE);

		*((uint32_t *)buf) = in32( pdev->pci_cfg_vbase + reg );
//		printf( "read: bus=%d dev=%d func=%d reg=%d width=%d data=0x%08x\n",
//				bus, dev, func, reg, width, *((uint32_t *)buf) );
	}
	return (EOK);
}


/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
int		imx6x_write_cnfg (void *hdl, unsigned bus, unsigned devfunc, unsigned reg, unsigned width,  const uint32_t data)
{
	imx6x_dev_t	*pdev = (imx6x_dev_t *)hdl;
	uint8_t		dev, func;
	uint32_t	addr;

	if (sigsetjmp (env, SIGBUS) == 14)
		return (-1);

	dev = (devfunc >> 3) & 0x1F;
	func = devfunc & 0x07;

	if (bus > 1 || dev > 0 || func > 7)
		return (-1);

//	printf( "write: bus=%d dev=%d func=%d reg=%d width=%d data=0x%08x\n",
//			bus, dev, func, reg, width, data );

	// Local config
	if (bus == 0 && devfunc == 0)
	{
		out32( pdev->pci_dbi_base + reg, data );
	}
	else
	{
		// Link must be trained
		if (pdev->link_trained != LINK_TRAINED)
		{
			printf( "write: link not trained\n" );
			return (-1);
		}

		/*
		 * The PCIe controller does not have a specific CONFIG_SETUP register
		 * where the bus,device,function of a PCI config request are specified.
		 * Instead the controller encodes these details into the 32bit address
		 * of a request from the ARM:
		 *
		 *	  31:24 Bus Number
		 *	  23:19 Device Number
		 *	  18:16 Function Number
		 *	  11:8  Extended Register Number
		 *	  7:2   Register Number
		 *
		 * However... The PCIe controller is only exposed to a 16MB region of
		 * the ARM address space.  So to form the CFG request address properly
		 * we use one of the 4 Address Translation Units (iATU) between the PCIe
		 * controller and the ARM.  This lets us map a 64KB CONFIG region to
		 * point at the address of a specific bus,device,function.
		 * Accesses within this mapped region form the 'register' and
		 * 'extended register' parts of the address.
		 *
		 * See page 4168 of the iMX6 Reference Manual, Rev C.
		 */

		addr = (bus << 24) | (dev << 19) | (func << 16);

		pcie_map_space( pdev, PCIE_IATU_VIEWPORT_0,
						TLP_TYPE_CfgRdWr0,
						PCI_CFG_BASE,	// Location in ARM memory space
						addr,
						PCI_CFG_SIZE);

		out32( pdev->pci_cfg_vbase + reg, data );
	}

	return (EOK);
}


/*****************************************************************************/
/* Map cpu address to pci address / pci address to cpu address               */
/*****************************************************************************/
int		imx6x_map_addr (void *hdl, uint64_t iaddr, uint64_t *oaddr, uint32_t type)
{
	imx6x_dev_t	*pdev = (imx6x_dev_t *)hdl;

	if (type & PCI_MAP_ADDR_PCI)
	{
		/*
		 * Map CPU to PCI
		 */
		if (type & PCI_IO_SPACE)
		{
			if (iaddr == 0)
			{	/* Special case for I/O xlation */
				*oaddr = -pdev->pci_io_base;
			}
			else
			{
				*oaddr = iaddr - pdev->pci_io_base;
			}
		}
		if (type & (PCI_MEM_SPACE | PCI_ROM_SPACE))
		{
			*oaddr = iaddr;
		}
		if (type & PCI_ISA_SPACE)
		{
			*oaddr = iaddr;
		}
		if (type & PCI_BMSTR_SPACE)
		{
			*oaddr = iaddr;
		}
	}
	if (type & PCI_MAP_ADDR_CPU)
	{
		/*
		 * Map PCI to CPU
		 */
		if (type & PCI_IO_SPACE)
		{
			*oaddr = iaddr + pdev->pci_io_base;
		}
		if (type & (PCI_MEM_SPACE | PCI_ROM_SPACE))
		{
			*oaddr = iaddr;
		}
		if (type & PCI_ISA_SPACE)
		{
			*oaddr = iaddr;
		}
		if (type & PCI_BMSTR_SPACE)
		{
			*oaddr = iaddr;
		}
	}
//	printf( "map: iaddr=0x%llx *oaddr=0x%llx type=0x%08x\n",
//			iaddr, *oaddr, type );
	return (EOK);
}


/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
int		imx6x_map_irq(void *hdl, uint32_t bus, uint32_t devfunc, uint32_t line, uint32_t pin, uint32_t flags)
{
	return (EOK);
}


/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
int		imx6x_special_cycle (void *hdl, uint32_t bus, uint32_t data)
{
	hdl = hdl, bus = bus, data = data;
	return (EOK);
}


/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
int		imx6x_avail_irq (void *hdl, uint32_t bus, uint32_t devfunc, uint32_t *list, uint32_t *nelm)
{
	imx6x_dev_t	*pdev;
	uint8_t		func;

	pdev = (imx6x_dev_t *)hdl;
	func = PCI_FUNCNO (devfunc);
	switch (func)
	{
		case 0:
			*list = PCI_IRQ_A;
			break;
		case 1:
			*list = PCI_IRQ_B;
			break;
		case 2:
			*list = PCI_IRQ_C;
			break;
		case 3:
			*list = PCI_IRQ_D;
			break;
	}
	*nelm = 1;
	return (EOK);
}


/**************************************************************************/
/*                                                                        */
/**************************************************************************/
int		imx6x_detach (void *hdl)
{
	uint32_t	val;
	imx6x_dev_t	*pdev;

	pdev = (imx6x_dev_t *)hdl;

    // disable bus master, io, memory
    val = in32( pdev->pci_dbi_base + STS_CMD_RGSTR );
    val &= ~0x07;
    out32( pdev->pci_dbi_base + STS_CMD_RGSTR, val );

    // disable link training
    pcie_gpr_write_field(pdev, app_ltssm_enable, 0);
    delay(50);

    // disable ref clk of the phy within i.mx6x
    pcie_gpr_write_field(pdev, ref_ssp_en, 0);
    delay(20);

	// reset the PCIe card
	pcie_card_rst( pdev );

	// disable power to the PCIe slot
	pcie_card_pwr_setup( pdev, 0 );

    // disable clocks to the PCIe block
	pcie_clk_setup( pdev, 0 );
	delay(20);

	// free I/O
	munmap_device_io( pdev->pci_dbi_base, PCI_DBI_SIZE );
	munmap_device_io( pdev->pci_cfg_vbase, PCI_CFG_SIZE );
	munmap_device_io( pdev->ccm_base, MX6X_CCM_SIZE );
	munmap_device_io( pdev->ccm_analog_base, MX6X_ANATOP_SIZE);
	munmap_device_io( pdev->gpio_base, (7*MX6X_GPIO_SIZE) );
	munmap_device_io( pdev->iomux_base, MX6X_IOMUXC_SIZE );

	// release system resources
	imx6x_unseed(pdev);

	// release the structure
	free (pdev);

	return (EOK);
}


/**************************************************************************/
/*                                                                        */
/**************************************************************************/
void	busfault (int sig_number)
{
	siglongjmp (env, 14);
}


/**************************************************************************/
/*                                                                        */
/**************************************************************************/
int		imx6x_attach (char *options, void **handle)
{
	int				opt, rval = EOK;
	imx6x_dev_t	*pdev;
	char			*value, *c;
	static char		*pci_drvr_opts[] = {
						"verbose",
						NULL
						};

	signal (SIGBUS, busfault);
	if ((pdev = calloc (1, sizeof (imx6x_dev_t))) == NULL)
	{
		return (ENOMEM);
	}

	while (options && *options != '\0')
	{
		c = options;
		if ((opt = getsubopt (&options, pci_drvr_opts, &value)) == -1 || value == NULL)
		{
			continue;
		}
		switch (opt)
		{
			case 0:
				verbose = strtol (value, 0, 0);
				break;

			default:
				slogf (_SLOGC_PCI, _SLOG_ERROR, "Invalid option %s", c);
				break;
			}
	}

	rval = imx6x_config (pdev);
	if (rval != EOK)
	{
		free (pdev);
		*handle = NULL;
	}
	else
	{
		*handle = pdev;
	}
	return (rval);
}


__SRCVERSION( "$URL$ $Rev$" )


