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
 * @short Upipe helper functions for refcount structures
 */

#ifndef _UPIPE_UPIPE_HELPER_UREFCOUNT_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UREFCOUNT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/upipe.h>

#include <assert.h>

/** @This declares three functions dealing with public and private parts
 * of the allocated pipe structure.
 *
 * You must add the urefcount structure to your private pipe structure:
 * @code
 *  struct urefcount urefcount;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro,
 * and have a function to free the structure when the refcount goes down to 0:
 * @code
 *  void upipe_foo_free(struct upipe *upipe)
 * @end code
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_dead_urefcount(struct urefcount *urefcount)
 * @end code
 * Internal wrapper for upipe_foo_free.
 *
 * @item @code
 *  void upipe_foo_init_urefcount(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  void upipe_foo_clean_urefcount(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param UREFCOUNT name of the @tt{struct urefcount} field of
 * your private upipe structure
 * @param DEAD name of the function to free the structure
 */
#define UPIPE_HELPER_UREFCOUNT(STRUCTURE, UREFCOUNT, DEAD)                  \
UBASE_FROM_TO(STRUCTURE, urefcount, UREFCOUNT, UREFCOUNT)                   \
/** @hidden */                                                              \
static void DEAD(struct upipe *upipe);                                      \
/** @internal @This is called when the refcount goes down to zero.          \
 *                                                                          \
 * @param urefcount pointer to the urefcount structure                      \
 */                                                                         \
static void STRUCTURE##_dead_urefcount(struct urefcount *urefcount)         \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_##UREFCOUNT(urefcount);          \
    DEAD(STRUCTURE##_to_upipe(s));                                          \
}                                                                           \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_urefcount(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    urefcount_init(&s->UREFCOUNT, STRUCTURE##_dead_urefcount);              \
    upipe->refcount = STRUCTURE##_to_##UREFCOUNT(s);                        \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_urefcount(struct upipe *upipe)                \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    urefcount_clean(&s->UREFCOUNT);                                         \
}

#ifdef __cplusplus
}
#endif
#endif
