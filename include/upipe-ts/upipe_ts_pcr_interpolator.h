/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module reading PCR
 */

#ifndef _UPIPE_TS_UPIPE_TS_PCR_INTERPOLATOR_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PCR_INTERPOLATOR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_PCR_INTERPOLATOR_SIGNATURE UBASE_FOURCC('t','s','p','i')

/** @This extends upipe_command with specific commands. */
enum upipe_ts_pcr_interpolator_sink_command {
    UPIPE_TS_PCR_INTERPOLATOR_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the bitrate (struct urational*) **/
    UPIPE_TS_PCR_INTERPOLATOR_GET_BITRATE,
};

/** @This returns the current bitrate of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param urational filled in with the bitrate in bits per clock tick.
 * @return an error code
 */
static inline int upipe_ts_pcr_interpolator_get_bitrate(struct upipe *upipe,
                                              struct urational *urational)
{
    return upipe_control(upipe, UPIPE_TS_PCR_INTERPOLATOR_GET_BITRATE,
                          UPIPE_TS_PCR_INTERPOLATOR_SIGNATURE, urational);
}

/** @This returns the management structure for all ts_pcr_interpolator pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pcr_interpolator_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
