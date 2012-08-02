/*****************************************************************************
 * uring.h: upipe ring, thread-safe data structures without mutex
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

#ifndef _UPIPE_URING_H_
/** @hidden */
#define _UPIPE_URING_H_

#ifdef HAVE_ATOMIC_OPS

#include <upipe/ubase.h>

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/** @This defines the position of an element in the uring array. */
typedef uint32_t uring_index;

/** @This represents a NULL index position. */
#define URING_INDEX_NULL 0

/** @This is designed to create thread-safe data structures with uchains. */
struct uring_elem {
    /** tag incremented at each use */
    uint32_t tag;
    /** index of the next element */
    uring_index next;
    /** pointer to embedded uchain */
    struct uchain *uchain;
};

/** @This keeps a uniquely-allocated ring of elements, used for thread-safe
 * data structures. */
struct uring {
    /** number of elements in the ring */
    uint32_t length;
    /** array of elements */
    struct uring_elem *elems;
};

/** @This returns the required size of extra data space for uring.
 *
 * @param length number of elements in the ring
 * @return size in octets to allocate
 */
#define uring_sizeof(length) (length * sizeof(struct uring_elem))

/** @internal @This returns a pointer to an element from an index.
 *
 * @param index of the element in the ring
 * @return pointer to the element in the ring
 */
static inline struct uring_elem *uring_elem_from_index(struct uring *uring,
                                                       uring_index index)
{
    assert(index != URING_INDEX_NULL);
    assert(index <= uring->length);
    return &uring->elems[index - 1];
}

/** @This sets the uchain of a uring element.
 *
 * @param uring pointer to uring structure
 * @param index index of the element in the ring
 * @param uchain uchain to associate with the element
 */
static inline void uring_elem_set(struct uring *uring, uring_index index,
                                  struct uchain *uchain)
{
    struct uring_elem *elem = uring_elem_from_index(uring, index);
    elem->tag++;
    elem->uchain = uchain;
}

/** @This gets the uchain of a uring element.
 *
 * @param uring pointer to uring structure
 * @param index index of the element in the ring
 * @return uchain associated with the element, or NULL
 */
static inline struct uchain *uring_elem_get(struct uring *uring,
                                            uring_index index)
{
    struct uring_elem *elem = uring_elem_from_index(uring, index);
    return elem->uchain;
}

/** @This defines a multiplexed structure from an element index (top) and a tag
 * that increments at each use of the element. This is to avoid the ABA problem
 * in concurrent operations. The bit-field definition is:
 * @table 2
 * @item bits @item description
 * @item 32 @item tag
 * @item 32 @item index
 * @end table
 */
typedef uint64_t uring_lifo;

/** @This represents a NULL LIFO descriptor. */
#define URING_LIFO_NULL 0

/** @internal @This returns the index of an element from a LIFO descriptor.
 *
 * @param lifo uring_lifo multiplexed structure
 * @return index of the element in the ring 
 */
static inline uring_index uring_lifo_to_index(struct uring *uring,
                                              uring_lifo lifo)
{
    if (unlikely(lifo == URING_LIFO_NULL))
        return URING_INDEX_NULL;

    uring_index index = lifo & UINT32_MAX;
    assert(index <= uring->length);
    return index;
}

/** @internal @This returns a LIFO descriptor (multiplexed tag and index)
 * for a given element index. There is no memory barrier in this function
 * because we assume it's been done by the caller.
 *
 * @param uring pointer to uring structure
 * @param index index of the element in the ring
 * @return uring_lifo multiplexed structure
 */
static inline uring_lifo uring_lifo_from_index(struct uring *uring,
                                               uring_index index)
{
    if (unlikely(index == URING_INDEX_NULL))
        return URING_LIFO_NULL;

    assert(index <= uring->length);
    uring_lifo lifo = ((uring_lifo)uring->elems[index - 1].tag << 32) |
                      (uring_lifo)index;
    return lifo;
}

/** @This pops an element from a LIFO in a thread-safe manner.
 *
 * @param uring pointer to uring structure
 * @param lifo_p pointer to the LIFO descriptor
 * @return index of the first LIFO element, or URING_INDEX_NULL
 */
static inline uring_index uring_lifo_pop(struct uring *uring,
                                         uring_lifo *lifo_p)
{
    uring_lifo old_lifo, new_lifo;
    uring_index index;
    __sync_synchronize();

    do {
        old_lifo = *lifo_p;
        if (old_lifo == URING_LIFO_NULL)
            return URING_INDEX_NULL;

        index = uring_lifo_to_index(uring, old_lifo);
        struct uring_elem *elem = uring_elem_from_index(uring, index);
        new_lifo = uring_lifo_from_index(uring, elem->next);
    } while (unlikely(!__sync_bool_compare_and_swap(lifo_p, old_lifo,
                                                    new_lifo)));

    return index;
}

/** @This pushes an element into a LIFO in a thread-safe manner.
 *
 * @param uring pointer to uring structure
 * @param lifo_p pointer to the LIFO descriptor
 * @param index index of the element to push
 */
static inline void uring_lifo_push(struct uring *uring, uring_lifo *lifo_p,
                                   uring_index index)
{
    struct uring_elem *elem = uring_elem_from_index(uring, index);
    uring_lifo old_lifo, new_lifo = uring_lifo_from_index(uring, index);
    __sync_synchronize();

    do {
        old_lifo = *lifo_p;
        elem->next = uring_lifo_to_index(uring, old_lifo);
    } while (unlikely(!__sync_bool_compare_and_swap(lifo_p, old_lifo,
                                                    new_lifo)));
}

/** @This defines a multiplexed structure from two element indexes (head and
 * tail) and associated tags. The bit-field definition is:
 * @table 2
 * @item bits @item description
 * @item 24 @item tail tag
 * @item 8 @item tail index
 * @item 24 @item head description
 * @item 8 @item head index
 * @end table
 */
typedef uint64_t uring_fifo;

/** @This represents a (NULL, NULL) FIFO descriptor. */
#define URING_FIFO_NULL 0

/** @internal @This sets the index of the tail element of a dmux.
 *
 * @param dmux uring_fifo multiplexed structure
 * @param index index of the tail element in the ring
 */
static inline void uring_fifo_set_tail(struct uring *uring, uring_fifo *dmux_p,
                                       uring_index index)
{
    *dmux_p &= UINT32_MAX;
    if (unlikely(index == URING_INDEX_NULL))
        return;

    assert(index <= uring->length);
    *dmux_p |= ((uint64_t)uring->elems[index - 1].tag & UINT64_C(0xffffff))
                << 40;
    *dmux_p |= (uint64_t)index << 32;
}

/** @internal @This sets the index of the head element of a dmux.
 *
 * @param dmux uring_fifo multiplexed structure
 * @param index index of the head element in the ring
 */
static inline void uring_fifo_set_head(struct uring *uring, uring_fifo *dmux_p,
                                       uring_index index)
{
    *dmux_p &= (uint64_t)UINT32_MAX << 32;
    if (unlikely(index == URING_INDEX_NULL))
        return;

    assert(index <= uring->length);
    *dmux_p |= ((uint64_t)uring->elems[index - 1].tag & UINT64_C(0xffffff))
                << 8;
    *dmux_p |= (uint64_t)index;
}

/** @internal @This returns the index of the tail element of a dmux.
 *
 * @param dmux uring_fifo multiplexed structure
 * @return index of the tail element in the ring
 */
static inline uring_index uring_fifo_get_tail(struct uring *uring,
                                              uring_fifo dmux)
{
    uring_index index = (dmux >> 32) & UINT8_MAX;
    assert(index <= uring->length);
    return index;
}

/** @internal @This returns the index of the head element of a dmux.
 *
 * @param dmux uring_fifo multiplexed structure
 * @return index of the head element in the ring
 */
static inline uring_index uring_fifo_get_head(struct uring *uring,
                                              uring_fifo dmux)
{
    uring_index index = dmux & UINT8_MAX;
    assert(index <= uring->length);
    return index;
}

/** @internal @This finds in a chained list of elements the one pointing to a
 * given index.
 *
 * @param uring pointer to uring structure
 * @param start index of the first element of the list
 * @param find index to find
 * @return index of the element pointing to find, or URING_INDEX_NULL if not
 * found
 */
static inline uring_index uring_fifo_find(struct uring *uring,
                                          uring_index start, uring_index find)
{
    uint32_t tries = uring->length;
    uring_index index = start;
    do {
        struct uring_elem *elem = uring_elem_from_index(uring, index);
        uring_index next = elem->next;
        if (next == find)
            return index;
        index = next;
    } while (index != URING_INDEX_NULL && tries--);

    /* We arrive here if the list is inconsistent, and has been modified by
     * another thread. */
    return URING_INDEX_NULL;
}

/** @This pops an element from head of a FIFO in a thread-safe manner.
 *
 * @param uring pointer to uring structure
 * @param fifo_p pointer to the FIFO descriptor
 * @return index of the first FIFO element, or URING_INDEX_NULL
 */
static inline uring_index uring_fifo_pop(struct uring *uring,
                                         uring_fifo *fifo_p)
{
    __sync_synchronize();

    for ( ; ; ) {
        uring_fifo old_fifo = *fifo_p;
        if (old_fifo == URING_FIFO_NULL)
            return URING_INDEX_NULL;

        uring_index tail = uring_fifo_get_tail(uring, old_fifo);
        uring_index head = uring_fifo_get_head(uring, old_fifo);

        if (head == tail) {
            /* one-element FIFO */
            if (likely(__sync_bool_compare_and_swap(fifo_p, old_fifo,
                                                    URING_FIFO_NULL)))
                return head;

        } else {
            /* multiple elements FIFO */
            uring_fifo new_fifo = old_fifo;
            uring_index prev = uring_fifo_find(uring, tail, head);
            if (prev == URING_INDEX_NULL) {
                /* The search failed: the FIFO was modified by another
                 * thread. */
                __sync_synchronize();
                continue;
            }

            for ( ; ; ) {
                uring_fifo_set_head(uring, &new_fifo, prev);
                if (likely(__sync_bool_compare_and_swap(fifo_p, old_fifo,
                                                        new_fifo)))
                    return head;

                new_fifo = old_fifo = *fifo_p;
                /* Check if only the tail was changed (and then try again),
                 * or if we need to restart everything. */
                if (unlikely(head != uring_fifo_get_head(uring, old_fifo)))
                    break;
            }
        }
    }
}

/** @This pushes an element into the tail of a FIFO in a thread-safe manner.
 *
 * @param uring pointer to uring structure
 * @param fifo_p pointer to the FIFO descriptor
 * @param index index of the element to push
 */
static inline void uring_fifo_push(struct uring *uring, uring_fifo *fifo_p,
                                   uring_index index)
{
    struct uring_elem *elem = uring_elem_from_index(uring, index);
    uring_fifo old_fifo, new_fifo;
    __sync_synchronize();

    do {
        old_fifo = new_fifo = *fifo_p;
        uring_index tail = uring_fifo_get_tail(uring, old_fifo);
        elem->next = tail;
        if (tail == URING_INDEX_NULL)
            uring_fifo_set_head(uring, &new_fifo, index);
        uring_fifo_set_tail(uring, &new_fifo, index);
    } while (unlikely(!__sync_bool_compare_and_swap(fifo_p, old_fifo,
                                                    new_fifo)));
}

/** @This initializes a ring. By default all elements are chained, and the
 * first element is the head of the chain.
 *
 * @param uring pointer to uring structure
 * @param length number of elements in the ring
 * @param extra mandatory extra space allocated by the caller, with the size
 * returned by @ref #uring_sizeof
 * @return uring_lifo descriptor of the first element
 */
static inline uring_lifo uring_init(struct uring *uring, uint32_t length,
                                    void *extra)
{
    assert(extra != NULL);
    uring->length = length;
    uring->elems = (struct uring_elem *)extra;
    /* indexes start at 1 */
    for (uint32_t i = 1; i < length; i++) {
        uring->elems[i - 1].tag = 0;
        uring->elems[i - 1].next = i + 1;
        uring->elems[i - 1].uchain = NULL;
    }
    uring->elems[length - 1].tag = 0;
    uring->elems[length - 1].next = URING_INDEX_NULL;
    uring->elems[length - 1].uchain = NULL;
    return uring_lifo_from_index(uring, 1);
}

#else

#warning uring.h included without atomic operations

#endif

#endif
