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
 */

#ifndef _UPIPE_TS_UPIPE_TS_DEMUX_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_DEMUX_H_

#include <upipe/upipe.h>

#define UPIPE_TS_DEMUX_SIGNATURE UBASE_FOURCC('t','s','d','x')
#define UPIPE_TS_DEMUX_PROGRAM_SIGNATURE UBASE_FOURCC('t','s','d','p')
#define UPIPE_TS_DEMUX_OUTPUT_SIGNATURE UBASE_FOURCC('t','s','d','o')

/** @This extends uprobe_event with specific events for ts demux. */
enum uprobe_ts_demux_event {
    UPROBE_TS_DEMUX_SENTINEL = UPROBE_LOCAL,

    /** ts_split events begin here */
    UPROBE_TS_DEMUX_SPLIT = UPROBE_LOCAL + 0x1000,
    /** ts_patd events begin here */
    UPROBE_TS_DEMUX_PATD = UPROBE_LOCAL + 0x1100,
    /** ts_pmtd events begin here */
    UPROBE_TS_DEMUX_PMTD = UPROBE_LOCAL + 0x1200
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
    UPIPE_TS_DEMUX_SET_CONFORMANCE
};

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

/** @This returns the management structure for all ts_demux pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_demux_mgr_alloc(void);

/** @This is a list of specific commands for ts demux managers. */
enum upipe_ts_demux_mgr_command {
/** @hidden */
#define UPIPE_TS_DEMUX_MGR_GET_SET_MGR(name, NAME)                          \
    /** returns the current manager for name subpipes                       \
     * (struct upipe_mgr **) */                                             \
    UPIPE_TS_DEMUX_MGR_GET_##NAME##_MGR,                                    \
    /** sets the manager for name subpipes (struct upipe_mgr *) */          \
    UPIPE_TS_DEMUX_MGR_SET_##NAME##_MGR,

    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(null, NULL)
    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(ts_split, TS_SPLIT)
    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(ts_sync, TS_SYNC)
    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(ts_check, TS_CHECK)
    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(ts_decaps, TS_DECAPS)
    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(ts_psim, TS_PSIM)
    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(ts_psi_split, TS_PSI_SPLIT)
    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(ts_patd, TS_PATD)
    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(ts_pmtd, TS_PMTD)
    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(ts_pesd, TS_PESD)

    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(mp2vf, MP2VF)
    UPIPE_TS_DEMUX_MGR_GET_SET_MGR(h264f, H264F)
#undef UPIPE_TS_DEMUX_MGR_GET_SET_MGR
};

/** @This processes control commands on a ts_demux manager. This may only be
 * called before any pipe has been allocated.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
bool upipe_ts_demux_mgr_control_va(struct upipe_mgr *mgr,
                                   enum upipe_ts_demux_mgr_command command,
                                   va_list args);

/** @This processes control commands on a ts_demux manager. This may only be
 * called before any pipe has been allocated.
 *
 * @param mgr pointer to manager
 * @param command type of command to process, followed by optional arguments
 * @return false in case of error
 */
bool upipe_ts_demux_mgr_control(struct upipe_mgr *mgr,
                                enum upipe_ts_demux_mgr_command command, ...);

/** @hidden */
#define UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(name, NAME)                         \
/** @This returns the current manager for name subpipes.                    \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param p filled in with the name manager                                 \
 * @return false in case of error                                           \
 */                                                                         \
static inline bool                                                          \
    upipe_ts_demux_mgr_get_##name##_mgr(struct upipe_mgr *mgr,              \
                                        struct upipe_mgr *p)                \
{                                                                           \
    return upipe_ts_demux_mgr_control(mgr,                                  \
                                      UPIPE_TS_DEMUX_MGR_GET_##NAME##_MGR,  \
                                      p);                                   \
}                                                                           \
/** @This sets the manager for name subpipes.                               \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param m pointer to name manager                                         \
 * @return false in case of error                                           \
 */                                                                         \
static inline bool                                                          \
    upipe_ts_demux_mgr_set_##name##_mgr(struct upipe_mgr *mgr,              \
                                        struct upipe_mgr *m)                \
{                                                                           \
    return upipe_ts_demux_mgr_control(mgr,                                  \
                                      UPIPE_TS_DEMUX_MGR_SET_##NAME##_MGR,  \
                                      m);                                   \
}

UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(null, NULL)
UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(ts_split, TS_SPLIT)
UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(ts_sync, TS_SYNC)
UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(ts_check, TS_CHECK)
UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(ts_decaps, TS_DECAPS)
UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(ts_psim, TS_PSIM)
UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(ts_psi_split, TS_PSI_SPLIT)
UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(ts_patd, TS_PATD)
UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(ts_pmtd, TS_PMTD)
UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(ts_pesd, TS_PESD)

UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(mp2vf, MP2VF)
UPIPE_TS_DEMUX_MGR_GET_SET_MGR2(h264f, H264F)
#undef UPIPE_TS_DEMUX_MGR_GET_SET_MGR2

#endif
