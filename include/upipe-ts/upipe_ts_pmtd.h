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
 * @short Upipe module decoding the program map table of TS streams
 */

#ifndef _UPIPE_TS_UPIPE_TS_PMTD_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PMTD_H_

#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_demux.h>

#define UPIPE_TS_PMTD_SIGNATURE UBASE_FOURCC('t','s','2','d')

/** @This extends uprobe_event with specific events for ts pmtd. */
enum uprobe_ts_pmtd_event {
    UPROBE_TS_PMTD_SENTINEL = UPROBE_TS_DEMUX_PMTD,

    /** a new PMT header was found in the given uref (struct uref *,
     * unsigned int, unsigned int, unsigned int) */
    UPROBE_TS_PMTD_HEADER,
    /** a new ES was found in the given uref (struct uref *, unsigned int,
     * unsigned int, unsigned int, unsigned int) */
    UPROBE_TS_PMTD_ADD_ES,
    /** an ES was deleted in the given uref (struct uref *, unsigned int) */
    UPROBE_TS_PMTD_DEL_ES,
};

/** @This returns the management structure for all ts_pmtd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pmtd_mgr_alloc(void);

#endif
