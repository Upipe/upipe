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

/** @This returns the index of an element from a utag (multiplexed structure
 * from tag and index, used for thread-safety).
 *
 * @param utag utag multiplexed structure
 * @return index of the element in the array
 */
static inline uint32_t utag_to_index(uint64_t utag)
{
    return utag & UINT32_MAX;
}

/** @This returns a utag, composited from the index of an element in an array,
 * and from a tag value used for thread-safety. Note that the index here
 * starts at 1 (0 is the NULL element).
 *
 * @param index index of the element in the array
 * @param tag current tag of the element, used for thread-safety
 * @return utag multiplexed structure
 */
static inline uint64_t utag_from_index(uint32_t index, uint32_t tag)
{
    uint64_t utag = ((uint64_t)tag << 32) | index;
    return utag;
}

/** @This represents the NULL utag element. */
#define UTAG_NULL 0

/** @This is designed to create thread-safe data structures with uchains. */
struct uring_elem {
    /** tag incremented at each use */
    uint32_t tag;
    /** utag of the next element */
    uint64_t next_utag;
    /** pointer to uchain */
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

/** @This initializes a ring. By default all elements are chained, and the
 * first element is the head of the chain.
 *
 * @param uring pointer to uring structure
 * @param length number of elements in the ring
 * @param extra mandatory extra space allocated by the caller, with the size
 * returned by @ref #uring_sizeof
 * @return utag of the first element
 */
static inline uint64_t uring_init(struct uring *uring, uint32_t length,
                                  void *extra)
{
    assert(extra != NULL);
    uring->length = length;
    uring->elems = (struct uring_elem *)extra;
    /* indexes start at 1 */
    for (uint32_t i = 1; i < length; i++) {
        uring->elems[i - 1].tag = 0;
        uring->elems[i - 1].next_utag = utag_from_index(i + 1, 0);
        uring->elems[i - 1].uchain = NULL;
    }
    uring->elems[length - 1].tag = 0;
    uring->elems[length - 1].next_utag = UTAG_NULL;
    uring->elems[length - 1].uchain = NULL;
    return utag_from_index(1, 0);
}

/** @This pops an element from a stack in a thread-safe manner.
 *
 * @param uring pointer to uring structure
 * @param top pointer to top utag
 * @return top utag value, or UTAG_NULL
 */
static inline uint64_t uring_pop(struct uring *uring, uint64_t *top)
{
    uint64_t utag, next_utag;
    __sync_synchronize();
    do {
        utag = *top;
        if (unlikely(utag == UTAG_NULL))
            return UTAG_NULL;

        uint32_t index = utag_to_index(utag);
        assert(index <= uring->length);
        struct uring_elem *elem = &uring->elems[index - 1];
        next_utag = elem->next_utag;
    } while (unlikely(!__sync_bool_compare_and_swap(top, utag, next_utag)));
    return utag;
}

/** @This returns the last element of a stack in a thread-safe manner.
 *
 * @param uring pointer to uring structure
 * @param top pointer to top utag
 * @return bottom utag value, or UTAG_NULL
 */
static inline uint64_t uring_shift(struct uring *uring, uint64_t *top)
{
    uint64_t utag;
    uint64_t *prev_utag_p;
    __sync_synchronize();
    do {
        prev_utag_p = top;
        utag = *prev_utag_p;
        if (unlikely(utag == UTAG_NULL))
            return UTAG_NULL;

        for ( ; ; ) {
            uint32_t index = utag_to_index(utag);
            assert(index <= uring->length);
            struct uring_elem *elem = &uring->elems[index - 1];
            uint64_t next_utag = elem->next_utag;
            if (unlikely(next_utag == UTAG_NULL))
                break;
            prev_utag_p = &elem->next_utag;
            utag = next_utag;
        }
    } while (unlikely(!__sync_bool_compare_and_swap(prev_utag_p, utag,
                                                    UTAG_NULL)));
    return utag;
}

/** @This pushes an element into a stack in a thread-safe manner.
 *
 * @param uring pointer to uring structure
 * @param top pointer to top utag
 * @param utag utag to push
 */
static inline void uring_push(struct uring *uring, uint64_t *top, uint64_t utag)
{
    uint32_t index = utag_to_index(utag);
    assert(index <= uring->length);
    __sync_synchronize();
    struct uring_elem *elem = &uring->elems[index - 1];
    do {
        elem->next_utag = *top;
    } while (unlikely(!__sync_bool_compare_and_swap(top, elem->next_utag,
                                                    utag)));
}

/** @This sets the uchain and the next utag of a uring element.
 *
 * @param uring pointer to uring structure
 * @param utag_p reference to utag of the element, is changed during execution
 * @param uchain uchain to associate with the element
 * @return false in case the element doesn't exist
 */
static inline bool uring_set_elem(struct uring *uring, uint64_t *utag_p,
                                  struct uchain *uchain)
{
    uint32_t index = utag_to_index(*utag_p);
    if (index == UTAG_NULL || index > uring->length)
        return false;
    struct uring_elem *elem = &uring->elems[index - 1];
    elem->tag++;
    elem->uchain = uchain;
    *utag_p = utag_from_index(index, elem->tag);
    return true;
}

/** @This gets the uchain and the next utag of a uring element.
 *
 * @param uring pointer to uring structure
 * @param utag utag of the element
 * @param uchain_p reference to a uchain written on execution
 * @return false in case the element doesn't exist
 */
static inline bool uring_get_elem(struct uring *uring, uint64_t utag,
                                  struct uchain **uchain_p)
{
    assert(uchain_p != NULL);
    uint32_t index = utag_to_index(utag);
    if (index == UTAG_NULL || index > uring->length)
        return false;
    struct uring_elem *elem = &uring->elems[index - 1];
    *uchain_p = elem->uchain;
    return true;
}

#else

#warning uring.h included without atomic operations

#endif

#endif
