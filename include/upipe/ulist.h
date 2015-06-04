/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 *
 * Please note that ulists cannot be assigned, as in:
 * struct uchain mylist = mystruct->mylist;
 */

#ifndef _UPIPE_ULIST_H_
/** @hidden */
#define _UPIPE_ULIST_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>

#include <stdlib.h>
#include <stdbool.h>

/** @This initializes a ulist.
 *
 * @param uchain pointer to a ulist
 */
static inline void ulist_init(struct uchain *ulist)
{
    ulist->next = ulist->prev = ulist;
}

#define ULIST_INIT(List) (struct uchain){ .next = &(List), .prev = &(List) }

/** @This checks if the element is the first of the list.
 *
 * @param ulist pointer to a ulist
 * @param element pointer to element
 * @return true if the element is the first
 */
static inline bool ulist_is_first(struct uchain *ulist, struct uchain *element)
{
    return element->prev == ulist;
}

/** @This checks if the element is the last of the list.
 *
 * @param ulist pointer to a ulist
 * @param element pointer to element
 * @return true if the element is the last
 */
static inline bool ulist_is_last(struct uchain *ulist, struct uchain *element)
{
    return element->next == ulist;
}

/** @This checks if the element is in a list.
 *
 * @param element pointer to element
 * @return true if the element is in the list
 */
static inline bool ulist_is_in(struct uchain *element)
{
    return !(element->next == NULL);
}

/** @This checks if the list is empty.
 *
 * @param ulist pointer to a ulist
 * @return true if the list is empty
 */
static inline bool ulist_empty(struct uchain *ulist)
{
    return ulist_is_last(ulist, ulist);
}

/** @This calculates the depth of the list (suboptimal, only for debug).
 *
 * @param ulist pointer to a ulist
 * @return the depth of the list
 */
static inline size_t ulist_depth(struct uchain *ulist)
{
    struct uchain *uchain = ulist->next;
    size_t depth = 0;
    while (uchain != ulist) {
        depth++;
        uchain = uchain->next;
    }
    return depth;
}

/** @This adds a new element to a ulist at the given position.
 *
 * @param element pointer to element to add
 * @param prev pointer to previous element
 * @param next pointer to next element
 */
static inline void ulist_insert(struct uchain *prev, struct uchain *next,
                                struct uchain *element)
{
    next->prev = element;
    element->next = next;
    element->prev = prev;
    prev->next = element;
}

/** @This deletes an element from a ulist.
 *
 * @param element pointer to element to delete
 */
static inline void ulist_delete(struct uchain *element)
{
    element->prev->next = element->next;
    element->next->prev = element->prev;
    uchain_init(element);
}

/** @This adds a new element at the end.
 *
 * @param ulist pointer to a ulist structure
 * @param element pointer to element to add
 */
static inline void ulist_add(struct uchain *ulist, struct uchain *element)
{
    ulist_insert(ulist->prev, ulist, element);
}

/** @This adds a new element at the beginning.
 *
 * @param ulist pointer to a ulist
 * @param element pointer to the first element to add
 */
static inline void ulist_unshift(struct uchain *ulist, struct uchain *element)
{
    ulist_insert(ulist, ulist->next, element);
}

/** @This returns a pointer to the first element of the list (without
 * removing it).
 *
 * @param ulist pointer to a ulist
 * @return pointer to the first element
 */
static inline struct uchain *ulist_peek(struct uchain *ulist)
{
    if (ulist_empty(ulist))
        return NULL;
    return ulist->next;
}

/** @This returns a pointer to the first element of the list and removes
 * it.
 *
 * @param ulist pointer to a ulist
 * @return pointer to the first element
 */
static inline struct uchain *ulist_pop(struct uchain *ulist)
{
    if (ulist_empty(ulist))
        return NULL;
    struct uchain *element = ulist->next;
    ulist->next = element->next;
    ulist->next->prev = ulist;
    uchain_init(element);
    return element;
}

/** @This sorts through a list using a comparison function.
 *
 * @param ulist pointer to a ulist
 * @param compar comparison function accepting two uchains as arguments
 */
static inline void ulist_sort(struct uchain *ulist,
                              int (*compar)(struct uchain **, struct uchain **))
{
    size_t depth = ulist_depth(ulist);
    size_t i;
    if (!depth)
        return;

    struct uchain *array[depth];
    for (i = 0; i < depth; i++)
        array[i] = ulist_pop(ulist);

    qsort(array, depth, sizeof(struct uchain *),
            (int (*)(const void *, const void *))compar);

    for (i = 0; i < depth; i++)
        ulist_add(ulist, array[i]);
}

/** @This walks through a ulist. Please note that the list may not be altered
 * during the walk (see @ref #ulist_delete_foreach).
 *
 * @param ulist pointer to a ulist
 * @param uchain iterator
 */
#define ulist_foreach(ulist, uchain)                                        \
    for ((uchain) = (ulist)->next; (uchain) != (ulist);                     \
         (uchain) = (uchain)->next)

/** @This walks through a ulist in reverse. Please note that the list may not be altered
 * during the walk (see @ref #ulist_delete_foreach_reverse).
 *
 * @param ulist pointer to a ulist
 * @param uchain iterator
 */
#define ulist_foreach_reverse(ulist, uchain)                                \
    for ((uchain) = (ulist)->prev; (uchain) != (ulist);                     \
         (uchain) = (uchain)->prev)

/** @This walks through a ulist. This variant allows to remove the current
 * element safely.
 *
 * @param ulist pointer to a ulist
 * @param uchain iterator
 * @param uchain_tmp uchain to use for temporary storage
 */
#define ulist_delete_foreach(ulist, uchain, uchain_tmp)                     \
    for ((uchain) = (ulist)->next, (uchain_tmp) = (uchain)->next;           \
         (uchain) != (ulist);                                               \
         (uchain) = (uchain_tmp), (uchain_tmp) = (uchain)->next)

/** @This walks through a ulist in reverse. This variant allows to remove the current
 * element safely.
 *
 * @param ulist pointer to a ulist
 * @param uchain iterator
 * @param uchain_tmp uchain to use for temporary storage
 */
#define ulist_delete_foreach_reverse(ulist, uchain, uchain_tmp)              \
    for ((uchain) = (ulist)->prev, (uchain_tmp) = (uchain)->prev;           \
         (uchain) != (ulist);                                               \
         (uchain) = (uchain_tmp), (uchain_tmp) = (uchain)->prev)

#ifdef __cplusplus
}
#endif
#endif
