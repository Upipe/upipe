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
 * @short Upipe helper functions for bin output
 */

#ifndef _UPIPE_UPIPE_HELPER_BIN_OUTPUT_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_BIN_OUTPUT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>

#include <stdbool.h>

/** @This declares seven functions dealing with specials pipes called "bins",
 * which internally implement an inner pipeline to handle a given task. It also
 * acts as a proxy to the last element of the inner pipeline.
 *
 * @strong{You must} add four members to your private upipe structure,
 * for instance:
 * @code
 *  struct upipe *output;
 *  struct uchain output_request_list;
 * @end code
 *
 * @strong{You must} also declare @ref #UPIPE_HELPER_UPIPE and
 * @ref #UPIPE_HELPER_INNER prior to using
 * this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  int upipe_foo_store_bin_output(struct upipe *upipe, struct upipe *inner)
 * @end code
 * Called whenever you change the last inner pipe of this bin.
 *
 * @item @code
 *  void upipe_foo_init_bin_output(struct upipe *upipe,
 *                                 struct urefcount *refcount)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  int upipe_foo_control_bin_output(struct upipe *upipe,
 *                                   enum upipe_command command, va_list args)
 * @end code
 * Typically called from your upipe_foo_control() handler. It handles the
 * set_output commands internally, and then acts as a proxy for other commands.
 *
 * @item @code
 *  void upipe_foo_clean_bin_output(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * your private upipe structure
 * @param OUTPUT name of the @tt{struct upipe *} field of
 * your private upipe structure, pointing to the output of the bin
 * @param REQUEST_LIST name of the @tt{struct uchain} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_BIN_OUTPUT(STRUCTURE, LAST_INNER, OUTPUT, REQUEST_LIST)\
/** @internal @This sets the output of the new last inner pipe.             \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param last_inner last inner pipe                                        \
 */                                                                         \
static void STRUCTURE##_store_bin_output(struct upipe *upipe,               \
                                         struct upipe *last_inner)          \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    STRUCTURE##_store_##LAST_INNER(upipe, last_inner);                      \
    if (last_inner != NULL && s->OUTPUT != NULL)                            \
        upipe_set_output(last_inner, s->OUTPUT);                            \
}                                                                           \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param refcount refcount to pass to the inner probe                      \
 */                                                                         \
static void STRUCTURE##_init_bin_output(struct upipe *upipe)                \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    /* NULL is the reason why we don't need to uprobe_clean() it */         \
    STRUCTURE##_init_##LAST_INNER(upipe);                                   \
    s->OUTPUT = NULL;                                                       \
    ulist_init(&s->REQUEST_LIST);                                           \
}                                                                           \
/** @internal @This registers a request to be forwarded downstream. The     \
 * request will be replayed if the output changes. If there is no output,   \
 * the request will be sent via a probe.                                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param urequest request to forward                                       \
 * @return an error code                                                    \
 */                                                                         \
static UBASE_UNUSED int                                                     \
    STRUCTURE##_register_bin_output_request(struct upipe *upipe,            \
                                            struct urequest *urequest)      \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    ulist_add(&s->REQUEST_LIST, urequest_to_uchain(urequest));              \
    if (likely(s->OUTPUT != NULL))                                          \
        return upipe_register_request(s->OUTPUT, urequest);                 \
    return upipe_throw_provide_request(upipe, urequest);                    \
}                                                                           \
/** @internal @This unregisters a request to be forwarded downstream.       \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param urequest request to stop forwarding                               \
 * @return an error code                                                    \
 */                                                                         \
static UBASE_UNUSED int                                                     \
    STRUCTURE##_unregister_bin_output_request(struct upipe *upipe,          \
                                              struct urequest *urequest)    \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    ulist_delete(urequest_to_uchain(urequest));                             \
    if (likely(s->OUTPUT != NULL))                                          \
        return upipe_unregister_request(s->OUTPUT, urequest);               \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This handles the set_output control command.                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output new output                                                 \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_set_bin_output(struct upipe *upipe,                  \
                                      struct upipe *output)                 \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (likely(s->OUTPUT != NULL)) {                                        \
        struct uchain *uchain;                                              \
        ulist_foreach (&s->REQUEST_LIST, uchain) {                          \
            struct urequest *urequest = urequest_from_uchain(uchain);       \
            upipe_unregister_request(s->OUTPUT, urequest);                  \
        }                                                                   \
    }                                                                       \
    upipe_release(s->OUTPUT);                                               \
    s->OUTPUT = NULL;                                                       \
                                                                            \
    int err;                                                                \
    if (unlikely(s->LAST_INNER != NULL &&                                   \
                 (err = upipe_set_output(s->LAST_INNER, output)) !=         \
                 UBASE_ERR_NONE))                                           \
        return err;                                                         \
    s->OUTPUT = upipe_use(output);                                          \
    if (likely(s->OUTPUT != NULL)) {                                        \
        struct uchain *uchain;                                              \
        ulist_foreach (&s->REQUEST_LIST, uchain) {                          \
            struct urequest *urequest = urequest_from_uchain(uchain);       \
            upipe_register_request(s->OUTPUT, urequest);                    \
        }                                                                   \
    }                                                                       \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This handles the control commands.                           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param command control command                                           \
 * @param args optional control command arguments                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_control_bin_output(struct upipe *upipe,              \
                                          int command, va_list args)        \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    switch (command) {                                                      \
        case UPIPE_GET_OUTPUT: {                                            \
            struct upipe **p = va_arg(args, struct upipe **);               \
            *p = s->OUTPUT;                                                 \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_SET_OUTPUT: {                                            \
            struct upipe *output = va_arg(args, struct upipe *);            \
            return STRUCTURE##_set_bin_output(upipe, output);               \
        }                                                                   \
        default:                                                            \
            return STRUCTURE##_control_##LAST_INNER(upipe, command, args);  \
    }                                                                       \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_bin_output(struct upipe *upipe)               \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    struct uchain *uchain;                                                  \
    while ((uchain = ulist_pop(&s->REQUEST_LIST)) != NULL) {                \
        struct urequest *urequest = urequest_from_uchain(uchain);           \
        if (likely(s->OUTPUT != NULL))                                      \
            upipe_unregister_request(s->OUTPUT, urequest);                  \
        urequest_clean(urequest);                                           \
        urequest_free(urequest);                                            \
    }                                                                       \
    STRUCTURE##_clean_##LAST_INNER(upipe);                                  \
    upipe_release(s->OUTPUT);                                               \
}

#ifdef __cplusplus
}
#endif
#endif
