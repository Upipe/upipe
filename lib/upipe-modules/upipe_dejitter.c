/*
 * Copyright (C) 2016-2017 OpenHeadend S.A.R.L.
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
 * @short Upipe module calling dejtter on timestamps
 *
 * The input of the super-pipe is supposed to be the clock ref. Its output
 * and the outputs of the subpipes are then dejittered with the clock ts probe.
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_dejitter.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

/** @internal @This is the private context of a dejitter pipe. */
struct upipe_dejitter {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** set to true after the first packet has been sent */
    bool inited;

    /** list of subs */
    struct uchain subs;
    /** manager to create subs */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dejitter, upipe, UPIPE_DEJITTER_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_dejitter, urefcount, upipe_dejitter_free)
UPIPE_HELPER_VOID(upipe_dejitter)
UPIPE_HELPER_OUTPUT(upipe_dejitter, output, flow_def, output_state, request_list)

/** @internal @This is the private context of a subpipe of a dejitter pipe. */
struct upipe_dejitter_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

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

UPIPE_HELPER_UPIPE(upipe_dejitter_sub, upipe, UPIPE_DEJITTER_SUB_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_dejitter_sub, urefcount, upipe_dejitter_sub_free)
UPIPE_HELPER_VOID(upipe_dejitter_sub)
UPIPE_HELPER_OUTPUT(upipe_dejitter_sub, output, flow_def, output_state, request_list)

UPIPE_HELPER_SUBPIPE(upipe_dejitter, upipe_dejitter_sub, sub, sub_mgr, subs, uchain)

/** @internal @This allocates a subpipe of a dejitter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_dejitter_sub_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature,
                                              va_list args)
{
    struct upipe *upipe = upipe_dejitter_sub_alloc_void(mgr, uprobe, signature,
                                                        args);
    if (unlikely(upipe == NULL))
        return NULL;
    upipe_dejitter_sub_init_urefcount(upipe);
    upipe_dejitter_sub_init_output(upipe);
    upipe_dejitter_sub_init_sub(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_dejitter_sub_input(struct upipe *upipe, struct uref *uref,
                                     struct upump **upump_p)
{
    uint64_t date;
    if (ubase_check(uref_clock_get_dts_prog(uref, &date)))
        upipe_throw_clock_ts(upipe, uref);
    upipe_dejitter_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_dejitter_sub_set_flow_def(struct upipe *upipe,
                                           struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    upipe_dejitter_sub_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a dejitter subpipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_dejitter_sub_control(struct upipe *upipe,
                                      int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_dejitter_sub_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_dejitter_sub_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dejitter_sub_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_dejitter_sub_clean_output(upipe);
    upipe_dejitter_sub_clean_sub(upipe);
    upipe_dejitter_sub_clean_urefcount(upipe);
    upipe_dejitter_sub_free_void(upipe);
}

/** @internal @This initializes the output manager for a dejitter pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dejitter_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_dejitter *upipe_dejitter =
        upipe_dejitter_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_dejitter->sub_mgr;
    sub_mgr->refcount = upipe_dejitter_to_urefcount(upipe_dejitter);
    sub_mgr->signature = UPIPE_DEJITTER_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_dejitter_sub_alloc;
    sub_mgr->upipe_input = upipe_dejitter_sub_input;
    sub_mgr->upipe_control = upipe_dejitter_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a dejitter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_dejitter_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_dejitter_alloc_void(mgr, uprobe, signature,
                                                    args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_dejitter *upipe_dejitter = upipe_dejitter_from_upipe(upipe);
    upipe_dejitter_init_urefcount(upipe);
    upipe_dejitter_init_sub_mgr(upipe);
    upipe_dejitter_init_sub_subs(upipe);
    upipe_dejitter_init_output(upipe);
    upipe_dejitter->inited = false;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_dejitter_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    uint64_t date;
    if (ubase_check(uref_clock_get_dts_prog(uref, &date))) {
        struct upipe_dejitter *upipe_dejitter =
            upipe_dejitter_from_upipe(upipe);
        upipe_throw_clock_ref(upipe, uref, date, !upipe_dejitter->inited);
        upipe_throw_clock_ts(upipe, uref);
        upipe_dejitter->inited = true;
    }
    upipe_dejitter_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_dejitter_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    upipe_dejitter_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a dejitter pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_dejitter_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_dejitter_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_dejitter_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dejitter_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_dejitter_clean_output(upipe);
    upipe_dejitter_clean_sub_subs(upipe);
    upipe_dejitter_clean_urefcount(upipe);
    upipe_dejitter_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_dejitter_mgr = {
    .refcount = NULL,
    .signature = UPIPE_DEJITTER_SIGNATURE,

    .upipe_alloc = upipe_dejitter_alloc,
    .upipe_input = upipe_dejitter_input,
    .upipe_control = upipe_dejitter_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all dejitter pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dejitter_mgr_alloc(void)
{
    return &upipe_dejitter_mgr;
}
