/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe source module for queues
 *
 * Note that the allocator requires an additional parameter:
 * @table 2
 * @item queue_length @item maximum length of the queue (<= 255)
 * @end table
 *
 * Also note that this module is exceptional in that upipe_release() may be
 * called from another thread. The release function is thread-safe.
 */

#ifndef _UPIPE_MODULES_UPIPE_QUEUE_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_QUEUE_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uqueue.h>
#include <upipe/upipe.h>

#include <assert.h>

#define UPIPE_QSRC_SIGNATURE UBASE_FOURCC('q','s','r','c')

/** @This extends upipe_command with specific commands for queue source. */
enum upipe_qsrc_command {
    UPIPE_QSRC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the maximum length of the queue (unsigned int *) */
    UPIPE_QSRC_GET_MAX_LENGTH,
    /** returns the current length of the queue (unsigned int *) */
    UPIPE_QSRC_GET_LENGTH
};

/** @This returns the management structure for all queue sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_qsrc_mgr_alloc(void);

/** @This returns the maximum length of the queue.
 *
 * @param upipe description structure of the pipe
 * @param length_p filled in with the maximum length of the queue
 * @return an error code
 */
static inline int upipe_qsrc_get_max_length(struct upipe *upipe,
                                            unsigned int *length_p)
{
    return upipe_control(upipe, UPIPE_QSRC_GET_MAX_LENGTH,
                         UPIPE_QSRC_SIGNATURE, length_p);
}

/** @This returns the current length of the queue. This function, like all
 * control functions, may only be called from the thread which runs the
 * queue source pipe. The length of the queue may change at any time and the
 * value returned may no longer be valid.
 *
 * @param upipe description structure of the pipe
 * @param length_p filled in with the current length of the queue
 * @return an error code
 */
static inline int upipe_qsrc_get_length(struct upipe *upipe,
                                        unsigned int *length_p)
{
    return upipe_control(upipe, UPIPE_QSRC_GET_LENGTH,
                         UPIPE_QSRC_SIGNATURE, length_p);
}

/** @hidden */
#define ARGS_DECL , unsigned int queue_length
/** @hidden */
#define ARGS , queue_length
UPIPE_HELPER_ALLOC(qsrc, UPIPE_QSRC_SIGNATURE)
#undef ARGS
#undef ARGS_DECL

#ifdef __cplusplus
}
#endif
#endif
