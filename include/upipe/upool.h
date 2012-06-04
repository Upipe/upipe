/*****************************************************************************
 * upool.h: upipe pool of buffers (multiple writers but only ONE reader)
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

#ifndef _UPIPE_UPOOL_H_
/** @hidden */
#define _UPIPE_UPOOL_H_

#include <upipe/ubase.h>
#include <upipe/ulifo.h>

/** @This is the implementation of a pool. */
struct upool {
    /** LIFO */
    struct ulifo lifo;
    /** maximum number of elements in the LIFO */
    unsigned int max_depth;
};

/** @This initializes a upool.
 *
 * @param upool pointer to a upool structure
 * @param max_depth maximum number of elements in the pool
 */
static inline void upool_init(struct upool *upool, unsigned int max_depth)
{
    ulifo_init(&upool->lifo);
    upool->max_depth = max_depth;
}

/** @This pushes an element into the pool. It may be called from any thread.
 *
 * @param upool pointer to a upool structure
 * @param element pointer to element to push
 * @return true if there was enough space to push the element
 */
static inline bool upool_push(struct upool *upool, struct uchain *element)
{
    if (!upool->max_depth || ulifo_depth(&upool->lifo) >= upool->max_depth)
        return false;
    ulifo_push(&upool->lifo, element);
    return true;
}

/** @This pops an element from the pool. It may only be called by the thread
 * which "owns" the structure, otherwise there is a race condition.
 *
 * @param upool pointer to a upool structure
 * @return pointer to element, or NULL if the LIFO is empty
 */
static inline struct uchain *upool_pop(struct upool *upool)
{
    if (!upool->max_depth) return NULL;
    return ulifo_pop(&upool->lifo);
}

/** @This cleans up the pool data structure. Please note that it is the
 * caller's responsibility to empty the pool first.
 *
 * @param upool pointer to a upool structure
 */
static inline void upool_clean(struct upool *upool)
{
    ulifo_clean(&upool->lifo);
}

#endif
