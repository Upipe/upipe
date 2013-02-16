/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe helper functions for upipe output structures
 */

#ifndef _UPIPE_UPIPE_HELPER_OUTPUT_SUBPIPE_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_OUTPUT_SUBPIPE_H_

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/upipe.h>

/** @This declares two functions dealing with public and private parts
 * of the allocated pipe structure.
 *
 * You must add the upipe structure to your private pipe structure:
 * @code
 *  struct upipe upipe;
 * @end code
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  struct upipe *upipe_foo_to_upipe(struct upipe_foo *s)
 * @end code
 * Returns a pointer to the public upipe structure.
 *
 * @item @code
 *  struct upipe_foo upipe_foo_from_upipe(struct upipe *upipe)
 * @end code
 * Returns a pointer to the private upipe_foo structure.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param UPIPE name of the @tt{struct upipe} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_SUBPIPE(STRUCTURE, STRUCTURE_SUB, SUB, MGR, ULIST,     \
                             UCHAIN)                                        \
/** @internal @This returns the public MGR structure.                       \
 *                                                                          \
 * @param STRUCTURE pointer to the private STRUCTURE structure              \
 * @return pointer to the public MGR structure                              \
 */                                                                         \
static inline struct upipe_mgr *STRUCTURE##_to_##MGR(struct STRUCTURE *s)   \
{                                                                           \
    return &s->MGR;                                                         \
}                                                                           \
/** @internal @This returns the private STRUCTURE structure.                \
 *                                                                          \
 * @param mgr public MGR structure of the pipe                              \
 * @return pointer to the private STRUCTURE structure                       \
 */                                                                         \
static inline struct STRUCTURE *                                            \
    STRUCTURE##_from_##MGR(struct upipe_mgr *mgr)                           \
{                                                                           \
    return container_of(mgr, struct STRUCTURE, MGR);                        \
}                                                                           \
/** @This returns the high-level STRUCTURE_SUB structure.                   \
 *                                                                          \
 * @param uchain pointer to the uchain structure wrapped into the           \
 * STRUCTURE_SUB                                                            \
 * @return pointer to the STRUCTURE_SUB structure                           \
 */                                                                         \
static inline struct STRUCTURE_SUB *                                        \
    STRUCTURE_SUB##_from_uchain(struct uchain *uchain)                      \
{                                                                           \
    return container_of(uchain, struct STRUCTURE_SUB, UCHAIN);              \
}                                                                           \
/** @This returns the uchain structure used for FIFO, LIFO and lists.       \
 *                                                                          \
 * @param s STRUCTURE_SUB structure                                         \
 * @return pointer to the uchain structure                                  \
 */                                                                         \
static inline struct uchain *                                               \
    STRUCTURE_SUB##_to_uchain(struct STRUCTURE_SUB *s)                      \
{                                                                           \
    return &s->UCHAIN;                                                      \
}                                                                           \
/** @This initializes the private members for this helper in STRUCTURE_SUB, \
 * and adds it to the ULIST in STRUCTURE.                                   \
 *                                                                          \
 * @param upipe description structure of the subpipe.                       \
 */                                                                         \
static void STRUCTURE_SUB##_init_sub(struct upipe *upipe)                   \
{                                                                           \
    struct STRUCTURE_SUB *sub = STRUCTURE_SUB##_from_upipe(upipe);          \
    uchain_init(&sub->UCHAIN);                                              \
    struct STRUCTURE *s = STRUCTURE##_from_##MGR(upipe->mgr);               \
    ulist_add(&s->ULIST, STRUCTURE_SUB##_to_uchain(sub));                   \
}                                                                           \
/** @This cleans up the private members for this helper in STRUCTURE_SUB,   \
 * and removes it from the ULIST in STRUCTURE.                              \
 *                                                                          \
 * @param upipe description structure of the subpipe.                       \
 */                                                                         \
static void STRUCTURE_SUB##_clean_sub(struct upipe *upipe)                  \
{                                                                           \
    struct STRUCTURE_SUB *sub = STRUCTURE_SUB##_from_upipe(upipe);          \
    struct STRUCTURE *s = STRUCTURE##_from_##MGR(upipe->mgr);               \
    struct uchain *uchain;                                                  \
    ulist_delete_foreach(&s->ULIST, uchain) {                               \
        if (STRUCTURE_SUB##_from_uchain(uchain) == sub) {                   \
            ulist_delete(&s->ULIST, uchain);                                \
            break;                                                          \
        }                                                                   \
    }                                                                       \
}                                                                           \
/** @This increments the reference count of MGR by increasing the reference \
 * count of the corresponding pipe.                                         \
 *                                                                          \
 * @param mgr pointer to subpipe manager                                    \
 */                                                                         \
static void STRUCTURE_SUB##_mgr_use(struct upipe_mgr *mgr)                  \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_##MGR(mgr);                      \
    upipe_use(STRUCTURE##_to_upipe(s));                                     \
}                                                                           \
/** @This decrements the reference count of a MGR by decreasing the refence \
 * count of the corresponding pipe.                                         \
 *                                                                          \
 * @param mgr pointer to subpipe manager.                                   \
 */                                                                         \
static void STRUCTURE_SUB##_mgr_release(struct upipe_mgr *mgr)              \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_##MGR(mgr);                      \
    upipe_release(STRUCTURE##_to_upipe(s));                                 \
}                                                                           \
/** @This initializes the private members for this helper in STRUCTURE.     \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_sub_##SUB##s(struct upipe *upipe)              \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    ulist_init(&s->ULIST);                                                  \
}                                                                           \
/** @This cleans up the private members for this helper in STRUCTURE.       \
 * It currently does nothing because by construction, the ULIST must be     \
 * empty before STRUCTURE can be destroyed.                                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_sub_##SUB##s(struct upipe *upipe)             \
{                                                                           \
}                                                                           \

#endif
