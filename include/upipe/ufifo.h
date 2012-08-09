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
 * @short Upipe thread-safe first-in first-out data structure
 */

#ifndef _UPIPE_UFIFO_H_
/** @hidden */
#define _UPIPE_UFIFO_H_

#include <upipe/ubase.h>
#include <upipe/uring.h>

/** @This is the implementation of first-in first-out data structure. */
struct ufifo {
    /** ring structure */
    struct uring uring;
    /** uring FIFO of elements carrying a uchain */
    uring_fifo fifo_carrier;
    /** uring LIFO of elements not carrying a uchain */
    uring_lifo lifo_empty;
};

/** @This returns the required size of extra data space for ufifo.
 *
 * @param length maximum number of elements in the FIFO
 * @return size in octets to allocate
 */
#define ufifo_sizeof(length) uring_sizeof(length)

/** @This initializes a ufifo.
 *
 * @param ufifo pointer to a ufifo structure
 * @param length maximum number of elements in the FIFO (max 255)
 * @param extra mandatory extra space allocated by the caller, with the size
 * returned by @ref #ufifo_sizeof
 */
static inline void ufifo_init(struct ufifo *ufifo, uint8_t length, void *extra)
{
    uring_lifo_init(&ufifo->uring, &ufifo->lifo_empty,
                    uring_init(&ufifo->uring, length, extra));
    uring_fifo_init(&ufifo->uring, &ufifo->fifo_carrier);
}

/** @This pushes a new element.
 *
 * @param ufifo pointer to a ufifo structure
 * @param opaque opaque to associate with element (not NULL)
 * @return false if the maximum number of elements was reached and the
 * element couldn't be queued
 */
static inline bool ufifo_push(struct ufifo *ufifo, void *opaque)
{
    assert(opaque != NULL);
    uring_index index = uring_lifo_pop(&ufifo->uring, &ufifo->lifo_empty);
    if (index == URING_INDEX_NULL)
        return false;
    uring_elem_set(&ufifo->uring, index, opaque);
    uring_fifo_push(&ufifo->uring, &ufifo->fifo_carrier, index);
    return true;
}

/** @internal @This pops an element.
 *
 * @param ufifo pointer to a ufifo structure
 * @return pointer to opaque, or NULL if the FIFO is empty
 */
static inline void *ufifo_pop_internal(struct ufifo *ufifo)
{
    void *opaque;
    uring_index index = uring_fifo_pop(&ufifo->uring, &ufifo->fifo_carrier);
    if (index == URING_INDEX_NULL)
        return NULL;
    opaque = uring_elem_get(&ufifo->uring, index);
    uring_elem_set(&ufifo->uring, index, NULL);
    uring_lifo_push(&ufifo->uring, &ufifo->lifo_empty, index);
    return opaque;
}

/** @This pops an element with type checking.
 *
 * @param ufifo pointer to a ufifo structure
 * @param type type of the opaque pointer
 * @return pointer to opaque, or NULL if the FIFO is empty
 */
#define ufifo_pop(ufifo, type) (type)ufifo_pop_internal(ufifo)

/** @This cleans up the ufifo data structure. Please note that it is the
 * caller's responsibility to empty the FIFO first.
 *
 * @param ufifo pointer to a ufifo structure
 */
static inline void ufifo_clean(struct ufifo *ufifo)
{
    uring_lifo_clean(&ufifo->uring, &ufifo->lifo_empty);
    uring_fifo_clean(&ufifo->uring, &ufifo->fifo_carrier);
}

#endif
