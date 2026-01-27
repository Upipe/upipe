/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module decoding the program association table of TS streams
 */

#ifndef _UPIPE_TS_UPIPE_TS_PAT_DECODER_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PAT_DECODER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-ts/upipe_ts_demux.h"

#define UPIPE_TS_PATD_SIGNATURE UBASE_FOURCC('t','s','1','d')

/** @This extends upipe_command with specific commands for ts patd. */
enum upipe_ts_patd_command {
    UPIPE_TS_PATD_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the flow definition of the NIT (struct uref **) */
    UPIPE_TS_PATD_GET_NIT
};

/** @This returns the flow definition of the NIT.
 *
 * @param upipe description structure of the pipe
 * @param flow_def_p filled in with the flow definition
 * @return false in case of error
 */
static inline bool upipe_ts_patd_get_nit(struct upipe *upipe,
                                         struct uref **flow_def_p)
{
    return upipe_control(upipe, UPIPE_TS_PATD_GET_NIT,
                         UPIPE_TS_PATD_SIGNATURE, flow_def_p);
}

/** @This returns the management structure for all ts_patd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_patd_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
