/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module setting arbitrary attributes to urefs
 */

#ifndef _UPIPE_MODULES_UPIPE_SETRAP_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SETRAP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_SETRAP_SIGNATURE UBASE_FOURCC('s','r','a','p')

/** @This extends upipe_command with specific commands for setrap pipes. */
enum upipe_setrap_command {
    UPIPE_SETRAP_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current rap being set into urefs (struct uref **) */
    UPIPE_SETRAP_GET_RAP,
    /** sets the rap to set into urefs (struct uref *) */
    UPIPE_SETRAP_SET_RAP
};

/** @This returns the management structure for all setrap pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_setrap_mgr_alloc(void);

/** @This returns the current rap_sys being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param rap_sys_p filled with the current rap_sys
 * @return an error code
 */
static inline int upipe_setrap_get_rap(struct upipe *upipe,
                                       uint64_t *rap_sys_p)
{
    return upipe_control(upipe, UPIPE_SETRAP_GET_RAP,
                         UPIPE_SETRAP_SIGNATURE, rap_sys_p);
}

/** @This sets the rap_sys to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param rap_sys rap_sys to set
 * @return an error code
 */
static inline int upipe_setrap_set_rap(struct upipe *upipe,
                                       uint64_t rap_sys)
{
    return upipe_control(upipe, UPIPE_SETRAP_SET_RAP,
                         UPIPE_SETRAP_SIGNATURE, rap_sys);
}

#ifdef __cplusplus
}
#endif
#endif
