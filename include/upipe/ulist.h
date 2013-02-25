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
 * @short Upipe implementation of lists of structures (NOT thread-safe)
 */

#ifndef _UPIPE_ULIST_H_
/** @hidden */
#define _UPIPE_ULIST_H_

#include <upipe/ubase.h>

#include <stdbool.h>

/** @This is the implementation of local list data structure. Please note
 * that it is not thread-safe. */
struct ulist {
    /** pointer to first queued structure, or NULL */
    struct uchain *first;
    /** pointer to next pointer of last queued structure, or &first */
    struct uchain **last_p;
};

/** @This initializes a struct ulist.
 *
 * @param ulist pointer to a ulist structure
 */
static inline void ulist_init(struct ulist *ulist)
{
    ulist->first = NULL;
    ulist->last_p = &ulist->first;
}

/** @This checks if the list is empty.
 *
 * @param ulist pointer to a ulist structure
 */
static inline bool ulist_empty(struct ulist *ulist)
{
    return ulist->first == NULL;
}

/** @debug @This calculates the depth of the list (suboptimal, only for debug).
 *
 * @param ulist pointer to a ulist structure
 */
static inline size_t ulist_depth(struct ulist *ulist)
{
    struct uchain *uchain = ulist->first;
    size_t depth = 0;
    while (uchain != NULL) {
        depth ++;
        uchain = uchain->next;
    }
    return depth;
}

/** @This adds a new element at the end.
 *
 * @param ulist pointer to a ulist structure
 * @param element pointer to element to add
 */
static inline void ulist_add(struct ulist *ulist, struct uchain *element)
{
    element->next = NULL;
    *ulist->last_p = element;
    ulist->last_p = &element->next;
}

/** @This adds new elements at the end.
 *
 * @param ulist pointer to a ulist structure
 * @param element pointer to the first element to add
 */
static inline void ulist_add_list(struct ulist *ulist, struct uchain *element)
{
    *ulist->last_p = element;
    while (element->next != NULL)
        element = element->next;
    ulist->last_p = &element->next;
}

/** @This returns a pointer to the first element of the list (without
 * removing it).
 *
 * @param ulist pointer to a struct ulist structure
 * @return pointer to the first element
 */
static inline struct uchain *ulist_peek(struct ulist *ulist)
{
    return ulist->first;
}

/** @This returns a pointer to the first element of the list and removes
 * it.
 *
 * @param ulist pointer to a struct ulist structure
 * @return pointer to the first element
 */
static inline struct uchain *ulist_pop(struct ulist *ulist)
{
    struct uchain *uchain = ulist->first;
    if (uchain != NULL) {
        ulist->first = uchain->next;
        uchain->next = NULL;
        if (ulist->first == NULL)
            ulist->last_p = &ulist->first;
    }
    return uchain;
}

/** @This walks through a ulist.
 *
 * @param ulist pointer to a ulist structure
 * @param uchain iterator
 */
#define ulist_foreach(ulist, uchain)                                        \
    for ((uchain) = (ulist)->first; (uchain) != NULL; (uchain) = (uchain)->next)

/** @This walks through a ulist for deletion.
 *
 * @param ulist pointer to a ulist structure
 * @param uchain iterator
 */
#define ulist_delete_foreach(ulist, uchain)                                 \
    struct uchain **uchain_delete_p, **uchain_delete_next_p;                \
    for (uchain_delete_p = &(ulist)->first, (uchain) = (ulist)->first,      \
             uchain_delete_next_p = likely((uchain) != NULL) ?              \
                                           &(uchain)->next : NULL;          \
         (uchain) != NULL;                                                  \
         uchain_delete_p = uchain_delete_next_p,                            \
             (uchain) = *uchain_delete_p,                                   \
             uchain_delete_next_p = likely((uchain) != NULL) ?              \
                                            &(uchain)->next : NULL)

/** @This deletes an element from a ulist. This macro can only be called
 * from inside a ulist_delete_foreach loop.
 *
 * @param ulist pointer to a ulist structure
 * @param uchain iterator
 */
#define ulist_delete(ulist, uchain)                                         \
    *uchain_delete_p = (uchain)->next;                                      \
    uchain_delete_next_p = uchain_delete_p;                                 \
    if (unlikely(*uchain_delete_p == NULL))                                 \
        (ulist)->last_p = uchain_delete_p;

#endif
