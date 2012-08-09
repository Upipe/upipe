/*
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
 */

/** @file
 * @short Upipe thread-safe last-in first-out data structure
 */

#ifndef _UPIPE_ULIFO_H_
/** @hidden */
#define _UPIPE_ULIFO_H_

#include <upipe/ubase.h>
#include <upipe/uring.h>

/** @This is the implementation of last-in first-out data structure. */
struct ulifo {
    /** ring structure */
    struct uring uring;
    /** uring LIFO of elements carrying a uchain */
    uring_lifo lifo_carrier;
    /** uring LIFO of elements not carrying a uchain */
    uring_lifo lifo_empty;
};

/** @This returns the required size of extra data space for ulifo.
 *
 * @param length maximum number of elements in the LIFO
 * @return size in octets to allocate
 */
#define ulifo_sizeof(length) uring_sizeof(length)

/** @This initializes a ulifo.
 *
 * @param ulifo pointer to a ulifo structure
 * @param length maximum number of elements in the LIFO
 * @param extra mandatory extra space allocated by the caller, with the size
 * returned by @ref #ulifo_sizeof
 */
static inline void ulifo_init(struct ulifo *ulifo, uint16_t length, void *extra)
{
    uring_lifo_init(&ulifo->uring, &ulifo->lifo_empty,
                    uring_init(&ulifo->uring, length, extra));
    uring_lifo_init(&ulifo->uring, &ulifo->lifo_carrier, URING_LIFO_NULL);
}

/** @This pushes a new element.
 *
 * @param ulifo pointer to a ulifo structure
 * @param opaque opaque to associate with element (not NULL)
 * @return false if the maximum number of elements was reached and the
 * element couldn't be queued
 */
static inline bool ulifo_push(struct ulifo *ulifo, void *opaque)
{
    assert(opaque != NULL);
    uring_index index = uring_lifo_pop(&ulifo->uring, &ulifo->lifo_empty);
    if (index == URING_INDEX_NULL)
        return false;
    uring_elem_set(&ulifo->uring, index, opaque);
    uring_lifo_push(&ulifo->uring, &ulifo->lifo_carrier, index);
    return true;
}

/** @internal @This pops an element.
 *
 * @param ulifo pointer to a ulifo structure
 * @return pointer to opaque, or NULL if the LIFO is empty
 */
static inline void *ulifo_pop_internal(struct ulifo *ulifo)
{
    void *opaque;
    uring_index index = uring_lifo_pop(&ulifo->uring, &ulifo->lifo_carrier);
    if (index == URING_INDEX_NULL)
        return NULL;
    opaque = uring_elem_get(&ulifo->uring, index);
    uring_elem_set(&ulifo->uring, index, NULL);
    uring_lifo_push(&ulifo->uring, &ulifo->lifo_empty, index);
    return opaque;
}

/** @This pops an element with type checking.
 *
 * @param ulifo pointer to a ulifo structure
 * @param type type of the opaque pointer
 * @return pointer to opaque, or NULL if the LIFO is empty
 */
#define ulifo_pop(ulifo, type) (type)ulifo_pop_internal(ulifo)

/** @This cleans up the ulifo data structure. Please note that it is the
 * caller's responsibility to empty the LIFO first, and to release the
 * extra data passed to @ref ulifo_init.
 *
 * @param ulifo pointer to a ulifo structure
 */
static inline void ulifo_clean(struct ulifo *ulifo)
{
    uring_lifo_clean(&ulifo->uring, &ulifo->lifo_empty);
    uring_lifo_clean(&ulifo->uring, &ulifo->lifo_carrier);
}

#endif
