/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe helper functions for output size
 */

#ifndef _UPIPE_UPIPE_HELPER_OUTPUT_SIZE_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_OUTPUT_SIZE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/upipe.h>
#include <upipe/uref.h>
#include <upipe/uref_block_flow.h>

#include <stdbool.h>

/** @This declares four functions dealing with the output size of a pipe.
 *
 * You must add one member to your private upipe structure, for instance:
 * @code
 *  unsigned int output_size;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_OUTPUT prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_output_size(struct upipe *upipe,
 *                                  unsigned int output_size)
 * @end code
 * Typically called in your upipe_foo_alloc() function. The @tt output_size
 * parameter is used for initialization.
 *
 * @item @code
 *  int upipe_foo_get_output_size(struct upipe *upipe, unsigned int *p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_GET_OUTPUT_SIZE: {
 *      unsgigned int *p = va_arg(args, unsigned int *);
 *      return upipe_foo_get_output_size(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  int upipe_foo_set_output_size(struct upipe *upipe, unsigned int output_size)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SET_OUTPUT_SIZE: {
 *      unsigned int output_size = va_arg(args, unsigned int);
 *      return upipe_foo_set_output_size(upipe, output_size);
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_clean_output_size(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param OUTPUT_SIZE name of the @tt {unsigned int} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_OUTPUT_SIZE(STRUCTURE, OUTPUT_SIZE)                    \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output_size initial output size value                             \
 */                                                                         \
static void STRUCTURE##_init_output_size(struct upipe *upipe,               \
                                         unsigned int output_size)          \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->OUTPUT_SIZE = output_size;                                   \
}                                                                           \
/** @internal @This gets the current output size of the source.             \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the output size                                  \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_get_output_size(struct upipe *upipe, unsigned int *p)\
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    assert(p != NULL);                                                      \
    *p = STRUCTURE->OUTPUT_SIZE;                                            \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This sets the output_size of the source.                     \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output_size new output size                                       \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_set_output_size(struct upipe *upipe,                 \
                                       unsigned int output_size)            \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->OUTPUT_SIZE = output_size;                                   \
    struct uref *flow_def;                                                  \
    if (likely(STRUCTURE##_get_flow_def(upipe, &flow_def))) {               \
        flow_def = uref_dup(flow_def);                                      \
        UBASE_ALLOC_RETURN(flow_def)                                        \
        UBASE_RETURN(uref_block_flow_set_size(flow_def, output_size))       \
        STRUCTURE##_store_flow_def(upipe, flow_def);                        \
    }                                                                       \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_output_size(struct upipe *upipe)              \
{                                                                           \
}

#ifdef __cplusplus
}
#endif
#endif
