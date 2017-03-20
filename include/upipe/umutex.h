/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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
 * @short Upipe exclusive access to non-reentrant resource
 * Primitives in this file allow to grant exclusive, locking access to a
 * resource.
 */

#ifndef _UPIPE_UMUTEX_H_
/** @hidden */
#define _UPIPE_UMUTEX_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/urefcount.h>

/** @This is the implementation of a structure that protects access to a
 * non-reentrant resource. */
struct umutex {
    /** pointer to refcount management structure */
    struct urefcount *refcount;

    /** lock mutex */
    int (*umutex_lock)(struct umutex *);
    /** unlock mutex */
    int (*umutex_unlock)(struct umutex *);
};

/** @This locks a mutex.
 *
 * @param umutex pointer to a umutex structure
 * @return an error code
 */
static inline int umutex_lock(struct umutex *umutex)
{
    if (unlikely(umutex == NULL))
        return UBASE_ERR_INVALID;
    return umutex->umutex_lock(umutex);
}

/** @This unlocks a mutex.
 *
 * @param umutex pointer to a umutex structure
 * @return an error code
 */
static inline int umutex_unlock(struct umutex *umutex)
{
    if (unlikely(umutex == NULL))
        return UBASE_ERR_INVALID;
    return umutex->umutex_unlock(umutex);
}

/** @This increments the reference count of a umutex.
 *
 * @param umutex pointer to umutex
 * @return same pointer to umutex
 */
static inline struct umutex *umutex_use(struct umutex *umutex)
{
    if (umutex == NULL)
        return NULL;
    urefcount_use(umutex->refcount);
    return umutex;
}

/** @This decrements the reference count of a umutex or frees it.
 *
 * @param umutex pointer to umutex
 */
static inline void umutex_release(struct umutex *umutex)
{
    if (umutex != NULL)
        urefcount_release(umutex->refcount);
}

#ifdef __cplusplus
}
#endif
#endif
