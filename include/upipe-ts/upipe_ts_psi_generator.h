/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module generating PSI tables
 */

#ifndef _UPIPE_TS_UPIPE_TS_PSI_GENERATOR_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PSI_GENERATOR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_mux.h>

#define UPIPE_TS_PSIG_SIGNATURE UBASE_FOURCC('t','P','g',' ')
#define UPIPE_TS_PSIG_PROGRAM_SIGNATURE UBASE_FOURCC('t','P','g','p')
#define UPIPE_TS_PSIG_FLOW_SIGNATURE UBASE_FOURCC('t','P','g','f')

/** @This extends upipe_command with specific commands for ts_psig_program. */
enum upipe_ts_psig_program_command {
    UPIPE_TS_PSIG_PROGRAM_SENTINEL = UPIPE_TS_MUX_PSIG_PROGRAM,

    /** returns the current PCR PID (unsigned int *) */
    UPIPE_TS_PSIG_PROGRAM_GET_PCR_PID,
    /** sets the PCR PID (unsigned int) */
    UPIPE_TS_PSIG_PROGRAM_SET_PCR_PID
};

/** @This returns the current PCR PID.
 *
 * @param upipe description structure of the pipe
 * @param pcr_pid_p filled in with the pcr_pid
 * @return false in case of error
 */
static inline bool upipe_ts_psig_program_get_pcr_pid(struct upipe *upipe,
                                                     unsigned int *pcr_pid_p)
{
    return upipe_control(upipe, UPIPE_TS_PSIG_PROGRAM_GET_PCR_PID,
                         UPIPE_TS_PSIG_PROGRAM_SIGNATURE, pcr_pid_p);
}

/** @This sets the PCR PID.
 *
 * @param upipe description structure of the pipe
 * @param pcr_pid pcr_pid
 * @return false in case of error
 */
static inline bool upipe_ts_psig_program_set_pcr_pid(struct upipe *upipe,
                                                     unsigned int pcr_pid)
{
    return upipe_control(upipe, UPIPE_TS_PSIG_PROGRAM_SET_PCR_PID,
                         UPIPE_TS_PSIG_PROGRAM_SIGNATURE, pcr_pid);
}

/** @This extends upipe_command with specific commands for ts_psig. */
enum upipe_ts_psig_command {
    UPIPE_TS_PSIG_SENTINEL = UPIPE_TS_MUX_PSIG,

    /** prepares the next PSI sections for the given date (uint64_t) */
    UPIPE_TS_PSIG_PREPARE
};

/** @This prepares the next PSI sections for the given date.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys current muxing date
 * @return an error code
 */
static inline int upipe_ts_psig_prepare(struct upipe *upipe, uint64_t cr_sys)
{
    return upipe_control_nodbg(upipe, UPIPE_TS_PSIG_PREPARE,
                               UPIPE_TS_PSIG_SIGNATURE, cr_sys);
}

/** @This returns the management structure for all ts_psig pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psig_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
