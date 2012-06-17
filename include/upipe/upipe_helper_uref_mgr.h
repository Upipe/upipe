/*****************************************************************************
 * upipe_helper_uref_mgr.h: upipe helper functions for uref manager
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

#ifndef _UPIPE_UPIPE_HELPER_UREF_MGR_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UREF_MGR_H_

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>

#include <stdbool.h>

/** @This declares four functions dealing with the uref manager.
 *
 * You must add one pointer to your private upipe structure, for instance:
 * @code
 *  struct uref_mgr *uref_mgr;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_uref_mgr(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  bool upipe_foo_get_uref_mgr(struct upipe *upipe, struct uref_mgr **p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_GET_UREF_MGR: {
 *      struct uref_mgr **p = va_arg(args, struct uref_mgr **);
 *      return upipe_foo_get_uref_mgr(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  bool upipe_foo_set_uref_mgr(struct upipe *upipe, struct uref_mgr *uref_mgr)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SET_UREF_MGR: {
 *      struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
 *      return upipe_foo_set_uref_mgr(upipe, uref_mgr);
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_clean_uref_mgr(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param UREF_MGR name of the @tt {struct uref_mgr *} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_UREF_MGR(STRUCTURE, UREF_MGR)                          \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_uref_mgr(struct upipe *upipe)                  \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->UREF_MGR = NULL;                                             \
}                                                                           \
/** @internal @This gets the current uref manager.                          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the uref manager                                 \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_get_uref_mgr(struct upipe *upipe,                   \
                                     struct uref_mgr **p)                   \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    assert(p != NULL);                                                      \
    *p = STRUCTURE->UREF_MGR;                                               \
    return true;                                                            \
}                                                                           \
/** @internal @This sets the uref manager.                                  \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param uref_mgr new uref manager                                         \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_set_uref_mgr(struct upipe *upipe,                   \
                                     struct uref_mgr *uref_mgr)             \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (unlikely(STRUCTURE->UREF_MGR != NULL))                              \
        uref_mgr_release(STRUCTURE->UREF_MGR);                              \
    STRUCTURE->UREF_MGR = uref_mgr;                                         \
    if (likely(uref_mgr != NULL))                                           \
        uref_mgr_use(STRUCTURE->UREF_MGR);                                  \
    return true;                                                            \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_uref_mgr(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (likely(STRUCTURE->UREF_MGR != NULL))                                \
        uref_mgr_release(STRUCTURE->UREF_MGR);                              \
}

#endif
