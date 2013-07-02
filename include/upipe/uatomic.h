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
 * @short Upipe thread-safe atomic operations
 * This API mimicks a partial C11 stdatomic implementation. Atomic variables
 * must be initialized with @ref uatomic_init before use, and released with
 * @ref uatomic_clean before deallocation.
 */

#ifndef _UPIPE_UATOMIC_H_
/** @hidden */
#define _UPIPE_UATOMIC_H_

#include <upipe/ubase.h>
#include <upipe/config.h>

#include <stdint.h>
#include <stdbool.h>

#ifdef UPIPE_HAVE_ATOMIC_OPS

/*
 * Preferred method: gcc atomic operations
 */

/** @This defines an atomic 32-bits unsigned integer. ARM platforms do not
 * support larger atomic operations. */
typedef volatile uint32_t uatomic_uint32_t;

/** @This initializes a uatomic variable. It must be executed before any other
 * uatomic call. It is not thread-safe.
 *
 * @param obj pointer to a uatomic variable
 * @param value initial value
 */
static inline void uatomic_init(uatomic_uint32_t *obj, uint32_t value)
{
    *obj = value;
    __sync_synchronize();
}

/** @This sets the value of the uatomic variable.
 *
 * @param obj pointer to a uatomic variable
 * @param value value to set
 */
static inline void uatomic_store(uatomic_uint32_t *obj, uint32_t value)
{
    *obj = value;
    __sync_synchronize();
}

/** @This returns the value of the uatomic variable.
 *
 * @param obj pointer to a uatomic variable
 * @return value
 */
static inline uint32_t uatomic_load(uatomic_uint32_t *obj)
{
    __sync_synchronize();
    return *obj;
}

/** @This atomically replaces the uatomic variable, if it contains an expected
 * value, with a desired value.
 *
 * @param obj pointer to a uatomic variable
 * @param expected reference to expected value, overwritten with actual value
 * if it fails
 * @param desired desired value
 * @return false if the exchange failed
 */
static inline bool uatomic_compare_exchange(uatomic_uint32_t *obj,
                                            uint32_t *expected,
                                            uint32_t desired)
{
    bool ret;
    ret = __sync_bool_compare_and_swap(obj, *expected, desired);
    if (unlikely(!ret))
        *expected = *obj;
    return ret;
}

/** @This increments a uatomic variable.
 *
 * @param obj pointer to a uatomic variable
 * @param operand value to add
 * @return value before the operation
 */
static inline uint32_t uatomic_fetch_add(uatomic_uint32_t *obj,
                                         uint32_t operand)
{
    return __sync_fetch_and_add(obj, operand);
}

/** @This decrements a uatomic variable.
 *
 * @param obj pointer to a uatomic variable
 * @param operand value to substract
 * @return value before the operation
 */
static inline uint32_t uatomic_fetch_sub(uatomic_uint32_t *obj,
                                         uint32_t operand)
{
    return __sync_fetch_and_sub(obj, operand);
}

/** @This cleans up the uatomic variable.
 *
 * @param obj pointer to a uatomic variable
 */
static inline void uatomic_clean(uatomic_uint32_t *obj)
{
}


#elif defined(UPIPE_HAVE_SEMAPHORE_H) /* mkdoc:skip */

/*
 * On POSIX platforms use semaphores (slower)
 */

#include <semaphore.h>

typedef struct uatomic_uint32_t {
    sem_t lock;
    uint32_t value;
} uatomic_uint32_t;

static inline void uatomic_init(uatomic_uint32_t *obj, uint32_t value)
{
    obj->value = value;
    sem_init(&obj->lock, 0, 1);
}

static inline void uatomic_store(uatomic_uint32_t *obj, uint32_t value)
{
    while (sem_wait(&obj->lock) == -1);
    obj->value = value;
    sem_post(&obj->lock);
}

static inline uint32_t uatomic_load(uatomic_uint32_t *obj)
{
    uint32_t ret;
    while (sem_wait(&obj->lock) == -1);
    ret = obj->value;
    sem_post(&obj->lock);
    return ret;
}

static inline bool uatomic_compare_exchange(uatomic_uint32_t *obj,
                                            uint32_t *expected,
                                            uint32_t desired)
{
    bool ret;
    while (sem_wait(&obj->lock) == -1);
    ret = obj->value == *expected;
    if (likely(ret))
        obj->value = desired;
    else
        *expected = obj->value;
    sem_post(&obj->lock);
    return ret;
}
static inline uint32_t uatomic_fetch_add(uatomic_uint32_t *obj,
                                         uint32_t operand)
{
    uint32_t ret;
    while (sem_wait(&obj->lock) == -1);
    ret = obj->value;
    obj->value += operand;
    sem_post(&obj->lock);
    return ret;
}

static inline uint32_t uatomic_fetch_sub(uatomic_uint32_t *obj,
                                         uint32_t operand)
{
    uint32_t ret;
    while (sem_wait(&obj->lock) == -1);
    ret = obj->value;
    obj->value -= operand;
    sem_post(&obj->lock);
    return ret;
}

static inline void uatomic_clean(uatomic_uint32_t *obj)
{
    sem_destroy(&obj->lock);
}


#else /* mkdoc:skip */

/*
 * FIXME: TBW
 * TODO: write C11 support
 */

#error no atomic support

#endif

#endif
