/*****************************************************************************
 * upipe_helper_source_read_size.h: upipe helper functions for read size
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

#ifndef _UPIPE_UPIPE_HELPER_SOURCE_READ_SIZE_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_SOURCE_READ_SIZE_H_

#include <upipe/ubase.h>
#include <upipe/upipe.h>

#include <stdbool.h>

/** @This declares four functions dealing with the read size of a source pipe.
 *
 * You must add one member to your private upipe structure, for instance:
 * @code
 *  unsigned int read_size;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_read_size(struct upipe *upipe, unsigned int read_size)
 * @end code
 * Typically called in your upipe_foo_alloc() function. The @tt read_size
 * parameter is used for initialization.
 *
 * @item @code
 *  bool upipe_foo_get_read_size(struct upipe *upipe, unsigned int *p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SOURCE_GET_READ_SIZE: {
 *      unsgigned int *p = va_arg(args, unsigned int *);
 *      return upipe_foo_get_read_size(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  bool upipe_foo_set_read_size(struct upipe *upipe, unsigned int read_size)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SET_READ_SIZE: {
 *      unsigned int read_size = va_arg(args, unsigned int);
 *      return upipe_foo_set_read_size(upipe, read_size);
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_clean_read_size(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param READ_SIZE name of the @tt {unsigned int} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_SOURCE_READ_SIZE(STRUCTURE, READ_SIZE)                 \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param read_size initial read size value                                 \
 */                                                                         \
static void STRUCTURE##_init_read_size(struct upipe *upipe,                 \
                                       unsigned int read_size)              \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->READ_SIZE = read_size;                                       \
}                                                                           \
/** @internal @This gets the current read size of the source.               \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the read size                                    \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_get_read_size(struct upipe *upipe, unsigned int *p) \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    assert(p != NULL);                                                      \
    *p = STRUCTURE->READ_SIZE;                                              \
    return true;                                                            \
}                                                                           \
/** @internal @This sets the read_size of the source.                       \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param read_size new read size                                           \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_set_read_size(struct upipe *upipe,                  \
                                      unsigned int read_size)               \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->READ_SIZE = read_size;                                       \
    return true;                                                            \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_read_size(struct upipe *upipe)                \
{                                                                           \
}

#endif
