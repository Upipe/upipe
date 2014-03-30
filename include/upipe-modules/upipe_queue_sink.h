/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe sink module for queues
 *
 * This pipe must be inited with @ref upipe_qsink_set_qsrc before accepting
 * packets. The qsrc may no longer be changed afterwards. The pipe also
 * requires a upump manager.
 *
 * This pipe also handles the @ref upipe_set_output and @ref upipe_get_output
 * calls, though the output is only directed towards the qsrc. The purpose
 * of this pseudo-output is to be automatically released when the qsink dies.
 * Technically the pseudo-output doesn't get any input.
 *
 * The typical use case is when using @ref upipe_xfer_alloc. The qsrc has to
 * be the original structure allocated by @ref upipe_qsrc_alloc, but the
 * pseudo-output can be the upipe_xfer pipe that acts as a proxy to the qsrc
 * in a different thread. That way the upipe_xfer is released on termination,
 * and releases in turn the qsrc in the appropriate upump_mgr (thread)
 * context.
 */

#ifndef _UPIPE_MODULES_UPIPE_QUEUE_SINK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_QUEUE_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_QSINK_SIGNATURE UBASE_FOURCC('q','s','n','k')

/** @This extends upipe_command with specific commands for queue sink. */
enum upipe_qsink_command {
    UPIPE_QSINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns a pointer to the current queue source (struct upipe **) */
    UPIPE_QSINK_GET_QSRC,
    /** sets the pointer to the current queue source (struct upipe *) */
    UPIPE_QSINK_SET_QSRC
};

/** @This returns the management structure for all queue sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_qsink_mgr_alloc(void);

/** @This returns a pointer to the current queue source.
 *
 * @param upipe description structure of the pipe
 * @param queue_p filled in with a pointer to the queue source
 * @return an error code
 */
static inline int upipe_qsink_get_qsrc(struct upipe *upipe,
                                       struct upipe **qsrc_p)
{
    return upipe_control(upipe, UPIPE_QSINK_GET_QSRC, UPIPE_QSINK_SIGNATURE,
                         qsrc_p);
}

/** @This sets the pointer to the current queue source. Please note that once it
 * has been set to a non-NULL value, it is no longer possible to change the
 * pairing.
 *
 * @param upipe description structure of the pipe
 * @param queue pointer to the queue source
 * @return an error code
 */
static inline int upipe_qsink_set_qsrc(struct upipe *upipe,
                                       struct upipe *qsrc)
{
    return upipe_control(upipe, UPIPE_QSINK_SET_QSRC, UPIPE_QSINK_SIGNATURE,
                         qsrc);
}

#ifdef __cplusplus
}
#endif
#endif
