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
 * @short Upipe module encapsulating (adding TS header) PES and PSI access units
 */

#ifndef _UPIPE_TS_UPIPE_TS_ENCAPS_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_ENCAPS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_TS_ENCAPS_SIGNATURE UBASE_FOURCC('t','s','e','c')

/** @This extends upipe_command with specific commands for ts encaps. */
enum upipe_ts_encaps_command {
    UPIPE_TS_ENCAPS_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the currently configured PCR period (uint64_t *) */
    UPIPE_TS_ENCAPS_GET_PCR_PERIOD,
    /** sets the PCR period (uint64_t) */
    UPIPE_TS_ENCAPS_SET_PCR_PERIOD
};

/** @This returns the currently configured PCR period.
 *
 * @param upipe description structure of the pipe
 * @param pcr_period_p filled in with the PCR period
 * @return false in case of error
 */
static inline bool upipe_ts_encaps_get_pcr_period(struct upipe *upipe,
                                                  uint64_t *pcr_period_p)
{
    return upipe_control(upipe, UPIPE_TS_ENCAPS_GET_PCR_PERIOD,
                         UPIPE_TS_ENCAPS_SIGNATURE, pcr_period_p);
}

/** @This sets the PCR period. To cancel insertion of PCRs, set it to 0.
 *
 * @param upipe description structure of the pipe
 * @param pcr_period new PCR period
 * @return false in case of error
 */
static inline bool upipe_ts_encaps_set_pcr_period(struct upipe *upipe,
                                                  uint64_t pcr_period)
{
    return upipe_control(upipe, UPIPE_TS_ENCAPS_SET_PCR_PERIOD,
                         UPIPE_TS_ENCAPS_SIGNATURE, pcr_period);
}

/** @This returns the management structure for all ts_encaps pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_encaps_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
