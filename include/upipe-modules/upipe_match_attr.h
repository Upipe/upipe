/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module dropping urefs not matching certain values for
 * int attributes
 */

#ifndef _UPIPE_MODULES_UPIPE_MATCH_ATTR_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_MATCH_ATTR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/uref.h"

#define UPIPE_MATCH_ATTR_SIGNATURE UBASE_FOURCC('m','a','t','t')

/** @This extends upipe_command with specific commands for match_attr pipes. */
enum upipe_match_attr_command {
    UPIPE_MATCH_ATTR_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** match uint8_t attr (int (*)(uref*, uint8_t, uint8_t)) */
    UPIPE_MATCH_ATTR_SET_UINT8_T,
    /** match uint64_t attr */
    UPIPE_MATCH_ATTR_SET_UINT64_T,
    /** set boundaries (uint64_t, uint64_t) */
    UPIPE_MATCH_ATTR_SET_BOUNDARIES,
};

/** @This sets the match callback to check uint8_t attribute with.
 *
 * @param upipe description structure of the pipe
 * @param match callback
 * @return an error code
 */
static inline int upipe_match_attr_set_uint8_t(struct upipe *upipe,
                    int (*match)(struct uref*, uint8_t, uint8_t))
{
    return upipe_control(upipe, UPIPE_MATCH_ATTR_SET_UINT8_T,
                         UPIPE_MATCH_ATTR_SIGNATURE, match);
}

/** @This sets the match callback to check uint64_t attribute with.
 *
 * @param upipe description structure of the pipe
 * @param match callback
 * @return an error code
 */
static inline int upipe_match_attr_set_uint64_t(struct upipe *upipe,
                    int (*match)(struct uref*, uint64_t, uint64_t))
{
    return upipe_control(upipe, UPIPE_MATCH_ATTR_SET_UINT64_T,
                         UPIPE_MATCH_ATTR_SIGNATURE, match);
}

/** @This sets the match boundaries.
 *
 * @param upipe description structure of the pipe
 * @param min min
 * @param min max
 * @return an error code
 */
static inline int
    upipe_match_attr_set_boundaries(struct upipe *upipe,
                                    uint64_t min, uint64_t max)
{
    return upipe_control(upipe, UPIPE_MATCH_ATTR_SET_BOUNDARIES,
                         UPIPE_MATCH_ATTR_SIGNATURE, min, max);
}

/** @This returns the management structure for all match_attr pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_match_attr_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
