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
 * @short Upipe source module for queues
 */

#ifndef _UPIPE_MODULES_UPIPE_QUEUE_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_QUEUE_SOURCE_H_

#include <upipe/ubase.h>
#include <upipe/uqueue.h>
#include <upipe/upipe.h>

#include <assert.h>

#define UPIPE_QSRC_SIGNATURE UBASE_FOURCC('q','s','r','c')

/** @internal @This is the structure exported from source to sinks. */
struct upipe_queue {
    /** max length of the queue */
    unsigned int max_length;
    /** queue */
    struct uqueue uqueue;

    /** public upipe structure */
    struct upipe upipe;
};

/** @internal @This returns a pointer the uqueue structure.
 *
 * @param upipe pointer to upipe structure of type queue source
 * @return pointer to uqueue
 */
static inline struct uqueue *upipe_queue(struct upipe *upipe)
{
    assert(upipe->mgr->signature == UPIPE_QSRC_SIGNATURE);
    struct upipe_queue *queue = container_of(upipe, struct upipe_queue, upipe);
    return &queue->uqueue;
}

/** @internal @This returns the max length of the queue.
 *
 * @param upipe pointer to upipe structure of type queue source
 * @return max length of the queue
 */
static inline unsigned int upipe_queue_max_length(struct upipe *upipe)
{
    assert(upipe->mgr->signature == UPIPE_QSRC_SIGNATURE);
    struct upipe_queue *queue = container_of(upipe, struct upipe_queue, upipe);
    return queue->max_length;
}

/** @This extends upipe_command with specific commands for queue source. */
enum upipe_qsrc_command {
    UPIPE_QSRC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the maximum length of the queue (unsigned int *) */
    UPIPE_QSRC_GET_MAX_LENGTH,
    /** sets the maximum length of the queue (unsigned int) */
    UPIPE_QSRC_SET_MAX_LENGTH,
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
 * @return false in case of error
 */
static inline bool upipe_qsrc_get_max_length(struct upipe *upipe,
                                             unsigned int *length_p)
{
    return upipe_control(upipe, UPIPE_QSRC_GET_MAX_LENGTH,
                         UPIPE_QSRC_SIGNATURE, length_p);
}

/** @internal @This sets the maximum length of the queue. Note that the queue
 * won't accept sinks until it is initialized by this function with a non-zero
 * value. Also note that it may not be changed afterwards.
 *
 * @param upipe description structure of the pipe
 * @param length maximum length of the queue
 * @return false in case of error
 */
static inline bool upipe_qsrc_set_max_length(struct upipe *upipe,
                                             unsigned int length)
{
    return upipe_control(upipe, UPIPE_QSRC_SET_MAX_LENGTH,
                         UPIPE_QSRC_SIGNATURE, length);
}

/** @This returns the current length of the queue. This function, like all
 * control functions, may only be called from the thread which runs the
 * queue source pipe. The length of the queue may change at any time and the
 * value returned may no longer be valid.
 *
 * @param upipe description structure of the pipe
 * @param length_p filled in with the current length of the queue
 * @return false in case of error
 */
static inline bool upipe_qsrc_get_length(struct upipe *upipe,
                                         unsigned int *length_p)
{
    return upipe_control(upipe, UPIPE_QSRC_GET_LENGTH,
                         UPIPE_QSRC_SIGNATURE, length_p);
}

/** @This allocates and initializes a queue source pipe.
 *
 * @param mgr management structure for queue source type
 * @param uprobe structure used to raise events
 * @param length maximum length of the queue
 * @return pointer to allocated pipe, or NULL in case of failure
 */
static inline struct upipe *upipe_qsrc_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             unsigned int length)
{
    struct upipe *upipe = upipe_alloc(mgr, uprobe);
    if (unlikely(upipe == NULL))
        return NULL;
    if (unlikely(!upipe_qsrc_set_max_length(upipe, length))) {
        upipe_release(upipe);
        return NULL;
    }
    return upipe;
}

#endif
