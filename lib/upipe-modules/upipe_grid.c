/*
 * Copyright (C) 2017-2018 OpenHeadend S.A.R.L.
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

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_uclock.h>

#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_void_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>

#include <upipe-modules/upipe_grid.h>

/** expected flow def for reference input */
#define REF_EXPECTED_FLOW "void."
/** default pts tolerance (late packets) */
#define DEFAULT_TOLERANCE ((UCLOCK_FREQ / 25) - 1)
/** maximum retention when there is no packet afterwards */
#define MAX_RETENTION UCLOCK_FREQ

/** @internal @This is the private structure of a grid pipe. */
struct upipe_grid {
    /** pipe public structure */
    struct upipe upipe;
    /** refcount public structure */
    struct urefcount urefcount;
    /** real refcount structure */
    struct urefcount urefcount_real;
    /** input sub pipe manager */
    struct upipe_mgr in_mgr;
    /** output sub pipe manager */
    struct upipe_mgr out_mgr;
    /** input sub pipe list */
    struct uchain inputs;
    /** output sub pipe list */
    struct uchain outputs;
    /** uclock */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;
    /** late buffer tolerance */
    uint64_t tolerance;
    /** last update pts */
    uint64_t last_update_pts;
    /** next update diff */
    uint64_t next_update;
    /** grid max rentention */
    uint64_t max_retention;
};

/** @hidden */
static int upipe_grid_update_pts(struct upipe *upipe, uint64_t next_pts);
/** @hidden */
static int upipe_grid_uclock_now(struct upipe *upipe, uint64_t *now);
/** @hidden */
static int upipe_grid_catch_out(struct uprobe *uprobe, struct upipe *upipe,
                                 int event, va_list args);

UPIPE_HELPER_UPIPE(upipe_grid, upipe, UPIPE_GRID_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_grid, urefcount, upipe_grid_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_grid, urefcount_real, upipe_grid_free);
UPIPE_HELPER_VOID(upipe_grid);
UPIPE_HELPER_UCLOCK(upipe_grid, uclock, uclock_request, NULL,
                    upipe_throw_provide_request, NULL);

/** @internal @This is the private structure for grid input sub pipe. */
struct upipe_grid_in {
    /** pipe public structure */
    struct upipe upipe;
    /** refcount public structure */
    struct urefcount urefcount;
    /** uchain for upipe_grid input list */
    struct uchain uchain;
    /** list of input urefs */
    struct uchain urefs;
    /** input flow def */
    struct uref *flow_def;
    /** flow def attr */
    struct uref *flow_attr;
    /** proxy probe */
    struct uprobe proxy;
    /** last received PTS */
    uint64_t last_pts;
    /** last update PTS */
    uint64_t last_update;
    /** input latency */
    uint64_t latency;
    /** next update diff */
    uint64_t next_update;
};

UPIPE_HELPER_UPIPE(upipe_grid_in, upipe, UPIPE_GRID_IN_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_grid_in, urefcount, upipe_grid_in_free);
UPIPE_HELPER_VOID(upipe_grid_in);
UPIPE_HELPER_SUBPIPE(upipe_grid, upipe_grid_in, input, in_mgr,
                     inputs, uchain);
UPIPE_HELPER_FLOW_DEF(upipe_grid_in, flow_def, flow_attr);

/** @internal @This enumatates the grid output inner pipe events. */
enum uprobe_grid_out_event {
    /** sentinel */
    UPROBE_GRID_OUT_SENTINEL = UPROBE_LOCAL,
    /** last pts update */
    UPROBE_GRID_OUT_UPDATE_PTS,
};

/** @internal @This is the private structure for the grid output inner
 * pipe. */
struct upipe_grid_out {
    /** public structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** input flow def */
    struct uref *input_flow_def;
    /** input flow attributes */
    struct uref *input_flow_attr;
    /** output pipe */
    struct upipe *output;
    /** output flow def */
    struct uref *flow_def;
    /** output internal state */
    enum upipe_helper_output_state output_state;
    /** output request list */
    struct uchain requests;
    /** inputs */
    struct uchain inputs;
    /** true if flow def is from current input */
    bool flow_def_input;
    /** selected input */
    struct upipe *input;
    /** true if flow def is up to date */
    bool flow_def_uptodate;
    /** uchain for super pipe list */
    struct uchain uchain;
    /** output tolerance */
    uint64_t tolerance;
    /** last input pts */
    uint64_t last_input_pts;
};

static void upipe_grid_out_handle_input_changed(struct upipe *upipe,
                                                 struct upipe *input);
static void upipe_grid_out_handle_input_removed(struct upipe *upipe,
                                                 struct upipe *input);

UPIPE_HELPER_UPIPE(upipe_grid_out, upipe, UPIPE_GRID_OUT_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_grid_out, urefcount,
                       upipe_grid_out_free);
UPIPE_HELPER_VOID(upipe_grid_out);
UPIPE_HELPER_OUTPUT(upipe_grid_out, output, flow_def, output_state,
                    requests);
UPIPE_HELPER_SUBPIPE(upipe_grid, upipe_grid_out, output, out_mgr,
                     outputs, uchain);
UPIPE_HELPER_FLOW_DEF(upipe_grid_out, input_flow_def, input_flow_attr);

/** @internal @This frees a grid input sub pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_grid_in_free(struct upipe *upipe)
{
    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe);

    upipe_throw_dead(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_grid_in->urefs, uchain, uchain_tmp) {
        ulist_delete(uchain);
        uref_free(uref_from_uchain(uchain));
    }
    upipe_grid_in_clean_flow_def(upipe);
    upipe_grid_in_clean_sub(upipe);
    upipe_grid_in_clean_urefcount(upipe);

    upipe_grid_in_free_void(upipe);
}

/** @internal @This allocates a grid input sub pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe
 */
static struct upipe *upipe_grid_in_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe =
        upipe_grid_in_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(!upipe))
        return NULL;

    upipe_grid_in_init_urefcount(upipe);
    upipe_grid_in_init_sub(upipe);
    upipe_grid_in_init_flow_def(upipe);

    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe);
    ulist_init(&upipe_grid_in->urefs);
    upipe_grid_in->last_pts = 0;
    upipe_grid_in->last_update = 0;
    upipe_grid_in->latency = 0;
    upipe_grid_in->next_update = 0;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This catches events from a grid input pipe.
 *
 * @param uprobe structure used to raise events
 * @param upipe the grid input pipe description
 * @param event event raised
 * @param args optional arguments
 * @return an error code
 */
static int upipe_grid_in_catch(struct uprobe *uprobe,
                               struct upipe *upipe,
                               int event, va_list args)
{
    if (unlikely(!upipe))
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct upipe_grid *upipe_grid = upipe_grid_from_in_mgr(upipe->mgr);
    struct upipe *super = upipe_grid_to_upipe(upipe_grid);

    switch (event) {
        case UPROBE_NEW_FLOW_DEF: {
            struct upipe *output = NULL;
            while (ubase_check(upipe_grid_iterate_output(super, &output)) &&
                   output)
                upipe_grid_out_handle_input_changed(output, upipe);
            return UBASE_ERR_NONE;
        }

        case UPROBE_DEAD: {
            struct upipe *output = NULL;
            while (ubase_check(upipe_grid_iterate_output(super, &output)) &&
                   output)
                upipe_grid_out_handle_input_removed(output, upipe);
            break;
        }
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

/** @internal @This handles input buffer from input pipe.
 *
 * @param upipe input pipe description
 * @param uref input buffer to handle
 * @param upump_p reference to upump structure
 */
static void upipe_grid_in_input(struct upipe *upipe,
                                struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe);
    struct upipe_grid *upipe_grid = upipe_grid_from_in_mgr(upipe->mgr);

    /* handle flow format */
    if (unlikely(ubase_check(uref_flow_get_def(uref, NULL)))) {
        upipe_grid_in->latency = 0;
        uref_clock_get_latency(uref, &upipe_grid_in->latency);
        ulist_add(&upipe_grid_in->urefs, uref_to_uchain(uref));
        return;
    }

    if (unlikely(!uref->ubuf)) {
        upipe_warn(upipe, "received empty buffer");
        uref_free(uref);
        return;
    }

    uint64_t pts = 0;
    if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &pts)))) {
        upipe_warn(upipe, "packet without pts");
        uref_free(uref);
        return;
    }

    if (pts <= upipe_grid_in->last_pts) {
        upipe_warn(upipe, "PTS is in the past");
        uref_free(uref);
        return;
    }

    uint64_t rebase_pts = pts + upipe_grid_in->latency;
    if (rebase_pts + upipe_grid->tolerance < upipe_grid_in->last_update) {
        upipe_warn_va(upipe, "PTS is too far in the past %"PRIu64"ms",
                     (upipe_grid_in->last_update - rebase_pts) /
                     (UCLOCK_FREQ / 1000));
        uref_free(uref);
        return;
    }

    upipe_grid_in->last_pts = pts;
    ulist_add(&upipe_grid_in->urefs, uref_to_uchain(uref));

    uint64_t now;
    if (unlikely(!ubase_check(
            upipe_grid_uclock_now(upipe_grid_to_upipe(upipe_grid), &now)))) {
        upipe_warn(upipe, "no clock set");
        return;
    }

    struct uref *last_flow_def = NULL;
    struct uchain *uchain;
    uint64_t latency = 0;
    if (upipe_grid_in->flow_def)
        uref_clock_get_latency(upipe_grid_in->flow_def, &latency);
    while ((uchain = ulist_pop(&upipe_grid_in->urefs))) {
        struct uref *uref = uref_from_uchain(uchain);

        if (unlikely(ubase_check(uref_flow_get_def(uref, NULL)))) {
            uref_free(last_flow_def);
            last_flow_def = uref;
            latency = 0;
            uref_clock_get_latency(uref, &latency);
            continue;
        }

        ubase_assert(uref_clock_get_pts_sys(uref, &pts));
        uint64_t pts_max = pts + latency + upipe_grid->tolerance;
        if (pts_max >= now) {
            ulist_unshift(&upipe_grid_in->urefs, uchain);
            break;
        }

        if (ulist_empty(&upipe_grid_in->urefs) &&
            pts_max + upipe_grid->max_retention >= now) {
            ulist_unshift(&upipe_grid_in->urefs, uchain);
            break;
        }

        upipe_verbose_va(upipe, "drop late frame %"PRIu64"ms, "
                         "latency %"PRIu64"ms "
                         "retention %"PRIu64"ms",
                         (now - pts) / (UCLOCK_FREQ / 1000),
                         latency / (UCLOCK_FREQ / 1000),
                         upipe_grid->max_retention / (UCLOCK_FREQ / 1000));
        uref_free(uref);
    }

    if (last_flow_def)
        ulist_unshift(&upipe_grid_in->urefs, uref_to_uchain(last_flow_def));
}

/** @internal @This sets the input flow def for real.
 * @This applies a flow def pushed by the set flow def control command.
 * @see upipe_grid_in_set_flow_def.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow format definition
 */
static void upipe_grid_in_set_flow_def_real(struct upipe *upipe,
                                            struct uref *flow_def)
{
    upipe_grid_in_store_flow_def_input(upipe, flow_def);
    upipe_throw_new_flow_def(upipe, flow_def);
}

/** @internal @This sets a new flow def to a grid input pipe.
 * @This pushes the new flow def into the pipe input to be handled later,
 * i.e. when the flow def will be poped by an output pipe.
 * @See upipe_grid_in_set_flow_def_real, upipe_grid_update_pts
 *
 * @param upipe input pipe description
 * @param flow_def flow format definition
 * @return an error code
 */
static int upipe_grid_in_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (!ubase_check(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF)) &&
        !ubase_check(uref_flow_match_def(flow_def, UREF_SOUND_FLOW_DEF)))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_grid_in_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the grid input pipe flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def_p filled with the flow definition
 * @return an error code
 */
static int upipe_grid_in_get_flow_def(struct upipe *upipe,
                                      struct uref **flow_def_p)
{
    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe);
    if (flow_def_p)
        *flow_def_p = upipe_grid_in->flow_def;
    return UBASE_ERR_NONE;
}

/** @internal @This removes all past urefs from an input pipe.
 *
 * @param upipe description structure of the input pipe
 * @param next_pts new ptr reference
 */
static void upipe_grid_in_update_pts(struct upipe *upipe, uint64_t next_pts)
{
    struct upipe_grid_in *upipe_grid_in = upipe_grid_in_from_upipe(upipe);
    struct upipe_grid *upipe_grid = upipe_grid_from_in_mgr(upipe->mgr);
    struct uref *flow_def = upipe_grid_in->flow_def;
    uint64_t latency = 0;

    upipe_verbose_va(upipe, "update PTS %"PRIu64, next_pts);

    /* get current input latency */
    if (flow_def) {
        latency = 0;
        uref_clock_get_latency(flow_def, &latency);
    }

    /* iterarte through the input buffers */
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_grid_in->urefs, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);

        /* if this is a new flow def, apply it and continue */
        if (unlikely(ubase_check(uref_flow_get_def(uref, NULL)))) {
            ulist_delete(uchain);
            upipe_grid_in_set_flow_def_real(upipe, uref);
            /* update current input latency */
            flow_def = upipe_grid_in->flow_def;
            latency = 0;
            uref_clock_get_latency(flow_def, &latency);
            continue;
        }

        if (unlikely(!flow_def)) {
            /* no input flow definition set, drop */
            upipe_warn(upipe, "no input flow def set");
            ulist_delete(uchain);
            uref_free(uref);
            continue;
        }

        /* if late buffer, free it and continue */
        uint64_t pts;
        /* checked in upipe_grid_in_input */
        ubase_assert(uref_clock_get_pts_sys(uref, &pts));

        uint64_t retention = ulist_is_last(&upipe_grid_in->urefs, uchain) ?
            upipe_grid->max_retention : 0;

        if (pts + latency + upipe_grid->tolerance + retention < next_pts) {
            upipe_verbose_va(upipe, "drop uref pts %"PRIu64, pts);
            ulist_delete(uchain);
            uref_free(uref);
            continue;
        }

        upipe_grid_in->next_update = 0;
        if (pts + latency + upipe_grid->tolerance > next_pts)
            upipe_grid_in->next_update =
                pts + latency + upipe_grid->tolerance - next_pts;
        if (upipe_grid_in->next_update < upipe_grid->next_update)
            upipe_grid->next_update = upipe_grid_in->next_update;

        /* remaining buffers are up to date,.. */
        break;
    }

    upipe_grid_in->last_update = next_pts;
}

/** @internal @This handles grid input controls.
 *
 * @param upipe input pipe description
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_grid_in_control(struct upipe *upipe,
                                 int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_grid_in_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **flow_def_p = va_arg(args, struct uref **);
            return upipe_grid_in_get_flow_def(upipe, flow_def_p);
        }
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This frees a grid output inner pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_grid_out_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_grid_out_clean_flow_def(upipe);
    upipe_grid_out_clean_sub(upipe);
    upipe_grid_out_clean_output(upipe);
    upipe_grid_out_clean_urefcount(upipe);

    upipe_grid_out_free_void(upipe);
}

/** @internal @This allocates a grid output inner pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe
 */
static struct upipe *upipe_grid_out_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature,
                                          va_list args)
{
    struct upipe *upipe =
        upipe_grid_out_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(!upipe))
        return NULL;

    upipe_grid_out_init_urefcount(upipe);
    upipe_grid_out_init_output(upipe);
    upipe_grid_out_init_sub(upipe);
    upipe_grid_out_init_flow_def(upipe);

    struct upipe_grid_out *upipe_grid_out =
        upipe_grid_out_from_upipe(upipe);

    ulist_init(&upipe_grid_out->inputs);
    upipe_grid_out->flow_def_uptodate = false;
    upipe_grid_out->flow_def_input = false;
    upipe_grid_out->input = NULL;
    upipe_grid_out->tolerance = DEFAULT_TOLERANCE;
    upipe_grid_out->last_input_pts = UINT64_MAX;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This catches event from output sub pipes.
 *
 * @param uprobe structure used to raise events
 * @param upipe description structure of the pipe
 * @param event event raised
 * @param args optional arguments
 * @return an error code
 */
static int upipe_grid_out_catch(struct uprobe *uprobe, struct upipe *inner,
                                int event, va_list args)
{
    if (unlikely(!inner))
        return uprobe_throw_next(uprobe, inner, event, args);

    struct upipe_grid *upipe_grid = upipe_grid_from_out_mgr(inner->mgr);
    struct upipe *upipe = upipe_grid_to_upipe(upipe_grid);

    if (event >= UPROBE_LOCAL) {
        switch (event) {
            case UPROBE_GRID_OUT_UPDATE_PTS:
                UBASE_SIGNATURE_CHECK(args, UPIPE_GRID_OUT_SIGNATURE);
                uint64_t pts = va_arg(args, uint64_t);
                return upipe_grid_update_pts(upipe, pts);
        }
    }

    return uprobe_throw_next(uprobe, inner, event, args);
}

/** @internal @This imports format-related information from input flow to
 * output flow.
 *
 * @param upipe description structure of the pipe
 * @param out_flow destination flow
 * @param in_flow input flow
 * @return an error code
 */
static int upipe_grid_out_import_format(struct upipe *upipe,
                                        struct uref *out_flow,
                                        struct uref *in_flow)
{
    uint64_t hsize, vsize;
    struct urational sar;

    if (ubase_check(uref_flow_match_def(in_flow, UREF_PIC_FLOW_DEF))) {
        uref_pic_flow_clear_format(out_flow);
        uref_pic_flow_copy_format(out_flow, in_flow);
        uref_pic_flow_copy_fps(out_flow, in_flow);
        if (likely(ubase_check(uref_pic_flow_get_hsize(in_flow, &hsize)))) {
            uref_pic_flow_set_hsize(out_flow, hsize);
        } else {
            uref_pic_flow_delete_hsize(out_flow);
        }
        if (likely(ubase_check(uref_pic_flow_get_vsize(in_flow, &vsize)))) {
            uref_pic_flow_set_vsize(out_flow, vsize);
        } else {
            uref_pic_flow_delete_vsize(out_flow);
        }
        if (likely(ubase_check(uref_pic_flow_get_sar(in_flow, &sar)))) {
            uref_pic_flow_set_sar(out_flow, sar);
        } else {
            uref_pic_flow_delete_sar(out_flow);
        }
        bool overscan;
        if (likely(ubase_check(uref_pic_flow_get_overscan(in_flow, &overscan)))) {
            uref_pic_flow_set_overscan(out_flow, overscan);
        } else {
            uref_pic_flow_delete_overscan(out_flow);
        }
        if (likely(ubase_check(uref_pic_get_progressive(in_flow)))) {
            uref_pic_set_progressive(out_flow);
        } else {
            uref_pic_delete_progressive(out_flow);
        }
    }
    else if (ubase_check(uref_flow_match_def(in_flow, UREF_SOUND_FLOW_DEF))) {
        uref_sound_flow_copy_format(out_flow, in_flow);

        uint64_t samples;
        if (likely(ubase_check(uref_sound_flow_get_samples(in_flow, &samples))))
            uref_sound_flow_set_samples(out_flow, samples);

        uint64_t rate;
        if (likely(ubase_check(uref_sound_flow_get_rate(in_flow, &rate))))
            uref_sound_flow_set_rate(out_flow, rate);

        uint8_t channels;
        if (likely(ubase_check(uref_sound_flow_get_channels(in_flow,
                                                            &channels))))
            uref_sound_flow_set_channels(out_flow, channels);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This throws a grid output update pts event.
 *
 * @param upipe description structure of the pipe
 * @param pts new received pts
 * @return an error code
 */
static int upipe_grid_out_throw_update_pts(struct upipe *upipe, uint64_t pts)
{
    upipe_verbose_va(upipe, "update PTS %"PRIu64, pts);
    return upipe_throw(upipe, UPROBE_GRID_OUT_UPDATE_PTS,
                       UPIPE_GRID_OUT_SIGNATURE, pts);
}

/** @internal @This extracts picture data from selected input pipe.
 *
 * @param upipe description structure of the output pipe
 * @param uref picture buffer filled with input picture data
 * @return an error code
 */
static int upipe_grid_out_extract_pic(struct upipe *upipe, struct uref *uref)
{
    struct upipe_grid_out *upipe_grid_out = upipe_grid_out_from_upipe(upipe);
    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe_grid_out->input);

    uint64_t pts = 0;
    /* checked in upipe_grid_out_input */
    ubase_assert(uref_clock_get_pts_sys(uref, &pts));

    /* get first picture */
    struct uchain *uchain = ulist_peek(&upipe_grid_in->urefs);
    if (unlikely(!uchain)) {
        upipe_warn(upipe, "no input buffer available");
        return UBASE_ERR_INVALID;
    }
    struct uref *input_uref = uref_from_uchain(uchain);

    /* duplicate picture buffer */
    struct ubuf *ubuf = ubuf_dup(input_uref->ubuf);
    if (unlikely(!ubuf)) {
        upipe_err(upipe, "fail to duplicate ubuf");
        return UBASE_ERR_ALLOC;
    }

    /* attach buffer to the uref */
    uref_attach_ubuf(uref, ubuf);
    uref_attr_import(uref, input_uref);
    uref_clock_delete_rate(uref);
    return UBASE_ERR_NONE;
}

/** @internal @This extracts sound data from selected input pipe to an uref.
 *
 * @param upipe description structure of the output pipe
 * @param uref sound buffer filled with input sound data
 * @return an error code
 */
static int upipe_grid_out_extract_sound(struct upipe *upipe, struct uref *uref)
{
    struct upipe_grid_out *upipe_grid_out = upipe_grid_out_from_upipe(upipe);
    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe_grid_out->input);
    struct uref *input_flow_def = upipe_grid_in->flow_def;
    uint64_t next_pts;
    int ret = UBASE_ERR_NONE;

    /* checked before */
    assert(input_flow_def);
    /* checked before */
    ubase_assert(uref_clock_get_pts_sys(uref, &next_pts));

    struct uchain *uchain = ulist_peek(&upipe_grid_in->urefs);
    if (unlikely(!uchain)) {
        upipe_warn(upipe, "no input buffer available");
        return UBASE_ERR_INVALID;
    }
    struct uref *input_uref = uref_from_uchain(uchain);

    uint64_t input_pts;
    /* checked before */
    ubase_assert(uref_clock_get_pts_sys(input_uref, &input_pts));
    if (input_pts > next_pts + upipe_grid_out->tolerance) {
        upipe_dbg(upipe, "next input in the futur");
        return UBASE_ERR_INVALID;
    }

    if (upipe_grid_out->last_input_pts != UINT64_MAX &&
        input_pts == upipe_grid_out->last_input_pts) {
        upipe_warn(upipe, "drop duplicate output");
        return UBASE_ERR_INVALID;
    }
    struct ubuf *ubuf = ubuf_dup(input_uref->ubuf);
    if (unlikely(!ubuf)) {
        upipe_err(upipe, "fail to duplicate buffer");
        return UBASE_ERR_ALLOC;
    }
    uref_attach_ubuf(uref, ubuf);
    uref_attr_import(uref, input_uref);
    upipe_grid_out->last_input_pts = input_pts;
    return UBASE_ERR_NONE;
}

/** @internal @This extracts data from the selected input pipe.
 *
 * @param upipe description structure of the output pipe
 * @param uref buffer filled with input data
 * @return an error code
 */
static int upipe_grid_out_extract_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_grid_out *upipe_grid_out = upipe_grid_out_from_upipe(upipe);

    if (!upipe_grid_out->input) {
        upipe_verbose(upipe, "no input set");
        return UBASE_ERR_INVALID;
    }

    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe_grid_out->input);
    struct uref *input_flow_def = upipe_grid_in->flow_def;
    if (unlikely(!input_flow_def))
        return UBASE_ERR_INVALID;

    if (ubase_check(uref_flow_match_def(input_flow_def, UREF_PIC_FLOW_DEF)))
        return upipe_grid_out_extract_pic(upipe, uref);
    else if (ubase_check(uref_flow_match_def(input_flow_def,
                                             UREF_SOUND_FLOW_DEF)))
        return upipe_grid_out_extract_sound(upipe, uref);

    const char *def = "(none)";
    uref_flow_get_def(input_flow_def, &def);
    upipe_warn_va(upipe, "invalid input %s", def);
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This handles grid output pipe input buffers.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer to handle
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_grid_out_input(struct upipe *upipe,
                                 struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_grid_out *upipe_grid_out =
        upipe_grid_out_from_upipe(upipe);
    bool sub_attached = false;

    /* check the input flow def */
    if (unlikely(!upipe_grid_out->input_flow_def)) {
        upipe_warn(upipe, "input flow def is no set");
        uref_free(uref);
        return;
    }

    /* check for pts presence */
    uint64_t pts;
    if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &pts)))) {
        upipe_warn(upipe, "packet without pts");
        uref_free(uref);
        return;
    }

    /* notify new received pts */
    upipe_grid_out_throw_update_pts(upipe, pts);

    if (unlikely(!upipe_grid_out->input)) {
        upipe_verbose(upipe, "no input set");
        goto output;
    }

    /* extract from current input */
    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe_grid_out->input);
    int ret = upipe_grid_out_extract_input(upipe, uref);
    if (unlikely(!ubase_check(ret)))
        goto output;

    sub_attached = true;

output:
    /* input has changed? */
    if (unlikely(!upipe_grid_out->flow_def_uptodate ||
                 upipe_grid_out->flow_def_input != sub_attached)) {
        struct uref *flow_def = uref_dup(upipe_grid_out->input_flow_def);
        if (unlikely(!flow_def)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        /* has input? */
        if (sub_attached) {
            /* import input flow def */
            upipe_grid_out_import_format(
                upipe, flow_def, upipe_grid_in->flow_def);
        }

        /* store new flow def */
        upipe_notice(upipe, "change output flow def");
        uref_dump(flow_def, upipe->uprobe);
        upipe_grid_out_store_flow_def(upipe, flow_def);
        upipe_grid_out->flow_def_uptodate = true;
        upipe_grid_out->flow_def_input = sub_attached;
    }

    upipe_grid_out_output(upipe, uref, upump_p);
}

/** @internal @This set the grid output flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow format definition
 * @return an error code
 */
static int upipe_grid_out_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    struct upipe_grid_out *upipe_grid_out =
        upipe_grid_out_from_upipe(upipe);

    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_grid_out_store_flow_def_input(upipe, flow_def_dup);
    upipe_grid_out->flow_def_uptodate = false;
    return UBASE_ERR_NONE;
}

/** @internal @This set the grid output input pipe.
 *
 * @param upipe description structure of the pipe
 * @param input description of the input pipe to set
 * @return an error code
 */
static int upipe_grid_out_set_input_real(struct upipe *upipe,
                                         struct upipe *input)
{
    struct upipe_grid_out *upipe_grid_out =
        upipe_grid_out_from_upipe(upipe);

    upipe_notice_va(upipe, "switch input %p -> %p",
                    upipe_grid_out->input, input);
    upipe_grid_out->input = input;
    upipe_grid_out->flow_def_uptodate = false;
    upipe_grid_out->last_input_pts = UINT64_MAX;
    return UBASE_ERR_NONE;
}

/** @internal @This gets the grid output input pipe.
 *
 * @param upipe description structure of the pipe
 * @param input_p filled with the input pipe
 * @return an error code
 */
static int upipe_grid_out_get_input_real(struct upipe *upipe,
                                         struct upipe **input_p)
{
    struct upipe_grid_out *upipe_grid_out =
        upipe_grid_out_from_upipe(upipe);
    if (input_p)
        *input_p = upipe_grid_out->input;
    return UBASE_ERR_NONE;
}

/** @internal @This iterates the grid output possible input.
 *
 * @param upipe description structure of the output pipe
 * @param input_p pointer filled with the next input, must be set to NULL the first time.
 * @return an error code
 */
static int upipe_grid_out_iterate_input_real(struct upipe *upipe,
                                             struct upipe **input_p)
{
    struct upipe_grid *upipe_grid = upipe_grid_from_out_mgr(upipe->mgr);
    struct upipe *super = upipe_grid_to_upipe(upipe_grid);
    return upipe_grid_iterate_input(super, input_p);
}

/** @internal @This handles an input changed.
 *
 * @param upipe output pipe description
 * @param input input pipe description
 */
static void upipe_grid_out_handle_input_changed(struct upipe *upipe,
                                                struct upipe *input)
{
    struct upipe_grid_out *upipe_grid_out =
        upipe_grid_out_from_upipe(upipe);

    if (unlikely(upipe_grid_out->input == input))
        upipe_grid_out->flow_def_uptodate = false;
}

/** @internal @This is called when an input pipe is removed.
 *
 * @param upipe output pipe description
 * @param input input pipe description
 */
static void upipe_grid_out_handle_input_removed(struct upipe *upipe,
                                                struct upipe *input)
{
    struct upipe_grid_out *upipe_grid_out =
        upipe_grid_out_from_upipe(upipe);

    if (unlikely(upipe_grid_out->input == input))
        upipe_grid_out_set_input_real(upipe, NULL);
}

/** @internal @This handles control commands of the grid outputs.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_grid_out_control(struct upipe *upipe,
                                  int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_grid_out_control_output(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_grid_out_set_flow_def(upipe, flow_def);
        }

        case UPIPE_GRID_OUT_SET_INPUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_GRID_OUT_SIGNATURE);
            struct upipe *input = va_arg(args, struct upipe *);
            return upipe_grid_out_set_input_real(upipe, input);
        }
        case UPIPE_GRID_OUT_GET_INPUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_GRID_OUT_SIGNATURE);
            struct upipe **input_p = va_arg(args, struct upipe **);
            return upipe_grid_out_get_input_real(upipe, input_p);
        }
        case UPIPE_GRID_OUT_ITERATE_INPUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_GRID_OUT_SIGNATURE);
            struct upipe **input_p = va_arg(args, struct upipe **);
            return upipe_grid_out_iterate_input_real(upipe, input_p);
        }
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This converts grid output pipe events to strings.
 *
 * @param event event to convert
 * @return a string or NULL
 */
static const char *upipe_grid_out_event_str(int event)
{
    switch ((enum uprobe_grid_out_event)event) {
        UBASE_CASE_TO_STR(UPROBE_GRID_OUT_UPDATE_PTS);
        case UPROBE_GRID_OUT_SENTINEL: break;
    }
    return NULL;
}

/** @internal @This converts grid output pipe commands to strings.
 *
 * @param command command to convert
 * @return a string or NULL
 */
static const char *upipe_grid_out_command_str(int command)
{
    switch ((enum upipe_grid_out_command)command) {
        UBASE_CASE_TO_STR(UPIPE_GRID_OUT_SET_INPUT);
        UBASE_CASE_TO_STR(UPIPE_GRID_OUT_GET_INPUT);
        UBASE_CASE_TO_STR(UPIPE_GRID_OUT_ITERATE_INPUT);
        case UPIPE_GRID_OUT_SENTINEL: break;
    }
    return NULL;
}

/** @internal @This initializes the input sub pipe manager.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_grid_init_in_mgr(struct upipe *upipe)
{
    struct upipe_grid *upipe_grid = upipe_grid_from_upipe(upipe);
    struct upipe_mgr *mgr = &upipe_grid->in_mgr;
    mgr->refcount = upipe_grid_to_urefcount_real(upipe_grid);
    mgr->signature = UPIPE_GRID_IN_SIGNATURE;
    mgr->upipe_alloc = upipe_grid_in_alloc;
    mgr->upipe_input = upipe_grid_in_input;
    mgr->upipe_control = upipe_grid_in_control;
}

/** @internal @This initializes the output sub pipe manager.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_grid_init_out_mgr(struct upipe *upipe)
{
    struct upipe_grid *upipe_grid = upipe_grid_from_upipe(upipe);
    struct upipe_mgr *mgr = &upipe_grid->out_mgr;
    mgr->refcount = upipe_grid_to_urefcount_real(upipe_grid);
    mgr->signature = UPIPE_GRID_OUT_SIGNATURE;
    mgr->upipe_alloc = upipe_grid_out_alloc;
    mgr->upipe_input = upipe_grid_out_input;
    mgr->upipe_control = upipe_grid_out_control;
    mgr->upipe_command_str = upipe_grid_out_command_str;
}

/** @internal @This frees a grid pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_grid_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_grid_clean_uclock(upipe);
    upipe_grid_clean_sub_outputs(upipe);
    upipe_grid_clean_sub_inputs(upipe);
    upipe_grid_clean_urefcount(upipe);
    upipe_grid_clean_urefcount_real(upipe);
    upipe_grid_free_void(upipe);
}

/** @internal @This is called when there is no more reference on grid
 * pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_grid_no_ref(struct upipe *upipe)
{
    struct upipe_grid *upipe_grid = upipe_grid_from_upipe(upipe);
    urefcount_release(&upipe_grid->urefcount_real);
}

/** @internal @This allocates a grid pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe.
 */
static struct upipe *upipe_grid_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_grid_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(!upipe))
        return NULL;

    upipe_grid_init_urefcount_real(upipe);
    upipe_grid_init_urefcount(upipe);
    upipe_grid_init_sub_inputs(upipe);
    upipe_grid_init_sub_outputs(upipe);
    upipe_grid_init_in_mgr(upipe);
    upipe_grid_init_out_mgr(upipe);
    upipe_grid_init_uclock(upipe);

    struct upipe_grid *upipe_grid = upipe_grid_from_upipe(upipe);
    upipe_grid->tolerance = DEFAULT_TOLERANCE;
    upipe_grid->last_update_pts = 0;
    upipe_grid->next_update = 0;
    upipe_grid->max_retention = MAX_RETENTION;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This get the current system time.
 *
 * @param upipe description of the pipe structure
 * @return an error code
 */
static int upipe_grid_uclock_now(struct upipe *upipe, uint64_t *now)
{
    struct upipe_grid *upipe_grid = upipe_grid_from_upipe(upipe);
    if (!upipe_grid->uclock)
        return UBASE_ERR_INVALID;
    if (now)
        *now = uclock_now(upipe_grid->uclock);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the max retention time for input buffers.
 *
 * @param upipe description structure of the pipe
 * @param max_retention max retention time value in 27MHz ticks
 * @return an error code
 */
static int upipe_grid_set_max_retention_real(struct upipe *upipe,
                                             uint64_t max_retention)
{
    struct upipe_grid *upipe_grid = upipe_grid_from_upipe(upipe);
    upipe_grid->max_retention = max_retention;
    return UBASE_ERR_NONE;
}

/** @internal @This handles control command of the grid pipe.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_grid_control(struct upipe *upipe,
                              int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UCLOCK:
            upipe_grid_require_uclock(upipe);
            return UBASE_ERR_NONE;
    }

    if (command >= UPIPE_CONTROL_LOCAL &&
        ubase_get_signature(args) != UPIPE_GRID_SIGNATURE)
        return UBASE_ERR_UNHANDLED;

    switch (command) {
        case UPIPE_GRID_SET_MAX_RETENTION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_GRID_SIGNATURE);
            uint64_t max_retention = va_arg(args, uint64_t);
            return upipe_grid_set_max_retention_real(upipe, max_retention);
        }
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This removes all past urefs from input pipes.
 *
 * @param upipe description structure of the pipe
 * @param next_pts new pts reference
 * @return an error code
 */
static int upipe_grid_update_pts(struct upipe *upipe, uint64_t next_pts)
{
    struct upipe_grid *upipe_grid = upipe_grid_from_upipe(upipe);

    if (upipe_grid->last_update_pts &&
        upipe_grid->last_update_pts + upipe_grid->next_update >= next_pts)
        return UBASE_ERR_NONE;
    upipe_grid->last_update_pts = next_pts;

    /* iterate through the input pipes */
    struct upipe *in = NULL;
    while (ubase_check(upipe_grid_iterate_input(upipe, &in)) && in)
        upipe_grid_in_update_pts(in, next_pts);
    return UBASE_ERR_NONE;
}

/** @This allocates a new grid input.
 *
 * @param upipe description structure of the pipe
 * @param uprobe structure used to raise events
 * @return an allocated sub pipe.
 */
struct upipe *upipe_grid_alloc_input(struct upipe *upipe,
                                     struct uprobe *uprobe)
{
    struct upipe_grid *upipe_grid = upipe_grid_from_upipe(upipe);
    return upipe_void_alloc(&upipe_grid->in_mgr,
                            uprobe_alloc(upipe_grid_in_catch, uprobe));
}

/** @This allocates a new grid output.
 *
 * @param upipe description structure of the pipe
 * @param uprobe structure used to raise events
 * @return an allocated sub pipe.
 */
struct upipe *upipe_grid_alloc_output(struct upipe *upipe,
                                      struct uprobe *uprobe)
{
    struct upipe_grid *upipe_grid = upipe_grid_from_upipe(upipe);
    return upipe_void_alloc(&upipe_grid->out_mgr,
                            uprobe_alloc(upipe_grid_out_catch, uprobe));
}

/** @internal @This is the grid manager. */
static struct upipe_mgr upipe_grid_mgr = {
    .refcount = NULL,
    .signature = UPIPE_GRID_SIGNATURE,

    .upipe_alloc = upipe_grid_alloc,
    .upipe_control = upipe_grid_control,
};

/** @This returns grid pipe manager.
 *
 * @return a pointer to the pipe manager
 */
struct upipe_mgr *upipe_grid_mgr_alloc(void)
{
    return &upipe_grid_mgr;
}
