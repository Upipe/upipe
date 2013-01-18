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
 * @short Upipe module decoding the program association table of TS streams
 */

#ifndef _UPIPE_TS_UPIPE_TS_PATD_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PATD_H_

#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_demux.h>

#define UPIPE_TS_PATD_SIGNATURE UBASE_FOURCC('t','s','1','d')

/** @This extends uprobe_event with specific events for ts patd. */
enum uprobe_ts_patd_event {
    UPROBE_TS_PATD_SENTINEL = UPROBE_TS_DEMUX_PATD,

    /** a new TSID was detected (struct uref *, unsigned int tsid) */
    UPROBE_TS_PATD_TSID,
    /** a new program was found in the given uref (struct uref *,
     * unsigned int, unsigned int) */
    UPROBE_TS_PATD_ADD_PROGRAM,
    /** a program was deleted in the given uref (struct uref *,
     * unsigned int) */
    UPROBE_TS_PATD_DEL_PROGRAM,
};

/** @This returns the management structure for all ts_patd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_patd_mgr_alloc(void);

#endif
