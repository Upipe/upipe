/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module to aggregate complete TS packets up to specified MTU
 */

#ifndef _UPIPE_TS_UPIPE_TS_AGGREGATE_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_AGGREGATE_H_

#include <upipe/upipe.h>

#define UPIPE_TS_AGGREGATE_SIGNATURE UBASE_FOURCC('t','s','a','g')

/** @This extends upipe_command with specific commands for ts check. */
enum upipe_ts_aggregate_command {
    UPIPE_TS_AGGREGATE_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the configured mtu of TS packets (int *) */
    UPIPE_TS_AGGREGATE_GET_MTU,
    /** sets the configured mtu of TS packets (int) */
    UPIPE_TS_AGGREGATE_SET_MTU
};

/** @This returns the management structure for all ts_aggregate pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_aggregate_mgr_alloc(void);

/** @This returns the configured mtu of TS packets.
 *
 * @param upipe description structure of the pipe
 * @param mtu_p filled in with the configured mtu, in octets
 * @return false in case of error
 */
static inline bool upipe_ts_aggregate_get_mtu(struct upipe *upipe, int *mtu_p)
{
    return upipe_control(upipe, UPIPE_TS_AGGREGATE_GET_MTU,
                         UPIPE_TS_AGGREGATE_SIGNATURE, mtu_p);
}

/** @This sets the configured mtu of TS packets.
 * @param upipe description structure of the pipe
 * @param mtu configured mtu, in octets
 * @return false in case of error
 */
static inline bool upipe_ts_aggregate_set_mtu(struct upipe *upipe, int mtu)
{
    return upipe_control(upipe, UPIPE_TS_AGGREGATE_SET_MTU,
                         UPIPE_TS_AGGREGATE_SIGNATURE, mtu);
}

#endif
