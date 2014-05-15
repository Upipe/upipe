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
 * @short Upipe helper functions to check input flow definition
 */

#ifndef _UPIPE_UPIPE_HELPER_FLOW_DEF_CHECK_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_FLOW_DEF_CHECK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/udict.h>
#include <upipe/upipe.h>

/** @This declares five functions dealing with the checking of input 
 * flow definitions in pipes.
 *
 * You must add one member to your private upipe structure, for instance:
 * @code
 *  struct uref *flow_def_check;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_flow_def_check(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  struct uref *upipe_foo_alloc_flow_def_check(struct upipe *upipe,
 *                                              struct uref *flow_def_input)
 * @end code
 * Called whenever you need to allocate a flow definition check packet.
 *
 * @item @code
 *  bool upipe_foo_check_flow_def_check(struct upipe *upipe,
 *                                      struct uref *flow_def_check)
 * @end code
 * Checks a flow definition check packet derived from a new input flow
 * definition, against the stored flow def check uref.
 *
 * @item @code
 *  void upipe_foo_store_flow_def_check(struct upipe *upipe,
 *                                      struct uref *flow_def_check)
 * @end code
 * Stores a new flow def check uref.
 *
 * @item @code
 *  void upipe_foo_clean_flow_def_check(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param FLOW_DEF_CHECK name of the @tt {struct uref *} field of your
 * private upipe structure, pointing to the flow definition check
 */
#define UPIPE_HELPER_FLOW_DEF_CHECK(STRUCTURE, FLOW_DEF_CHECK)              \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_flow_def_check(struct upipe *upipe)            \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    s->FLOW_DEF_CHECK = NULL;                                               \
}                                                                           \
/** @internal @This allocates a flow def check uref, from the flow          \
 * def input.                                                               \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return a pointer to a flow def check                                    \
 */                                                                         \
static struct uref *                                                        \
    STRUCTURE##_alloc_flow_def_check(struct upipe *upipe,                   \
                                     struct uref *flow_def_input)           \
{                                                                           \
    return uref_sibling_alloc_control(flow_def_input);                      \
}                                                                           \
/** @internal @This checks a flow definition check packet derived from      \
 * a new input flow definition, against the stored flow def check uref.     \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_def_check flow def check derived from new input flow def     \
 * @return false if the flow def check packets are different                \
 */                                                                         \
static bool STRUCTURE##_check_flow_def_check(struct upipe *upipe,           \
                                             struct uref *flow_def_check)   \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    return s->FLOW_DEF_CHECK != NULL &&                                     \
           !udict_cmp(s->FLOW_DEF_CHECK->udict, flow_def_check->udict);     \
}                                                                           \
/** @internal @This stores a flow def check uref.                           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_def_check flow def check uref                                \
 */                                                                         \
static void STRUCTURE##_store_flow_def_check(struct upipe *upipe,           \
                                             struct uref *flow_def_check)   \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->FLOW_DEF_CHECK != NULL)                                          \
        uref_free(s->FLOW_DEF_CHECK);                                       \
    s->FLOW_DEF_CHECK = flow_def_check;                                     \
}                                                                           \
/** @internal @This cleans up to private members for this helper.           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_flow_def_check(struct upipe *upipe)           \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->FLOW_DEF_CHECK != NULL)                                          \
        uref_free(s->FLOW_DEF_CHECK);                                       \
}

#ifdef __cplusplus
}
#endif
#endif
