/*****************************************************************************
 * uqueue.h: upipe thread-safe queue of elements
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

#include <upipe/config.h>
#include <upipe/ubase.h>
#include <upipe/ufifo.h>
#include <upipe/ucounter.h>
#include <upipe/ueventfd.h>
#include <upipe/upump.h>

#include <stdint.h>
#include <assert.h>

/** @This is the implementation of a queue. */
struct uqueue {
    /** FIFO */
    struct ufifo fifo;
    /** number of elements in the queue */
    ucounter counter;
    /** maximum number of elements in the queue */
    uint32_t length;
    /** ueventfd triggered when data can be pushed */
    struct ueventfd event_push;
    /** ueventfd triggered when data can be popped */
    struct ueventfd event_pop;
};

/** @This returns the required size of extra data space for uqueue.
 *
 * @param length maximum number of elements in the queue
 * @return size in octets to allocate
 */
#define uqueue_sizeof(length) ufifo_sizeof(length)

/** @This initializes a uqueue.
 *
 * @param uqueue pointer to a uqueue structure
 * @param length maximum number of elements in the queue
 * @param extra mandatory extra space allocated by the caller, with the size
 * returned by @ref #ufifo_sizeof
 * @return false in case of failure
 */
static inline bool uqueue_init(struct uqueue *uqueue, uint32_t length,
                               void *extra)
{
    if (unlikely(!ueventfd_init(&uqueue->event_push, true)))
        return false;
    if (unlikely(!ueventfd_init(&uqueue->event_pop, false))) {
        ueventfd_clean(&uqueue->event_push);
        return false;
    }

    ufifo_init(&uqueue->fifo, length, extra);
    ucounter_init(&uqueue->counter, 0);
    uqueue->length = length;
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

/** @This pushes an element into the queue.
 *
 * @param uqueue pointer to a uqueue structure
 * @param element pointer to element to push
 * @return false if the queue is full and the element couldn't be queued
 */
static inline bool uqueue_push(struct uqueue *uqueue, struct uchain *element)
{
    if (unlikely(!ufifo_push(&uqueue->fifo, element))) {
        /* signal that we are full */
        ueventfd_read(&uqueue->event_push);

        /* double-check */
        if (likely(!ufifo_push(&uqueue->fifo, element)))
            return false;

        /* signal that we're alright again */
        ueventfd_write(&uqueue->event_push);
    }

    unsigned int counter_before = ucounter_add(&uqueue->counter, 1);
    if (unlikely(counter_before == 0))
        ueventfd_write(&uqueue->event_pop);
    return true;
}

/** @This pops an element from the queue.
 *
 * @param uqueue pointer to a uqueue structure
 * @return pointer to element, or NULL if the LIFO is empty
 */
static inline struct uchain *uqueue_pop(struct uqueue *uqueue)
{
    struct uchain *uchain = ufifo_pop(&uqueue->fifo);
    if (unlikely(uchain == NULL)) {
        /* signal that we starve */
        ueventfd_read(&uqueue->event_pop);

        /* double-check */
        uchain = ufifo_pop(&uqueue->fifo);
        if (likely(uchain == NULL))
            return NULL;

        /* signal that we're alright again */
        ueventfd_write(&uqueue->event_pop);
    }

    unsigned int counter_before = ucounter_sub(&uqueue->counter, 1);
    if (unlikely(counter_before == uqueue->length))
        ueventfd_write(&uqueue->event_push);
    return uchain;
}

/** @This returns the number of elements in the queue.
 *
 * @param uqueue pointer to a uqueue structure
 */
static inline unsigned int uqueue_length(struct uqueue *uqueue)
{
    return ucounter_value(&uqueue->counter);
}

/** @This cleans up the queue data structure. Please note that it is the
 * caller's responsibility to empty the queue first.
 *
 * @param uqueue pointer to a uqueue structure
 */
static inline void uqueue_clean(struct uqueue *uqueue)
{
    ucounter_clean(&uqueue->counter);
    ufifo_clean(&uqueue->fifo);
    ueventfd_clean(&uqueue->event_push);
    ueventfd_clean(&uqueue->event_pop);
}

#endif
