/*****************************************************************************
 * ufifo.h: upipe efficient and thread-safe FIFO implementation (multiple
 * writers but only ONE reader)
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
#include <upipe/config.h>

#ifdef HAVE_ATOMIC_OPS

/*
 * Preferred method: gcc atomic operations
 *
 * Please note that in this version, the counter and top pointer are not
 * atomically protected, so they can be off by a small value at a given time
 * (and fixed later). This is not a problem as in our use case, the counter
 * is only indicative.
 *
 * Also please note that it is thread-safe for multiple writers, but for
 * only one reader thread (otherwise it is much more complicated).
 */

/** @This is the implementation of first-in first-out data structure. */
struct ufifo {
    /** last queued pointer, or NULL */
    struct uchain *tail;
    /* number of elements in the queue */
    unsigned int counter;
};

/** @This initializes a ufifo.
 *
 * @param ufifo pointer to a ufifo structure
 */
static inline void ufifo_init(struct ufifo *ufifo)
{
    ufifo->tail = NULL;
    ufifo->counter = 0;
    __sync_synchronize();
}

/** @This pushes a new element.
 *
 * @param ufifo pointer to a ufifo structure
 * @param element pointer to element to push
 * @param counter_before filled with the number of elements before pushing
 */
static inline void ufifo_push(struct ufifo *ufifo, struct uchain *element,
                              unsigned int *counter_before)
{
    struct uchain *prev;
    *counter_before = __sync_fetch_and_add(&ufifo->counter, 1);
    do {
        __sync_synchronize();
        prev = ufifo->tail;
        element->prev = prev;
    } while (unlikely(!__sync_bool_compare_and_swap(&ufifo->tail, prev,
                                                    element)));
}

/** @This pops an element.
 *
 * @param ufifo pointer to a ufifo structure
 * @param counter_after filled with the number of elements after pushing
 * @return pointer to element, or NULL if the FIFO is empty
 */
static inline struct uchain *ufifo_pop(struct ufifo *ufifo,
                                       unsigned int *counter_after)
{
    struct uchain *element;
    do {
        __sync_synchronize();
        element = ufifo->tail;
        if (unlikely(element == NULL)) return NULL;
        if (likely(element->prev != NULL))
            break;
    } while (unlikely(!__sync_bool_compare_and_swap(&ufifo->tail, element,
                                                    NULL)));

    if (likely(element->prev != NULL)) {
        struct uchain **next = &element->prev;
        while (likely((*next)->prev != NULL))
            next = &(*next)->prev;
        /* no need for a barrier here because we are the only thread accessing
         * packets before tail */
        element = *next;
        *next = NULL;
    }
    *counter_after = __sync_sub_and_fetch(&ufifo->counter, 1);
    return element;
}

/** @This returns the number of elements in the FIFO.
 *
 * @param ufifo pointer to a ufifo structure
 * @return number of elements in the FIFO (approximately)
 */
static inline unsigned int ufifo_length(struct ufifo *ufifo)
{
    __sync_synchronize();
    return ufifo->counter;
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
    unsigned int counter;
};

static inline void ufifo_init(struct ufifo *ufifo)
{
    sem_init(&ufifo->lock, 0, 1);
    ufifo->tail = NULL;
    ufifo->counter = 0;
}

static inline void ufifo_push(struct ufifo *ufifo, struct uchain *element)
{
    while (sem_wait(&ufifo->lock) == -1);
    element->prev = ufifo->tail;
    ufifo->tail = element;
    ufifo->counter++;
    sem_post(&ufifo->lock);
}

static inline struct uchain *ufifo_pop(struct ufifo *ufifo)
{
    struct uchain *element;
    while (sem_wait(&ufifo->lock) == -1);
    element = ufifo->tail;
    if (likely(element != NULL)) {
        if (unlikely(element->prev == NULL))
            ufifo->tail = NULL;
        ufifo->counter--;
    }
    sem_post(&ufifo->lock);

    if (likely(element != NULL && element->prev != NULL)) {
        struct uchain **next = &element->prev;
        while (likely((*next)->prev != NULL))
            next = &(*next)->prev;
        element = *next;
        *next = NULL;
    }
    return element;
}

static inline unsigned int ufifo_length(struct ufifo *ufifo)
{
    unsigned int counter;
    while (sem_wait(&ufifo->lock) == -1);
    counter = ufifo->counter;
    sem_post(&ufifo->lock);
    return counter;
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
