/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#include <stdlib.h>

#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-modules/upipe_stream_switcher.h>

#define DELTA_WARN       (UCLOCK_FREQ / 1000)

static inline int
upipe_stream_switcher_input_throw_sync(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw sync");
    return upipe_throw(upipe, UPROBE_STREAM_SWITCHER_SUB_SYNC,
                       UPIPE_STREAM_SWITCHER_SUB_SIGNATURE);
}

static inline int
upipe_stream_switcher_input_throw_entering(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw entering");
    return upipe_throw(upipe, UPROBE_STREAM_SWITCHER_SUB_ENTERING,
                       UPIPE_STREAM_SWITCHER_SUB_SIGNATURE);
}

static inline int
upipe_stream_switcher_input_throw_leaving(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw leaving");
    return upipe_throw(upipe, UPROBE_STREAM_SWITCHER_SUB_LEAVING,
                       UPIPE_STREAM_SWITCHER_SUB_SIGNATURE);
}

static inline int
upipe_stream_switcher_input_throw_destroy(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw destroy");
    return upipe_throw(upipe, UPROBE_STREAM_SWITCHER_SUB_DESTROY,
                       UPIPE_STREAM_SWITCHER_SUB_SIGNATURE);
}

/** @internal @This is the private context of a stream switcher pipe. */
struct upipe_stream_switcher {
    /** real refcount */
    struct urefcount urefcount_real;

    /** for urefcount helper */
    struct urefcount urefcount;

    /** for subpipe helper */
    struct uchain sub_pipes;
    struct upipe_mgr sub_mgr;

    /** for output helper */
    struct upipe *output;
    struct uref *flow_def;
    enum upipe_helper_output_state output_state;
    struct uchain request_list;

    /** private */
    struct upipe *selected;
    struct upipe *waiting;
    /** time to switch */
    uint64_t pts_orig;
    /** last pts of the output stream */
    uint64_t last_pts_orig;
    uint64_t rebase_timestamp;
    bool rebase_timestamp_set;

    /** for upipe helper */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_stream_switcher, upipe,
                   UPIPE_STREAM_SWITCHER_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_stream_switcher, urefcount,
                       upipe_stream_switcher_no_ref)
UPIPE_HELPER_VOID(upipe_stream_switcher)
UPIPE_HELPER_OUTPUT(upipe_stream_switcher, output, flow_def, output_state,
                    request_list)

UBASE_FROM_TO(upipe_stream_switcher, urefcount, urefcount_real, urefcount_real);

/** @hidden */
static void upipe_stream_switcher_input_free(struct urefcount *urefcount);

/** @hidden */
static void upipe_stream_switcher_switch(struct upipe_stream_switcher *super);

/** @internal @This is the private context for stream switcher sub pipes. */
struct upipe_stream_switcher_input {
    /** for urefcount helper */
    struct urefcount urefcount;
    /** for real refcount */
    struct urefcount urefcount_real;

    /** for subpipe helper */
    struct uchain uchain;

    /** for input helper */
    struct uchain urefs;
    unsigned int nb_urefs;
    unsigned int max_urefs;
    struct uchain blockers;

    /** private */
    bool sync;

    /** for upipe helper */
    struct upipe upipe;
};

UBASE_FROM_TO(upipe_stream_switcher_input, urefcount,
              urefcount_real, urefcount_real);

static bool upipe_stream_switcher_input_output(struct upipe *, struct uref *,
                                               struct upump **);

UPIPE_HELPER_UPIPE(upipe_stream_switcher_input, upipe,
                   UPIPE_STREAM_SWITCHER_SUB_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_stream_switcher_input, urefcount,
                       upipe_stream_switcher_input_no_ref)
UPIPE_HELPER_VOID(upipe_stream_switcher_input)
UPIPE_HELPER_INPUT(upipe_stream_switcher_input, urefs, nb_urefs, max_urefs,
                   blockers, upipe_stream_switcher_input_output)

UPIPE_HELPER_SUBPIPE(upipe_stream_switcher, upipe_stream_switcher_input,
                     input, sub_mgr, sub_pipes, uchain)

/*
 * sub pipes
 */

/* forward declarations */
static void upipe_stream_switcher_input_destroy(struct upipe *upipe);

/** @internal @This allocates an input stream.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_stream_switcher_input_alloc(struct upipe_mgr *mgr,
                                                       struct uprobe *uprobe,
                                                       uint32_t signature,
                                                       va_list args)
{
    struct upipe *upipe =
        upipe_stream_switcher_input_alloc_void(mgr, uprobe, signature, args);
    upipe_stream_switcher_input_init_urefcount(upipe);
    upipe_stream_switcher_input_init_sub(upipe);
    upipe_stream_switcher_input_init_input(upipe);

    struct upipe_stream_switcher_input *upipe_stream_switcher_input =
        upipe_stream_switcher_input_from_upipe(upipe);
    upipe_stream_switcher_input->sync = false;
    urefcount_init(&upipe_stream_switcher_input->urefcount_real,
                   upipe_stream_switcher_input_free);

    struct upipe_stream_switcher *super =
        upipe_stream_switcher_from_sub_mgr(mgr);
    if (super->waiting)
        upipe_stream_switcher_input_destroy(super->waiting);
    urefcount_use(&upipe_stream_switcher_input->urefcount_real);
    super->waiting = upipe;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This frees an input stream.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_stream_switcher_input_free(struct urefcount *urefcount)
{
    struct upipe_stream_switcher_input *upipe_stream_switcher_input =
        upipe_stream_switcher_input_from_urefcount_real(urefcount);
    struct upipe *upipe =
        upipe_stream_switcher_input_to_upipe(upipe_stream_switcher_input);

    upipe_throw_dead(upipe);

    upipe_stream_switcher_input_clean_input(upipe);
    upipe_stream_switcher_input_clean_sub(upipe);
    upipe_stream_switcher_input_clean_urefcount(upipe);
    upipe_stream_switcher_input_free_void(upipe);
}

/** @internal @This destroy an input stream.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_stream_switcher_input_destroy(struct upipe *upipe)
{
    struct upipe_stream_switcher_input *upipe_stream_switcher_input =
        upipe_stream_switcher_input_from_upipe(upipe);
    assert(upipe->mgr != NULL);
    struct upipe_stream_switcher *super =
        upipe_stream_switcher_from_sub_mgr(upipe->mgr);

    if (super->selected == upipe) {
        upipe_stream_switcher_input_throw_leaving(upipe);
        urefcount_release(&upipe_stream_switcher_input->urefcount_real);
        super->selected = NULL;
    }
    if (super->waiting == upipe) {
        upipe_stream_switcher_input_throw_destroy(upipe);
        urefcount_release(&upipe_stream_switcher_input->urefcount_real);
        super->waiting = NULL;
    }
}

static void upipe_stream_switcher_input_no_ref(struct upipe *upipe)
{
    struct upipe_stream_switcher_input *upipe_stream_switcher_input =
        upipe_stream_switcher_input_from_upipe(upipe);
    assert(upipe->mgr != NULL);
    struct upipe_stream_switcher *super =
        upipe_stream_switcher_from_sub_mgr(upipe->mgr);

    upipe_stream_switcher_input_destroy(upipe);
    if (unlikely(super->selected == NULL && super->waiting != NULL))
        upipe_stream_switcher_switch(super);
    urefcount_release(&upipe_stream_switcher_input->urefcount_real);
}

/** @internal @This implements input stream control commands.
 *
 * @param upipe description structure of the pipe
 * @param command command to execute
 * @param args command arguments
 * @return an error code
 */
static int upipe_stream_switcher_input_control(struct upipe *upipe,
                                               int command,
                                               va_list args)
{
    struct upipe_mgr *upipe_mgr = upipe->mgr;
    assert(upipe_mgr);
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_sub_mgr(upipe_mgr);
    struct upipe *super = upipe_stream_switcher_to_upipe(upipe_stream_switcher);

    switch (command) {
    case UPIPE_REGISTER_REQUEST: {
        struct urequest *request = va_arg(args, struct urequest *);
        return upipe_throw_provide_request(upipe, request);
    }
    case UPIPE_UNREGISTER_REQUEST:
        return UBASE_ERR_NONE;
    case UPIPE_SUB_GET_SUPER: {
        struct upipe **p = va_arg(args, struct upipe **);
        return upipe_stream_switcher_input_get_super(upipe, p);
    }
    case UPIPE_GET_MAX_LENGTH: {
        unsigned int *p = va_arg(args, unsigned int *);
        return upipe_stream_switcher_input_get_max_length(upipe, p);
    }
    case UPIPE_SET_MAX_LENGTH: {
        unsigned int max_length = va_arg(args, unsigned int);
        return upipe_stream_switcher_input_set_max_length(upipe, max_length);
    }
    case UPIPE_SET_FLOW_DEF: {
        struct uref *uref = va_arg(args, struct uref *);
        return upipe_set_flow_def(super, uref);
    }
    default:
        return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This drops an uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref to drop
 * @returns true
 */
static bool upipe_stream_switcher_drop(struct upipe *upipe, struct uref *uref)
{
    upipe_verbose(upipe, "drop...");
    uref_free(uref);
    return true;
}

/** @internal @This switch to a different sub pipe.
 *
 * @param super stream switcher super pipe.
 * @param upipe new sub pipe to switch on.
 */
static void upipe_stream_switcher_switch(struct upipe_stream_switcher *super)
{
    /* destroy the old one */
    if (super->selected)
        upipe_stream_switcher_input_destroy(super->selected);

    /* switch */
    super->selected = super->waiting;
    super->waiting = NULL;

    /* wake up the new one */
    if (super->selected) {
        upipe_stream_switcher_input_throw_entering(super->selected);
        if (upipe_stream_switcher_input_output_input(super->selected))
            upipe_stream_switcher_input_unblock_input(super->selected);
    }
}

/** @internal @This forwards uref to the super pipe
 *
 * @param upipe description structure of the pipe
 * @param uref pointer to uref to output
 * @param upump_p reference to the pump that generated the buffer
 * @returns true if uref was outputted
 */
static bool upipe_stream_switcher_fwd(struct upipe *upipe,
                                      struct uref *uref,
                                      struct upump **upump_p)
{
    struct upipe_mgr *upipe_mgr = upipe->mgr;
    assert(upipe_mgr);
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_sub_mgr(upipe_mgr);
    struct upipe *super = upipe_stream_switcher_to_upipe(upipe_stream_switcher);

    uint64_t dts_orig = 0;
    if (!ubase_check(uref_clock_get_dts_orig(uref, &dts_orig))) {
        upipe_err(upipe, "no dts orig");
        return upipe_stream_switcher_drop(upipe, uref);
    }
    if (!upipe_stream_switcher->rebase_timestamp_set) {
        upipe_stream_switcher->rebase_timestamp_set = true;
        upipe_stream_switcher->rebase_timestamp = dts_orig;
    }
    if (upipe_stream_switcher->rebase_timestamp > dts_orig) {
        upipe_warn(upipe, "dts is in the past");
        upipe_stream_switcher->rebase_timestamp = dts_orig;
    }
    dts_orig -= upipe_stream_switcher->rebase_timestamp;

    uint64_t dts_prog;
    if (!ubase_check(uref_clock_get_dts_prog(uref, &dts_prog)))
        dts_prog = 0;
    upipe_verbose_va(upipe, "DTS rebase %"PRIu64"(%"PRIu64"ms) "
                     "-> %"PRIu64" (%"PRIu64"ms)",
                     dts_prog, dts_prog / (UCLOCK_FREQ / 1000),
                     dts_orig, dts_orig / (UCLOCK_FREQ / 1000));
    uref_clock_set_dts_prog(uref, dts_orig);
    upipe_stream_switcher_output(super, uref, upump_p);

    return true;
}

/** @internal @This sets the upipe as waiting, return false to save the uref
 *
 * @param super pointer to the private description of the super pipe
 * @param upipe description structure of the pipe
 * @param uref pointer to uref to output
 * @return an error code
 */
static bool upipe_stream_switcher_wait(struct upipe_stream_switcher *super,
                                       struct upipe *upipe, struct uref *uref)
{
    struct upipe_stream_switcher_input *upipe_stream_switcher_input =
        upipe_stream_switcher_input_from_upipe(upipe);

    assert(super->waiting == upipe);

    if (upipe_stream_switcher_input->sync)
        return false;

    const char *flow_def;
    if (!ubase_check(uref_flow_get_def(super->flow_def, &flow_def))) {
        upipe_err(upipe, "fail to get flow format");
        return upipe_stream_switcher_drop(upipe, uref);
    }

    if (strstr(flow_def, ".pic.") && !ubase_check(uref_pic_get_key(uref)))
        /* drop if not a key frames */
        return upipe_stream_switcher_drop(upipe, uref);

    uint64_t pts_orig = 0;
    if (!ubase_check(uref_clock_get_pts_orig(uref, &pts_orig))) {
        /* fail to get pts, dropping... */
        upipe_warn(upipe, "fail to get pts from new stream");
        if (!ubase_check(uref_clock_get_dts_orig(uref, &pts_orig))) {
            upipe_err(upipe, "fail to fallback on dts");
            return upipe_stream_switcher_drop(upipe, uref);
        }
    }

    if (pts_orig <= super->last_pts_orig) {
        /* late frame, drop... */
        upipe_dbg_va(upipe, "late frame %"PRIu64 " <= %"PRIu64,
                     pts_orig, super->last_pts_orig);
        return upipe_stream_switcher_drop(upipe, uref);
    }
    super->pts_orig = pts_orig;
    upipe_stream_switcher_input->sync = true;
    upipe_stream_switcher_input_throw_sync(upipe);
    return false;
}

/** @internal @This forward, drop or switch the uref.
 * This function only forwards the uref of the selected sub pipe.
 * If another sub pipe is waiting, this function will switch between the
 * selected and waiting stream at the date of the first key frame found
 * in the waiting stream.
 *
 * @param upipe sub pipe asking for output.
 * @param uref uref to output.
 * @param upump_p
 */
static bool upipe_stream_switcher_input_output(struct upipe *upipe,
                                               struct uref *uref,
                                               struct upump **upump_p)
{
    assert(upipe->mgr);
    struct upipe_stream_switcher *super =
        upipe_stream_switcher_from_sub_mgr(upipe->mgr);

    if (super->selected == NULL) {
        if (super->waiting)
            upipe_stream_switcher_switch(super);
    }

    if (super->selected == upipe) {
        /* current selected stream */

        uint64_t pts_orig = 0;
        if (!ubase_check(uref_clock_get_pts_orig(uref, &pts_orig))) {
            /* fail to get pts, dropping... */
            upipe_warn(upipe, "fail to get pts");

            if (!ubase_check(uref_clock_get_dts_orig(uref, &pts_orig))) {
                upipe_err(upipe, "fail to fallback on dts");
                return upipe_stream_switcher_drop(upipe, uref);
            }
        }

        super->last_pts_orig = pts_orig;
        if (!super->waiting)
            /* no waiting stream, forward */
            return upipe_stream_switcher_fwd(upipe, uref, upump_p);

        struct upipe_stream_switcher_input *waiting =
            upipe_stream_switcher_input_from_upipe(super->waiting);
        if (!waiting->sync)
            /* no key frame found, forward */
            return upipe_stream_switcher_fwd(upipe, uref, upump_p);

        if (pts_orig < super->pts_orig)
            /* previous frame, forward */
            return upipe_stream_switcher_fwd(upipe, uref, upump_p);

        if (pts_orig - super->pts_orig > DELTA_WARN)
            upipe_warn_va(upipe, "switch too late %"PRIu64,
                          pts_orig - super->pts_orig);

        /* the selected stream meet the waiting stream, switch */
        upipe_dbg_va(upipe, "switch at %"PRIu64, pts_orig);
        upipe_stream_switcher_switch(super);
        /* drop */
    }
    else if (super->waiting == upipe) {
        /* waiting for switch */
        return upipe_stream_switcher_wait(super, upipe, uref);
    }

    return upipe_stream_switcher_drop(upipe, uref);
}

/** @internal @This hold and block stream uref if necessary.
 *
 * @param upipe the sub pipe.
 * @param uref uref to hold.
 * @param upump_p
 */
static void upipe_stream_switcher_input_input(struct upipe *upipe,
                                              struct uref *uref,
                                              struct upump **upump_p)
{
    if (!upipe_stream_switcher_input_output(upipe, uref, upump_p)) {
        upipe_stream_switcher_input_hold_input(upipe, uref);
        upipe_stream_switcher_input_block_input(upipe, upump_p);
    }
}

/** @internal @This initialize the manager for sub stream pipes.
 *
 * @param upipe stream switcher pipe.
 */
static void upipe_stream_switcher_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_upipe(upipe);
    struct upipe_mgr *sub_mgr =
        upipe_stream_switcher_to_sub_mgr(upipe_stream_switcher);

    memset(sub_mgr, 0, sizeof (*sub_mgr));
    sub_mgr->signature = UPIPE_STREAM_SWITCHER_SUB_SIGNATURE;
    sub_mgr->upipe_event_str = uprobe_stream_switcher_sub_event_str;
    sub_mgr->upipe_alloc = upipe_stream_switcher_input_alloc;
    sub_mgr->upipe_control = upipe_stream_switcher_input_control;
    sub_mgr->upipe_input = upipe_stream_switcher_input_input;
    sub_mgr->refcount = &upipe_stream_switcher->urefcount_real;
}

/*
 * super pipe
 */

/* forward declarations */
static void upipe_stream_switcher_free(struct urefcount *urefcount);

/** @internal @This allocates a stream switcher pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_stream_switcher_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature,
                                                 va_list args)
{
    struct upipe *upipe =
        upipe_stream_switcher_alloc_void(mgr, uprobe, signature, args);
    upipe_stream_switcher_init_urefcount(upipe);
    upipe_stream_switcher_init_output(upipe);
    upipe_stream_switcher_init_sub_inputs(upipe);
    upipe_stream_switcher_init_sub_mgr(upipe);

    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_upipe(upipe);
    upipe_stream_switcher->selected = NULL;
    upipe_stream_switcher->waiting = NULL;
    upipe_stream_switcher->pts_orig = 0;
    upipe_stream_switcher->last_pts_orig = 0;
    upipe_stream_switcher->rebase_timestamp_set = false;
    upipe_stream_switcher->rebase_timestamp = 0;
    urefcount_init(
        upipe_stream_switcher_to_urefcount_real(upipe_stream_switcher),
        upipe_stream_switcher_free);

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This frees a stream switcher pipe.
 *
 * @param urefcount pointer to the embedded urefcount
 */
static void upipe_stream_switcher_free(struct urefcount *urefcount)
{
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_urefcount_real(urefcount);
    struct upipe *upipe = upipe_stream_switcher_to_upipe(upipe_stream_switcher);

    upipe_throw_dead(upipe);

    upipe_stream_switcher_clean_sub_inputs(upipe);
    upipe_stream_switcher_clean_output(upipe);
    upipe_stream_switcher_clean_urefcount(upipe);
    upipe_stream_switcher_free_void(upipe);
}

/** @internal @This is called when there is no external reference to the
 * pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_stream_switcher_no_ref(struct upipe *upipe)
{
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_upipe(upipe);

    if (upipe_stream_switcher->waiting)
        upipe_stream_switcher_input_destroy(upipe_stream_switcher->waiting);
    assert(upipe_stream_switcher->waiting == NULL);

    if (upipe_stream_switcher->selected)
        upipe_stream_switcher_input_destroy(upipe_stream_switcher->selected);
    assert(upipe_stream_switcher->selected == NULL);

    urefcount_release(
        upipe_stream_switcher_to_urefcount_real(upipe_stream_switcher));
}

/** @internal @This sets the flow format of the stream switcher pipe.
 *
 * @param upipe description structure of the pipe
 * @param flow_def pointer to the flow definition
 * @return an error code
 */
static int upipe_stream_switcher_set_flow_def(struct upipe *upipe,
                                              struct uref *flow_def)
{
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_upipe(upipe);

    if (upipe_stream_switcher->flow_def == NULL) {
        struct uref *flow_def_dup = uref_dup(flow_def);
        if (unlikely(flow_def_dup == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }

        upipe_stream_switcher_store_flow_def(upipe, flow_def_dup);
    }
    else if (uref_flow_cmp_def(upipe_stream_switcher->flow_def, flow_def))
        return UBASE_ERR_INVALID;

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a stream switcher pipe,
 * and checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_stream_switcher_control(struct upipe *upipe,
                                         int command, va_list args)
{
    switch (command) {
    case UPIPE_REGISTER_REQUEST: {
        struct urequest *request = va_arg(args, struct urequest *);
        return upipe_throw_provide_request(upipe, request);
    }
    case UPIPE_UNREGISTER_REQUEST:
        return UBASE_ERR_NONE;

    case UPIPE_GET_FLOW_DEF: {
        struct uref **p = va_arg(args, struct uref **);
        return upipe_stream_switcher_get_flow_def(upipe, p);
    }
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_stream_switcher_set_flow_def(upipe, flow_def);
    }

    case UPIPE_GET_OUTPUT: {
        struct upipe **p = va_arg(args, struct upipe **);
        return upipe_stream_switcher_get_output(upipe, p);
    }
    case UPIPE_SET_OUTPUT: {
        struct upipe *output = va_arg(args, struct upipe *);
        return upipe_stream_switcher_set_output(upipe, output);
    }

    case UPIPE_GET_SUB_MGR: {
        struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
        return upipe_stream_switcher_get_sub_mgr(upipe, p);
    }
    case UPIPE_ITERATE_SUB: {
        struct upipe **p = va_arg(args, struct upipe **);
        return upipe_stream_switcher_iterate_sub(upipe, p);
    }

    default:
        return UBASE_ERR_UNHANDLED;
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_stream_switcher_mgr = {
    .signature = UPIPE_STREAM_SWITCHER_SIGNATURE,
    .upipe_alloc = upipe_stream_switcher_alloc,
    .upipe_control = upipe_stream_switcher_control,
};

/** @This returns the management structure for all stream switcher pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_stream_switcher_mgr_alloc(void)
{
    return &upipe_stream_switcher_mgr;
}
