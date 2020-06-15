/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This event is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This event is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this event; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module handling the splice information table of SCTE streams
 * Normative references:
 *  - SCTE 35 2013 (Digital Program Insertion Cueing Message for Cable)
 */

#ifndef _UPIPE_TS_UPIPE_TS_SCTE35_PROBE_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_SCTE35_PROBE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_TS_SCTE35P_SIGNATURE UBASE_FOURCC('s','c','t','e')

/** @This extends uprobe_event with specific events for ts scte35p. */
enum uprobe_ts_scte35p_event {
    UPROBE_TS_SCTE35P_SENTINEL = UPROBE_LOCAL,

    /** the given uref triggers an event that takes place now (struct uref *) */
    UPROBE_TS_SCTE35P_EVENT,
    /** the given uref triggers a null event (struct uref *) */
    UPROBE_TS_SCTE35P_NULL,
    /** the given uref triggers a signal that takes place now (struct uref *) */
    UPROBE_TS_SCTE35P_SIGNAL,
};

/** @This returns the management structure for all ts_scte35p pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte35p_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
