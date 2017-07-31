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
 * @short Upipe module facilitating trick play operations
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-modules/upipe_trickplay.h>

#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <stdarg.h>
#include <assert.h>

/** @hidden */
static uint64_t upipe_trickp_get_date_sys(struct upipe *upipe, uint64_t ts);
/** @hidden */
static int upipe_trickp_check_start(struct upipe *upipe, struct uref *);
/** @hidden */
static bool upipe_trickp_sub_process(struct upipe *upipe, struct uref *uref,
                                     struct upump **upump);

/** @internal @This is the private context of a trickp pipe. */
struct upipe_trickp {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uclock structure */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** origins of timestamps */
    uint64_t ts_origin;
    /** offset of systimes */
    uint64_t systime_offset;
    /** true if we are in preroll */
    bool preroll;

    /** current rate */
    struct urational rate;
    /** list of subs */
    struct uchain subs;

    /** manager to create subs */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_trickp, upipe, UPIPE_TRICKP_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_trickp, urefcount, upipe_trickp_free)
UPIPE_HELPER_VOID(upipe_trickp)
UPIPE_HELPER_UCLOCK(upipe_trickp, uclock, uclock_request,
                    upipe_trickp_check_start, upipe_throw_provide_request, NULL)

/** @internal @This is the type of the flow (different behaviours). */
enum upipe_trickp_sub_type {
    UPIPE_TRICKP_UNKNOWN,
    UPIPE_TRICKP_PIC,
    UPIPE_TRICKP_SOUND,
    UPIPE_TRICKP_SUBPIC
};

/** @internal @This is the private context of an output of a trickp pipe. */
struct upipe_trickp_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** type of the flow */
    enum upipe_trickp_sub_type type;
    /** temporary uref storage */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet on this output */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_trickp_sub, upipe, UPIPE_TRICKP_SUB_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_trickp_sub, urefcount, upipe_trickp_sub_free)
UPIPE_HELPER_VOID(upipe_trickp_sub)
UPIPE_HELPER_OUTPUT(upipe_trickp_sub, output, flow_def, output_state, request_list)
UPIPE_HELPER_INPUT(upipe_trickp_sub, urefs, nb_urefs, max_urefs, blockers, upipe_trickp_sub_process)

UPIPE_HELPER_SUBPIPE(upipe_trickp, upipe_trickp_sub, sub, sub_mgr, subs, uchain)

/** @internal @This allocates an output subpipe of a trickp pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_trickp_sub_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature,
                                            va_list args)
{
    struct upipe *upipe = upipe_trickp_sub_alloc_void(mgr, uprobe, signature,
                                                      args);
    if (unlikely(upipe == NULL))
        return NULL;
    upipe_trickp_sub_init_urefcount(upipe);
    upipe_trickp_sub_init_output(upipe);
    upipe_trickp_sub_init_input(upipe);
    upipe_trickp_sub_init_sub(upipe);
    struct upipe_trickp_sub *upipe_trickp_sub =
        upipe_trickp_sub_from_upipe(upipe);
    ulist_init(&upipe_trickp_sub->urefs);
    upipe_trickp_sub->type = UPIPE_TRICKP_UNKNOWN;

    struct upipe_trickp *upipe_trickp = upipe_trickp_from_sub_mgr(upipe->mgr);
    if (upipe_trickp->rate.den)
        upipe_trickp_sub->max_urefs = UINT_MAX;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This processes data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return true if the uref was processed
 */
static bool upipe_trickp_sub_process(struct upipe *upipe, struct uref *uref,
                                     struct upump **upump_p)
{
    struct upipe_trickp *upipe_trickp = upipe_trickp_from_sub_mgr(upipe->mgr);
    if (upipe_trickp->rate.num == 0 || upipe_trickp->rate.den == 0) {
        /* pause */
        return false;
    }

    uref_clock_set_rate(uref, upipe_trickp->rate);
    uint64_t date;
    int type;
    uref_clock_get_date_prog(uref, &date, &type);
    if (likely(type != UREF_DATE_NONE)) {
        uint64_t date_sys =
                upipe_trickp_get_date_sys(upipe_trickp_to_upipe(upipe_trickp),
                                          date);
        uref_clock_set_date_sys(uref, date_sys, type);
        upipe_verbose_va(upipe, "stamping %"PRIu64" -> %"PRIu64,
                         date, date_sys);
    }

    upipe_trickp_sub_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_trickp_sub_input(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p)
{
    struct upipe_trickp *upipe_trickp = upipe_trickp_from_sub_mgr(upipe->mgr);

    if (upipe_trickp->uclock == NULL || upipe_trickp->rate.num == 0 ||
        upipe_trickp->rate.den == 0) {
        /* pause */
        upipe_trickp_sub_hold_input(upipe, uref);
        upipe_trickp_sub_block_input(upipe, upump_p);
    } else if (upipe_trickp->systime_offset == 0) {
        upipe_trickp_sub_hold_input(upipe, uref);
        upipe_trickp_check_start(upipe_trickp_to_upipe(upipe_trickp), NULL);
    } else
        upipe_trickp_sub_process(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_trickp_sub_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct upipe_trickp_sub *upipe_trickp_sub =
        upipe_trickp_sub_from_upipe(upipe);
    const char *def;
    if (likely(ubase_check(uref_flow_get_def(flow_def, &def)))) {
        if (!ubase_ncmp(def, "pic.sub.") || strstr(def, ".pic.sub."))
            upipe_trickp_sub->type = UPIPE_TRICKP_SUBPIC;
        else if (!ubase_ncmp(def, "pic.") || strstr(def, ".pic."))
            upipe_trickp_sub->type = UPIPE_TRICKP_PIC;
        else if (!ubase_ncmp(def, "sound.") || strstr(def, ".sound."))
            upipe_trickp_sub->type = UPIPE_TRICKP_SOUND;
        else
            upipe_trickp_sub->type = UPIPE_TRICKP_UNKNOWN;
    }
    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_trickp_sub_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an output subpipe of a
 * trickp pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_trickp_sub_control(struct upipe *upipe,
                                    int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_trickp_sub_control_output(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_trickp_sub_control_super(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_trickp_sub_set_flow_def(upipe, flow_def);
        }

        case UPIPE_GET_MAX_LENGTH: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_trickp_sub_get_max_length(upipe, p);
        }
        case UPIPE_SET_MAX_LENGTH: {
            unsigned int max_length = va_arg(args, unsigned int);
            return upipe_trickp_sub_set_max_length(upipe, max_length);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_trickp_sub_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_trickp_sub_clean_output(upipe);
    upipe_trickp_sub_clean_input(upipe);
    upipe_trickp_sub_clean_sub(upipe);
    upipe_trickp_sub_clean_urefcount(upipe);
    upipe_trickp_sub_free_void(upipe);
}

/** @internal @This initializes the output manager for a trickp pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_trickp_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_trickp *upipe_trickp =
        upipe_trickp_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_trickp->sub_mgr;
    sub_mgr->refcount = upipe_trickp_to_urefcount(upipe_trickp);
    sub_mgr->signature = UPIPE_TRICKP_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_trickp_sub_alloc;
    sub_mgr->upipe_input = upipe_trickp_sub_input;
    sub_mgr->upipe_control = upipe_trickp_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a trickp pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_trickp_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_trickp_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    upipe_trickp_init_urefcount(upipe);
    upipe_trickp_init_sub_mgr(upipe);
    upipe_trickp_init_sub_subs(upipe);
    upipe_trickp_init_uclock(upipe);
    struct upipe_trickp *upipe_trickp = upipe_trickp_from_upipe(upipe);
    upipe_trickp->systime_offset = 0;
    upipe_trickp->ts_origin = 0;
    upipe_trickp->preroll = true;
    upipe_trickp->rate.num = upipe_trickp->rate.den = 1;
    upipe_throw_ready(upipe);
    upipe_trickp_require_uclock(upipe);
    return upipe;
}

/** @internal @This checks if we have got packets on video and audio inputs, so
 * we are ready to output them.
 *
 * @param upipe description structure of the pipe
 * @param uref unused uref
 * @return an error code
 */
static int upipe_trickp_check_start(struct upipe *upipe, struct uref *uref)
{
    struct upipe_trickp *upipe_trickp = upipe_trickp_from_upipe(upipe);
    uint64_t earliest_ts = UINT64_MAX;
    struct uchain *uchain;
    ulist_foreach (&upipe_trickp->subs, uchain) {
        struct upipe_trickp_sub *upipe_trickp_sub =
            upipe_trickp_sub_from_uchain(uchain);
        if (upipe_trickp_sub->type == UPIPE_TRICKP_SUBPIC)
            continue;

        for ( ; ; ) {
            struct uchain *uchain2;
            uchain2 = ulist_peek(&upipe_trickp_sub->urefs);
            if (uchain2 == NULL) {
                if (!upipe_trickp->preroll)
                    break;
                else
                    return UBASE_ERR_NONE; /* not ready */
            }

            struct uref *uref = uref_from_uchain(uchain2);
            uint64_t ts;
            int type;
            uref_clock_get_date_prog(uref, &ts, &type);
            if (unlikely(type == UREF_DATE_NONE)) {
                upipe_warn(upipe, "non-dated uref");
                upipe_trickp_sub_pop_input(
                        upipe_trickp_sub_to_upipe(upipe_trickp_sub));
                uref_free(uref);
                continue;
            }
            if (ts < earliest_ts)
                earliest_ts = ts;
            break;
        }
    }

    if (earliest_ts == UINT64_MAX)
        return UBASE_ERR_NONE;
    upipe_trickp->ts_origin = earliest_ts;
    upipe_trickp->systime_offset = uclock_now(upipe_trickp->uclock);
    upipe_trickp->preroll = false;
    upipe_verbose_va(upipe, "setting origin=%"PRIu64" now=%"PRIu64,
                     upipe_trickp->ts_origin, upipe_trickp->systime_offset);

    ulist_foreach (&upipe_trickp->subs, uchain) {
        struct upipe_trickp_sub *upipe_trickp_sub =
            upipe_trickp_sub_from_uchain(uchain);
        upipe_trickp_sub_output_input(
                    upipe_trickp_sub_to_upipe(upipe_trickp_sub));
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns a system date converted from a timestamp.
 *
 * @param upipe description structure of the pipe
 * @param ts timestamp
 * @return systime
 */
static uint64_t upipe_trickp_get_date_sys(struct upipe *upipe, uint64_t ts)
{
    struct upipe_trickp *upipe_trickp = upipe_trickp_from_upipe(upipe);
    if (unlikely(ts < upipe_trickp->ts_origin)) {
        upipe_warn(upipe, "got a timestamp in the past");
        ts = upipe_trickp->ts_origin;
    }
    return (ts - upipe_trickp->ts_origin) *
               upipe_trickp->rate.den / upipe_trickp->rate.num +
           upipe_trickp->systime_offset;
}

/** @internal @This resets uclock-related fields.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_trickp_reset_uclock(struct upipe *upipe)
{
    struct upipe_trickp *upipe_trickp = upipe_trickp_from_upipe(upipe);
    upipe_trickp->systime_offset = 0;
    upipe_trickp->ts_origin = 0;
}

/** @This returns the current playing rate.
 *
 * @param upipe description structure of the pipe
 * @param rate_p filled with the current rate
 * @return an error code
 */
static inline int _upipe_trickp_get_rate(struct upipe *upipe,
                                         struct urational *rate_p)
{
    struct upipe_trickp *upipe_trickp = upipe_trickp_from_upipe(upipe);
    *rate_p = upipe_trickp->rate;
    return UBASE_ERR_NONE;
}

/** @This sets the playing rate.
 *
 * @param upipe description structure of the pipe
 * @param rate new rate (1/1 = normal play, 0 = pause)
 * @return an error code
 */
static inline int _upipe_trickp_set_rate(struct upipe *upipe,
                                         struct urational rate)
{
    struct upipe_trickp *upipe_trickp = upipe_trickp_from_upipe(upipe);
    upipe_trickp->rate = rate;
    upipe_trickp_reset_uclock(upipe);
    if (rate.den) {
        upipe_dbg_va(upipe, "setting rate to %f", (float)rate.num/rate.den);
        struct uchain *uchain;
        ulist_foreach (&upipe_trickp->subs, uchain) {
            struct upipe_trickp_sub *upipe_trickp_sub =
                upipe_trickp_sub_from_uchain(uchain);
            upipe_trickp_sub->max_urefs = UINT_MAX;
            upipe_trickp_sub_unblock_input(
                    upipe_trickp_sub_to_upipe(upipe_trickp_sub));
        }
    } else {
        upipe_dbg_va(upipe, "setting rate to pause");
        struct uchain *uchain;
        ulist_foreach (&upipe_trickp->subs, uchain) {
            struct upipe_trickp_sub *upipe_trickp_sub =
                upipe_trickp_sub_from_uchain(uchain);
            upipe_trickp_sub->max_urefs = 0;
            upipe_trickp_sub_unblock_input(
                    upipe_trickp_sub_to_upipe(upipe_trickp_sub));
        }
    }
    upipe_trickp_check_start(upipe, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a trickp pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_trickp_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_trickp_control_subs(upipe, command, args));

    switch (command) {
        case UPIPE_END_PREROLL: {
            struct upipe_trickp *upipe_trickp = upipe_trickp_from_upipe(upipe);
            upipe_trickp->preroll = false;
            upipe_trickp_check_start(upipe, NULL);
            return UBASE_ERR_NONE;
        }

        case UPIPE_TRICKP_GET_RATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TRICKP_SIGNATURE)
            struct urational *p = va_arg(args, struct urational *);
            return _upipe_trickp_get_rate(upipe, p);
        }
        case UPIPE_TRICKP_SET_RATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TRICKP_SIGNATURE)
            struct urational rate = va_arg(args, struct urational);
            return _upipe_trickp_set_rate(upipe, rate);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_trickp_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_trickp_clean_sub_subs(upipe);
    upipe_trickp_clean_uclock(upipe);
    upipe_trickp_clean_urefcount(upipe);
    upipe_trickp_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_trickp_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TRICKP_SIGNATURE,

    .upipe_alloc = upipe_trickp_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_trickp_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all trickp pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_trickp_mgr_alloc(void)
{
    return &upipe_trickp_mgr;
}
