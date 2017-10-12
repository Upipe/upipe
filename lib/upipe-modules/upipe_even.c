/*
 * Copyright (C) 2015-2016 OpenHeadend S.A.R.L.
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
 * @short Upipe module evening the start and end of a stream
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
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-modules/upipe_even.h>

#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <stdarg.h>
#include <assert.h>

/** @hidden */
static void upipe_even_process(struct upipe *upipe, struct upump **upump_p);
/** @hidden */
static void upipe_even_sub_process(struct upipe *upipe, struct upump **upump_p);

/** @internal @This is the private context of a even pipe. */
struct upipe_even {
    /** refcount management structure */
    struct urefcount urefcount;

    /** first date */
    uint64_t first_date;
    /** last date */
    uint64_t last_date;
    /** true during the preroll period */
    bool preroll;
    /** true if an input is dead */
    bool dead;
    /** true if we are in the subpipe loop */
    bool in_loop;
    /** true if we need to restart the subpipe loop */
    bool restart;

    /** list of subs */
    struct uchain subs;

    /** manager to create subs */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_even, upipe, UPIPE_EVEN_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_even, urefcount, upipe_even_free)
UPIPE_HELPER_VOID(upipe_even)

/** @internal @This is the type of the flow (different behaviours). */
enum upipe_even_sub_type {
    UPIPE_EVEN_UNKNOWN,
    UPIPE_EVEN_PIC,
    UPIPE_EVEN_SOUND,
    UPIPE_EVEN_SUBPIC
};

/** @internal @This is the private context of an output of a even pipe. */
struct upipe_even_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** type of the flow */
    enum upipe_even_sub_type type;
    /** first date */
    uint64_t first_date;
    /** last date */
    uint64_t last_date;
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

UPIPE_HELPER_UPIPE(upipe_even_sub, upipe, UPIPE_EVEN_SUB_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_even_sub, urefcount, upipe_even_sub_free)
UPIPE_HELPER_VOID(upipe_even_sub)
UPIPE_HELPER_OUTPUT(upipe_even_sub, output, flow_def, output_state, request_list)
UPIPE_HELPER_INPUT(upipe_even_sub, urefs, nb_urefs, max_urefs, blockers, NULL)

UPIPE_HELPER_SUBPIPE(upipe_even, upipe_even_sub, sub, sub_mgr, subs, uchain)

/** @internal @This allocates an output subpipe of a even pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_even_sub_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature,
                                          va_list args)
{
    struct upipe *upipe = upipe_even_sub_alloc_void(mgr, uprobe, signature,
                                                    args);
    if (unlikely(upipe == NULL))
        return NULL;
    upipe_even_sub_init_urefcount(upipe);
    upipe_even_sub_init_output(upipe);
    upipe_even_sub_init_input(upipe);
    upipe_even_sub_init_sub(upipe);
    struct upipe_even_sub *upipe_even_sub = upipe_even_sub_from_upipe(upipe);
    upipe_even_sub->type = UPIPE_EVEN_UNKNOWN;
    upipe_even_sub->first_date = UINT64_MAX;
    upipe_even_sub->last_date = 0;
    upipe_even_sub->max_urefs = UINT_MAX;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_even_sub_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_even_sub *upipe_even_sub = upipe_even_sub_from_upipe(upipe);
    struct upipe_even *upipe_even = upipe_even_from_sub_mgr(upipe->mgr);
    uint64_t date;
    if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &date)))) {
        int type;
        uref_clock_get_date_sys(uref, &date, &type);
        if (unlikely(type == UREF_DATE_NONE)) {
            upipe_warn(upipe, "dropping non-dated buffer");
            uref_free(uref);
            return;
        }
    }
    uint64_t duration = 0;
    uref_clock_get_duration(uref, &duration);
    if (unlikely(upipe_even_sub->first_date == UINT64_MAX))
        upipe_even_sub->first_date = date;
    upipe_even_sub->last_date = date + duration;
    if (upipe_even_sub_check_input(upipe))
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    upipe_even_sub_hold_input(upipe, uref);
    upipe_even_process(upipe_even_to_upipe(upipe_even), upump_p);
}

/** @internal @This handles an uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return true if the uref was handled
 */
static bool upipe_even_sub_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_even *upipe_even = upipe_even_from_sub_mgr(upipe->mgr);
    uint64_t date;
    if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &date)))) {
        int type;
        uref_clock_get_date_sys(uref, &date, &type);
        assert(type != UREF_DATE_NONE);
    }
    uint64_t duration = 0;
    uref_clock_get_duration(uref, &duration);

    if (unlikely(date + duration < upipe_even->first_date)) {
        upipe_dbg_va(upipe, "removing early uref (%"PRId64")",
                     upipe_even->first_date - date - duration);
        uref_free(uref);
        return true;
    }

    if (date > upipe_even->last_date) {
        if (upipe_even->dead) {
            upipe_dbg_va(upipe, "removing late uref (%"PRId64")",
                         date - upipe_even->last_date);
            uref_free(uref);
            return true;
        }
        return false;
    }

    upipe_even_sub_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This processes data.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_even_sub_process(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_even_sub *upipe_even_sub = upipe_even_sub_from_upipe(upipe);

    upipe_use(upipe);
    for ( ; ; ) {
        struct uref *uref =
            upipe_even_sub_pop_input(upipe_even_sub_to_upipe(upipe_even_sub));
        if (uref == NULL) /* not ready */
            break;

        if (!upipe_even_sub_handle(upipe, uref, upump_p)) {
            upipe_even_sub_unshift_input(upipe, uref);
            break;
        } else if (upipe_even_sub_check_input(upipe)) {
            /* All packets have been output, release again the pipe that has
             * been used in @ref upipe_even_sub_input. */
            upipe_release(upipe);
            break;
        }
    }
    upipe_release(upipe);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_even_sub_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct upipe_even_sub *upipe_even_sub = upipe_even_sub_from_upipe(upipe);
    const char *def;
    if (likely(ubase_check(uref_flow_get_def(flow_def, &def)))) {
        if (!ubase_ncmp(def, "pic.sub.") || strstr(def, ".pic.sub."))
            upipe_even_sub->type = UPIPE_EVEN_SUBPIC;
        else if (!ubase_ncmp(def, "pic.") || strstr(def, ".pic."))
            upipe_even_sub->type = UPIPE_EVEN_PIC;
        else if (!ubase_ncmp(def, "sound.") || strstr(def, ".sound."))
            upipe_even_sub->type = UPIPE_EVEN_SOUND;
        else
            upipe_even_sub->type = UPIPE_EVEN_UNKNOWN;
    }
    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_even_sub_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an output subpipe of a
 * even pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_even_sub_control(struct upipe *upipe,
                                  int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_even_sub_control_output(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_even_sub_control_super(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_even_sub_set_flow_def(upipe, flow_def);
        }

        case UPIPE_GET_MAX_LENGTH: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_even_sub_get_max_length(upipe, p);
        }
        case UPIPE_SET_MAX_LENGTH: {
            unsigned int max_length = va_arg(args, unsigned int);
            return upipe_even_sub_set_max_length(upipe, max_length);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_even_sub_free(struct upipe *upipe)
{
    struct upipe_even_sub *upipe_even_sub = upipe_even_sub_from_upipe(upipe);
    struct upipe_even *upipe_even = upipe_even_from_sub_mgr(upipe->mgr);

    upipe_throw_dead(upipe);
    upipe_even_sub_clean_output(upipe);
    upipe_even_sub_clean_input(upipe);
    upipe_even_sub_clean_sub(upipe);
    upipe_even_sub_clean_urefcount(upipe);

    if (upipe_even_sub->type != UPIPE_EVEN_SUBPIC &&
        upipe_even_sub->type != UPIPE_EVEN_UNKNOWN) {
        if (!upipe_even->dead)
            upipe_dbg_va(upipe, "last date %"PRIu64, upipe_even->last_date);
        upipe_even->dead = true;
        if (!upipe_even->in_loop)
            upipe_even_process(upipe_even_to_upipe(upipe_even), NULL);
        else
            upipe_even->restart = true;
    }

    upipe_even_sub_free_void(upipe);
}

/** @internal @This initializes the output manager for a even pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_even_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_even *upipe_even =
        upipe_even_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_even->sub_mgr;
    sub_mgr->refcount = upipe_even_to_urefcount(upipe_even);
    sub_mgr->signature = UPIPE_EVEN_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_even_sub_alloc;
    sub_mgr->upipe_input = upipe_even_sub_input;
    sub_mgr->upipe_control = upipe_even_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a even pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_even_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_even_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    upipe_even_init_urefcount(upipe);
    upipe_even_init_sub_mgr(upipe);
    upipe_even_init_sub_subs(upipe);
    struct upipe_even *upipe_even = upipe_even_from_upipe(upipe);
    upipe_even->first_date = UINT64_MAX;
    upipe_even->preroll = true;
    upipe_even->dead = false;
    upipe_even->in_loop = false;
    upipe_even->restart = false;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This checks if we have got packets on video and audio inputs.
 *
 * @param upipe description structure of the pipe
 * @return true if we are ready
 */
static bool upipe_even_check(struct upipe *upipe)
{
    struct upipe_even *upipe_even = upipe_even_from_upipe(upipe);
    struct uchain *uchain;

    if (upipe_even->dead)
        return true;

    if (unlikely(upipe_even->first_date == UINT64_MAX)) {
        uint64_t first_date = 0;
        ulist_foreach (&upipe_even->subs, uchain) {
            struct upipe_even_sub *upipe_even_sub =
                upipe_even_sub_from_uchain(uchain);
            if (upipe_even_sub->type == UPIPE_EVEN_SUBPIC ||
                (upipe_even_sub->type == UPIPE_EVEN_UNKNOWN &&
                 !upipe_even->preroll))
                continue;

            if (upipe_even_sub->first_date == UINT64_MAX)
                return false;
            if (upipe_even_sub->first_date > first_date)
                first_date = upipe_even_sub->first_date;
        }

        upipe_even->first_date = first_date;
        upipe_dbg_va(upipe, "first date %"PRIu64, first_date);
    }

    uint64_t last_date = UINT64_MAX;
    ulist_foreach (&upipe_even->subs, uchain) {
        struct upipe_even_sub *upipe_even_sub =
            upipe_even_sub_from_uchain(uchain);
        if (upipe_even_sub->type == UPIPE_EVEN_SUBPIC ||
            (upipe_even_sub->type == UPIPE_EVEN_UNKNOWN &&
             !upipe_even->preroll))
            continue;

        if (upipe_even_sub->last_date == UINT64_MAX)
            return false;
        if (upipe_even_sub->last_date < last_date)
            last_date = upipe_even_sub->last_date;
    }

    upipe_even->last_date = last_date;
    upipe_verbose_va(upipe, "last date %"PRIu64, last_date);
    return true;
}

/** @internal @This checks if we have got packets on video and audio inputs, so
 * we are ready to output them.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_even_process(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_even *upipe_even = upipe_even_from_upipe(upipe);
    upipe_use(upipe);

    do {
        upipe_even->restart = false;

        if (!upipe_even_check(upipe))
            break;

        upipe_even->in_loop = true;
        struct uchain *uchain, *uchain_tmp;
        ulist_delete_foreach (&upipe_even->subs, uchain, uchain_tmp) {
            struct upipe_even_sub *upipe_even_sub =
                upipe_even_sub_from_uchain(uchain);
            upipe_even_sub_process(upipe_even_sub_to_upipe(upipe_even_sub),
                                   upump_p);
        }
        upipe_even->in_loop = false;
    } while (upipe_even->restart);

    upipe_release(upipe);
}

/** @internal @This ends the preroll period.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_even_end_preroll(struct upipe *upipe)
{
    struct upipe_even *upipe_even = upipe_even_from_upipe(upipe);
    upipe_even->preroll = false;
    upipe_even_process(upipe, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a even pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_even_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_even_control_subs(upipe, command, args));

    switch (command) {
        case UPIPE_END_PREROLL:
            return upipe_even_end_preroll(upipe);

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_even_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_even_clean_sub_subs(upipe);
    upipe_even_clean_urefcount(upipe);
    upipe_even_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_even_mgr = {
    .refcount = NULL,
    .signature = UPIPE_EVEN_SIGNATURE,

    .upipe_alloc = upipe_even_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_even_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all even pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_even_mgr_alloc(void)
{
    return &upipe_even_mgr;
}
