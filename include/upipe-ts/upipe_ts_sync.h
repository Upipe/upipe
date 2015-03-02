/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe module syncing on a transport stream
 *
 * This module also accepts @ref upipe_set_output_size, with the following
 * common values:
 * @table 2
 * @item size (in octets) @item description
 * @item 188 @item standard size of TS packets according to ISO/IEC 13818-1
 * @item 196 @item TS packet followed by an 8-octet timestamp or checksum
 * @item 204 @item TS packet followed by a 16-octet checksum
 * @end table
 */

#ifndef _UPIPE_TS_UPIPE_TS_SYNC_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_SYNC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_TS_SYNC_SIGNATURE UBASE_FOURCC('t','s','s','y')

/** @This extends upipe_command with specific commands for ts sync. */
enum upipe_ts_sync_command {
    UPIPE_TS_SYNC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the configured number of packets to synchronize with (int *) */
    UPIPE_TS_SYNC_GET_SYNC,
    /** sets the configured number of packets to synchronize with (int) */
    UPIPE_TS_SYNC_SET_SYNC
};

/** @This returns the management structure for all ts_sync pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_sync_mgr_alloc(void);

/** @This returns the configured number of packets to synchronize with.
 *
 * @param upipe description structure of the pipe
 * @param sync_p filled in with number of packets
 * @return an error code
 */
static inline int upipe_ts_sync_get_sync(struct upipe *upipe, int *sync_p)
{
    return upipe_control(upipe, UPIPE_TS_SYNC_GET_SYNC, UPIPE_TS_SYNC_SIGNATURE,
                         sync_p);
}

/** @This sets the configured number of packets to synchronize with.
 * The higher the value, the slower the synchronization, but the fewer false
 * positives.
 *
 * @param upipe description structure of the pipe
 * @param sync number of packets
 * @return an error code
 */
static inline int upipe_ts_sync_set_sync(struct upipe *upipe, int sync)
{
    return upipe_control(upipe, UPIPE_TS_SYNC_SET_SYNC, UPIPE_TS_SYNC_SIGNATURE,
                         sync);
}

#ifdef __cplusplus
}
#endif
#endif
