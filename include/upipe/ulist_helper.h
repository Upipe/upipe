/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe helper macros for embedded ulist.
 */

#ifndef _UPIPE_ULIST_HELPER_H_
/** @hidden */
#define _UPIPE_ULIST_HELPER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ulist.h>

/** @This declares functions dealing with embedded ulist of a structure.
 *
 * @param STRUCTURE name of the structure containing the list
 * @param ULIST name of the embedded list in the structure
 * @param SUBSTRUCTURE name of the list item structure
 * @param UCHAIN name of the embedded chain in the item structure
 */
#define ULIST_HELPER(STRUCTURE, ULIST, SUBSTRUCTURE, UCHAIN)                \
/** @This declares functions to convert the structure from / to the list.   \
 */                                                                         \
UBASE_FROM_TO(STRUCTURE, uchain, ULIST, ULIST);                             \
/** @This declares functions to convert the item structure from / to the    \
 * link in the list.                                                        \
 */                                                                         \
UBASE_FROM_TO(SUBSTRUCTURE, uchain, ULIST##_##UCHAIN, UCHAIN);              \
                                                                            \
/** @This initializes the embedded list.                                    \
 *                                                                          \
 * @param s pointer to the structure containing the list                    \
 */                                                                         \
static void STRUCTURE##_init_##ULIST(struct STRUCTURE *s)                   \
{                                                                           \
    struct uchain *list = STRUCTURE##_to_##ULIST(s);                        \
    ulist_init(list);                                                       \
}                                                                           \
                                                                            \
/** @This cleans the embedded list.                                         \
 *                                                                          \
 * @param s pointer to the structure containing the list                    \
 */                                                                         \
static void STRUCTURE##_clean_##ULIST(struct STRUCTURE *s)                  \
{                                                                           \
}                                                                           \
                                                                            \
/** @This adds an item to the embedded list.                                \
 *                                                                          \
 * @param s pointer to the structure containing the list                    \
 * @param i pointer to the item structure containing the link               \
 */                                                                         \
static UBASE_UNUSED inline void                                             \
STRUCTURE##_add_##ULIST(struct STRUCTURE *s, struct SUBSTRUCTURE *i)        \
{                                                                           \
    ulist_add(STRUCTURE##_to_##ULIST(s),                                    \
              SUBSTRUCTURE##_to_##ULIST##_##UCHAIN(i));                     \
}                                                                           \
                                                                            \
/** @This peeks from the embedded list.                                     \
 *                                                                          \
 * @param s pointer to the structure containing the list                    \
 * @return the first element of the embedded list (without removing it)     \
 */                                                                         \
static UBASE_UNUSED inline struct SUBSTRUCTURE *                            \
STRUCTURE##_peek_##ULIST(struct STRUCTURE *s)                               \
{                                                                           \
    struct uchain *list = STRUCTURE##_to_##ULIST(s);                        \
    struct uchain *elt = ulist_peek(list);                                  \
    return elt ? SUBSTRUCTURE##_from_##ULIST##_##UCHAIN(elt) : NULL;        \
}                                                                           \
                                                                            \
/** @This pops from the embedded list.                                      \
 *                                                                          \
 * @param s pointer to the structure containing the list                    \
 * @return the first element of the embedded list (and remove it)           \
 */                                                                         \
static UBASE_UNUSED inline struct SUBSTRUCTURE *                            \
STRUCTURE##_pop_##ULIST(struct STRUCTURE *s)                                \
{                                                                           \
    struct uchain *list = STRUCTURE##_to_##ULIST(s);                        \
    struct uchain *elt = ulist_pop(list);                                   \
    return elt ? SUBSTRUCTURE##_from_##ULIST##_##UCHAIN(elt) : NULL;        \
}                                                                           \
                                                                            \
/** @This iterates elements from the embedded list.                         \
 * The parameter tmp must contain NULL at the first iteration.              \
 *                                                                          \
 * @param s pointer to the structure containing the list                    \
 * @param tmp filled with the next link in the list                         \
 * @return the next item in the list                                        \
 */                                                                         \
static UBASE_UNUSED inline struct SUBSTRUCTURE *                            \
STRUCTURE##_iterator_##ULIST(struct STRUCTURE *s,                           \
                             struct uchain **tmp)                           \
{                                                                           \
    struct uchain *list = STRUCTURE##_to_##ULIST(s);                        \
    struct uchain *next = *tmp;                                             \
    if (!next)                                                              \
        next = ulist_peek(list);                                            \
    else if (next == list) {                                                \
        *tmp = NULL;                                                        \
        return NULL;                                                        \
    }                                                                       \
    *tmp = next->next;                                                      \
    return SUBSTRUCTURE##_from_##ULIST##_##UCHAIN(next);                    \
}

/** @This is an helper to iterate the element of an embedded list.
 * The macro ULIST_HELPER must be define.
 * It may be useful to define:
 * @code
 *  #define STRUCTURE##_foreach_##ULIST(s, i) \
 *      ulist_helper_foreach(STRUCTURE, ULIST, s, i)
 * @end code
 *
 * @param STRUCTURE name of the structure containing the list
 * @param ULIST name of the embedded list in the structure
 * @param list pointer to the structure containing the list
 * @param item pointer iterating the elements
 */
#define ulist_helper_foreach(STRUCTURE, ULIST, list, item)                  \
    for (struct uchain *uchain_tmp = NULL;                                  \
         (item = STRUCTURE##_iterator_##ULIST(list, &uchain_tmp)); )

#ifdef __cplusplus
}
#endif
#endif
