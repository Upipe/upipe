/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe helper functions for ubuf manager
 */

#ifndef _UPIPE_UPIPE_HELPER_UBUF_MGR_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UBUF_MGR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/urequest.h>

#include <stdbool.h>

/** @This defines a function that will be called after a ubuf_mgr has been
 * received. The second argument is the amended flow format (belongs to the
 * callee). */
typedef int (*upipe_helper_ubuf_mgr_check)(struct upipe *, struct uref *);

/** @This defines a function that will be called to register or unregister a
 * request. */
typedef int (*upipe_helper_ubuf_mgr_register)(struct upipe *, struct urequest *);

/** @This declares functions dealing with the ubuf manager used on the output
 * of a pipe.
 *
 * You must add three members to your private upipe structure, for instance:
 * @code
 *  struct ubuf_mgr *ubuf_mgr;
 *  struct uref *flow_format;
 *  struct urequest ubuf_mgr_request;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro,
 * and provide two functions which will be called 1/ when the ubuf manager is
 * provided, 2/ and 3/ when a request needs to be registered/unregistered.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_ubuf_mgr(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  int upipe_foo_provide_ubuf_mgr(struct upipe *upipe, va_list args)
 * @end code
 * Internal function called when the request is answered.
 *
 * @item @code
 *  int upipe_foo_require_ubuf_mgr(struct upipe *upipe,
 *                                 struct uref *flow_format)
 * @end code
 * Initializes and registers the request to get a ubuf manager. The flow
 * format belongs to the callee and will be eventually freed.
 *
 * @item @code
 *  bool upipe_foo_demand_ubuf_mgr(struct upipe *upipe,
                                   struct uref *flow_format)
 * @end code
 * Initializes and registers the request to get a ubuf manager, and send it
 * via a probe if no answer has been received synchronously. Returns false
 * if no ubuf_mgr was received.
 *
 * @item @code
 *  void upipe_foo_clean_ubuf_mgr(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 *
 * @item @code
 *  int upipe_foo_control_ubuf_mgr(struct upipe *upipe, int command,
 *                                 va_list args);
 * @end code
 * Typically called from your upipe_foo_control function. Make sure to call
 * this function before the output control helper function
 * upipe_foo_control_output.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param UBUF_MGR name of the @tt {struct ubuf_mgr *} field of
 * your private upipe structure
 * @param FLOW_FORMAT name of the @tt {struct uref *} field of
 * your private upipe structure
 * @param REQUEST name of the @tt {struct urequest} field of
 * your private upipe structure
 * @param CHECK function called after a uref manager has been received
 * @param REGISTER function called to register a request
 * @param UNREGISTER function called to unregister a request
 */
#define UPIPE_HELPER_UBUF_MGR(STRUCTURE, UBUF_MGR, FLOW_FORMAT, REQUEST,    \
                              CHECK, REGISTER, UNREGISTER)                  \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_ubuf_mgr(struct upipe *upipe)                  \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    s->UBUF_MGR = NULL;                                                     \
    s->FLOW_FORMAT = NULL;                                                  \
    urequest_set_opaque(&s->REQUEST, NULL);                                 \
}                                                                           \
/** @internal @This handles the result of a ubuf manager request.           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_provide_ubuf_mgr(struct urequest *urequest,          \
                                        va_list args)                       \
{                                                                           \
    struct upipe *upipe = urequest_get_opaque(urequest, struct upipe *);    \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);            \
    struct uref *flow_format = va_arg(args, struct uref *);                 \
    if (ubuf_mgr == s->UBUF_MGR && s->FLOW_FORMAT != NULL &&                \
        s->FLOW_FORMAT->udict != NULL && flow_format->udict != NULL &&      \
        !udict_cmp(s->FLOW_FORMAT->udict, flow_format->udict)) {            \
        ubuf_mgr_release(ubuf_mgr);                                         \
        uref_free(flow_format);                                             \
        return UBASE_ERR_NONE;                                              \
    }                                                                       \
    ubuf_mgr_release(s->UBUF_MGR);                                          \
    s->UBUF_MGR = ubuf_mgr;                                                 \
    upipe_dbg_va(upipe, "provided ubuf_mgr %p", s->UBUF_MGR);               \
    uref_free(s->FLOW_FORMAT);                                              \
    s->FLOW_FORMAT = uref_dup(flow_format);                                 \
    upipe_helper_ubuf_mgr_check check = CHECK;                              \
    if (check != NULL)                                                      \
        return check(upipe, flow_format);                                   \
    uref_free(flow_format);                                                 \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This registers a request to get a ubuf manager.              \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_format flow format for which a ubuf is required (belongs     \
 * to the callee)                                                           \
 */                                                                         \
static void STRUCTURE##_require_ubuf_mgr(struct upipe *upipe,               \
                                         struct uref *flow_format)          \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    upipe_helper_ubuf_mgr_register reg = REGISTER;                          \
    upipe_helper_ubuf_mgr_register unreg = UNREGISTER;                      \
    assert(flow_format != NULL);                                            \
    if (urequest_get_opaque(&s->REQUEST, struct upipe *) != NULL) {         \
        if (unreg != NULL)                                                  \
            unreg(upipe, &s->REQUEST);                                      \
        urequest_clean(&s->REQUEST);                                        \
        ubuf_mgr_release(s->UBUF_MGR);                                      \
        s->UBUF_MGR = NULL;                                                 \
    }                                                                       \
    urequest_init_ubuf_mgr(&s->REQUEST, flow_format,                        \
                           STRUCTURE##_provide_ubuf_mgr, NULL);             \
    urequest_set_opaque(&s->REQUEST, upipe);                                \
    upipe_dbg(upipe, "require ubuf_mgr");                                   \
    if (reg != NULL)                                                        \
        reg(upipe, &s->REQUEST);                                            \
}                                                                           \
/** @internal @This registers a request to get a ubuf manager, and also     \
 * send it via a probe if nothing has been received synchronously.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_format flow format for which a ubuf manager is required      \
 * (belongs to the callee)                                                  \
 * @return false if the ubuf manager couldn't be allocated                  \
 */                                                                         \
static UBASE_UNUSED bool STRUCTURE##_demand_ubuf_mgr(struct upipe *upipe,   \
                                        struct uref *flow_format)           \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    STRUCTURE##_require_ubuf_mgr(upipe, flow_format);                       \
    if (unlikely(s->UBUF_MGR == NULL))                                      \
        upipe_throw_provide_request(upipe, &s->REQUEST);                    \
    return s->UBUF_MGR != NULL;                                             \
}                                                                           \
/** @internal @This cleans up the private members of this helper.           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_ubuf_mgr(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    ubuf_mgr_release(s->UBUF_MGR);                                          \
    uref_free(s->FLOW_FORMAT);                                              \
    /* If the request was registered, it should be unregistered             \
     * automatically. Otherwise it has not been initialized. */             \
}                                                                           \
/** @internal @This handles the ubuf manager and flow format request        \
 * register/unregister.                                                     \
 *                                                                          \
 * Make sure to call this helper before the control output helper which     \
 * handle all the register/unregister request.                              \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param command control command to handle if needed                       \
 * @param args optional arguments                                           \
 * @return an error code                                                    \
 */                                                                         \
static UBASE_UNUSED int STRUCTURE##_control_ubuf_mgr(struct upipe *upipe,   \
                                                     int command,           \
                                                     va_list args)          \
{                                                                           \
    if (command != UPIPE_REGISTER_REQUEST &&                                \
        command != UPIPE_UNREGISTER_REQUEST)                                \
        return UBASE_ERR_UNHANDLED;                                         \
    va_list args_copy;                                                      \
    va_copy(args_copy, args);                                               \
    struct urequest *urequest = va_arg(args_copy, struct urequest *);       \
    int ret = UBASE_ERR_UNHANDLED;                                          \
    switch (command) {                                                      \
        case UPIPE_REGISTER_REQUEST:                                        \
            if (urequest->type == UREQUEST_UBUF_MGR ||                      \
                urequest->type == UREQUEST_FLOW_FORMAT)                     \
                ret = upipe_throw_provide_request(upipe, urequest);         \
            break;                                                          \
        case UPIPE_UNREGISTER_REQUEST:                                      \
            if (urequest->type == UREQUEST_UBUF_MGR ||                      \
                urequest->type == UREQUEST_FLOW_FORMAT)                     \
                ret = UBASE_ERR_NONE;                                       \
            break;                                                          \
    }                                                                       \
    va_end(args_copy);                                                      \
    return ret;                                                             \
}

#ifdef __cplusplus
}
#endif
#endif
