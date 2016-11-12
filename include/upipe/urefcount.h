/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short Upipe thread-safe reference counting
 */

#ifndef _UPIPE_UREFCOUNT_H_
/** @hidden */
#define _UPIPE_UREFCOUNT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uatomic.h>

#include <stdbool.h>
#include <assert.h>

/** @hidden */
struct urefcount;

/** @This is a function pointer freeing a structure. */
typedef void (*urefcount_cb)(struct urefcount *);

/** @This defines an object with reference counting. */
struct urefcount {
    /** number of pointers to the parent object */
    uatomic_uint32_t refcount;
    /** function called when the refcount goes down to 0 */
    urefcount_cb cb;
};

/** @This initializes a urefcount. It must be executed before any other
 * call to the refcount structure.
 *
 * @param refcount pointer to a urefcount structure
 * @param cb function called when the refcount goes down to 0 (may be NULL)
 */
static inline void urefcount_init(struct urefcount *refcount, urefcount_cb cb)
{
    assert(refcount != NULL);
    uatomic_init(&refcount->refcount, 1);
    refcount->cb = cb;
}

/** @This resets a urefcount to 1.
 *
 * @param refcount pointer to a urefcount structure
 */
static inline void urefcount_reset(struct urefcount *refcount)
{
    assert(refcount != NULL);
    uatomic_store(&refcount->refcount, 1);
}

/** @This increments a reference counter.
 *
 * @param refcount pointer to a urefcount structure
 * @return previous refcount value
 */
static inline uint32_t urefcount_use(struct urefcount *refcount)
{
    if (refcount != NULL && refcount->cb != NULL)
        return uatomic_fetch_add(&refcount->refcount, 1);
    return 0;
}

/** @This decrements a reference counter, and possibly frees the object if
 * the refcount goes down to 0.
 *
 * @param refcount pointer to a urefcount structure
 */
static inline void urefcount_release(struct urefcount *refcount)
{
    if (refcount != NULL && refcount->cb != NULL &&
        uatomic_fetch_sub(&refcount->refcount, 1) == 1) {
        urefcount_cb cb = refcount->cb;
        refcount->cb = NULL; /* avoid triggering it twice */
        cb(refcount);
    }
}

/** @This checks for more than one reference.
 *
 * @param refcount pointer to a urefcount structure
 * @return true if there is only one reference to the object
 */
static inline bool urefcount_single(struct urefcount *refcount)
{
    assert(refcount != NULL);
    return uatomic_load(&refcount->refcount) == 1;
}

/** @This checks for no reference.
 *
 * @param refcount pointer to a urefcount structure
 * @return true if there is no reference to the object
 */
static inline bool urefcount_dead(struct urefcount *refcount)
{
    assert(refcount != NULL);
    return uatomic_load(&refcount->refcount) == 0;
}

/** @This cleans up the urefcount structure.
 *
 * @param refcount pointer to a urefcount structure
 */
static inline void urefcount_clean(struct urefcount *refcount)
{
    assert(refcount != NULL);
    uatomic_clean(&refcount->refcount);
}

#ifdef __cplusplus
}
#endif
#endif
