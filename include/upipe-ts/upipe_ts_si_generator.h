/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This service is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This service is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this service; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module generating DVB SI tables
 */

#ifndef _UPIPE_TS_UPIPE_TS_SI_GENERATOR_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_SI_GENERATOR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_mux.h>

#define UPIPE_TS_SIG_SIGNATURE UBASE_FOURCC('t','S','g',' ')
#define UPIPE_TS_SIG_SERVICE_SIGNATURE UBASE_FOURCC('t','S','g','p')
#define UPIPE_TS_SIG_OUTPUT_SIGNATURE UBASE_FOURCC('t','S','g','o')

/** @This extends upipe_command with specific commands for ts_sig. */
enum upipe_ts_sig_command {
    UPIPE_TS_SIG_SENTINEL = UPIPE_TS_MUX_SIG,

    /** prepares the next SI sections for the given date (uint64_t, uint64_t) */
    UPIPE_TS_SIG_PREPARE,
    /** returns the NIT subpipe (struct upipe **) */
    UPIPE_TS_SIG_GET_NIT_SUB,
    /** returns the SDT subpipe (struct upipe **) */
    UPIPE_TS_SIG_GET_SDT_SUB,
    /** returns the EIT subpipe (struct upipe **) */
    UPIPE_TS_SIG_GET_EIT_SUB,
    /** returns the TDT subpipe (struct upipe **) */
    UPIPE_TS_SIG_GET_TDT_SUB
};

/** @This prepares the next SI sections for the given date.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys current muxing date
 * @param latency latency before the packet is output
 * @return an error code
 */
static inline int upipe_ts_sig_prepare(struct upipe *upipe, uint64_t cr_sys,
                                      uint64_t latency)
{
    return upipe_control_nodbg(upipe, UPIPE_TS_SIG_PREPARE,
                               UPIPE_TS_SIG_SIGNATURE, cr_sys, latency);
}

/** @This returns the NIT subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the NIT subpipe
 * @return an error code
 */
static inline int upipe_ts_sig_get_nit_sub(struct upipe *upipe,
                                           struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_TS_SIG_GET_NIT_SUB,
                         UPIPE_TS_SIG_SIGNATURE, upipe_p);
}

/** @This returns the SDT subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the SDT subpipe
 * @return an error code
 */
static inline int upipe_ts_sig_get_sdt_sub(struct upipe *upipe,
                                           struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_TS_SIG_GET_SDT_SUB,
                         UPIPE_TS_SIG_SIGNATURE, upipe_p);
}

/** @This returns the EIT subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the EIT subpipe
 * @return an error code
 */
static inline int upipe_ts_sig_get_eit_sub(struct upipe *upipe,
                                           struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_TS_SIG_GET_EIT_SUB,
                         UPIPE_TS_SIG_SIGNATURE, upipe_p);
}

/** @This returns the TDT subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the TDT subpipe
 * @return an error code
 */
static inline int upipe_ts_sig_get_tdt_sub(struct upipe *upipe,
                                           struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_TS_SIG_GET_TDT_SUB,
                         UPIPE_TS_SIG_SIGNATURE, upipe_p);
}

/** @This returns the management structure for all ts_sig pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_sig_mgr_alloc(void);

/** @hidden */
#define ARGS_DECL , struct uprobe *uprobe_nit, struct uprobe *uprobe_sdt, struct uprobe *uprobe_eit, struct uprobe *uprobe_tdt
/** @hidden */
#define ARGS , uprobe_nit, uprobe_sdt, uprobe_eit, uprobe_tdt
UPIPE_HELPER_ALLOC(ts_sig, UPIPE_TS_SIG_SIGNATURE)
#undef ARGS
#undef ARGS_DECL

#ifdef __cplusplus
}
#endif
#endif
