/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module adding a delay to all dates
 */

#ifndef _UPIPE_MODULES_UPIPE_DELAY_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_DELAY_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/uref.h"

#define UPIPE_DELAY_SIGNATURE UBASE_FOURCC('d','l','a','y')

/** @This extends upipe_command with specific commands for delay pipes. */
enum upipe_delay_command {
    UPIPE_DELAY_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current delay being set into urefs (int64_t *) */
    UPIPE_DELAY_GET_DELAY,
    /** sets the delay to set into urefs (int64_t) */
    UPIPE_DELAY_SET_DELAY
};

/** @This returns the management structure for all delay pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_delay_mgr_alloc(void);

/** @This returns the current delay being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param delay_p filled with the current delay
 * @return an error code
 */
static inline int upipe_delay_get_delay(struct upipe *upipe, int64_t *delay_p)
{
    return upipe_control(upipe, UPIPE_DELAY_GET_DELAY,
                         UPIPE_DELAY_SIGNATURE, delay_p);
}

/** @This sets the delay to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param delay delay to set
 * @return an error code
 */
static inline int upipe_delay_set_delay(struct upipe *upipe, int64_t delay)
{
    return upipe_control(upipe, UPIPE_DELAY_SET_DELAY,
                         UPIPE_DELAY_SIGNATURE, delay);
}

#ifdef __cplusplus
}
#endif
#endif
