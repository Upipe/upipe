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
 * @short Upipe helper functions for uref manager
 */

#ifndef _UPIPE_UPIPE_HELPER_UREF_MGR_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UREF_MGR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/urequest.h>

#include <stdbool.h>

/** @This defines a function that will be called after a uref_mgr has been
 * received. The second argument is an unused uref. */
typedef int (*upipe_helper_uref_mgr_check)(struct upipe *, struct uref *);

/** @This defines a function that will be called to register or unregister a
 * request. */
typedef int (*upipe_helper_uref_mgr_register)(struct upipe *, struct urequest *);

/** @This declares five functions dealing with the uref manager.
 *
 * You must add two members to your private upipe structure, for instance:
 * @code
 *  struct uref_mgr *uref_mgr;
 *  struct urequest uref_mgr_request;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro,
 * and provide three functions which will be called 1/ when the uref manager is
 * provided, 2/ and 3/ when a request needs to be registered/unregistered.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_uref_mgr(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  int upipe_foo_provide_uref_mgr(struct upipe *upipe, va_list args)
 * @end code
 * Internal function called when the request is answered.
 *
 * @item @code
 *  int upipe_foo_require_uref_mgr(struct upipe *upipe)
 * @end code
 * Initializes and registers the request to get a uref manager.
 *
 * @item @code
 *  bool upipe_foo_demand_uref_mgr(struct upipe *upipe)
 * @end code
 * Initializes and registers the request to get a uref manager, and send it
 * via a probe if no answer has been received synchronously. Returns false
 * if no uref_mgr was received.
 *
 * @item @code
 *  void upipe_foo_clean_uref_mgr(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param UREF_MGR name of the @tt {struct uref_mgr *} field of
 * your private upipe structure
 * @param REQUEST name of the @tt {struct urequest} field of
 * your private upipe structure
 * @param CHECK function called after a uref manager has been received
 * @param REGISTER function called to register a request
 * @param UNREGISTER function called to unregister a request
 */
#define UPIPE_HELPER_UREF_MGR(STRUCTURE, UREF_MGR, REQUEST, CHECK,          \
                              REGISTER, UNREGISTER)                         \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_uref_mgr(struct upipe *upipe)                  \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    s->UREF_MGR = NULL;                                                     \
    urequest_set_opaque(&s->REQUEST, NULL);                                 \
}                                                                           \
/** @internal @This handles the result of a uref manager request.           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_provide_uref_mgr(struct urequest *urequest,          \
                                        va_list args)                       \
{                                                                           \
    struct upipe *upipe = urequest_get_opaque(urequest, struct upipe *);    \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);            \
    if (uref_mgr == s->UREF_MGR) {                                          \
        uref_mgr_release(uref_mgr);                                         \
        return UBASE_ERR_NONE;                                              \
    }                                                                       \
    uref_mgr_release(s->UREF_MGR);                                          \
    s->UREF_MGR = uref_mgr;                                                 \
    upipe_dbg_va(upipe, "provided uref_mgr %p", s->UREF_MGR);               \
    upipe_helper_uref_mgr_check check = CHECK;                              \
    return check != NULL ? check(upipe, NULL) : UBASE_ERR_NONE;             \
}                                                                           \
/** @internal @This registers a request to get a uref manager.              \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_require_uref_mgr(struct upipe *upipe)               \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    upipe_helper_uref_mgr_register reg = REGISTER;                          \
    upipe_helper_uref_mgr_register unreg = UNREGISTER;                      \
    if (urequest_get_opaque(&s->REQUEST, struct upipe *) != NULL) {         \
        if (unreg != NULL)                                                  \
            unreg(upipe, &s->REQUEST);                                      \
        urequest_clean(&s->REQUEST);                                        \
        uref_mgr_release(s->UREF_MGR);                                      \
        s->UREF_MGR = NULL;                                                 \
    }                                                                       \
    urequest_init_uref_mgr(&s->REQUEST,                                     \
                           STRUCTURE##_provide_uref_mgr, NULL);             \
    urequest_set_opaque(&s->REQUEST, upipe);                                \
    upipe_dbg(upipe, "require uref_mgr");                                   \
    if (reg != NULL)                                                        \
        reg(upipe, &s->REQUEST);                                            \
}                                                                           \
/** @internal @This registers a request to get a uref manager, and also     \
 * send it via a probe if nothing has been received synchronously.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return false if the uref manager couldn't be allocated                  \
 */                                                                         \
static UBASE_UNUSED bool STRUCTURE##_demand_uref_mgr(struct upipe *upipe)   \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    STRUCTURE##_require_uref_mgr(upipe);                                    \
    if (unlikely(s->UREF_MGR == NULL))                                      \
        upipe_throw_provide_request(upipe, &s->REQUEST);                    \
    return s->UREF_MGR != NULL;                                             \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_uref_mgr(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    uref_mgr_release(s->UREF_MGR);                                          \
    /* If the request was registered, it should be unregistered             \
     * automatically. Otherwise it has not been initialized. */             \
}

#ifdef __cplusplus
}
#endif
#endif
