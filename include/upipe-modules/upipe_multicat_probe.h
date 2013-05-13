/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe module - multicat probe
 * This linear module sends probe depending on the uref k.systime attribute.
 */

#ifndef _UPIPE_MODULES_UPIPE_MULTICAT_PROBE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_MULTICAT_PROBE_H_

#include <stdint.h>
#include <upipe/upipe.h>

#define UPIPE_MULTICAT_PROBE_SIGNATURE UBASE_FOURCC('m','p','r','b')
#define UPIPE_MULTICAT_PROBE_DEF_ROTATE UINT64_C(97200000000) // FIXME

/** @This extends upipe_command with specific commands for multicat sink. */
enum upipe_multicat_probe_command {
    UPIPE_MULTICAT_PROBE_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** get rotate interval (uint64_t *) */
    UPIPE_MULTICAT_PROBE_GET_ROTATE,
    /** change rotate interval (uint64_t) (default: UPIPE_MULTICAT_PROBE_DEF_ROTATE) */
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

/** @This returns the rotate interval (in 27Mhz unit).
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the rotate interval in 27Mhz
 * @return false in case of error
 */
static inline bool upipe_multicat_probe_get_rotate(struct upipe *upipe, uint64_t *interval_p)
{
    return upipe_control(upipe, UPIPE_MULTICAT_PROBE_GET_ROTATE, UPIPE_MULTICAT_PROBE_SIGNATURE, interval_p);
}

/** @This changes the rotate interval (in 27Mhz unit) (default: UPIPE_MULTICAT_PROBE_DEF_ROTATE).
 *
 * @param upipe description structure of the pipe
 * @param interval rotate interval in 27Mhz
 * @return false in case of error
 */
static inline bool upipe_multicat_probe_set_rotate(struct upipe *upipe, uint64_t interval)
{
    return upipe_control(upipe, UPIPE_MULTICAT_PROBE_SET_ROTATE, UPIPE_MULTICAT_PROBE_SIGNATURE, interval);
}

#endif
