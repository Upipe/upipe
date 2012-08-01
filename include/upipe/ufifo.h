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
    /** uring FIFO of elements carrying a uchain */
    uring_dmux fifo_carrier;
    /** uring LIFO of elements not carrying a uchain */
    uring_smux lifo_empty;
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
 * @param length maximum number of elements in the FIFO (max 255)
 * @param extra mandatory extra space allocated by the caller, with the size
 * returned by @ref #ufifo_sizeof
 */
static inline void ufifo_init(struct ufifo *ufifo, uint8_t length, void *extra)
{
    ufifo->lifo_empty = uring_init(&ufifo->uring, length, extra);
    ufifo->fifo_carrier = URING_DMUX_NULL;
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
    uring_index index = uring_lifo_pop(&ufifo->uring, &ufifo->lifo_empty);
    if (index == URING_INDEX_NULL)
        return false;
    uring_elem_set(&ufifo->uring, index, element);
    uring_fifo_push(&ufifo->uring, &ufifo->fifo_carrier, index);
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
    uring_index index = uring_fifo_pop(&ufifo->uring, &ufifo->fifo_carrier);
    if (index == URING_INDEX_NULL)
        return NULL;
    element = uring_elem_get(&ufifo->uring, index);
    uring_elem_set(&ufifo->uring, index, NULL);
    uring_lifo_push(&ufifo->uring, &ufifo->lifo_empty, index);
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

static inline void ufifo_init(struct ufifo *ufifo, uint8_t length, void *extra)
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
