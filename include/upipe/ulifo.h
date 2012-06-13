/*****************************************************************************
 * ulifo.h: upipe efficient and thread-safe LIFO implementation (multiple
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

#ifndef _UPIPE_ULIFO_H_
/** @hidden */
#define _UPIPE_ULIFO_H_

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
 * only one reader thread (otherwise we fall into the ABA problem).
 */

/** @This is the implementation of last-in first-out data structure. */
struct ulifo {
    /** last queued pointer, and first to be dequeued, or NULL */
    struct uchain *top;
    /* number of elements in the queue */
    unsigned int counter;
};

/** @This initializes a ulifo.
 *
 * @param ulifo pointer to a ulifo structure
 */
static inline void ulifo_init(struct ulifo *ulifo)
{
    ulifo->top = NULL;
    ulifo->counter = 0;
    __sync_synchronize();
}

/** @This pushes a new element.
 *
 * @param ulifo pointer to a ulifo structure
 * @param element pointer to element to push
 */
static inline void ulifo_push(struct ulifo *ulifo, struct uchain *element)
{
    struct uchain *next;
    __sync_add_and_fetch(&ulifo->counter, 1);
    do {
        __sync_synchronize();
        next = ulifo->top;
        element->next = next;
    } while (unlikely(!__sync_bool_compare_and_swap(&ulifo->top, next,
                                                    element)));
}

/** @This pops an element.
 *
 * @param ulifo pointer to a ulifo structure
 * @return pointer to element, or NULL if the LIFO is empty
 */
static inline struct uchain *ulifo_pop(struct ulifo *ulifo)
{
    struct uchain *element;
    do {
        __sync_synchronize();
        element = ulifo->top;
        if (unlikely(element == NULL)) return NULL;
        /* There can only be one reader here, otherwise another thread could
         * have dequeued two elements and re-queued the first, and we'd have
         * an erroneous element->next (ABA problem). */
    } while (unlikely(!__sync_bool_compare_and_swap(&ulifo->top, element,
                                                    element->next)));
    element->next = NULL;
    __sync_sub_and_fetch(&ulifo->counter, 1);
    return element;
}

/** @This returns the number of elements in the LIFO.
 *
 * @param ulifo pointer to a ulifo structure
 * @return number of elements in the LIFO (approximately)
 */
static inline unsigned int ulifo_depth(struct ulifo *ulifo)
{
    __sync_synchronize();
    return ulifo->counter;
}

/** @This cleans up the ulifo data structure. Please note that it is the
 * caller's responsibility to empty the LIFO first.
 *
 * @param ulifo pointer to a ulifo structure
 */
static inline void ulifo_clean(struct ulifo *ulifo)
{
}


#elif defined(HAVE_SEMAPHORE_H) /* mkdoc:skip */

/*
 * On POSIX platforms use semaphores (slower)
 */

#include <semaphore.h>

struct ulifo {
    sem_t lock;
    struct uchain *top;
    unsigned int counter;
};

static inline void ulifo_init(struct ulifo *ulifo)
{
    sem_init(&ulifo->lock, 0, 1);
    ulifo->top = NULL;
    ulifo->counter = 0;
}

static inline void ulifo_push(struct ulifo *ulifo, struct uchain *element)
{
    while (sem_wait(&ulifo->lock) == -1);
    element->next = ulifo->top;
    ulifo->top = element;
    ulifo->counter++;
    sem_post(&ulifo->lock);
}

static inline struct uchain *ulifo_pop(struct ulifo *ulifo)
{
    struct uchain *element;
    while (sem_wait(&ulifo->lock) == -1);
    element = ulifo->top;
    if (likely(element != NULL)) {
        ulifo->top = element->next;
        ulifo->counter--;
    }
    sem_post(&ulifo->lock);
    if (likely(element != NULL))
        element->next = NULL;
    return element;
}

static inline unsigned int ulifo_depth(struct ulifo *ulifo)
{
    unsigned int counter;
    while (sem_wait(&ulifo->lock) == -1);
    counter = ulifo->counter;
    sem_post(&ulifo->lock);
    return counter;
}

static inline void ulifo_clean(struct ulifo *ulifo)
{
    sem_destroy(&ulifo->lock);
}


#else /* mkdoc:skip */

/*
 * FIXME: TBW
 */

#error no LIFO available

#endif

#endif
