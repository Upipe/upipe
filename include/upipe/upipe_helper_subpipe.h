/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe helper functions for upipe subpipes
 */

#ifndef _UPIPE_UPIPE_HELPER_SUBPIPE_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_SUBPIPE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/upipe.h>

#include <assert.h>

/** @This declares nine functions dealing with subpipes of split and join pipes.
 *
 * You must add two members to your private pipe structure:
 * @code
 *  struct uchain subpipes;
 *  struct upipe_mgr subpipe_mgr;
 * @end code
 *
 * Youe must add one member to your private subpipe structure:
 * @code
 *  struct uchain uchain;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE on both the pipe and the
 * subpipe.
 *
 * Supposing the name of your pipe structure is upipe_foo, and subpipe
 * structure is upipe_foo_output, it declares:
 * @list
 * @item @code
 *  struct upipe *upipe_foo_to_output_mgr(struct upipe_foo *s)
 * @end code
 * Returns a pointer to the public subpipe manager.
 *
 * @item @code
 *  struct upipe_foo *upipe_foo_from_output_mgr(struct upipe_mgr *mgr)
 * @end code
 * Returns a pointer to the private upipe_foo structure from the subpipe
 * manager.
 *
 * @item @code
 *  struct upipe_foo_output *upipe_foo_output_from_uchain(struct uchain *uchain)
 * @end code
 * Returns a pointer to the private upipe_foo structure from the chaining
 * structure.
 *
 * @item @code
 *  struct uchain *upipe_foo_output_to_uchain(struct upipe_foo_output *s)
 * @end code
 * Returns a pointer to the chaining structure of the subpipe.
 *
 * @item @code
 *  void upipe_foo_output_init_sub(struct upipe *upipe)
 * @end code
 * Initializes the private members of upipe_foo_output for this helper and
 * adds the output to the list in upipe_foo.
 *
 * @item @code
 *  int upipe_foo_output_get_super(struct upipe *upipe, struct upipe **p)
 * @end code
 * Typically called from your upipe_foo_output_control() handler, such as:
 * @code
 *  case UPIPE_SUB_GET_SUPER: {
 *      struct upipe **p = va_arg(args, struct upipe **);
 *      return upipe_foo_output_get_super(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_output_clean_sub(struct upipe *upipe)
 * @end code
 * Cleans up the private members of upipe_foo_output for this helper and
 * removes the output from the list in upipe_foo.
 *
 * @item @code
 *  void upipe_foo_init_sub_outputs(struct upipe *upipe)
 * @end code
 * Initializes the list in upipe_foo.
 *
 * @item @code
 *  int upipe_foo_get_sub_mgr(struct upipe *upipe, struct upipe_mgr **p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_GET_SUB_MGR: {
 *      struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
 *      return upipe_foo_get_sub_mgr(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  int upipe_foo_iterate_sub(struct upipe *upipe, struct upipe **p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_ITERATE_SUB: {
 *      struct upipe **p = va_arg(args, struct upipe **);
 *      return upipe_foo_iterate_sub(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_throw_sub_outputs(struct upipe *upipe,
 *                                   int event, ...)
 * @end code
 * Throws the given event from all subpipes.                              
 *
 * @item @code
 *  void upipe_foo_clean_sub_outputs(struct upipe *upipe)
 * @end code
 * Cleans up the list in upipe_foo.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param STRUCTURE_SUB name of your private subpipe structure 
 * @param SUB suffix to use in upipe_foo_init_sub_XXX and
 * upipe_foo_clean_sub_XXX
 * @param MGR name of the @tt{struct upipe_mgr} member of your private upipe
 * structure
 * @param ULIST name of the @tt{struct uchain} member of your private upipe
 * structure
 * @param UCHAIN name of the @tt{struct uchain} member of your private subpipe
 * structure
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
static UBASE_UNUSED inline struct upipe_mgr *                               \
    STRUCTURE##_to_##MGR(struct STRUCTURE *s)                               \
{                                                                           \
    return &s->MGR;                                                         \
}                                                                           \
/** @internal @This returns the private STRUCTURE structure.                \
 *                                                                          \
 * @param mgr public MGR structure of the pipe                              \
 * @return pointer to the private STRUCTURE structure                       \
 */                                                                         \
static UBASE_UNUSED inline struct STRUCTURE *                               \
    STRUCTURE##_from_##MGR(struct upipe_mgr *mgr)                           \
{                                                                           \
    struct STRUCTURE *s = container_of(mgr, struct STRUCTURE, MGR);         \
    STRUCTURE##_from_upipe(STRUCTURE##_to_upipe(s));                        \
        /* for the assert on signature */                                   \
    return s;                                                               \
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
    struct STRUCTURE_SUB *sub =                                             \
            container_of(uchain, struct STRUCTURE_SUB, UCHAIN);             \
    STRUCTURE_SUB##_from_upipe(STRUCTURE_SUB##_to_upipe(sub));              \
        /* for the assert on signature */                                   \
    return sub;                                                             \
}                                                                           \
/** @This returns the uchain structure used for FIFO, LIFO and lists.       \
 *                                                                          \
 * @param s STRUCTURE_SUB structure                                         \
 * @return pointer to the uchain structure                                  \
 */                                                                         \
static inline struct uchain *                                               \
    STRUCTURE_SUB##_to_uchain(struct STRUCTURE_SUB *sub)                    \
{                                                                           \
    return &sub->UCHAIN;                                                    \
}                                                                           \
/** @This initializes the private members for this helper in STRUCTURE_SUB, \
 * and adds it to the ULIST in STRUCTURE.                                   \
 *                                                                          \
 * @param upipe description structure of the subpipe                        \
 */                                                                         \
static void STRUCTURE_SUB##_init_sub(struct upipe *upipe)                   \
{                                                                           \
    struct STRUCTURE_SUB *sub = STRUCTURE_SUB##_from_upipe(upipe);          \
    uchain_init(&sub->UCHAIN);                                              \
    struct STRUCTURE *s = STRUCTURE##_from_##MGR(upipe->mgr);               \
    ulist_add(&s->ULIST, STRUCTURE_SUB##_to_uchain(sub));                   \
}                                                                           \
/** @This returns the super-pipe of the subpipe.                            \
 *                                                                          \
 * @param upipe description structure of the subpipe                        \
 * @param p filled in with a pointer to the super-pipe                      \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE_SUB##_get_super(struct upipe *upipe, struct upipe **p) \
{                                                                           \
    assert(p != NULL);                                                      \
    struct STRUCTURE *s = STRUCTURE##_from_##MGR(upipe->mgr);               \
    *p = STRUCTURE##_to_upipe(s);                                           \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @This cleans up the private members for this helper in STRUCTURE_SUB,   \
 * and removes it from the ULIST in STRUCTURE.                              \
 *                                                                          \
 * @param upipe description structure of the subpipe                        \
 */                                                                         \
static void STRUCTURE_SUB##_clean_sub(struct upipe *upipe)                  \
{                                                                           \
    struct STRUCTURE_SUB *sub = STRUCTURE_SUB##_from_upipe(upipe);          \
    ulist_delete(STRUCTURE_SUB##_to_uchain(sub));                           \
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
/** @This returns the subpipe manager of a super-pipe.                      \
 *                                                                          \
 * @param upipe description structure of the super-pipe                     \
 * @param p filled in with a pointer to the subpipe manager                 \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_get_##MGR(struct upipe *upipe,                       \
                                 struct upipe_mgr **p)                      \
{                                                                           \
    assert(p != NULL);                                                      \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    *p = &s->MGR;                                                           \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @This iterates over the subpipes of a super-pipe.                       \
 *                                                                          \
 * @param upipe description structure of the super-pipe                     \
 * @param p filled in with the next subpipe, initialize with NULL           \
 * return an error code                                                     \
 */                                                                         \
static int STRUCTURE##_iterate_##SUB(struct upipe *upipe, struct upipe **p) \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    assert(p != NULL);                                                      \
    struct uchain *u;                                                       \
    if (*p == NULL) {                                                       \
        u = &s->ULIST;                                                      \
    } else {                                                                \
        struct STRUCTURE_SUB *sub = STRUCTURE_SUB##_from_upipe(*p);         \
        u = STRUCTURE_SUB##_to_uchain(sub);                                 \
    }                                                                       \
    if (ulist_is_last(&s->ULIST, u)) {                                      \
        *p = NULL;                                                          \
        return UBASE_ERR_NONE;                                              \
    }                                                                       \
    *p = STRUCTURE_SUB##_to_upipe(STRUCTURE_SUB##_from_uchain(u->next));    \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @This handles specific super pipes commands.                            \
 *                                                                          \
 * @param upipe description structure of the super-pipe                     \
 * @param command type of command to handle                                 \
 * @param args optional arguments                                           \
 * @return an error code                                                    \
 */                                                                         \
static inline int STRUCTURE##_control_##SUB##s(struct upipe *upipe,         \
                                               int command, va_list args)   \
{                                                                           \
    switch (command) {                                                      \
        case UPIPE_GET_SUB_MGR: {                                           \
            struct upipe_mgr **mgr_p = va_arg(args, struct upipe_mgr **);   \
            return STRUCTURE##_get_##MGR(upipe, mgr_p);                     \
        }                                                                   \
        case UPIPE_ITERATE_SUB: {                                           \
            struct upipe **sub_p = va_arg(args, struct upipe **);           \
            return STRUCTURE##_iterate_##SUB(upipe, sub_p);                 \
        }                                                                   \
    }                                                                       \
    return UBASE_ERR_UNHANDLED;                                             \
}                                                                           \
/** @This throws an event from all subpipes.                                \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param event event to throw, followed by arguments                       \
 */                                                                         \
static UBASE_UNUSED void STRUCTURE##_throw_sub_##SUB##s(struct upipe *upipe,\
                                                        int event, ...)     \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    struct uchain *uchain;                                                  \
    struct STRUCTURE_SUB *sub = NULL;                                       \
    ulist_foreach (&s->ULIST, uchain) {                                     \
        if (sub != NULL)                                                    \
            upipe_release(STRUCTURE_SUB##_to_upipe(sub));                   \
        sub = STRUCTURE_SUB##_from_uchain(uchain);                          \
        /* to avoid having the uchain disappear during throw */             \
        upipe_use(STRUCTURE_SUB##_to_upipe(sub));                           \
        va_list args;                                                       \
        va_start(args, event);                                              \
        upipe_throw_va(STRUCTURE_SUB##_to_upipe(sub), event, args);         \
        va_end(args);                                                       \
    }                                                                       \
    if (sub != NULL)                                                        \
        upipe_release(STRUCTURE_SUB##_to_upipe(sub));                       \
}                                                                           \
/** @This cleans up the private members for this helper in STRUCTURE.       \
 * It currently does nothing because by construction, the ULIST must be     \
 * empty before STRUCTURE can be destroyed.                                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_sub_##SUB##s(struct upipe *upipe)             \
{                                                                           \
}

#ifdef __cplusplus
}
#endif
#endif
