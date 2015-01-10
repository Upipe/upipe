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
 * @short Upipe module inserting PSI tables inside a TS stream
 */

#ifndef _UPIPE_TS_UPIPE_TS_PSI_INSERTER_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PSI_INSERTER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_TS_PSII_SIGNATURE UBASE_FOURCC('t','P','i',' ')
#define UPIPE_TS_PSII_SUB_SIGNATURE UBASE_FOURCC('t','P','i','s')
#define UPIPE_TS_PSII_INNER_SINK_SIGNATURE UBASE_FOURCC('t','P','i','S')

/** @This extends upipe_command with specific commands for ts_psii_sub. */
enum upipe_ts_psii_sub_command {
    UPIPE_TS_PSII_SUB_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current interval (uint64_t *) */
    UPIPE_TS_PSII_SUB_GET_INTERVAL,
    /** sets the interval (uint64_t) */
    UPIPE_TS_PSII_SUB_SET_INTERVAL
};

/** @This returns the current interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return false in case of error
 */
static inline bool upipe_ts_psii_sub_get_interval(struct upipe *upipe,
                                                  uint64_t *interval_p)
{
    return upipe_control(upipe, UPIPE_TS_PSII_SUB_GET_INTERVAL,
                         UPIPE_TS_PSII_SUB_SIGNATURE, interval_p);
}

/** @This sets the interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval.
 * @return false in case of error
 */
static inline bool upipe_ts_psii_sub_set_interval(struct upipe *upipe,
                                                  uint64_t interval)
{
    return upipe_control(upipe, UPIPE_TS_PSII_SUB_SET_INTERVAL,
                         UPIPE_TS_PSII_SUB_SIGNATURE, interval);
}

/** @This returns the management structure for all ts_psii pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psii_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
