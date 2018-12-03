/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
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
 * @short Upipe helper functions for output
 * It allows to deal with flow definitions, output requests, and output pipes.
 */

#ifndef _UPIPE_UPIPE_HELPER_OUTPUT_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_OUTPUT_H_
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

/** @This represents the state of the output. */
enum upipe_helper_output_state {
    /** no output defined, or no flow def sent */
    UPIPE_HELPER_OUTPUT_NONE,
    /** output defined and flow def accepted */
    UPIPE_HELPER_OUTPUT_VALID,
    /** output defined but flow def rejected */
    UPIPE_HELPER_OUTPUT_INVALID
};

/** @This declares functions dealing with the output of a pipe,
 * and an associated uref which is the flow definition on the output.
 *
 * You must add four members to your private upipe structure, for instance:
 * @code
 *  struct upipe *output;
 *  struct uref *flow_def;
 *  enum upipe_helper_output_state output_state;
 *  struct uchain request_list;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_output(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  void upipe_foo_output(struct upipe *upipe, struct uref *uref,
 *                        struct upump **upump_p)
 * @end code
 * Called whenever you need to send a packet to your output. It takes care
 * of sending the flow definition if necessary.
 *
 * @item @code
 *  int upipe_foo_register_output_request(struct upipe *upipe,
 *                                        struct urequest *urequest)
 * @end code
 * Registers a request on the output. The request will be replayed if the
 * output changes. If there is no output, the request is sent via a probe.
 *
 * @item @code
 *  int upipe_foo_unregister_output_request(struct upipe *upipe,
 *                                          struct urequest *urequest)
 * @end code
 * Unregisters a request on the output.
 *
 * @item @code
 *  int upipe_foo_provide_output_proxy(struct urequest *urequest, va_list args)
 * @end code
 * Internal function used to receive answers to proxy requests.
 *
 * @item @code
 *  int upipe_foo_alloc_output_proxy(struct upipe *upipe,
 *                                   struct urequest *urequest)
 * @end code
 * Allocates a proxy request to forward a request downstream.
 *
 * @item @code
 *  int upipe_foo_free_output_proxy(struct upipe *upipe,
 *                                  struct urequest *urequest)
 * @end code
 * Frees a proxy request.
 *
 * @item @code
 *  void upipe_foo_store_flow_def(struct upipe *upipe, struct uref *flow_def)
 * @end code
 * Called whenever you change the flow definition on this output.
 *
 * @item @code
 *  int upipe_foo_get_flow_def(struct upipe *upipe, struct uref **p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_GET_FLOW_DEF: {
 *      struct uref **p = va_arg(args, struct uref **);
 *      return upipe_foo_get_flow_def(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  int upipe_foo_get_output(struct upipe *upipe, struct upipe **p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_GET_OUTPUT: {
 *      struct upipe **p = va_arg(args, struct upipe **);
 *      return upipe_foo_get_output(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  int upipe_foo_set_output(struct upipe *upipe, struct upipe *output)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SET_OUTPUT: {
 *      struct upipe *output = va_arg(args, struct upipe *);
 *      return upipe_foo_set_output(upipe, output);
 *  }
 * @end code
 *
 * @item @code
 *  int upipe_foo_control_output(struct upipe *upipe,
 *                               int command, va_list args)
 * @end code
 * This function handles the output helper control commands
 * (upipe_foo_get_flow_def, upipe_foo_get_output, upipe_foo_set_output, ...).
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  int upipe_foo_control(struct upipe *upipe, int command, va_list args)
 *  {
 *      ...
 *      UBASE_HANDLED_RETURN(upipe_foo_control_output(upipe, command, args));
 *      ...
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_clean_output(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param OUTPUT name of the @tt {struct upipe *} field of
 * your private upipe structure
 * @param FLOW_DEF name of the @tt{struct uref *} field of
 * your private upipe structure
 * @param OUTPUT_STATE name of the @tt{enum upipe_helper_output_state} field of
 * your private upipe structure
 * @param REQUEST_LIST name of the @tt{struct uchain} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_OUTPUT(STRUCTURE, OUTPUT, FLOW_DEF, OUTPUT_STATE,      \
                            REQUEST_LIST)                                   \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_output(struct upipe *upipe)                    \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    s->OUTPUT = NULL;                                                       \
    s->FLOW_DEF = NULL;                                                     \
    s->OUTPUT_STATE = UPIPE_HELPER_OUTPUT_NONE;                             \
    ulist_init(&s->REQUEST_LIST);                                           \
}                                                                           \
/** @internal @This sends a uref to the output. Note that uref is then      \
 * owned by the callee and shouldn't be used any longer.                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param uref uref structure to send (may be NULL)                         \
 * @param upump_p reference to pump that generated the buffer               \
 */                                                                         \
static UBASE_UNUSED void STRUCTURE##_output(struct upipe *upipe,            \
                                            struct uref *uref,              \
                                            struct upump **upump_p)         \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (unlikely(s->FLOW_DEF == NULL)) {                                    \
        upipe_warn(upipe, "no flow def, dropping uref");                    \
        uref_free(uref);                                                    \
        return;                                                             \
    }                                                                       \
    if (unlikely(s->OUTPUT == NULL))                                        \
        upipe_throw_need_output(upipe, s->FLOW_DEF);                        \
    if (unlikely(s->OUTPUT == NULL)) {                                      \
        uref_free(uref);                                                    \
        return;                                                             \
    }                                                                       \
                                                                            \
    bool already_retried = false;                                           \
    for ( ; ; ) {                                                           \
        if (unlikely(s->FLOW_DEF == NULL)) {                                \
            upipe_warn(upipe, "no flow def, dropping uref");                \
            uref_free(uref);                                                \
            return;                                                         \
        }                                                                   \
        switch (s->OUTPUT_STATE) {                                          \
            case UPIPE_HELPER_OUTPUT_NONE: {                                \
                int err = upipe_set_flow_def(s->OUTPUT, s->FLOW_DEF);       \
                if (likely(ubase_check(err))) {                             \
                    upipe_dbg(s->OUTPUT, "accepted flow def");              \
                    s->OUTPUT_STATE = UPIPE_HELPER_OUTPUT_VALID;            \
                    continue;                                               \
                }                                                           \
                upipe_dbg(s->OUTPUT, "rejected flow def");                  \
                upipe_throw_error(s->OUTPUT, err);                          \
                struct upipe *output = upipe_use(s->OUTPUT);                \
                upipe_throw_need_output(upipe, s->FLOW_DEF);                \
                if (output == s->OUTPUT || already_retried)                 \
                    s->OUTPUT_STATE = UPIPE_HELPER_OUTPUT_INVALID;          \
                else                                                        \
                    already_retried = true;                                 \
                upipe_release(output);                                      \
                continue;                                                   \
            }                                                               \
                                                                            \
            case UPIPE_HELPER_OUTPUT_VALID:                                 \
                if (uref != NULL)                                           \
                    upipe_input(s->OUTPUT, uref, upump_p);                  \
                return;                                                     \
                                                                            \
            case UPIPE_HELPER_OUTPUT_INVALID:                               \
                upipe_warn(upipe, "invalid output, dropping uref");         \
                uref_free(uref);                                            \
                return;                                                     \
        }                                                                   \
    }                                                                       \
}                                                                           \
/** @internal @This registers a request to be forwarded downstream. The     \
 * request will be replayed if the output changes. If there is no output,   \
 * the request will be sent via a probe.                                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param urequest request to forward                                       \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_register_output_request(struct upipe *upipe,         \
                                               struct urequest *urequest)   \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    ulist_add(&s->REQUEST_LIST, urequest_to_uchain(urequest));              \
    int err;                                                                \
    if (likely(s->OUTPUT != NULL &&                                         \
               (err = upipe_register_request(s->OUTPUT, urequest))          \
                 != UBASE_ERR_UNHANDLED))                                   \
        return err;                                                         \
    return upipe_throw_provide_request(upipe, urequest);                    \
}                                                                           \
/** @internal @This unregisters a request to be forwarded downstream.       \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param urequest request to stop forwarding                               \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_unregister_output_request(struct upipe *upipe,       \
                                                 struct urequest *urequest) \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    ulist_delete(urequest_to_uchain(urequest));                             \
    int err;                                                                \
    if (likely(s->OUTPUT != NULL && urequest->registered &&                 \
               (err = upipe_unregister_request(s->OUTPUT, urequest))        \
                 != UBASE_ERR_UNHANDLED))                                   \
        return err;                                                         \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This handles the result of a proxy request.                  \
 *                                                                          \
 * @param urequest request provided                                         \
 * @param args optional arguments                                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_provide_output_proxy(struct urequest *urequest,      \
                                            va_list args)                   \
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
static int UBASE_UNUSED STRUCTURE##_alloc_output_proxy(struct upipe *upipe, \
                                                 struct urequest *urequest) \
{                                                                           \
    struct urequest *proxy =                                                \
        (struct urequest *)malloc(sizeof(struct urequest));                 \
    UBASE_ALLOC_RETURN(proxy);                                              \
    urequest_set_opaque(proxy, urequest);                                   \
    struct uref *uref = NULL;                                               \
    if (urequest->uref != NULL &&                                           \
        (uref = uref_dup(urequest->uref)) == NULL) {                        \
        free(proxy);                                                        \
        return UBASE_ERR_ALLOC;                                             \
    }                                                                       \
    urequest_init(proxy, urequest->type, uref,                              \
                  STRUCTURE##_provide_output_proxy,                         \
                  (urequest_free_func)free);                                \
    return STRUCTURE##_register_output_request(upipe, proxy);               \
}                                                                           \
/** @internal @This unregisters and frees a proxy request.                  \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param urequest upstream request to proxy                                \
 * @return an error code                                                    \
 */                                                                         \
static int UBASE_UNUSED STRUCTURE##_free_output_proxy(struct upipe *upipe,  \
                                                struct urequest *urequest)  \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    struct uchain *uchain, *uchain_tmp;                                     \
    ulist_delete_foreach (&s->REQUEST_LIST, uchain, uchain_tmp) {           \
        struct urequest *proxy = urequest_from_uchain(uchain);              \
        if (urequest_get_opaque(proxy, struct urequest *) == urequest) {    \
            STRUCTURE##_unregister_output_request(upipe, proxy);            \
            urequest_clean(proxy);                                          \
            urequest_free(proxy);                                           \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
    }                                                                       \
    return UBASE_ERR_INVALID;                                               \
}                                                                           \
/** @internal @This stores the flow definition to use on the output.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_def control packet describing the output (we keep a pointer  \
 * to it so make sure it belongs to us)                                     \
 */                                                                         \
static void STRUCTURE##_store_flow_def(struct upipe *upipe,                 \
                                       struct uref *flow_def)               \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->FLOW_DEF != NULL && s->FLOW_DEF->udict != NULL &&                \
        flow_def != NULL && flow_def->udict != NULL &&                      \
        !udict_cmp(s->FLOW_DEF->udict, flow_def->udict)) {                  \
        uref_free(s->FLOW_DEF);                                             \
        s->FLOW_DEF = flow_def; /* doesn't change the state */              \
        return;                                                             \
    }                                                                       \
    if (unlikely(s->FLOW_DEF != NULL))                                      \
        uref_free(s->FLOW_DEF);                                             \
    s->OUTPUT_STATE = UPIPE_HELPER_OUTPUT_NONE;                             \
    s->FLOW_DEF = flow_def;                                                 \
    if (flow_def != NULL)                                                   \
        upipe_throw_new_flow_def(upipe, flow_def);                          \
}                                                                           \
/** @internal @This handles the get_flow_def control command.               \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the flow definition (to use on the output)       \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_get_flow_def(struct upipe *upipe, struct uref **p)   \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    assert(p != NULL);                                                      \
    *p = s->FLOW_DEF;                                                       \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This handles the get_output control command.                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the output                                       \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_get_output(struct upipe *upipe, struct upipe **p)    \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    assert(p != NULL);                                                      \
    *p = s->OUTPUT;                                                         \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This handles the set_output control command.                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output new output                                                 \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_set_output(struct upipe *upipe, struct upipe *output)\
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
                                                                            \
    s->OUTPUT = upipe_use(output);                                          \
    s->OUTPUT_STATE = UPIPE_HELPER_OUTPUT_NONE;                             \
    if (unlikely(s->OUTPUT == NULL))                                        \
        return UBASE_ERR_NONE;                                              \
                                                                            \
    /* Retry in a loop because the request list may change at any point. */ \
    for ( ; ; ) {                                                           \
        struct urequest *urequest = NULL;                                   \
        struct uchain *uchain;                                              \
        ulist_foreach (&s->REQUEST_LIST, uchain) {                          \
            struct urequest *urequest_chain = urequest_from_uchain(uchain); \
            if (!urequest_chain->registered) {                              \
                urequest = urequest_chain;                                  \
                break;                                                      \
            }                                                               \
        }                                                                   \
        if (urequest != NULL) {                                             \
            upipe_register_request(s->OUTPUT, urequest);                    \
            continue;                                                       \
        }                                                                   \
        break;                                                              \
    }                                                                       \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @This handles get/set output controls.                                  \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param command type of command to process                                \
 * @param args optional arguments                                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_control_output(struct upipe *upipe,                  \
                                      int command,                          \
                                      va_list args)                         \
{                                                                           \
    int ret = UBASE_ERR_UNHANDLED;                                          \
    va_list args_copy;                                                      \
    va_copy(args_copy, args);                                               \
    switch (command) {                                                      \
        case UPIPE_REGISTER_REQUEST: {                                      \
            struct urequest *urequest =                                     \
                va_arg(args_copy, struct urequest *);                       \
            ret = STRUCTURE##_alloc_output_proxy(upipe, urequest);          \
            break;                                                          \
        }                                                                   \
        case UPIPE_UNREGISTER_REQUEST: {                                    \
            struct urequest *urequest =                                     \
                va_arg(args_copy, struct urequest *);                       \
            ret = STRUCTURE##_free_output_proxy(upipe, urequest);           \
            break;                                                          \
        }                                                                   \
        case UPIPE_GET_FLOW_DEF: {                                          \
            struct uref **flow_def_p = va_arg(args_copy, struct uref **);   \
            ret = STRUCTURE##_get_flow_def(upipe, flow_def_p);              \
            break;                                                          \
        }                                                                   \
        case UPIPE_GET_OUTPUT: {                                            \
            struct upipe **output_p = va_arg(args_copy, struct upipe **);   \
            ret =  STRUCTURE##_get_output(upipe, output_p);                 \
            break;                                                          \
        }                                                                   \
        case UPIPE_SET_OUTPUT: {                                            \
            struct upipe *output = va_arg(args_copy, struct upipe *);       \
            ret = STRUCTURE##_set_output(upipe, output);                    \
            break;                                                          \
        }                                                                   \
    }                                                                       \
    va_end(args_copy);                                                      \
    return ret;                                                             \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_output(struct upipe *upipe)                   \
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
    upipe_release(s->OUTPUT);                                               \
    s->OUTPUT = NULL;                                                       \
    uref_free(s->FLOW_DEF);                                                 \
    s->FLOW_DEF = NULL;                                                     \
}

#ifdef __cplusplus
}
#endif
#endif
