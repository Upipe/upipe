/*****************************************************************************
 * uqueue.h: upipe queue of buffers (multiple writers but only ONE reader)
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#ifndef _UPIPE_UQUEUE_H_
/** @hidden */
#define _UPIPE_UQUEUE_H_

#include <assert.h>

#include <upipe/config.h>
#include <upipe/ubase.h>
#include <upipe/ufifo.h>
#include <upipe/ueventfd.h>
#include <upipe/upump.h>

/** @This is the implementation of a queue. */
struct uqueue {
    /** FIFO */
    struct ufifo fifo;
    /** maximum number of elements in the FIFO */
    unsigned int max_length;
    /** ueventfd triggered when data can be pushed */
    ueventfd event_push;
    /** ueventfd triggered when data can be popped */
    ueventfd event_pop;
};

/** @This initializes a uqueue.
 *
 * @param uqueue pointer to a uqueue structure
 * @param max_length maximum number of elements in the queue
 * @return false in case of failure
 */
static inline bool uqueue_init(struct uqueue *uqueue, unsigned int max_length)
{
    assert(max_length);
    if (unlikely(!ueventfd_init(&uqueue->event_push, true)))
        return false;
    if (unlikely(!ueventfd_init(&uqueue->event_pop, false))) {
        ueventfd_clean(&uqueue->event_push);
        return false;
    }

    ufifo_init(&uqueue->fifo);
    uqueue->max_length = max_length;
    return true;
}

/** @This allocates a watcher triggering when data is ready to be pushed.
 *
 * @param uqueue pointer to a uqueue structure
 * @param upump_mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *uqueue_upump_alloc_push(struct uqueue *uqueue,
                                                    struct upump_mgr *upump_mgr,
                                                    upump_cb cb, void *opaque)
{
    return ueventfd_upump_alloc(&uqueue->event_push, upump_mgr, cb, opaque,
                                false);
}

/** @This allocates a watcher triggering when data is ready to be popped.
 *
 * @param uqueue pointer to a uqueue structure
 * @param upump_mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *uqueue_upump_alloc_pop(struct uqueue *uqueue,
                                                   struct upump_mgr *upump_mgr,
                                                   upump_cb cb, void *opaque)
{
    return ueventfd_upump_alloc(&uqueue->event_pop, upump_mgr, cb, opaque,
                                true);
}

/** @This pushes an element into the queue. It may be called from any thread.
 *
 * @param uqueue pointer to a uqueue structure
 * @param element pointer to element to push
 * @return false if no more element should be pushed afterwards
 */
static inline bool uqueue_push(struct uqueue *uqueue, struct uchain *element)
{
    unsigned int counter_before;
    ufifo_push(&uqueue->fifo, element, &counter_before);
    if (unlikely(counter_before == 0))
        ueventfd_write(&uqueue->event_pop);

    while (unlikely(ufifo_length(&uqueue->fifo) >= uqueue->max_length)) {
        ueventfd_read(&uqueue->event_push);

        /* double-check */
        if (likely(ufifo_length(&uqueue->fifo) >= uqueue->max_length))
            return false;

        /* try again */
        ueventfd_write(&uqueue->event_push);
    }
    return true;
}

/** @This pops an element from the queue. It may only be called by the thread
 * which "owns" the structure, otherwise there is a race condition.
 *
 * @param uqueue pointer to a uqueue structure
 * @return pointer to element, or NULL if the LIFO is empty
 */
static inline struct uchain *uqueue_pop(struct uqueue *uqueue)
{
    while (unlikely(ufifo_length(&uqueue->fifo) == 0)) {
        ueventfd_read(&uqueue->event_pop);

        /* double-check */
        if (likely(ufifo_length(&uqueue->fifo) == 0))
            return NULL;

        /* try again */
        ueventfd_write(&uqueue->event_pop);
    }

    unsigned int counter_after;
    struct uchain *uchain = ufifo_pop(&uqueue->fifo, &counter_after);
    if (unlikely(counter_after == uqueue->max_length - 1))
        ueventfd_write(&uqueue->event_push);
    return uchain;
}

/** @This returns the number of elements in the queue.
 *
 * @param uqueue pointer to a uqueue structure
 */
static inline unsigned int uqueue_length(struct uqueue *uqueue)
{
    return ufifo_length(&uqueue->fifo);
}

/** @This cleans up the queue data structure. Please note that it is the
 * caller's responsibility to empty the queue first.
 *
 * @param uqueue pointer to a uqueue structure
 */
static inline void uqueue_clean(struct uqueue *uqueue)
{
    ufifo_clean(&uqueue->fifo);
    ueventfd_clean(&uqueue->event_push);
    ueventfd_clean(&uqueue->event_pop);
}

#endif
