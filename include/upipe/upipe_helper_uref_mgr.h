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
 * @short Upipe helper functions for uref manager
 */

#ifndef _UPIPE_UPIPE_HELPER_UREF_MGR_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UREF_MGR_H_
#ifdef __cplusplus
extern "C" {
#endif

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
 *  int upipe_foo_attach_uref_mgr(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_ATTACH_UREF_MGR: {
 *      return upipe_foo_attach_uref_mgr(upipe);
 *  }
 * @end code
 *
 * @item @code
 *  int upipe_foo_check_uref_mgr(struct upipe *upipe)
 * @end code
 * Checks if the uref manager is available, and asks for it otherwise.
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
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    s->UREF_MGR = NULL;                                                     \
}                                                                           \
/** @internal @This sends a probe to attach a uref manager.                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_attach_uref_mgr(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    uref_mgr_release(s->UREF_MGR);                                          \
    s->UREF_MGR = NULL;                                                     \
    return upipe_throw_need_uref_mgr(upipe, &s->UREF_MGR);                  \
}                                                                           \
/** @internal @This checks if the uref manager is available, and asks       \
 * for it otherwise.                                                        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_check_uref_mgr(struct upipe *upipe)                  \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (unlikely(s->UREF_MGR == NULL))                                      \
        return upipe_throw_need_uref_mgr(upipe, &s->UREF_MGR);              \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_uref_mgr(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    uref_mgr_release(s->UREF_MGR);                                          \
}

#ifdef __cplusplus
}
#endif
#endif
