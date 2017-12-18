/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module decapsulating (removing TS header) TS packets
 */

#ifndef _UPIPE_TS_UPIPE_TS_DECAPS_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_DECAPS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_TS_DECAPS_SIGNATURE UBASE_FOURCC('t','s','d','c')

/** @This extends upipe_command with specific commands for upipe_ts_decaps pipes.
 */
enum upipe_ts_decaps_command {
    UPIPE_TS_DECAPS_SENTINEL = UPIPE_CONTROL_LOCAL,

    UPIPE_TS_DECAPS_GET_PACKETS_LOST, /* int sig, uint64_t * */
};

/** @This returns the number of packets presumed lost due to continuity errors
 * since the last call to this function.
 * The pipe's internal counter is reset to 0 each time this function is called.
 *
 * @param upipe description structure of the pipe
 * @param lost_p filled in with the number of packets lost
 * @return an error code
 */
static inline int upipe_ts_decaps_get_packets_lost(struct upipe *upipe,
        uint64_t *lost_p)
{
    return upipe_control(upipe, UPIPE_TS_DECAPS_GET_PACKETS_LOST,
            UPIPE_TS_DECAPS_SIGNATURE, lost_p);
}

/** @This returns the management structure for all ts_decaps pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_decaps_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
