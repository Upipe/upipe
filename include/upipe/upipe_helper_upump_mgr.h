/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short Upipe helper functions for upump manager
 */

#ifndef _UPIPE_UPIPE_HELPER_UPUMP_MGR_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UPUMP_MGR_H_

#include <upipe/ubase.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>

#include <stdbool.h>

/** @This declares four functions dealing with the upump manager.
 *
 * You must add one pointer to your private upipe structure, for instance:
 * @code
 *  struct upump_mgr *upump_mgr;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_upump_mgr(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  bool upipe_foo_get_upump_mgr(struct upipe *upipe, struct upump_mgr **p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_GET_UPUMP_MGR: {
 *      struct upump_mgr **p = va_arg(args, struct upump_mgr **);
 *      return upipe_foo_get_upump_mgr(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  bool upipe_foo_set_upump_mgr(struct upipe *upipe, struct upump_mgr *upump_mgr)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SET_UPUMP_MGR: {
 *      struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
 *      return upipe_foo_set_upump_mgr(upipe, upump_mgr);
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_clean_upump_mgr(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param UPUMP_MGR name of the @tt {struct upump_mgr *} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_UPUMP_MGR(STRUCTURE, UPUMP_MGR)                        \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_upump_mgr(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->UPUMP_MGR = NULL;                                            \
}                                                                           \
/** @internal @This gets the current upump_mgr.                             \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the upump_mgr                                    \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_get_upump_mgr(struct upipe *upipe,                  \
                                      struct upump_mgr **p)                 \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    assert(p != NULL);                                                      \
    *p = STRUCTURE->UPUMP_MGR;                                              \
    return true;                                                            \
}                                                                           \
/** @internal @This sets the upump_mgr.                                     \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param upump_mgr new upump_mgr                                           \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_set_upump_mgr(struct upipe *upipe,                  \
                                      struct upump_mgr *upump_mgr)          \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (unlikely(STRUCTURE->UPUMP_MGR != NULL))                             \
        upump_mgr_release(STRUCTURE->UPUMP_MGR);                            \
                                                                            \
    STRUCTURE->UPUMP_MGR = upump_mgr;                                       \
    if (likely(upump_mgr != NULL))                                          \
        upump_mgr_use(STRUCTURE->UPUMP_MGR);                                \
    return true;                                                            \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_upump_mgr(struct upipe *upipe)                \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (likely(STRUCTURE->UPUMP_MGR != NULL))                               \
        upump_mgr_release(STRUCTURE->UPUMP_MGR);                            \
}

#endif
