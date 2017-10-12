/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe module generating regular void uref
 */

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>

#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upipe/uref_void_flow.h>
#include <upipe/uref_clock.h>

#include <upipe-modules/upipe_void_source.h>

#define EXPECTED_FLOW_DEF   "void."

/** @internal @This is the private structure for a void source pipe. */
struct upipe_voidsrc {
    /** public pipe structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** output pipe */
    struct upipe *output;
    /** output flow format */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain requests;
    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;
    /** uclock */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** upump timer */
    struct upump *timer;
    /** timer interval */
    uint64_t interval;
    /** current pts */
    uint64_t pts;
};

/** @hidden */
static void upipe_voidsrc_worker(struct upump *upump);
/** @hidden */
static int upipe_voidsrc_check(struct upipe *upipe, struct uref *flow);

UPIPE_HELPER_UPIPE(upipe_voidsrc, upipe, UPIPE_VOIDSRC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_voidsrc, urefcount, upipe_voidsrc_free);
UPIPE_HELPER_FLOW(upipe_voidsrc, EXPECTED_FLOW_DEF);
UPIPE_HELPER_OUTPUT(upipe_voidsrc, output, flow_def, output_state, requests);
UPIPE_HELPER_UREF_MGR(upipe_voidsrc, uref_mgr, uref_mgr_request,
                      upipe_voidsrc_check,
                      upipe_voidsrc_register_output_request,
                      upipe_voidsrc_unregister_output_request);
UPIPE_HELPER_UCLOCK(upipe_voidsrc, uclock, uclock_request,
                    upipe_voidsrc_check,
                    upipe_voidsrc_register_output_request,
                    upipe_voidsrc_unregister_output_request);
UPIPE_HELPER_UPUMP_MGR(upipe_voidsrc, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_voidsrc, timer, upump_mgr);

/** @internal @This frees a void source pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_voidsrc_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_voidsrc_clean_timer(upipe);
    upipe_voidsrc_clean_upump_mgr(upipe);
    upipe_voidsrc_clean_uclock(upipe);
    upipe_voidsrc_clean_uref_mgr(upipe);
    upipe_voidsrc_clean_output(upipe);
    upipe_voidsrc_clean_urefcount(upipe);
    upipe_voidsrc_free_flow(upipe);
}

/** @internal @This allocates and initializes a void source pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an alloocated and initialized pipe or NULL
 */
static struct upipe *upipe_voidsrc_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct uref *flow;
    struct upipe *upipe = upipe_voidsrc_alloc_flow(mgr, uprobe,
                                                   signature, args,
                                                   &flow);
    if (unlikely(!upipe))
        return NULL;

    uint64_t duration = 0;
    if (unlikely(!ubase_check(
        uref_clock_get_duration(flow, &duration)))) {
        uref_free(flow);
        upipe_voidsrc_free_flow(upipe);
        return NULL;
    }

    upipe_voidsrc_init_urefcount(upipe);
    upipe_voidsrc_init_output(upipe);
    upipe_voidsrc_init_uref_mgr(upipe);
    upipe_voidsrc_init_uclock(upipe);
    upipe_voidsrc_init_upump_mgr(upipe);
    upipe_voidsrc_init_timer(upipe);

    struct upipe_voidsrc *upipe_voidsrc = upipe_voidsrc_from_upipe(upipe);
    upipe_voidsrc->interval = duration;
    upipe_voidsrc->pts = UINT64_MAX;

    upipe_throw_ready(upipe);

    upipe_voidsrc_store_flow_def(upipe, flow);

    return upipe;
}

/** @internal @This creates regularly empty urefs.
 *
 * @param upump description structure of the timer
 */
static void upipe_voidsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_voidsrc *upipe_voidsrc = upipe_voidsrc_from_upipe(upipe);
    uint64_t now;

    if (upipe_voidsrc->pts == UINT64_MAX)
        upipe_voidsrc->pts = uclock_now(upipe_voidsrc->uclock);

    for (now = uclock_now(upipe_voidsrc->uclock);
         !upipe_single(upipe) && upipe_voidsrc->pts <= now;
         now = uclock_now(upipe_voidsrc->uclock)) {
        struct uref *uref = uref_alloc(upipe_voidsrc->uref_mgr);
        if (unlikely(!uref)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        uref_clock_set_duration(uref, upipe_voidsrc->interval);
        uref_clock_set_pts_sys(uref, upipe_voidsrc->pts);
        uref_clock_set_pts_prog(uref, upipe_voidsrc->pts);
        upipe_voidsrc->pts += upipe_voidsrc->interval;

        upipe_voidsrc_output(upipe, uref, &upipe_voidsrc->timer);
    }

    if (!upipe_single(upipe))
        upipe_voidsrc_wait_timer(upipe, upipe_voidsrc->pts - now,
                                 upipe_voidsrc_worker);
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow requested output flow format
 * @return an error code
 */
static int upipe_voidsrc_check(struct upipe *upipe, struct uref *flow)
{
    struct upipe_voidsrc *upipe_voidsrc = upipe_voidsrc_from_upipe(upipe);

    if (flow != NULL)
        upipe_voidsrc_store_flow_def(upipe, flow);

    if (!upipe_voidsrc->uref_mgr) {
        upipe_voidsrc_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (!upipe_voidsrc->uclock) {
        upipe_voidsrc_require_uclock(upipe);
        return UBASE_ERR_NONE;
    }

    upipe_voidsrc_check_upump_mgr(upipe);
    if (unlikely(!upipe_voidsrc->upump_mgr))
        return UBASE_ERR_NONE;

    if (!upipe_voidsrc->timer) {
        struct upump *timer = upump_alloc_timer(upipe_voidsrc->upump_mgr,
                                                upipe_voidsrc_worker,
                                                upipe, upipe->refcount,
                                                upipe_voidsrc->interval, 0);
        if (unlikely(!timer)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_voidsrc_set_timer(upipe, timer);
        upump_start(timer);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This handles control command of void source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_voidsrc_control_real(struct upipe *upipe, int command,
                                      va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_voidsrc_set_timer(upipe, NULL);
            return upipe_voidsrc_attach_upump_mgr(upipe);
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_GET_FLOW_DEF:
            return upipe_voidsrc_control_output(upipe, command, args);
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This handles control and checks if the timer may be created.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_voidsrc_control(struct upipe *upipe, int command,
                                 va_list args)
{
    UBASE_RETURN(upipe_voidsrc_control_real(upipe, command, args));
    return upipe_voidsrc_check(upipe, NULL);
}

/** @internal @This is the static void source pipe. */
static struct upipe_mgr upipe_voidsrc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_VOIDSRC_SIGNATURE,

    .upipe_alloc = upipe_voidsrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_voidsrc_control,
};

/** @This returns the static void source pipe manager.
 *
 * @return a pointer to the manager
 */
struct upipe_mgr *upipe_voidsrc_mgr_alloc(void)
{
    return &upipe_voidsrc_mgr;
}
