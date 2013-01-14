/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 */

#ifndef _UPIPE_MODULES_UPIPE_QUEUE_SINK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_QUEUE_SINK_H_

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
 * @return false in case of error
 */
static inline bool upipe_qsink_get_qsrc(struct upipe *upipe,
                                        struct upipe **qsrc_p)
{
    return upipe_control(upipe, UPIPE_QSINK_GET_QSRC, UPIPE_QSINK_SIGNATURE,
                         qsrc_p);
}

/** @This sets the pointer to the current queue source.
 *
 * @param upipe description structure of the pipe
 * @param queue pointer to the queue source
 * @return false in case of error
 */
static inline bool upipe_qsink_set_qsrc(struct upipe *upipe,
                                        struct upipe *qsrc)
{
    return upipe_control(upipe, UPIPE_QSINK_SET_QSRC, UPIPE_QSINK_SIGNATURE,
                         qsrc);
}

#endif
