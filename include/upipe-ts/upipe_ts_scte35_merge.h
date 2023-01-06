/*
 * Copyright (C) 2022 EasyTools
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe module merging the SCTE-35 events
 * Normative references:
 *  - SCTE 35 2013 (Digital Program Insertion Cueing Message for Cable)
 */

#ifndef _UPIPE_TS_UPIPE_TS_SCTE35_MERGE_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_SCTE35_MERGE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_SCTE35M_SIGNATURE UBASE_FOURCC('t','c',0xfc,'m')

/** @This extends uprobe_event with specific events for ts scte35m. */
enum uprobe_ts_scte35m_event {
    UPROBE_TS_SCTE35M_SENTINEL = UPROBE_LOCAL,

    /** the first uref, possibly NULL if created, is a modified by the second
     * (struct uref *, struct uref *) */
    UPROBE_TS_SCTE35M_CHANGED,
    /** the given uref triggers an event that expired now (struct uref *) */
    UPROBE_TS_SCTE35M_EXPIRED,
};

/** @This returns the management structure for all ts_scte35m pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte35m_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
