/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
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
 * @short Upipe module filtering on PIDs of a transport stream
 */

#ifndef _UPIPE_TS_UPIPE_TS_PID_FILTER_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PID_FILTER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_TS_PIDF_SIGNATURE UBASE_FOURCC('t','s','p','F')

/** @This extends upipe_command with specific commands for ts pid filter. */
enum upipe_ts_pidf_command {
    UPIPE_TS_PIDF_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** adds the given PID (unsigned int) */
    UPIPE_TS_PIDF_ADD_PID,
    /** deletes the given PID (unsigned int) */
    UPIPE_TS_PIDF_DEL_PID
};

/** @This adds the given PID.
 *
 * @param upipe description structure of the pipe
 * @param pid pid to add
 * @return an error code
 */
static inline int upipe_ts_pidf_add_pid(struct upipe *upipe, uint16_t pid)
{
    return upipe_control(upipe, UPIPE_TS_PIDF_ADD_PID,
                         UPIPE_TS_PIDF_SIGNATURE, (unsigned int)pid);
}

/** @This deletes the given PID.
 *
 * @param upipe description structure of the pipe
 * @param pid pid to delete
 * @return an error code
 */
static inline int upipe_ts_pidf_del_pid(struct upipe *upipe, uint16_t pid)
{
    return upipe_control(upipe, UPIPE_TS_PIDF_DEL_PID,
                         UPIPE_TS_PIDF_SIGNATURE, (unsigned int)pid);
}

/** @This returns the management structure for all ts_pidf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pidf_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
