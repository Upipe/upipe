/*****************************************************************************
 * ucounter.h: upipe thread-safe counter
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

#ifndef _UPIPE_UCOUNTER_H_
/** @hidden */
#define _UPIPE_UCOUNTER_H_

#include <upipe/ubase.h>
#include <upipe/config.h>

#ifdef HAVE_ATOMIC_OPS

/*
 * Preferred method: gcc atomic operations
 */

/** counter */
typedef unsigned int ucounter;

/** @This initializes a ucounter. It must be executed before any other
 * call to the ucounter structure.
 *
 * @param counter pointer to a ucounter structure
 * @param value initial value
 */
static inline void ucounter_init(ucounter *counter, unsigned int value)
{
    *counter = value;
    __sync_synchronize();
}

/** @This returns the value of the counter
 *
 * @param counter pointer to a ucounter structure
 * @return value
 */
static inline unsigned int ucounter_value(ucounter *counter)
{
    __sync_synchronize();
    return *counter;
}

/** @This increments the counter.
 *
 * @param counter pointer to a ucounter structure
 * @param value value to add
 * @return value before the operation
 */
static inline unsigned int ucounter_add(ucounter *counter, unsigned int value)
{
    return __sync_fetch_and_add(counter, value);
}

/** @This decrements the counter.
 *
 * @param counter pointer to a ucounter structure
 * @param value value to substract
 * @return value before the operation
 */
static inline unsigned int ucounter_sub(ucounter *counter, unsigned int value)
{
    return __sync_fetch_and_sub(counter, value);
}

/** @This cleans up the ucounter structure.
 *
 * @param counter pointer to a ucounter structure
 */
static inline void ucounter_clean(ucounter *counter)
{
}


#elif defined(HAVE_SEMAPHORE_H) /* mkdoc:skip */

/*
 * On POSIX platforms use semaphores (slower)
 */

#include <semaphore.h>

typedef struct ucounter {
    sem_t lock;
    unsigned int value;
} ucounter;

static inline void ucounter_init(ucounter *counter, unsigned int value)
{
    sem_init(&counter->lock, 0, 1);
    counter->value = value;
}

static inline unsigned int ucounter_value(ucounter *counter)
{
    unsigned int ret;
    while (sem_wait(&counter->lock) == -1);
    ret = counter->value;
    sem_post(&counter->lock);
    return ret;
}

static inline int ucounter_add(ucounter *counter, unsigned int value)
{
    unsigned int ret;
    while (sem_wait(&counter->lock) == -1);
    ret = counter->value;
    counter->value += value;
    sem_post(&counter->lock);
    return ret;
}

static inline int ucounter_sub(ucounter *counter, unsigned int value)
{
    unsigned int ret;
    while (sem_wait(&counter->lock) == -1);
    ret = counter->value;
    counter->value -= value;
    sem_post(&counter->lock);
    return ret;
}

static inline void ucounter_clean(ucounter *counter)
{
    sem_destroy(&counter->lock);
}


#else /* mkdoc:skip */

/*
 * FIXME: TBW
 */

#error no counter available

#endif

#endif
