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

/** @This contains the number of pointers to the parent object. */
typedef uatomic_uint32_t urefcount;

/** @This initializes a urefcount. It must be executed before any other
 * call to the refcount structure.
 *
 * @param refcount pointer to a urefcount structure
 */
static inline void urefcount_init(urefcount *refcount)
{
    uatomic_init(refcount, 1);
}

/** @This resets a urefcount to 1.
 *
 * @param refcount pointer to a urefcount structure
 */
static inline void urefcount_reset(urefcount *refcount)
{
    uatomic_store(refcount, 1);
}

/** @This increments a reference counter.
 *
 * @param refcount pointer to a urefcount structure
 */
static inline void urefcount_use(urefcount *refcount)
{
    uatomic_fetch_add(refcount, 1);
}

/** @This decrements a reference counter.
 *
 * @param refcount pointer to a urefcount structure
 * @return true if there is no longer a reference to the object
 */
static inline bool urefcount_release(urefcount *refcount)
{
    return uatomic_fetch_sub(refcount, 1) == 1;
}

/** @This checks for more than one reference.
 *
 * @param refcount pointer to a urefcount structure
 * @return true if there is only one reference to the object
 */
static inline bool urefcount_single(urefcount *refcount)
{
    return uatomic_load(refcount) == 1;
}

/** @This cleans up the urefcount structure.
 *
 * @param refcount pointer to a urefcount structure
 */
static inline void urefcount_clean(urefcount *refcount)
{
    uatomic_clean(refcount);
}

#ifdef __cplusplus
}
#endif
#endif
