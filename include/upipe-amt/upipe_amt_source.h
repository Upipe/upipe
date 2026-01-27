/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe source module for automatic multicast tunneling
 */

#ifndef _UPIPE_AMT_UPIPE_AMT_SOURCE_H_
/** @hidden */
#define _UPIPE_AMT_UPIPE_AMT_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_AMTSRC_SIGNATURE UBASE_FOURCC('a','m','t','c')

/** @This returns the management structure for all amtsrc pipes.
 *
 * @param amt_relay IP of the AMT relay
 * @return pointer to manager
 */
struct upipe_mgr *upipe_amtsrc_mgr_alloc(const char *amt_relay);

/** @This extends upipe_mgr_command with specific commands for amtsrc. */
enum upipe_amtsrc_mgr_command {
    UPIPE_AMTSRC_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

    /** sets the timeout to switch to AMT (unsigned int) */
    UPIPE_AMTSRC_MGR_SET_TIMEOUT
};

/** @This sets the timeout to switch from SSM to AMT.
 *
 * @param mgr pointer to manager
 * @param timeout timeout in seconds
 * @return an error code
 */
static inline int
    upipe_amtsrc_mgr_set_timeout(struct upipe_mgr *mgr, unsigned int timeout)
{
    return upipe_mgr_control(mgr, UPIPE_AMTSRC_MGR_SET_TIMEOUT,
                             UPIPE_AMTSRC_SIGNATURE, timeout);
}

#ifdef __cplusplus
}
#endif
#endif
