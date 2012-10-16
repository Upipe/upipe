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
 * @short Upipe module decapsulating (removing PES header) TS packets
 * containing PES headers
 */

#ifndef _UPIPE_TS_UPIPE_TS_PESD_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PESD_H_

#include <upipe/upipe.h>

#define UPIPE_TS_PESD_SIGNATURE 0x0F100004U

/** @This extends uprobe_event with specific events for ts pesd. */
enum uprobe_ts_pesd_event {
    UPROBE_TS_PESD_SENTINEL = UPROBE_LOCAL,

    /** the TS pesdhronization was acquired (void) */
    UPROBE_TS_PESD_ACQUIRED,
    /** the TS pesdhronization was lost (void) */
    UPROBE_TS_PESD_LOST
};

/** @This returns the management structure for all ts_pesd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pesd_mgr_alloc(void);

#endif
