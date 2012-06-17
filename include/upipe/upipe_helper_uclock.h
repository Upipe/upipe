/*****************************************************************************
 * upipe_helper_uclock.h: upipe helper functions for uclock
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

#ifndef _UPIPE_UPIPE_HELPER_UCLOCK_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UCLOCK_H_

#include <upipe/ubase.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>

#include <stdbool.h>

/** @This declares four functions dealing with the uclock.
 *
 * You must add one pointer to your private upipe structure, for instance:
 * @code
 *  struct uclock *uclock;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPUMP_MGR prior to using this
 * macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_uclock(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  bool upipe_foo_get_uclock(struct upipe *upipe, struct uclock **p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_GET_UCLOCK: {
 *      struct uclock **p = va_arg(args, struct uclock **);
 *      return upipe_foo_get_uclock(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  bool upipe_foo_set_uclock(struct upipe *upipe, struct uclock *uclock)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SET_UCLOCK: {
 *      struct uclock *uclock = va_arg(args, struct uclock *);
 *      return upipe_foo_set_uclock(upipe, uclock);
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_clean_uclock(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param UCLOCK name of the @tt {struct uclock *} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_UCLOCK(STRUCTURE, UCLOCK)                              \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_uclock(struct upipe *upipe)                    \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->UCLOCK = NULL;                                               \
}                                                                           \
/** @internal @This gets the current uclock.                                \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the uclock                                       \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_get_uclock(struct upipe *upipe, struct uclock **p)  \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    assert(p != NULL);                                                      \
    *p = STRUCTURE->UCLOCK;                                                 \
    return true;                                                            \
}                                                                           \
/** @internal @This sets the uclock.                                        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param uclock new uclock                                                 \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_set_uclock(struct upipe *upipe,                     \
                                   struct uclock *uclock)                   \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (unlikely(STRUCTURE->UCLOCK != NULL))                                \
        uclock_release(STRUCTURE->UCLOCK);                                  \
    STRUCTURE->UCLOCK = uclock;                                             \
    if (likely(uclock != NULL))                                             \
        uclock_use(STRUCTURE->UCLOCK);                                      \
    STRUCTURE##_set_upump(upipe, NULL);                                     \
    return true;                                                            \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_uclock(struct upipe *upipe)                   \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (likely(STRUCTURE->UCLOCK != NULL))                                  \
        uclock_release(STRUCTURE->UCLOCK);                                  \
}

#endif
