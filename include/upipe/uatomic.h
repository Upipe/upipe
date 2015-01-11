/*
 * Copyright (C) 2012, 2015 OpenHeadend S.A.R.L.
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
#ifdef __cplusplus
extern "C" {
#endif

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

/** @This defines an atomic pointer. */
typedef void * volatile uatomic_ptr_t;

/** @This defines a set of functions to manipulate atomic variables. */
#define UATOMIC_TEMPLATE(type, ctype, atomictype)                           \
/** @This initializes a uatomic variable. It must be executed before any    \
 * other uatomic call. It is not thread-safe.                               \
 *                                                                          \
 * @param obj pointer to a uatomic variable                                 \
 * @param value initial value                                               \
 */                                                                         \
static inline void type##_init(atomictype *obj, ctype value)                \
{                                                                           \
    *obj = value;                                                           \
    __sync_synchronize();                                                   \
}                                                                           \
/** @This sets the value of the uatomic variable.                           \
 *                                                                          \
 * @param obj pointer to a uatomic variable                                 \
 * @param value value to set                                                \
 */                                                                         \
static inline void type##_store(atomictype *obj, ctype value)               \
{                                                                           \
    *obj = value;                                                           \
    __sync_synchronize();                                                   \
}                                                                           \
/** @This returns the value of the uatomic variable.                        \
 *                                                                          \
 * @param obj pointer to a uatomic variable                                 \
 * @return the value                                                        \
 */                                                                         \
static inline ctype type##_load(atomictype *obj)                            \
{                                                                           \
    __sync_synchronize();                                                   \
    return *obj;                                                            \
}                                                                           \
/** @This atomically replaces the uatomic variable, if it contains an       \
 * expected value, with a desired value.                                    \
 *                                                                          \
 * @param obj pointer to a uatomic variable                                 \
 * @param expected reference to expected value, overwritten with actual     \
 * value if it fails                                                        \
 * @param desired desired value                                             \
 * @return false if the exchange failed                                     \
 */                                                                         \
static inline bool type##_compare_exchange(atomictype *obj,                 \
                                           ctype *expected, ctype desired)  \
{                                                                           \
    bool ret;                                                               \
    ret = __sync_bool_compare_and_swap(obj, *expected, desired);            \
    if (unlikely(!ret))                                                     \
        *expected = *obj;                                                   \
    return ret;                                                             \
}                                                                           \
/** @This cleans up the uatomic variable.                                   \
 *                                                                          \
 * @param obj pointer to a uatomic variable                                 \
 */                                                                         \
static inline void type##_clean(atomictype *obj)                            \
{                                                                           \
}
UATOMIC_TEMPLATE(uatomic, uint32_t, uatomic_uint32_t)
UATOMIC_TEMPLATE(uatomic_ptr, void *, uatomic_ptr_t)
#undef UATOMIC_TEMPLATE

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


#elif defined(UPIPE_HAVE_SEMAPHORE_H) /* mkdoc:skip */

/*
 * On POSIX platforms use semaphores (slower)
 */

#include <semaphore.h>

/** @This defines a set of functions to manipulate atomic variables. */
#define UATOMIC_TEMPLATE(type, ctype, atomictype)                           \
typedef struct atomictype {                                                 \
    sem_t lock;                                                             \
    ctype value;                                                            \
} atomictype;                                                               \
static inline void type##_init(atomictype *obj, ctype value)                \
{                                                                           \
    obj->value = value;                                                     \
    sem_init(&obj->lock, 0, 1);                                             \
}                                                                           \
static inline void type##_store(atomictype *obj, ctype value)               \
{                                                                           \
    while (sem_wait(&obj->lock) == -1);                                     \
    obj->value = value;                                                     \
    sem_post(&obj->lock);                                                   \
}                                                                           \
static inline ctype type##_load(atomictype *obj)                            \
{                                                                           \
    ctype ret;                                                              \
    while (sem_wait(&obj->lock) == -1);                                     \
    ret = obj->value;                                                       \
    sem_post(&obj->lock);                                                   \
    return ret;                                                             \
}                                                                           \
static inline bool type##_compare_exchange(atomictype *obj,                 \
                                           ctype *expected, ctype desired)  \
{                                                                           \
    bool ret;                                                               \
    while (sem_wait(&obj->lock) == -1);                                     \
    ret = obj->value == *expected;                                          \
    if (likely(ret))                                                        \
        obj->value = desired;                                               \
    else                                                                    \
        *expected = obj->value;                                             \
    sem_post(&obj->lock);                                                   \
    return ret;                                                             \
}                                                                           \
static inline void type##_clean(atomictype *obj)                            \
{                                                                           \
    sem_destroy(&obj->lock);                                                \
}
UATOMIC_TEMPLATE(uatomic, uint32_t, uatomic_uint32_t)
UATOMIC_TEMPLATE(uatomic_ptr, void *, uatomic_ptr_t)
#undef UATOMIC_TEMPLATE

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



#else /* mkdoc:skip */

/*
 * FIXME: TBW
 * TODO: write C11 support
 */

#error no atomic support

#endif

/** @This loads an atomic pointer.
 *
 * @param obj pointer to a uatomic variable
 * @param type type of the pointer
 * @return the loaded value
 */
#define uatomic_ptr_load_ptr(obj, type)                                     \
    (type)uatomic_ptr_load(obj)

/** @This atomically replaces the uatomic pointer, if it contains an
 * expected value, with a desired value.
 *
 * @param obj pointer to a uatomic variable
 * @param expected reference to expected value, overwritten with actual
 * value if it fails
 * @param desired desired value
 * @return false if the exchange failed
 */
#define uatomic_ptr_compare_exchange_ptr(obj, expected, desired)            \
    uatomic_ptr_compare_exchange(obj, (void **)expected, desired)

#ifdef __cplusplus
}
#endif
#endif
