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
 * @short Upipe helper functions for flow name
 */

#ifndef _UPIPE_UPIPE_HELPER_SOURCE_FLOW_NAME_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_SOURCE_FLOW_NAME_H_

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

/** @This declares five functions dealing with the flow name of a source pipe.
 *
 * You must add one member to your private upipe structure, for instance:
 * @code
 *  char *flow_name;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_LINEAR_OUTPUT prior to using this
 * macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_flow_name(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  void upipe_foo_set_flow_name_def(struct upipe *upipe, struct uref *flow_def)
 * @end code
 * Called whenever you change the flow definition on the output. It also
 * sets the configured flow name.
 *
 * @item @code
 *  bool upipe_foo_get_flow_name(struct upipe *upipe, struct upipe **p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SOURCE_GET_FLOW_NAME: {
 *      const char **p = va_arg(args, const char **);
 *      return upipe_foo_get_flow_name(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  bool upipe_foo_set_flow_name(struct upipe *upipe, struct upipe *flow_name)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SOURCE_SET_FLOW_NAME: {
 *      const char *flow_name = va_arg(args, const char *);
 *      return upipe_foo_set_flow_name(upipe, flow_name);
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_clean_flow_name(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param FLOW_NAME name of the @tt {char *} field of
 * your private upipe structure
 * @param FLOW_DEF name of the @tt{struct uref *} field of
 * your private upipe structure, declared in @ref #UPIPE_HELPER_LINEAR_OUTPUT
 * @param UREF_MGR name of the @tt{struct uref_mgr *} field of
 * your private upipe structure, declared in @ref #UPIPE_HELPER_UREF_MGR
 */
#define UPIPE_HELPER_SOURCE_FLOW_NAME(STRUCTURE, FLOW_NAME, FLOW_DEF,       \
                                      UREF_MGR)                             \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_flow_name(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->FLOW_NAME = NULL;                                            \
}                                                                           \
/** @internal @This sets the flow definition to use on the output, and sets \
 * the configured flow name. If set to NULL, also output a flow deletion    \
 * packet. Otherwise, schedule a flow definition packet next time a packet  \
 * must be output.                                                          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_def control packet describing the output, without name       \
 */                                                                         \
static void STRUCTURE##_set_flow_name_def(struct upipe *upipe,              \
                                          struct uref *flow_def)            \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (likely(STRUCTURE->FLOW_NAME != NULL))                               \
        uref_flow_set_name(flow_def, STRUCTURE->FLOW_NAME);                 \
    STRUCTURE##_set_flow_def(upipe, flow_def);                              \
}                                                                           \
/** @internal @This handles the get_flow_name control command.              \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the flow name                                    \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_get_flow_name(struct upipe *upipe, const char **p)  \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    assert(p != NULL);                                                      \
    *p = STRUCTURE->FLOW_NAME;                                              \
    return true;                                                            \
}                                                                           \
/** @internal @This handles the set_flow_name control command, and properly \
 * deletes and replays flows on old and new outputs.                        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_name new name of the flow                                    \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_set_flow_name(struct upipe *upipe,                  \
                                      const char *flow_name)                \
{                                                                           \
    assert(flow_name != NULL);                                              \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    free(STRUCTURE->FLOW_NAME);                                             \
    STRUCTURE->FLOW_NAME = strdup(flow_name);                               \
    if (unlikely(STRUCTURE->FLOW_NAME == NULL)) {                           \
        ulog_aerror(upipe->ulog);                                           \
        upipe_throw_aerror(upipe);                                          \
        return false;                                                       \
    }                                                                       \
                                                                            \
    if (unlikely(STRUCTURE->FLOW_DEF != NULL)) {                            \
        struct uref *uref = uref_flow_dup(STRUCTURE->FLOW_DEF, flow_name);  \
        if (unlikely(uref == NULL)) {                                       \
            ulog_aerror(upipe->ulog);                                       \
            upipe_throw_aerror(upipe);                                      \
        }                                                                   \
        STRUCTURE##_set_flow_def(upipe, uref);                              \
    }                                                                       \
    return true;                                                            \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_flow_name(struct upipe *upipe)                \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    free(STRUCTURE->FLOW_NAME);                                             \
}

#endif
