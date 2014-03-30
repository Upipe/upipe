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
 * @short Upipe helper functions for ubuf manager
 */

#ifndef _UPIPE_UPIPE_HELPER_UBUF_MGR_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UBUF_MGR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>

#include <stdbool.h>

/** @This declares four functions dealing with the ubuf manager used on the
 * output of a pipe.
 *
 * You must add two members to your private upipe structure, for instance:
 * @code
 *  struct ubuf_mgr *ubuf_mgr;
 *  struct uref *flow_def;
 * @end code
 * where the flow_def is probably shared with @ref #UPIPE_HELPER_OUTPUT.
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_ubuf_mgr(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  int upipe_foo_attach_ubuf_mgr(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_ATTACH_UBUF_MGR: {
 *      return upipe_foo_attach_ubuf_mgr(upipe);
 *  }
 * @end code
 *
 * @item @code
 *  int upipe_foo_check_ubuf_mgr(struct upipe *upipe)
 * @end code
 * Checks if the ubuf manager is available, and asks for it otherwise.
 *
 * @item @code
 *  void upipe_foo_clean_ubuf_mgr(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param UBUF_MGR name of the @tt {struct ubuf_mgr *} field of
 * your private upipe structure
 * @param FLOW_DEF name of the @tt{struct uref *} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_UBUF_MGR(STRUCTURE, UBUF_MGR, FLOW_DEF)                \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_ubuf_mgr(struct upipe *upipe)                  \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->UBUF_MGR = NULL;                                             \
}                                                                           \
/** @internal @This sends a probe to attach a ubuf manager.                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_attach_ubuf_mgr(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    ubuf_mgr_release(s->UBUF_MGR);                                          \
    s->UBUF_MGR = NULL;                                                     \
    if (likely(s->FLOW_DEF == NULL))                                        \
        return UBASE_ERR_UNHANDLED;                                         \
    return upipe_throw_new_flow_format(upipe, s->FLOW_DEF, &s->UBUF_MGR);   \
}                                                                           \
/** @internal @This checks if the ubuf manager is available, and asks       \
 * for it otherwise.                                                        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_check_ubuf_mgr(struct upipe *upipe)                  \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (likely(s->UBUF_MGR != NULL))                                        \
        return UBASE_ERR_NONE;                                              \
    if (unlikely(s->FLOW_DEF == NULL))                                      \
        return UBASE_ERR_INVALID;                                           \
    return upipe_throw_new_flow_format(upipe, s->FLOW_DEF, &s->UBUF_MGR);   \
}                                                                           \
/** @internal @This cleans up the private members of this helper.           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_ubuf_mgr(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    ubuf_mgr_release(STRUCTURE->UBUF_MGR);                                  \
}

#ifdef __cplusplus
}
#endif
#endif
