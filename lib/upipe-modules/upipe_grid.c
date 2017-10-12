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

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow_def.h>

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

#define UPIPE_GRID_IN_SIGNATURE        UBASE_FOURCC('i','n',' ',' ')
#define UPIPE_GRID_OUT_SIGNATURE       UBASE_FOURCC('o','u','t',' ')

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
    /** pts latency */
    uint64_t latency;
};

/** @hidden */
static int upipe_grid_update_pts(struct upipe *upipe, uint64_t next_pts);
/** @hidden */
static int upipe_grid_catch_out(struct uprobe *uprobe, struct upipe *upipe,
                                 int event, va_list args);

UPIPE_HELPER_UPIPE(upipe_grid, upipe, UPIPE_GRID_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_grid, urefcount, upipe_grid_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_grid, urefcount_real, upipe_grid_free);
UPIPE_HELPER_VOID(upipe_grid);

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
};

UPIPE_HELPER_UPIPE(upipe_grid_in, upipe, UPIPE_GRID_IN_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_grid_in, urefcount, upipe_grid_in_free);
UPIPE_HELPER_VOID(upipe_grid_in);
UPIPE_HELPER_SUBPIPE(upipe_grid, upipe_grid_in, input, in_mgr,
                     inputs, uchain);
UPIPE_HELPER_FLOW_DEF(upipe_grid_in, flow_def, flow_attr);

/** @internal @This enumerates the grid output control commands. */
enum upipe_grid_out_command {
    /** sentinel */
    UPIPE_GRID_OUT_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** get the grid output input pipe (struct upipe **) */
    UPIPE_GRID_OUT_GET_INPUT,
    /** set the grid output input pipe (struct upipe *) */
    UPIPE_GRID_OUT_SET_INPUT,
};

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

    if (unlikely(ubase_check(uref_flow_get_def(uref, NULL)))) {
        ulist_add(&upipe_grid_in->urefs, uref_to_uchain(uref));
        return;
    }

    uint64_t pts = 0;
    if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &pts)))) {
        upipe_warn(upipe, "packet without pts");
        uref_free(uref);
        return;
    }

    ulist_add(&upipe_grid_in->urefs, uref_to_uchain(uref));

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_grid_in->urefs, uchain, uchain_tmp) {
        uref = uref_from_uchain(uchain);

        if (unlikely(ubase_check(uref_flow_get_def(uref, NULL))))
            continue;

        uint64_t current_pts;
        ubase_assert(uref_clock_get_pts_sys(uref, &current_pts));
        if (unlikely(current_pts > pts)) {
            ulist_delete(uchain);
            uref_free(uref);
            continue;
        }

        if (unlikely(pts - current_pts > MAX_RETENTION)) {
            ulist_delete(uchain);
            uref_free(uref);
            continue;
        };

        break;
    }
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
    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe);
    struct upipe_grid *upipe_grid = upipe_grid_from_in_mgr(upipe->mgr);
    struct upipe *super = upipe_grid_to_upipe(upipe_grid);
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

    /* iterarte through the input buffers */
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_grid_in->urefs, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);

        /* if this is a new flow def, apply it and continue */
        const char *def;
        if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
            ulist_delete(uchain);
            upipe_grid_in_set_flow_def_real(upipe, uref);
            continue;
        }

        /* if late buffer, free it and continue */
        uint64_t pts = 0;
        uref_clock_get_pts_sys(uref, &pts);
        if ((!ulist_is_last(&upipe_grid_in->urefs, uchain) &&
             pts < next_pts) ||
            (next_pts > MAX_RETENTION && pts < next_pts - MAX_RETENTION)) {
            upipe_verbose_va(upipe, "drop uref pts %"PRIu64, pts);
            ulist_delete(uchain);
            uref_free(uref);
            continue;
        }

        /* remaining buffers are up to date,.. */
        break;
    }
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
    struct upipe_grid_out *upipe_grid_out =
        upipe_grid_out_from_upipe(upipe);

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

/** @internal @This copies format-related information from
 * input flow to output flow.
 *
 * @param upipe description structure of the pipe
 * @param out_flow destination flow
 * @param in_flow input flow
 * @return an error code
 */
static int upipe_grid_out_switch_format(struct upipe *upipe,
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
    return upipe_throw(upipe, UPROBE_GRID_OUT_UPDATE_PTS,
                       UPIPE_GRID_OUT_SIGNATURE, pts);
}

static int upipe_grid_out_extract_pic(struct upipe *upipe,
                                      struct uref *uref,
                                      struct uchain *urefs)
{
    struct upipe_grid_out *upipe_grid_out = upipe_grid_out_from_upipe(upipe);
    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe_grid_out->input);

    if (unlikely(ulist_empty(&upipe_grid_in->urefs))) {
        upipe_dbg(upipe, "input underflow");
        return UBASE_ERR_INVALID;
    }

    struct uref *input_ref =
        uref_from_uchain(ulist_peek(&upipe_grid_in->urefs));
    struct ubuf *ubuf = ubuf_dup(input_ref->ubuf);
    if (unlikely(!ubuf)) {
        upipe_err(upipe, "fail to duplicate ubuf");
        return UBASE_ERR_ALLOC;
    }

    uint64_t ref_pts = 0;
    uref_clock_get_pts_sys(uref, &ref_pts);
    uint64_t pts = 0;
    uref_clock_get_pts_sys(input_ref, &pts);
    uint64_t diff = pts >= ref_pts ? pts - ref_pts : ref_pts - pts;
    if (diff > 41 * (UCLOCK_FREQ / 1000))
        upipe_warn_va(upipe, "diff %s%"PRIu64" ms",
                      pts > ref_pts ? "" : "-",  diff / (UCLOCK_FREQ / 1000));

    ulist_init(urefs);
    uref_attach_ubuf(uref, ubuf);
    uref_attr_import(uref, input_ref);
    uref_clock_delete_rate(uref);
    ulist_add(urefs, uref_to_uchain(uref));
    return UBASE_ERR_NONE;
}

static int upipe_grid_out_extract_sound(struct upipe *upipe,
                                        struct uref *uref,
                                        struct uchain *urefs)
{
    struct upipe_grid_out *upipe_grid_out = upipe_grid_out_from_upipe(upipe);
    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe_grid_out->input);
    struct uref *input_flow_def = upipe_grid_in->flow_def;
    uint64_t next_pts = 0, next_duration = 0;
    int ret = UBASE_ERR_NONE;

    ubase_assert(uref_clock_get_pts_sys(uref, &next_pts));
    ubase_assert(uref_clock_get_duration(uref, &next_duration));

    ulist_init(urefs);

    struct uchain *uchain;
    bool first = true;
    ulist_foreach(&upipe_grid_in->urefs, uchain) {
        struct uref *input_ref = uref_from_uchain(uchain);

        const char *def;
        if (unlikely(ubase_check(uref_flow_get_def(input_ref, &def))))
            break;

        uint64_t pts = 0, duration = 0;
        ubase_assert(uref_clock_get_pts_sys(input_ref, &pts));
        ubase_assert(uref_clock_get_duration(input_ref, &duration));

        if (pts > next_pts + next_duration)
            break;

        struct uref *next_uref;
        if (first) {
            next_uref = uref;
        }
        else {
            next_uref = uref_sibling_alloc_control(uref);
            if (unlikely(!next_uref)) {
                ret = UBASE_ERR_ALLOC;
                break;
            }
            uref_clock_set_pts_prog(next_uref, next_pts);
        }

        struct ubuf *ubuf = ubuf_dup(input_ref->ubuf);
        if (unlikely(!ubuf)) {
            if (!first)
                uref_free(next_uref);
            ret = UBASE_ERR_ALLOC;
            break;
        }
        uref_attach_ubuf(next_uref, ubuf);
        uref_attr_import(uref, input_ref);
        ubase_assert(uref_clock_set_duration(next_uref, duration));
        ulist_add(urefs, uref_to_uchain(next_uref));

        if (duration > next_duration)
            break;
        next_pts += duration;
        next_duration -= duration;
        first = false;
    }

    if (unlikely(!ubase_check(ret))) {
        while ((uchain = ulist_pop(urefs)))
            uref_free(uref_from_uchain(uchain));
    }

    return ret;
}

static int upipe_grid_out_extract_input(struct upipe *upipe,
                                        struct uref *uref,
                                        struct uchain *urefs)
{
    struct upipe_grid_out *upipe_grid_out = upipe_grid_out_from_upipe(upipe);

    if (!upipe_grid_out->input)
        return UBASE_ERR_INVALID;

    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe_grid_out->input);
    struct uref *input_flow_def = upipe_grid_in->flow_def;
    if (unlikely(!input_flow_def))
        return UBASE_ERR_INVALID;

    if (ubase_check(uref_flow_match_def(input_flow_def, UREF_PIC_FLOW_DEF)))
        return upipe_grid_out_extract_pic(upipe, uref, urefs);
    else if (ubase_check(uref_flow_match_def(input_flow_def,
                                             UREF_SOUND_FLOW_DEF)))
        return upipe_grid_out_extract_sound(upipe, uref, urefs);
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
        upipe_warn(upipe, "flow def set");
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
    //if (pts > DEFAULT_TOLERANCE)
    //    pts -= DEFAULT_TOLERANCE;
    upipe_grid_out_throw_update_pts(upipe, pts);

    if (!upipe_grid_out->input) {
        upipe_verbose(upipe, "no input set");
        goto output;
    }

    struct upipe_grid_in *upipe_grid_in =
        upipe_grid_in_from_upipe(upipe_grid_out->input);
    struct uchain urefs;
    int ret = upipe_grid_out_extract_input(upipe, uref, &urefs);
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
            upipe_grid_out_switch_format(upipe, flow_def,
                                          upipe_grid_in->flow_def);
        }

        /* store new flow def */
        upipe_notice(upipe, "change output flow def");
        uref_dump(flow_def, upipe->uprobe);
        upipe_grid_out_store_flow_def(upipe, NULL);
        upipe_grid_out_store_flow_def(upipe, flow_def);
        upipe_grid_out->flow_def_uptodate = true;
        upipe_grid_out->flow_def_input = sub_attached;
    }

    upipe_use(upipe);
    /* output buffer */
    if (sub_attached) {
        struct uchain *uchain;

        while ((uchain = ulist_pop(&urefs)))
            upipe_grid_out_output(upipe, uref_from_uchain(uchain), upump_p);
    }
    else {
        upipe_grid_out_output(upipe, uref, upump_p);
    }
    upipe_release(upipe);
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

    UBASE_RETURN(uref_flow_match_def(flow_def, UREF_VOID_FLOW_DEF));
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

/** @This exports the input setter of a grid output pipe.
 *
 * @param upipe description structure of the pipe
 * @param input description of the input pipe to set
 * @return an error code
 */
int upipe_grid_out_set_input(struct upipe *upipe, struct upipe *input)
{
    return upipe_control(upipe, UPIPE_GRID_OUT_SET_INPUT,
                         UPIPE_GRID_OUT_SIGNATURE, input);
}

/** @This exports the input getter of a grid output pipe.
 *
 * @param upipe description structure of the pipe
 * @param input_p filled with the input pipe
 * @return an error code
 */
int upipe_grid_out_get_input(struct upipe *upipe, struct upipe **input_p)
{
    return upipe_control(upipe, UPIPE_GRID_OUT_GET_INPUT,
                         UPIPE_GRID_OUT_SIGNATURE, input_p);
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

    struct upipe_grid *upipe_grid = upipe_grid_from_upipe(upipe);
    upipe_grid->latency = 0;

    upipe_throw_ready(upipe);

    return upipe;
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
};

/** @This returns grid pipe manager.
 *
 * @return a pointer to the pipe manager
 */
struct upipe_mgr *upipe_grid_mgr_alloc(void)
{
    return &upipe_grid_mgr;
}
