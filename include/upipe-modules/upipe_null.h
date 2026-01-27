/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe null module - free incoming urefs
 */

#ifndef _UPIPE_MODULES_UPIPE_NULL_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_NULL_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_NULL_SIGNATURE UBASE_FOURCC('n', 'u', 'l', 'l')

/** @This extends upipe_command with specific commands for null */
enum upipe_null_command {
    UPIPE_NULL_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** ask enable or disable dumping dicts (int) */
    UPIPE_NULL_DUMP_DICT
};

/** @This enables or disables dumping of uref->udict
 *
 * @param upipe description structure of the pipe
 * @param enable enable or disable
 * @return an error code
 */
static inline int upipe_null_dump_dict(struct upipe *upipe, bool enable)
{
    return upipe_control(upipe, UPIPE_NULL_DUMP_DICT, UPIPE_NULL_SIGNATURE, (enable ? 1 : 0));
}

/** @This returns the management structure for null pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_null_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
