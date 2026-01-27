/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module facilitating trick play operations
 */

#ifndef _UPIPE_MODULES_UPIPE_TRICKPLAY_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_TRICKPLAY_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TRICKP_SIGNATURE UBASE_FOURCC('t','r','c','k')
#define UPIPE_TRICKP_SUB_SIGNATURE UBASE_FOURCC('t','r','c','s')

/** @This extends upipe_command with specific commands for trickp pipes. */
enum upipe_trickp_command {
    UPIPE_TRICKP_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current playing rate (struct urational *) */
    UPIPE_TRICKP_GET_RATE,
    /** sets the playing rate (struct urational) */
    UPIPE_TRICKP_SET_RATE
};

/** @This returns the management structure for all trickp pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_trickp_mgr_alloc(void);

/** @This returns the current playing rate.
 *
 * @param upipe description structure of the pipe
 * @param rate_p filled with the current rate
 * @return an error code
 */
static inline int upipe_trickp_get_rate(struct upipe *upipe,
                                        struct urational *rate_p)
{
    return upipe_control(upipe, UPIPE_TRICKP_GET_RATE,
                         UPIPE_TRICKP_SIGNATURE, rate_p);
}

/** @This sets the playing rate.
 *
 * @param upipe description structure of the pipe
 * @param rate new rate (1/1 = normal play, 0 = pause)
 * @return an error code
 */
static inline int upipe_trickp_set_rate(struct upipe *upipe,
                                        struct urational rate)
{
    return upipe_control(upipe, UPIPE_TRICKP_SET_RATE,
                         UPIPE_TRICKP_SIGNATURE, rate);
}

#ifdef __cplusplus
}
#endif
#endif
