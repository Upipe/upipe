/*
 * Copyright (C) 2012-2016 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module - multicat probe
 * This linear module sends probe depending on the uref k.systime attribute.
 */

#ifndef _UPIPE_MODULES_UPIPE_MULTICAT_PROBE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_MULTICAT_PROBE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "upipe/upipe.h"

#define UPIPE_MULTICAT_PROBE_SIGNATURE UBASE_FOURCC('m','p','r','b')
#define UPIPE_MULTICAT_PROBE_DEF_ROTATE UINT64_C(97200000000)
#define UPIPE_MULTICAT_PROBE_DEF_ROTATE_OFFSET UINT64_C(0)

/** @This extends upipe_command with specific commands for multicat sink. */
enum upipe_multicat_probe_command {
    UPIPE_MULTICAT_PROBE_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** get rotate interval (uint64_t *, uint64_t *) */
    UPIPE_MULTICAT_PROBE_GET_ROTATE,
    /** change rotate interval (uint64_t, uint64_t) (default: UPIPE_MULTICAT_PROBE_DEF_ROTATE) */
    UPIPE_MULTICAT_PROBE_SET_ROTATE,
};

/** @This extends uprobe_event with specific events for multicat probe. */
enum uprobe_multicat_probe_event {
    UPROBE_MULTICAT_PROBE_SENTINEL = UPROBE_LOCAL,

    /** rotate event (struct uref *uref, uint64_t index) */
    UPROBE_MULTICAT_PROBE_ROTATE
};


/** @This returns the management structure for multicat_probe pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_multicat_probe_mgr_alloc(void);

/** @This returns the rotate interval (in 27MHz unit).
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the rotate interval in 27MHz
 * @param offset_p filled in with the rotate offset in 27MHz
 * @return an error code
 */
static inline int
    upipe_multicat_probe_get_rotate(struct upipe *upipe, uint64_t *interval_p,
                                    uint64_t *offset_p)
{
    return upipe_control(upipe, UPIPE_MULTICAT_PROBE_GET_ROTATE,
                         UPIPE_MULTICAT_PROBE_SIGNATURE, interval_p, offset_p);
}

/** @This changes the rotate interval (in 27MHz unit)
 * (default: UPIPE_MULTICAT_PROBE_DEF_ROTATE).
 *
 * @param upipe description structure of the pipe
 * @param interval rotate interval in 27MHz
 * @param offset rotate offset in 27MHz
 * @return an error code
 */
static inline int
    upipe_multicat_probe_set_rotate(struct upipe *upipe, uint64_t interval,
                                    uint64_t offset)
{
    return upipe_control(upipe, UPIPE_MULTICAT_PROBE_SET_ROTATE,
                         UPIPE_MULTICAT_PROBE_SIGNATURE, interval, offset);
}

#ifdef __cplusplus
}
#endif
#endif
