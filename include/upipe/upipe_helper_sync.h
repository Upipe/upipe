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
 * @short Upipe helper functions for sync_lost/sync_acquired probe events
 */

#ifndef _UPIPE_UPIPE_HELPER_SYNC_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_SYNC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/upipe.h>

/** @This declares four functions throwing the @ref UPROBE_SYNC_ACQUIRED and
 * @ref UPROBE_SYNC_LOST events in a consistent manner.
 *
 * You must add a boolean to your private pipe structure, for instance:
 * @code
 *  bool acquired;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_sync(struct upipe_foo *s)
 * @end code
 * Initializes the acquired field.
 *
 * @item @code
 *  int upipe_foo_sync_lost(struct upipe_foo *s)
 * @end code
 * Throws the @ref UPROBE_SYNC_LOST event, if it hasn't been thrown before.
 *
 * @item @code
 *  int upipe_foo_sync_acquired(struct upipe_foo *s)
 * @end code
 * Throws the @ref UPROBE_SYNC_ACQUIRED event, if it hasn't been thrown before.
 *
 * @item @code
 *  void upipe_foo_clean_sync(struct upipe_foo *s)
 * @end code
 * Currently does nothing.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param ACQUIRED name of the @tt{bool} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_SYNC(STRUCTURE, ACQUIRED)                              \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_sync(struct upipe *upipe)                      \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->ACQUIRED = false;                                            \
}                                                                           \
/** @internal @This sends the sync_lost event if it has not already been    \
 * sent.                                                                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static UBASE_UNUSED int STRUCTURE##_sync_lost(struct upipe *upipe)          \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (STRUCTURE->ACQUIRED) {                                              \
        STRUCTURE->ACQUIRED = false;                                        \
        return upipe_throw_sync_lost(upipe);                                \
    }                                                                       \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This sends the sync_acquired event if it has not already     \
 * been sent.                                                               \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static int STRUCTURE##_sync_acquired(struct upipe *upipe)                   \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (!STRUCTURE->ACQUIRED) {                                             \
        STRUCTURE->ACQUIRED = true;                                         \
        return upipe_throw_sync_acquired(upipe);                            \
    }                                                                       \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_sync(struct upipe *upipe)                     \
{                                                                           \
}

#ifdef __cplusplus
}
#endif
#endif
