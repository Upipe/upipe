/*
 * Copyright (C) 2017-2018 Open Broadcast Systems Ltd
 *
 * Author: Rafaël Carré
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe source module for dvb receivers
 */

#ifndef _UPIPE_DVB_UPIPE_DVBSRC_H_
/** @hidden */
#define _UPIPE_DVB_UPIPE_DVBSRC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include <linux/dvb/frontend.h>

#define UPIPE_DVBSRC_SIGNATURE UBASE_FOURCC('d','v','b',' ')

enum upipe_dvbsrc_command {
    UPIPE_DVBSRC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** gets the frontend status (unsigned int *, struct dtv_properties *) */
    UPIPE_DVBSRC_GET_FRONTEND_STATUS,
};

static inline int upipe_dvbsrc_get_frontend_status(struct upipe *upipe,
        unsigned int *status, struct dtv_properties *props)
{
    return upipe_control(upipe, UPIPE_DVBSRC_GET_FRONTEND_STATUS,
            UPIPE_DVBSRC_SIGNATURE, status, props);
}

/** @This returns the management structure for all dvb sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dvbsrc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
