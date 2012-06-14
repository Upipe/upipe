/*****************************************************************************
 * urefcount.h: upipe efficient and thread-safe reference counting
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

#ifndef _UPIPE_UREFCOUNT_H_
/** @hidden */
#define _UPIPE_UREFCOUNT_H_

#include <upipe/ubase.h>
#include <upipe/config.h>

#include <stdbool.h>

#ifdef HAVE_ATOMIC_OPS

/*
 * Preferred method: gcc atomic operations
 */

/** number of pointers to the parent object */
typedef unsigned int urefcount;

/** @This initializes a urefcount. It must be executed before any other
 * call to the refcount structure.
 *
 * @param refcount pointer to a urefcount structure
 */
static inline void urefcount_init(urefcount *refcount)
{
    *refcount = 1;
    __sync_synchronize();
}

/** @This increments a reference counter.
 *
 * @param refcount pointer to a urefcount structure
 */
static inline void urefcount_use(urefcount *refcount)
{
    __sync_add_and_fetch(refcount, 1);
}

/** @This decrements a reference counter.
 *
 * @param refcount pointer to a urefcount structure
 * @return true if there is no longer a reference to the object
 */
static inline bool urefcount_release(urefcount *refcount)
{
    urefcount value = __sync_sub_and_fetch(refcount, 1);
    return !value;
}

/** @This checks for more than one reference.
 *
 * @param refcount pointer to a urefcount structure
 * @return true if there is only one reference to the object
 */
static inline bool urefcount_single(urefcount *refcount)
{
    __sync_synchronize();
    return *refcount == 1;
}

/** @This cleans up the urefcount structure.
 *
 * @param refcount pointer to a urefcount structure
 */
static inline void urefcount_clean(urefcount *refcount)
{
}


#elif defined(HAVE_SEMAPHORE_H) /* mkdoc:skip */

/*
 * On POSIX platforms use semaphores (slower)
 */

#include <semaphore.h>
#include <limits.h>

typedef sem_t urefcount;

static inline void urefcount_init(urefcount *refcount)
{
    sem_init(refcount, 0, SEM_VALUE_MAX);
}

static inline void urefcount_use(urefcount *refcount)
{
    sem_wait(refcount);
}

static inline bool urefcount_release(urefcount *refcount)
{
    return !!sem_post(refcount);
}

static inline bool urefcount_single(urefcount *refcount)
{
    int val;
    sem_getvalue(refcount, &val);
    return val == SEM_VALUE_MAX;
}

static inline void urefcount_clean(urefcount *refcount)
{
    sem_destroy(refcount);
}


#else /* mkdoc:skip */

/*
 * FIXME: TBW
 */

#error no refcounting available

#endif

#endif
