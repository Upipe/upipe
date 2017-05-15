/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short Upipe helper functions for flow format
 */

#ifndef _UPIPE_UPIPE_HELPER_FLOW_FORMAT_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_FLOW_FORMAT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/urequest.h>
#include <upipe/uref_dump.h>

#include <stdbool.h>

/** @This defines a function that will be called after a flow_format has been
 * received. The second argument is the amended flow format (belongs to the
 * callee). */
typedef int (*upipe_helper_flow_format_check)(struct upipe *, struct uref *);

/** @This defines a function that will be called to register or unregister a
 * request. */
typedef int (*upipe_helper_flow_format_register)(struct upipe *,
                                                 struct urequest *);

/** @This declares four functions dealing with the flow format used on the
 * output of a pipe.
 *
 * You must add one member to your private upipe structure, for instance:
 * @code
 *  struct urequest flow_format_request;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro,
 * and provide two functions which will be called 1/ when the flow format is
 * provided, 2/ and 3/ when a request needs to be registered/unregistered.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_flow_format(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  int upipe_foo_provide_flow_format(struct upipe *upipe, va_list args)
 * @end code
 * Internal function called when the request is answered.
 *
 * @item @code
 *  int upipe_foo_require_flow_format(struct upipe *upipe,
 *                                    struct uref *flow_format)
 * @end code
 * Initializes and registers the request to get a flow format. The base flow
 * format belongs to the callee and will be eventually freed.
 *
 * @item @code
 *  void upipe_foo_clean_flow_format(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param REQUEST name of the @tt {struct urequest} field of
 * your private upipe structure
 * @param CHECK function called after a uref manager has been received
 * @param REGISTER function called to register a request
 * @param UNREGISTER function called to unregister a request
 */
#define UPIPE_HELPER_FLOW_FORMAT(STRUCTURE, REQUEST,                        \
                                 CHECK, REGISTER, UNREGISTER)               \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_flow_format(struct upipe *upipe)               \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    urequest_set_opaque(&s->REQUEST, NULL);                                 \
}                                                                           \
/** @internal @This handles the result of a flow format request.            \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_provide_flow_format(struct urequest *urequest,       \
                                           va_list args)                    \
{                                                                           \
    struct upipe *upipe = urequest_get_opaque(urequest, struct upipe *);    \
    struct uref *flow_format = va_arg(args, struct uref *);                 \
    upipe_dbg_va(upipe, "provided flow_format");                            \
    uref_dump(flow_format, upipe->uprobe);                                  \
    upipe_helper_flow_format_check check = CHECK;                           \
    if (check != NULL)                                                      \
        return check(upipe, flow_format);                                   \
    uref_free(flow_format);                                                 \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This registers a request to get a flow format.               \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_format flow format for which a ubuf is required (belongs     \
 * to the callee)                                                           \
 */                                                                         \
static void STRUCTURE##_require_flow_format(struct upipe *upipe,            \
                                            struct uref *flow_format)       \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    upipe_helper_flow_format_register reg = REGISTER;                       \
    upipe_helper_flow_format_register unreg = UNREGISTER;                   \
    assert(flow_format != NULL);                                            \
    if (urequest_get_opaque(&s->REQUEST, struct upipe *) != NULL) {         \
        if (unreg != NULL)                                                  \
            unreg(upipe, &s->REQUEST);                                      \
        urequest_clean(&s->REQUEST);                                        \
    }                                                                       \
    urequest_init_flow_format(&s->REQUEST, flow_format,                     \
                              STRUCTURE##_provide_flow_format, NULL);       \
    urequest_set_opaque(&s->REQUEST, upipe);                                \
    upipe_dbg(upipe, "require flow_format");                                \
    uref_dump(flow_format, upipe->uprobe);                                  \
    if (reg != NULL)                                                        \
        reg(upipe, &s->REQUEST);                                            \
}                                                                           \
/** @internal @This cleans up the private members of this helper.           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_flow_format(struct upipe *upipe)              \
{                                                                           \
    /* If the request was registered, it should be unregistered             \
     * automatically. Otherwise it has not been initialized. */             \
}

#ifdef __cplusplus
}
#endif
#endif
