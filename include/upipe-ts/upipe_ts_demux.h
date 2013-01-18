/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe higher-level module demuxing elementary streams of a TS
 *
 * Please note the special behavior of @ref upipe_demux_set_flow_def. If the
 * flow suffix doesn't exist, it creates it. If flow_def is NULL, it deletes
 * it. This function must be called before @ref upipe_demux_set_output.
 */

#ifndef _UPIPE_TS_UPIPE_TS_DEMUX_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_DEMUX_H_

#include <upipe/upipe.h>

#define UPIPE_TS_DEMUX_SIGNATURE UBASE_FOURCC('t','s','d','x')

/** @This extends uprobe_event with specific events for ts demux. */
enum uprobe_ts_demux_event {
    UPROBE_TS_DEMUX_SENTINEL = UPROBE_LOCAL,

    /** a new PSI flow is proposed (struct uref *, const char *) */
    UPROBE_TS_DEMUX_NEW_PSI_FLOW,

    /** ts_split events begin here */
    UPROBE_TS_DEMUX_SPLIT = 0x9000,
    /** ts_decaps events begin here */
    UPROBE_TS_DEMUX_DECAPS = 0x9100,
    /** ts_patd events begin here */
    UPROBE_TS_DEMUX_PATD = 0x9200,
    /** ts_pmtd events begin here */
    UPROBE_TS_DEMUX_PMTD = 0x9300
};

/** @This is the conformance mode of a transport stream. */
enum upipe_ts_demux_conformance {
    /** automatic conformance */
    CONFORMANCE_AUTO,
    /** no conformance, just ISO 13818-1 */
    CONFORMANCE_ISO,
    /** DVB conformance (ETSI EN 300 468) */
    CONFORMANCE_DVB,
    /** ATSC conformance */
    CONFORMANCE_ATSC,
    /** ISDB conformance */
    CONFORMANCE_ISDB
};

/** @This extends upipe_command with specific commands for ts demux. */
enum upipe_ts_demux_command {
    UPIPE_TS_DEMUX_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the currently detected conformance (int *) */
    UPIPE_TS_DEMUX_GET_CONFORMANCE,
    /** sets the conformance (int) */
    UPIPE_TS_DEMUX_SET_CONFORMANCE,
    /** sets flow definition for a PSI table (struct uref *, const char *) */
    UPIPE_TS_DEMUX_SET_PSI_FLOW_DEF
};

/** @This returns the management structure for all ts_demux pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_demux_mgr_alloc(void);

/** @This returns the currently detected conformance mode. It cannot return
 * CONFORMANCE_AUTO.
 *
 * @param upipe description structure of the pipe
 * @param conformance_p filled in with the conformance
 * @return false in case of error
 */
static inline bool upipe_ts_demux_get_conformance(struct upipe *upipe,
                                enum upipe_ts_demux_conformance *conformance_p)
{
    return upipe_control(upipe, UPIPE_TS_DEMUX_GET_CONFORMANCE,
                         UPIPE_TS_DEMUX_SIGNATURE, conformance_p);
}

/** @This sets the conformance mode.
 *
 * @param upipe description structure of the pipe
 * @param conformance conformance mode
 * @return false in case of error
 */
static inline bool upipe_ts_demux_set_conformance(struct upipe *upipe,
                                enum upipe_ts_demux_conformance conformance)
{
    return upipe_control(upipe, UPIPE_TS_DEMUX_SET_CONFORMANCE,
                         UPIPE_TS_DEMUX_SIGNATURE, conformance);
}

/** @This sets a flow definition for a PSI table. The flow definition
 * packet is typically sent by a @ref #UPROBE_TS_DEMUX_NEW_PSI_FLOW probe, sent
 * when a new PMT table is present is may be needed.
 * The flow definition packet contains a PSI section filter and a PID to
 * attach to.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @param flow_suffix flow suffix
 * @return false in case of error
 */
static inline bool upipe_ts_demux_set_psi_flow_def(struct upipe *upipe,
                                                   struct uref *flow_def,
                                                   const char *flow_suffix)
{
    return upipe_control(upipe, UPIPE_TS_DEMUX_SET_PSI_FLOW_DEF,
                         UPIPE_TS_DEMUX_SIGNATURE, flow_def, flow_suffix);
}

#endif
