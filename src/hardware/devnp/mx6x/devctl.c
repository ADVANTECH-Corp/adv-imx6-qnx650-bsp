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



#include "mx6q.h"

#include <net/ifdrvcom.h>
#include <sys/sockio.h>


static void
mx6q_get_stats(mx6q_dev_t *mx6q, void *data)
{
	nic_stats_t				*stats = data;

	// read nic hardware registers into mx6q data struct stats 
	mx6q_update_stats(mx6q);

	// copy it to the user buffer
	memcpy(stats, &mx6q->stats, sizeof(mx6q->stats));
}

int
mx6q_ioctl(struct ifnet *ifp, unsigned long cmd, caddr_t data)
{
	mx6q_dev_t			*mx6q = ifp->if_softc;
	int				error = 0;
	struct ifdrv_com		*ifdc;
	struct drvcom_config		*dcfgp;
	struct drvcom_stats		*dstp;


	switch (cmd) {
	case SIOCGDRVCOM:
		ifdc = (struct ifdrv_com *)data;
		switch (ifdc->ifdc_cmd) {
		case DRVCOM_CONFIG:
			dcfgp = (struct drvcom_config *)ifdc;

			if (ifdc->ifdc_len != sizeof(nic_config_t)) {
				error = EINVAL;
				break;
			}
			memcpy(&dcfgp->dcom_config, &mx6q->cfg, sizeof(mx6q->cfg));
			break;

		case DRVCOM_STATS:
			dstp = (struct drvcom_stats *)ifdc;

			if (ifdc->ifdc_len != sizeof(nic_stats_t)) {
				error = EINVAL;
				break;
			}
			mx6q_get_stats(mx6q, &dstp->dcom_stats);
			break;

		default:
			error = EOPNOTSUPP;
			break;

		}
		break;


    case SIOCSIFMEDIA:
    case SIOCGIFMEDIA: {
        struct ifreq *ifr = (struct ifreq *)data;

        error = ifmedia_ioctl(ifp, ifr, &mx6q->bsd_mii.mii_media, cmd);
        break;
        }

    case SIOCSDRVSPEC:
    case SIOCGDRVSPEC:
        {
            struct ifdrv *ifd = (struct ifdrv *)data;

            error = mx6q_ptp_ioctl(mx6q, ifd);
            break;
        }

	default:
		error = ether_ioctl(ifp, cmd, data);
		if (error == ENETRESET) {
			//
			// Multicast list has changed; set the
			// hardware filter accordingly.
			//
			if ((ifp->if_flags_tx & IFF_RUNNING) == 0) {
				//
				// interface is currently down: mx6q_init() 
				// will call mx6q_set_multicast() so
				// nothing to do
				//
			} else {
				//
				// interface is up, recalc and hit gaddrs
				//
				mx6q_set_multicast(mx6q);
			}
			error = 0;
			break;
		}
		break;
	}

	return (error);
}

__SRCVERSION( "$URL$ $REV$" )
