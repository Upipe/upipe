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
 * You must add one pointer to your private upipe structure, for instance:
 * @code
 *  struct ubuf_mgr *ubuf_mgr;
 * @end code
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
 *  enum ubase_err upipe_foo_get_ubuf_mgr(struct upipe *upipe,
 *                                        struct ubuf_mgr **p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_GET_UBUF_MGR: {
 *      struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
 *      return upipe_foo_get_ubuf_mgr(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  enum ubase_err upipe_foo_set_ubuf_mgr(struct upipe *upipe,
 *                                        struct ubuf_mgr *ubuf_mgr)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SET_UBUF_MGR: {
 *      struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
 *      return upipe_foo_set_ubuf_mgr(upipe, ubuf_mgr);
 *  }
 * @end code
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
 */
#define UPIPE_HELPER_UBUF_MGR(STRUCTURE, UBUF_MGR)                          \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_ubuf_mgr(struct upipe *upipe)                  \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->UBUF_MGR = NULL;                                             \
}                                                                           \
/** @internal @This gets the current ubuf manager.                          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the ubuf manager                                 \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_get_ubuf_mgr(struct upipe *upipe,                   \
                                     struct ubuf_mgr **p)                   \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    assert(p != NULL);                                                      \
    *p = STRUCTURE->UBUF_MGR;                                               \
    return true;                                                            \
}                                                                           \
/** @internal @This sets the ubuf manager.                                  \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param ubuf_mgr new ubuf manager                                         \
 * @return an error code                                                    \
 */                                                                         \
static enum ubase_err STRUCTURE##_set_ubuf_mgr(struct upipe *upipe,         \
                                               struct ubuf_mgr *ubuf_mgr)   \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (unlikely(STRUCTURE->UBUF_MGR != NULL))                              \
        ubuf_mgr_release(STRUCTURE->UBUF_MGR);                              \
    STRUCTURE->UBUF_MGR = ubuf_mgr;                                         \
    if (likely(ubuf_mgr != NULL))                                           \
        ubuf_mgr_use(STRUCTURE->UBUF_MGR);                                  \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This cleans up the private members of this helper.           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_ubuf_mgr(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (likely(STRUCTURE->UBUF_MGR != NULL))                                \
        ubuf_mgr_release(STRUCTURE->UBUF_MGR);                              \
}

#ifdef __cplusplus
}
#endif
#endif
