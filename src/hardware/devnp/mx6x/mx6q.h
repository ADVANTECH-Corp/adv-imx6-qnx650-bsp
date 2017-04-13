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

#ifndef NIC_MX6Q_H_INCLUDED
#define NIC_MX6Q_H_INCLUDED

// Freescale i.mx 6Q ENET 

#include <io-pkt/iopkt_driver.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include <sys/syslog.h>
#include <sys/slogcodes.h>
#include <sys/cache.h>
#include <sys/param.h>
#include <sys/syspage.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/io-pkt.h>
#include <sys/callout.h>
#include <sys/mbuf.h>
#include <sys/ioctl.h>

#include <drvr/mdi.h>
#include <drvr/eth.h>
#include <drvr/nicsupport.h>
#include <drvr/common.h>

#include <hw/nicinfo.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_ether.h>
#include <net/if_types.h>

#include <quiesce.h>
#include <siglock.h>

#include <sys/device.h>
#include <net/if_media.h>
#include <dev/mii/miivar.h>
#include <sys/hwinfo.h>
#include <drvr/hwinfo.h>

#define CONFIG_MACH_MX6Q_SABREAUTO 


#if 0
// make LOG output to console
#define log fprintf
#undef LOG_ERR
#define LOG_ERR stderr
#undef LOG_INFO
#define LOG_INFO stderr
#endif
#define NIC_PRIORITY                    21

//
// We will arbitrarily transmit a packet with
// a maximum of 32 fragments (descriptors).  
//
#define MX6Q_MAX_FRAGS			1

//
// We will arbitrarily leave a buffer of
// 4 empty tx descriptors - we will not try
// to entirely fill the tx descr ring
//
#define MX6Q_UNUSED_DESCR		4

#define MX6Q_MTU_SIZE                   1536

#define MIN_NUM_RX_DESCRIPTORS  16
#define MIN_NUM_TX_DESCRIPTORS  64

#define DEFAULT_NUM_RX_DESCRIPTORS  1024   // 1M total cost - 2k mbuf per rx descr
#define DEFAULT_NUM_TX_DESCRIPTORS  1024  // 8k total cost - cheap

#define MAX_NUM_RX_DESCRIPTORS  2048
#define MAX_NUM_TX_DESCRIPTORS  2048


#define MPC_TIMEOUT     1000

/* ENET General Control and Status Registers */
/* Control/Status Registers */

#define MX6Q_ENET_ID                             (0x0000)
#define MX6Q_IEVENT                             (0x0004 >> 2)
        #define IEVENT_RSVR                     (1 << 31)
        #define IEVENT_BABR                     (1 << 30)
        #define IEVENT_BABT                     (1 << 29)
        #define IEVENT_GRA                      (1 << 28)
        #define IEVENT_TFINT            (1 << 27)
        #define IEVENT_TXB                      (1 << 26)
        #define IEVENT_RFINT            (1 << 25)
        #define IEVENT_RXB                      (1 << 24)
        #define IEVENT_MII                      (1 << 23)
        #define IEVENT_EBERR             (1 << 22)
        #define IEVENT_LATE_COL          (1 << 21)
        #define IEVENT_COL_RET_LIM       (1 << 20)
        #define IEVENT_XFIFO_UN          (1 << 19)
        #define IEVENT_PLR               (1 << 18)
        #define IEVENT_WAKEUP            (1 << 17)
		#define IEVENT_TS_AVAIL      (1 << 16)
		#define IEVENT_TS_TIMER      (1 << 15)
		#define IEVENT_RSRVD1        (1 << 14)


#define MX6Q_IMASK                              (0x0008 >> 2)   /* Interrupt Mask Register */
        #define IMASK_HBEEN                     (1 << 31)
        #define IMASK_BREN                      (1 << 30)
        #define IMASK_BTEN                      (1 << 29)
        #define IMASK_GRAEN                     (1 << 28)
        #define IMASK_TFIEN                     (1 << 27)
        #define IMASK_TBIEN                     (1 << 26)
        #define IMASK_RFIEN                     (1 << 25)
        #define IMASK_RBIEN                     (1 << 24)
        #define IMASK_MIIEN                     (1 << 23)
        #define IMASK_EBERREN           (1 << 22)
        #define IMASK_LCEN                      (1 << 21)
        #define IMASK_CRLEN                     (1 << 20)
        #define IMASK_XFUNEN            (1 << 19)
        #define IMASK_XFERREN           (1 << 18)
        #define IMASK_RFERREN           (1 << 17)
        #define IMASK_TS_AVAIL          (1 << 16)
        #define IMASK_TS_TIMER          (1 << 15)

#define MX6Q_R_DES_ACTIVE               (0x0010 >> 2)
        #define R_DES_ACTIVE            (1 << 24)

#define MX6Q_X_DES_ACTIVE               (0x0014 >> 2)
        #define X_DES_ACTIVE            (1 << 24)

#define MX6Q_ECNTRL                             (0x0024 >> 2)
		#define ECNTRL_DBSWP			(1 << 8)
		#define ECNTRL_DBG_EN			(1 << 6)
		#define ECNTRL_ETH_SPEED        (1 << 5)
		#define ECNTRL_ENA_1588         (1 << 4)
		#define ECNTRL_SLEEP            (1 << 3)
        #define ECNTRL_ENET_OE          (1 << 2)
        #define ECNTRL_ETHER_EN         (1 << 1)
        #define ECNTRL_RESET            (1 << 0)

#define MX6Q_MII_DATA                   (0x0040 >> 2)
		#define MAC_MMFR_ST             (1 << 30)
		#define MAC_MMFR_OP             (1 << 28)
		#define MAC_MMFR_PA             (1 << 23)
		#define MAC_MMFR_RA				(1 << 18)
		#define MAC_MMFR_TA             (1 << 16)
		#define MAC_MMFR_DATA			(1 << 0)

#define MX6Q_MII_SPEED                  (0x0044 >> 2)
		#define DIS_RSRVD0              (1 << 11)
		#define DIS_HOLDTIME			(1 << 8)
        #define DIS_PREAMBLE            (1 << 7)
		#define DIS_SPEED               (1 << 1)
		#define DIS_RSRVD1              (1 << 0)

#define MX6Q_MIB_CONTROL                        (0x0064 >> 2)
        #define MIB_DISABLE                     (1 << 31)
        #define MIB_IDLE                        (1 << 30)
		#define MIB_CLEAR						(1 << 29)

#define MX6Q_R_CNTRL                            (0x0084 >> 2)
		#define RCNTRL_GRS					    (1 << 31)
		#define RCNTRL_NO_LGTH_CHECK            (1 << 30)
		#define RCNTRL_MAX_FL                   (1 << 16)
		#define RCNTRL_FRM_ENA					(1 << 15)
		#define RCNTRL_CRC_FWD					(1 << 14)
		#define RCNTRL_PAUSE_FWD				(1 << 13)
		#define RCNTRL_PAD_EN					(1 << 12)
		#define RCNTRL_RMII_ECHO				(1 << 11)
		#define RCNTRL_RMII_LOOP				(1 << 10)
		#define RCNTRL_RMII_10T					(1 << 9)
		#define RCNTRL_RMII_MODE				(1 << 8)
		#define RCNTRL_SGMII_ENA				(1 << 7)
		#define RCNTRL_RGMII_ENA                (1 << 6)
        #define RCNTRL_FCE                      (1 << 5)
        #define RCNTRL_BC_REJ                   (1 << 4)
        #define RCNTRL_PROM                     (1 << 3)
        #define RCNTRL_MII_MODE                 (1 << 2)
        #define RCNTRL_DRT                      (1 << 1)
        #define RCNTRL_LOOP                     (1 << 0)

#define MX6Q_R_HASH                             (0x0088 >> 2)
        #define RHASH_MULTCAST          (1 << 30)

#define MX6Q_X_CNTRL                            (0x00c4 >> 2)
		#define XCNTRL_TX_CRC_FWD               (1 << 9)
		#define XCNTRL_TX_ADDR_INS              (1 << 8)
		#define XCNTRL_TX_ADDR_SEL              (1 << 5)
        #define XCNTRL_RFC_PAUSE				(1 << 4)
        #define XCNTRL_TFC_PAUSE				(1 << 3)
        #define XCNTRL_FDEN                     (1 << 2)
        #define XCNTRL_HBC                      (1 << 1)
        #define XCNTRL_GTS                      (1 << 0)

#define MX6Q_TIMER_CTRLR                        (0x0400 >> 2)
        #define MX6Q_TIMER_CTRL_SLAVE           (1 << 13)
        #define MX6Q_TIMER_CTRL_CAPTURE         (1 << 11)
        #define MX6Q_TIMER_CTRL_RESTART         (1 << 9)
        #define MX6Q_TIMER_CTRL_PINPER          (1 << 7)
        #define MX6Q_TIMER_CTRL_PEREN           (1 << 4)
        #define MX6Q_TIMER_CTRL_OFFRST          (1 << 3)
        #define MX6Q_TIMER_CTRL_OFFEN           (1 << 2)
        #define MX6Q_TIMER_CTRL_EN              (1 << 0)
#define MX6Q_TIMER_VALUER                       (0x0404 >> 2)
#define MX6Q_TIMER_OFFSETR                      (0x0408 >> 2)
#define MX6Q_TIMER_PERR                         (0x040c >> 2)
#define MX6Q_TIMER_CORR                         (0x0410 >> 2)
#define MX6Q_TIMER_INCR                         (0x0414 >> 2)
#define MX6Q_TIMER_TSTMP                        (0x0418 >> 2)
#define MX6Q_TIMER_INCR_MASK                    0x0000007f
#define MX6Q_TIMER_INCR_CORR_OFF                8
#define MX6Q_TIMER_PER1SEC                      1000000000  // Periodic evens interval(ns)

#define MX6Q_PADDR1                             (0x00e4 >> 2)
#define MX6Q_PADDR2                             (0x00e8 >> 2)
#define MX6Q_OP_PAUSE							(0x00ec >> 2)
#define MX6Q_IADDR1                             (0x0118 >> 2)
#define MX6Q_IADDR2                             (0x011c >> 2)
#define MX6Q_GADDR1                             (0x0120 >> 2)
#define MX6Q_GADDR2                             (0x0124 >> 2)
#define MX6Q_FIFO_ID                            (0x0140 >> 2)
#define MX6Q_X_WMRK                             (0x0144 >> 2)
		#define X_WMRK_STR_FWD              (1 << 8)
		#define X_WMRK_TFWR					(1 << 0)
#define MX6Q_R_BOUND                            (0x014c >> 2)
#define MX6Q_R_FSTART							(0x0150 >> 2)
#define MX6Q_R_DES_START                        (0x0180 >> 2)
#define MX6Q_X_DES_START                        (0x0184 >> 2)
#define MX6Q_R_BUFF_SIZE                        (0x0188 >> 2)
#define MX6Q_R_SECTION_FULL_ADDR				(0x0190 >> 2)
#define MX6Q_R_SECTION_EMPTY_ADDR				(0x0194 >> 2)
#define MX6Q_R_ALMOST_EMPTY_ADDR				(0x0198 >> 2)
#define MX6Q_R_ALMOST_FULL_ADDR				    (0x019c >> 2)
#define MX6Q_T_SECTION_EMPTY_ADDR				(0x01a0 >> 2)
#define MX6Q_T_ALMOST_EMPTY_ADDR				(0x01a4 >> 2)
#define MX6Q_T_ALMOST_FULL_ADDR					(0x01a8 >> 2)
#define MX6Q_IPG_LENGTH_ADDR				    (0x01ac >> 2)
#define MX6Q_TRUNC_FL_ADDR						(0x01b0 >> 2)
#define MX6Q_IPACCTXCONF_ADDR				    (0x01c0 >> 2)
#define MX6Q_IPACCRXCONF_ADDR				    (0x01c4 >> 2)

#define MX6Q_DMA_CONTROL                        (0x01f4 >> 2)
        #define DMA_DATA_BO                     (1 << 31)
        #define DMA_DESC_BO                     (1 << 30)

/* MIB Block Counters */

#define MX6Q_RMON_T_DROP                        (0x0200 >> 2)
#define MX6Q_RMON_T_PACKETS             (0x0204 >> 2)
#define MX6Q_RMON_T_BC_PKT              (0x0208 >> 2)
#define MX6Q_RMON_T_MC_PKT              (0x020c >> 2)
#define MX6Q_RMON_T_CRC_ALIGN   (0x0210 >> 2)
#define MX6Q_RMON_T_UNDERSIZE   (0x0214 >> 2)
#define MX6Q_RMON_T_OVERSIZE            (0x0218 >> 2)
#define MX6Q_RMON_T_FRAG                        (0x021c >> 2)
#define MX6Q_RMON_T_JAB                 (0x0220 >> 2)
#define MX6Q_RMON_T_COL                 (0x0224 >> 2)
#define MX6Q_RMON_T_P64                 (0x0228 >> 2)
#define MX6Q_RMON_T_P65TO127            (0x022c >> 2)
#define MX6Q_RMON_T_P128TO255   (0x0230 >> 2)
#define MX6Q_RMON_T_P256TO511   (0x0234 >> 2)
#define MX6Q_RMON_T_P512TO1023  (0x0238 >> 2)
#define MX6Q_RMON_T_P1024TO2047 (0x023c >> 2)
#define MX6Q_RMON_T_P_GTE2048   (0x0240 >> 2)
#define MX6Q_RMON_T_OCTETS              (0x0244 >> 2)
#define MX6Q_IEEE_T_DROP                        (0x0248 >> 2)
#define MX6Q_IEEE_T_FRAME_OK            (0x024c >> 2)
#define MX6Q_IEEE_T_1COL                        (0x0250 >> 2)
#define MX6Q_IEEE_T_MCOL                        (0x0254 >> 2)
#define MX6Q_IEEE_T_DEF                 (0x0258 >> 2)
#define MX6Q_IEEE_T_LCOL                        (0x025c >> 2)
#define MX6Q_IEEE_T_EXCOL               (0x0260 >> 2)
#define MX6Q_IEEE_T_MACERR              (0x0264 >> 2)
#define MX6Q_IEEE_T_CSERR               (0x0268 >> 2)
#define MX6Q_IEEE_T_SQE                 (0x026c >> 2)
#define MX6Q_T_FDXFC                            (0x0270 >> 2)
#define MX6Q_IEEE_T_OCTETS_OK   (0x0274 >> 2)
#define MX6Q_RMON_R_DROP                        (0x0280 >> 2)
#define MX6Q_RMON_R_PACKETS             (0x0284 >> 2)
#define MX6Q_RMON_R_BC_PKT              (0x0288 >> 2)
#define MX6Q_RMON_R_MC_PKT              (0x028c >> 2)
#define MX6Q_RMON_R_CRC_ALIGN   (0x0290 >> 2)
#define MX6Q_RMON_R_UNDERSIZE   (0x0294 >> 2)
#define MX6Q_RMON_R_OVERSIZE            (0x0298 >> 2)
#define MX6Q_RMON_R_FRAG                        (0x029c >> 2)
#define MX6Q_RMON_R_JAB                 (0x02a0 >> 2)
#define MX6Q_RMON_R_P64                 (0x02a8 >> 2)
#define MX6Q_RMON_R_P65TO127            (0x02ac >> 2)
#define MX6Q_RMON_R_P128TO255   (0x02b0 >> 2)
#define MX6Q_RMON_R_P256TO511   (0x02b4 >> 2)
#define MX6Q_RMON_R_P512TO1023  (0x02b8 >> 2)
#define MX6Q_RMON_R_P1024TO2047 (0x02bc >> 2)
#define MX6Q_RMON_R_P_GTE2048   (0x02c0 >> 2)
#define MX6Q_RMON_R_OCTETS              (0x02c4 >> 2)
#define MX6Q_IEEE_R_DROP                        (0x02c8 >> 2)
#define MX6Q_IEEE_R_FRAME_OK            (0x02cc >> 2)
#define MX6Q_IEEE_R_CRC                 (0x02d0 >> 2)
#define MX6Q_IEEE_R_ALIGN               (0x02d4 >> 2)
#define MX6Q_IEEE_R_MACERR              (0x02d8 >> 2)
#define MX6Q_R_FDXFC                            (0x02dc >> 2)
#define MX6Q_IEEE_OCTETS_OK             (0x02e0 >> 2)

/* MIIGSK Registers */

#define MX6Q_MIIGSK_CFGR 				(0x300 >> 2)
	#define MX6Q_MIIGSK_CFGR_IF_MODE			(0x1 << 0)

#define MX6Q_MIIGSK_ENR					(0x308 >> 2)
	#define MX6Q_MIIGSK_ENR_EN			(1 << 1)

/* FIFO RAM */

#define MX6Q_TX_FIFO                            (0x0400 >> 2)

/* Transmit/receive buffer descriptor */

typedef struct {
    uint16_t   length;             // Data length
    uint16_t   status;             // Status field
    uint32_t   buffer;             // Data buffer
    uint32_t   estatus;            // Enhanced status
    uint16_t   payload_chksum;     // Payload checksum
    uint8_t    ptype;              // Protocol type
    uint8_t    header_length;      // Header length
    uint32_t   bdu;                // BDU field
    uint32_t   timestamp;          // Timestamp of the frame
    uint16_t   reserved[4];
} __attribute__((__packed__)) mpc_bd_t;

#define TXBD_R                          (1 << 15)               /* Ready */
#define TXBD_TO1                        (1 << 14)               /* Transmit Ownership */
#define TXBD_W                          (1 << 13)               /* Wrap */
#define TXBD_TO2                        (1 << 12)               /* Transmit Ownership */
#define TXBD_L                          (1 << 11)               /* Last */
#define TXBD_TC                         (1 << 10)               /* Tx CRC */
#define TXBD_ABC                        (1 << 9)                /* Append bad CRC */

#define RXBD_E                          (1 << 15)               /* Empty */
#define RXBD_RO1                        (1 << 14)               /* Receive software ownership bit */
#define RXBD_W                          (1 << 13)               /* Wrap */
#define RXBD_RO2                        (1 << 12)               /* Receive Ownership */
#define RXBD_L                          (1 << 11)               /* Last in frame */
#define RXBD_M                          (1 << 8)                /* Miss */
#define RXBD_BC                         (1 << 7)                /* Broadcast */
#define RXBD_MC                         (1 << 6)                /* Multicast */
#define RXBD_LG                         (1 << 5)                /* Rx frame length violation */
#define RXBD_NO                         (1 << 4)                /* Rx non-octet aligned frame */
#define RXBD_SH                         (1 << 3)                /* Short frame */
#define RXBD_CR                         (1 << 2)                /* Rx CRC error */
#define RXBD_OV                         (1 << 1)                /* Overrun */
#define RXBD_TR                         (1 << 0)                /* Truncation */
#define RXBD_ERR                        (RXBD_TR | RXBD_OV | RXBD_CR | RXBD_SH | RXBD_NO | RXBD_LG)

#define RXBD_ESTATUS_ME             (1 << 31)           /* MAC Error */
#define RXBD_ESTATUS_PE             (1 << 26)           /* PHY Error */
#define RXBD_ESTATUS_CE             (1 << 25)           /* Collision detected */
#define RXBD_ESTATUS_UC             (1 << 24)           /* Unicast frame */
#define RXBD_ESTATUS_INT            (1 << 23)           /* Generate RXB/RXF interrupt */
#define RXBD_ESTATUS_ICE            (1 << 5)            /* IP header checksum error */
#define RXBD_ESTATUS_PCR            (1 << 4)            /* Protocol checksum error */
#define RXBD_ESTATUS_VLAN           (1 << 2)            /* Frame has a VLAN tag */
#define RXBD_ESTATUS_IPV6           (1 << 1)            /* Frame has a IPv6 frame type */
#define RXBD_ESTATUS_FRAG           (1 << 0)            /* Frame is an IPv4 fragment frame */

#define TXBD_ESTATUS_INT            (1 << 30)           /* Generate interrupt */
#define TXBD_ESTATUS_TS             (1 << 29)           /* Generate timestamp frame */
#define TXBD_ESTATUS_PINS           (1 << 28)           /* Insert protocol checksum */
#define TXBD_ESTATUS_IINS           (1 << 27)           /* Insert IP header checksum */
#define TXBD_ESTATUS_TXE            (1 << 15)           /* Transmit error occured */
#define TXBD_ESTATUS_UE             (1 << 13)           /* Underflow error */
#define TXBD_ESTATUS_EE             (1 << 12)           /* Excess Collision error */
#define TXBD_ESTATUS_FE             (1 << 11)           /* Frame with error */
#define TXBD_ESTATUS_LCE            (1 << 10)           /* Late collision error */
#define TXBD_ESTATUS_OE             (1 << 9)            /* Overflow error */
#define TXBD_ESTATUS_TSE            (1 << 8)            /* Timestamp error */

#define BD_BDU				(1 << 31)		/* Buffer Descriptor Update done */

#define NEXT_TX(x)              ((x + 1) % mx6q->num_tx_descriptors)
#define NEXT_RX(x)              ((x + 1) % mx6q->num_rx_descriptors)
#define PREV_TX(x)              ((x == 0) ? mx6q->num_tx_descriptors - 1 : x - 1)



#include <_pack1.h>

#include <_packpop.h>

typedef struct {
    uint32_t sec;    // ptp clock seconds
    uint32_t nsec;   // ptp clock nanoseconds
} mx6q_ptp_time_t;

typedef struct {
    uint8_t          sport_identity[10];    // Clock identity
    uint16_t         sequence_id;           // Sequence identifier
    mx6q_ptp_time_t  timestamp;             // Timestamp of the message
} mx6q_ptp_timestamp_t;

/*
 * PTP external timestamp structure. Used for the exchange
 * of timestamps between driver and the application
 */
typedef struct {
    uint8_t          msg_type;               // Type of the PTP message
    uint8_t          sport_identity[10];     // Clock identity
    uint16_t         sequence_id;            // Sequence identifier
    mx6q_ptp_time_t  ts;                     // Timestamp
} mx6q_ptp_extts_t;

typedef struct {
    uint32_t  offset;                       // Correction offset
    bool      ops;                          // Offset direction (slower, faster)
} mx6q_ptp_comp_t;

typedef struct {
    uint8_t  messageId;
    uint8_t  version;
    uint16_t messageLength;
    uint8_t  domainNumber;
    uint8_t  reserved1;
    uint16_t flags;
    uint64_t correctionField;
    uint32_t reserved2;
    uint8_t  sourcePortIdentity[10];
    uint16_t sequenceId;
    uint8_t  control;
    uint8_t  logMeanMessageInterval;
}  __attribute__((__packed__)) ptpv2hdr_t;

#define PTP_GET_RX_TIMESTAMP        0x100   // Get timestamp of received packet
#define PTP_GET_TX_TIMESTAMP        0x101   // Get timestamp of transmitted packet
#define PTP_GET_TIME                0x102   // Get current time
#define PTP_SET_TIME                0x103   // Set current time
#define PTP_SET_COMPENSATION        0x104   // Set the clock compensation
#define PTP_GET_COMPENSATION        0x105   // Get orign compensation

#define MX6Q_TX_TIMESTAMP_BUF_SZ    16      // Amount of timestamps of transmitted packets
#define MX6Q_RX_TIMESTAMP_BUF_SZ    64      // Amount of timestamps of received packets

typedef struct _nic_mx6q_ext {
	struct device			dev;
	struct ethercom         	ecom;

	struct _iopkt_inter     	inter;
	int							iid;
	void                        *sdhook;
	void                        *isrp;

	struct _iopkt_inter     	inter_err;
	int							iid_err;
	void                        *isrp_err;

	nic_config_t				cfg;
	nic_stats_t     			stats;
	nic_stats_t				old_stats;

	struct _iopkt_self			*iopkt;

	//
	// rx
	//
	int							rxd_pkts; 	
	int							num_rx_descriptors;
    mpc_bd_t        			*rx_bd;
    int							rx_cidx;
	struct mbuf					**rx_pkts;
	struct mbuf					*rx_inprog_head;		//  for dealing with
	struct mbuf					*rx_inprog_tail;        //  multi-descriptor 
	int 						rx_inprog_subtotal_len;	//  jumbo packets
	int							rx_cap_mask;			// hardware checksumming


	//
	// cmd line args and state variables
	//
	int							rx_delay;				// interrupt coalescing
	int							rx_frame;				// interrupt coalescing
	int							kermask;
	uint32_t        			flowctl_flag;
	uint32_t        			loopback;
    uint32_t        			fifo;                   // FIFO start level
    uint32_t        			fifo_starve;            // FIFO enter starve mode level
    uint32_t        			fifo_starve_shutoff;    // FIFO stop starve mode level
    uint32_t    				use_syspage;            // Whether or not the system page should
                                			           
                                            			// information.
	int							dying;
#ifndef NDEBUG
	int							spurious;
#endif
    uintptr_t     				iobase;
    uintptr_t       			phy_base;
    uint32_t        			*reg;
    uint32_t        			*phy_reg;

	//
	// mii
	//
    int                         phy_incr;
	struct callout          	mii_callout;
	struct callout          	tx_callout;
	int                     phy_addr;
	int							force_advertise;
	mdi_t						*mdi;
    uint32_t        			probe_phy;  
	struct mii_data				bsd_mii;        		// for media devctls
	int                     flow;

	struct cache_ctrl	cachectl;
	//
	// tx variables - hopefully a cache line or two away from
	// the rx variables above
	//
	int							num_tx_descriptors;		// size of tx ring
    mpc_bd_t 					*tx_bd;					// ptr to mmap tx descr
    int             			tx_descr_inuse;			// number of filled descr
    int    					    tx_pidx;				// descr producer index
    int             			tx_cidx;				// descr consumer index
	struct mbuf					**tx_pkts;				// array of mbufs for each descr
	uint32_t					tx_fcb_phys;			// physical address of fcb array
	int							on_83xx;				// are we on 83xx hardware?
	int							enable_tx_hw_csum;		// be default, do not perform tx hw csum - its slower
	int							tx_cap_mask;			// hardware checksumming
	int							tx_reaped;  			// flag for periodic descr ring cleaning

	/* This buffers are used to store TX/RX
	 * timestamps for PTP Event messages
	 */
	mx6q_ptp_timestamp_t        *tx_sync_ts;
	mx6q_ptp_timestamp_t        *tx_delayreq_ts;
	mx6q_ptp_timestamp_t        *tx_pdelayresp_ts;
	mx6q_ptp_timestamp_t        *tx_pdelayreq_ts;
    int                         tx_sync_cnt;
    int                         tx_delayreq_cnt;
    int                         tx_pdelayresp_cnt;
    int                         tx_pdelayreq_cnt;

	mx6q_ptp_timestamp_t        *rx_sync_ts;
	mx6q_ptp_timestamp_t        *rx_delayreq_ts;
	mx6q_ptp_timestamp_t        *rx_pdelayresp_ts;
	mx6q_ptp_timestamp_t        *rx_pdelayreq_ts;
    int                         rx_sync_cnt;
    int                         rx_delayreq_cnt;
    int                         rx_pdelayresp_cnt;
    int                         rx_pdelayreq_cnt;
    // Real Time Clock value (sec.)
    uint32_t                    rtc;
} mx6q_dev_t;

// event.c
const struct sigevent * mx6q_isr_rx(void *, int);
const struct sigevent * mx6q_isr_rx_kermask(void *, int);
const struct sigevent * mx6q_isr_err_kermask(void *, int);
int mx6q_enable_rx(void *arg);
int mx6q_enable_rx_kermask(void *arg);
int mx6q_enable_err_kermask(void *arg);
int mx6q_process_interrupt(void *arg, struct nw_work_thread *);

// multicast.c
void mx6q_set_multicast(mx6q_dev_t *);

// detect.c
int mx6q_detect(void *dll_hdl, struct _iopkt_self *iopkt, char *options);
void mx6q_speeduplex(mx6q_dev_t *);

/* devctl.c */
int mx6q_ioctl(struct ifnet *, unsigned long, caddr_t);

// transmit.c
void mx6q_start(struct ifnet *);
void mx6q_transmit_complete(mx6q_dev_t *);

// receive.c
void mx6q_receive(mx6q_dev_t *, struct nw_work_thread *);

// mii.c
void mx6q_MDI_MonitorPhy(void *);
void mx6q_init_phy(mx6q_dev_t *);
int mx6_sabrelite_get_phy_addr(mx6q_dev_t *);
int mx6_sabreauto_rework(mx6q_dev_t *mx6q);
int mx6_sabrelite_phy_init(mx6q_dev_t *mx6q);
uint16_t mx6q_mii_read (void *handle, uint8_t phy_add, uint8_t reg_add);
void mx6q_mii_write (void *handle, uint8_t phy_add, uint8_t reg_add, uint16_t data);



// stats.c
void mx6q_update_stats(mx6q_dev_t *);
void mx6q_clear_stats(mx6q_dev_t *);

// bsd_media.c
void bsd_mii_initmedia(mx6q_dev_t *);

// ptp.c
int mx6q_ptp_start(mx6q_dev_t *);
void mx6q_ptp_stop(mx6q_dev_t *);
int mx6q_ptp_is_eventmsg(struct mbuf *, ptpv2hdr_t **);
void mx6q_ptp_add_rx_timestamp(mx6q_dev_t *, ptpv2hdr_t *, mpc_bd_t *);
void mx6q_ptp_add_tx_timestamp(mx6q_dev_t *, ptpv2hdr_t *, mpc_bd_t *);
int mx6q_ptp_get_rx_timestamp(mx6q_dev_t *, mx6q_ptp_extts_t *);
int mx6q_ptp_get_tx_timestamp(mx6q_dev_t *, mx6q_ptp_extts_t *);
int mx6q_ptp_ioctl(mx6q_dev_t *, struct ifdrv *);
void mx6q_ptp_get_cnt(mx6q_dev_t *, mx6q_ptp_time_t *);
void mx6q_ptp_set_cnt(mx6q_dev_t *, mx6q_ptp_time_t *);
void mx6q_ptp_set_compensation(mx6q_dev_t *, mx6q_ptp_comp_t *);

#endif

__SRCVERSION( "$URL$ $REV$" )
