/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
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

#include <upipe/upipe.h>

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
