/*****************************************************************************
 * ufifo.h: upipe efficient and thread-safe FIFO implementation
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

#ifndef _UPIPE_UFIFO_H_
/** @hidden */
#define _UPIPE_UFIFO_H_

#include <upipe/ubase.h>

#include <stdint.h>

#ifdef HAVE_ATOMIC_OPS

#include <upipe/uring.h>

/*
 * Preferred method: gcc atomic operations
 */

/** @This is the implementation of first-in first-out data structure. */
struct ufifo {
    /** ring structure */
    struct uring uring;
    /** last queued utag carrying a uchain, and last to be dequeued,
     * or UTAG_NULL if no uchain is available */
    uint64_t tail_carrier;
    /** last queued utag not carrying a uchain, and first to be dequeued,
     * or UTAG_NULL if the FIFO is full */
    uint64_t top_empty;
};

/** @This returns the required size of extra data space for ufifo.
 *
 * @param length maximum number of elements in the FIFO
 * @return size in octets to allocate
 */
#define ufifo_sizeof(length) uring_sizeof(length)

/** @This initializes a ufifo.
 *
 * @param ufifo pointer to a ufifo structure
 * @param length maximum number of elements in the FIFO
 * @param extra mandatory extra space allocated by the caller, with the size
 * returned by @ref #ufifo_sizeof
 */
static inline void ufifo_init(struct ufifo *ufifo, uint32_t length, void *extra)
{
    ufifo->top_empty = uring_init(&ufifo->uring, length, extra);
    ufifo->tail_carrier = UTAG_NULL;
    __sync_synchronize();
}

/** @This pushes a new element.
 *
 * @param ufifo pointer to a ufifo structure
 * @param element pointer to element to push
 * @return false if the maximum number of elements was reached and the
 * element couldn't be queued
 */
static inline bool ufifo_push(struct ufifo *ufifo, struct uchain *element)
{
    uint64_t utag = uring_pop(&ufifo->uring, &ufifo->top_empty);
    if (utag == UTAG_NULL)
        return false;
    bool ret = uring_set_elem(&ufifo->uring, &utag, element);
    assert(ret);
    uring_push(&ufifo->uring, &ufifo->tail_carrier, utag);
    return true;
}

/** @This pops an element.
 *
 * @param ufifo pointer to a ufifo structure
 * @return pointer to element, or NULL if the FIFO is empty
 */
static inline struct uchain *ufifo_pop(struct ufifo *ufifo)
{
    struct uchain *element;
    uint64_t utag = uring_shift(&ufifo->uring, &ufifo->tail_carrier);
    if (utag == UTAG_NULL)
        return NULL;
    bool ret = uring_get_elem(&ufifo->uring, utag, &element);
    assert(ret);
    ret = uring_set_elem(&ufifo->uring, &utag, NULL);
    assert(ret);
    uring_push(&ufifo->uring, &ufifo->top_empty, utag);
    return element;
}

/** @This cleans up the ufifo data structure. Please note that it is the
 * caller's responsibility to empty the FIFO first.
 *
 * @param ufifo pointer to a ufifo structure
 */
static inline void ufifo_clean(struct ufifo *ufifo)
{
}


#elif defined(HAVE_SEMAPHORE_H) /* mkdoc:skip */

/*
 * On POSIX platforms use semaphores (slower)
 */

#include <semaphore.h>

struct ufifo {
    sem_t lock;
    struct uchain *tail;
    uint32_t length, counter;
};

#define ufifo_sizeof(length) 0

static inline void ufifo_init(struct ufifo *ufifo, uint32_t length, void *extra)
{
    sem_init(&ufifo->lock, 0, 1);
    ufifo->tail = NULL;
    ufifo->length = length;
    ufifo->counter = 0;
}

static inline bool ufifo_push(struct ufifo *ufifo, struct uchain *element)
{
    bool ret;
    while (sem_wait(&ufifo->lock) == -1);
    if (ufifo->counter < ufifo->length) {
        element->prev = ufifo->tail;
        ufifo->tail = element;
        ufifo->counter++;
        ret = true;
    } else
        ret = false;
    sem_post(&ufifo->lock);
    return ret;
}

static inline struct uchain *ufifo_pop(struct ufifo *ufifo)
{
    struct uchain *element;
    while (sem_wait(&ufifo->lock) == -1);
    element = ufifo->tail;
    if (likely(element != NULL)) {
        struct uchain **prev_p = &ufifo->tail;
        while (element->prev != NULL) {
            prev_p = &element->prev;
            element = element->prev;
        }
        ufifo->counter--;
        *prev_p = NULL;
    }
    sem_post(&ufifo->lock);
    return element;
}

static inline void ufifo_clean(struct ufifo *ufifo)
{
    sem_destroy(&ufifo->lock);
}


#else /* mkdoc:skip */

/*
 * FIXME: TBW
 */

#error no FIFO available

#endif

#endif
