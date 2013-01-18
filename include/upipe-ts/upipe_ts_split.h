/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module splitting PIDs of a transport stream
 *
 * Please note the special behavior of @ref upipe_split_set_flow_def. If the
 * flow suffix doesn't exist, it creates it. If flow_def is NULL, it deletes
 * it. This function must be called before @ref upipe_split_set_output.
 */

#ifndef _UPIPE_TS_UPIPE_TS_SPLIT_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_SPLIT_H_

#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_demux.h>

#define UPIPE_TS_SPLIT_SIGNATURE UBASE_FOURCC('t','s','s','p')

/** @This extends uprobe_event with specific events for ts split. */
enum uprobe_ts_split_event {
    UPROBE_TS_SPLIT_SENTINEL = UPROBE_TS_DEMUX_SPLIT,

    /** the given PID is needed for correct operation (unsigned int) */
    UPROBE_TS_SPLIT_SET_PID,
    /** the given PID is no longer needed (unsigned int) */
    UPROBE_TS_SPLIT_UNSET_PID
};

/** @This returns the management structure for all ts_split pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_split_mgr_alloc(void);

#endif
