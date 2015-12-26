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
 * @short Upipe helper functions for bin input
 */

#ifndef _UPIPE_UPIPE_HELPER_BIN_INPUT_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_BIN_INPUT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/urequest.h>
#include <upipe/upipe.h>

#include <stdbool.h>
#include <assert.h>

/** @This declares nine functions dealing with special pipes called "bins",
 * which internally implement an inner pipeline to handle a given task. This
 * helper deals with the input of the inner pipeline and incoming requests.
 *
 * @strong{You must} add a member to your private upipe structure,
 * for instance:
 * @code
 *  struct uchain input_request_list;
 * @end code
 *
 * @strong{You must} also declare @ref #UPIPE_HELPER_UPIPE and
 * @ref #UPIPE_HELPER_INNER prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_bin_input(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  void upipe_foo_store_bin_input(struct upipe *upipe, struct upipe *inner)
 * @end code
 * Called whenever you change the first inner pipe of this bin.
 *
 * @item @code
 *  int upipe_foo_register_bin_request(struct upipe *upipe,
 *                                     struct urequest *urequest)
 * @end code
 * Registers a request on the bin. The request will be replayed if the
 * first inner changes. If there is no first inner, the request is sent via a
 * probe.
 *
 * @item @code
 *  int upipe_foo_unregister_bin_request(struct upipe *upipe,
 *                                       struct urequest *urequest)
 * @end code
 * Unregisters a request on the bin.
 *
 * @item @code
 *  int upipe_foo_provide_bin_proxy(struct urequest *urequest, va_list args)
 * @end code
 * Internal function used to receive answers to proxy requests.
 *
 * @item @code
 *  int upipe_foo_alloc_bin_proxy(struct upipe *upipe,
 *                                struct urequest *urequest)
 * @end code
 * Allocates a proxy request to forward a request downstream.
 *
 * @item @code
 *  int upipe_foo_free_bin_proxy(struct upipe *upipe,
 *                               struct urequest *urequest)
 * @end code
 * Frees a proxy request.
 *
 * @item @code
 *  int upipe_foo_control_bin_input(struct upipe *upipe,
 *                                  enum upipe_command command, va_list args)
 * @end code
 * Typically called from your upipe_foo_control() handler. It handles the
 * registering/unregistering of requests.
 *
 * @item @code
 *  void upipe_foo_clean_bin_input(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param FIRST_INNER name of the @tt{struct upipe *} field of
 * your private upipe structure, pointing to the first inner pipe of the bin
 * @param REQUEST_LIST name of the @tt{struct uchain} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_BIN_INPUT(STRUCTURE, FIRST_INNER, REQUEST_LIST)        \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_bin_input(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    STRUCTURE##_init_##FIRST_INNER(upipe);                                  \
    ulist_init(&s->REQUEST_LIST);                                           \
}                                                                           \
/** @internal @This sends a uref to the input. Note that uref is then       \
 * owned by the callee and shouldn't be used any longer.                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param uref uref structure to send                                       \
 * @param upump_p reference to pump that generated the buffer               \
 */                                                                         \
static UBASE_UNUSED void STRUCTURE##_bin_input(struct upipe *upipe,         \
                                               struct uref *uref,           \
                                               struct upump **upump_p)      \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->FIRST_INNER == NULL) {                                           \
        upipe_warn(upipe, "invalid first inner, dropping uref");            \
        uref_free(uref);                                                    \
        return;                                                             \
    }                                                                       \
    upipe_input(s->FIRST_INNER, uref, upump_p);                             \
}                                                                           \
/** @internal @This stores the first inner pipe, while releasing the        \
 * previous one, and registers requests.                                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param first_inner first inner pipe (belongs to the callee)              \
 */                                                                         \
static void STRUCTURE##_store_bin_input(struct upipe *upipe,                \
                                        struct upipe *first_inner)          \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (likely(s->FIRST_INNER != NULL)) {                                   \
        struct uchain *uchain;                                              \
        ulist_foreach (&s->REQUEST_LIST, uchain) {                          \
            struct urequest *urequest = urequest_from_uchain(uchain);       \
            upipe_unregister_request(s->FIRST_INNER, urequest);             \
        }                                                                   \
    }                                                                       \
    STRUCTURE##_store_##FIRST_INNER(upipe, first_inner);                    \
    if (first_inner != NULL) {                                              \
        struct uchain *uchain;                                              \
        ulist_foreach (&s->REQUEST_LIST, uchain) {                          \
            struct urequest *urequest = urequest_from_uchain(uchain);       \
            upipe_register_request(first_inner, urequest);                  \
        }                                                                   \
    }                                                                       \
}                                                                           \
/** @internal @This registers a request to be forwarded downstream. The     \
 * request will be replayed if the first inner changes. If there is no first\
 * inner, the request will be sent via a probe.                             \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param urequest request to forward                                       \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_register_bin_request(struct upipe *upipe,            \
                                            struct urequest *urequest)      \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    ulist_add(&s->REQUEST_LIST, urequest_to_uchain(urequest));              \
    if (likely(s->FIRST_INNER != NULL))                                     \
        return upipe_register_request(s->FIRST_INNER, urequest);            \
    return upipe_throw_provide_request(upipe, urequest);                    \
}                                                                           \
/** @internal @This unregisters a request to be forwarded downstream.       \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param urequest request to stop forwarding                               \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_unregister_bin_request(struct upipe *upipe,          \
                                              struct urequest *urequest)    \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    ulist_delete(urequest_to_uchain(urequest));                             \
    if (likely(s->FIRST_INNER != NULL))                                     \
        return upipe_unregister_request(s->FIRST_INNER, urequest);          \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This handles the result of a proxy request.                  \
 *                                                                          \
 * @param urequest request provided                                         \
 * @param args optional arguments                                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_provide_bin_proxy(struct urequest *urequest,         \
                                         va_list args)                      \
{                                                                           \
    struct urequest *upstream = urequest_get_opaque(urequest,               \
                                                    struct urequest *);     \
    return urequest_provide_va(upstream, args);                             \
}                                                                           \
/** @internal @This creates and registers a proxy request for an upstream   \
 * request to be forwarded downstream.                                      \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param urequest upstream request to proxy                                \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_alloc_bin_proxy(struct upipe *upipe,                 \
                                       struct urequest *urequest)           \
{                                                                           \
    struct urequest *proxy = malloc(sizeof(struct urequest));               \
    UBASE_ALLOC_RETURN(proxy);                                              \
    urequest_set_opaque(proxy, urequest);                                   \
    struct uref *uref = NULL;                                               \
    if (urequest->uref != NULL &&                                           \
        (uref = uref_dup(urequest->uref)) == NULL) {                        \
        free(proxy);                                                        \
        return UBASE_ERR_ALLOC;                                             \
    }                                                                       \
    urequest_init(proxy, urequest->type, uref,                              \
                  STRUCTURE##_provide_bin_proxy,                            \
                  (urequest_free_func)free);                                \
    return STRUCTURE##_register_bin_request(upipe, proxy);                  \
}                                                                           \
/** @internal @This unregisters and frees a proxy request.                  \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param urequest upstream request to proxy                                \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_free_bin_proxy(struct upipe *upipe,                  \
                                      struct urequest *urequest)            \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    struct uchain *uchain, *uchain_tmp;                                     \
    ulist_delete_foreach (&s->REQUEST_LIST, uchain, uchain_tmp) {           \
        struct urequest *proxy = urequest_from_uchain(uchain);              \
        if (urequest_get_opaque(proxy, struct urequest *) == urequest) {    \
            STRUCTURE##_unregister_bin_request(upipe, proxy);               \
            urequest_clean(proxy);                                          \
            urequest_free(proxy);                                           \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
    }                                                                       \
    return UBASE_ERR_INVALID;                                               \
}                                                                           \
/** @internal @This handles the control commands.                           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param command control command                                           \
 * @param args optional control command arguments                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_control_bin_input(struct upipe *upipe,               \
                                         int command, va_list args)         \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    switch (command) {                                                      \
        case UPIPE_REGISTER_REQUEST: {                                      \
            struct urequest *request = va_arg(args, struct urequest *);     \
            return STRUCTURE##_alloc_bin_proxy(upipe, request);             \
        }                                                                   \
        case UPIPE_UNREGISTER_REQUEST: {                                    \
            struct urequest *request = va_arg(args, struct urequest *);     \
            return STRUCTURE##_free_bin_proxy(upipe, request);              \
        }                                                                   \
        case UPIPE_SET_FLOW_DEF:                                            \
            if (s->FIRST_INNER == NULL)                                     \
                return UBASE_ERR_INVALID;                                   \
            return upipe_control_va(s->FIRST_INNER, command, args);         \
        default:                                                            \
            return UBASE_ERR_UNHANDLED;                                     \
    }                                                                       \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_bin_input(struct upipe *upipe)                \
{                                                                           \
    STRUCTURE##_clean_##FIRST_INNER(upipe);                                 \
}

#ifdef __cplusplus
}
#endif
#endif
