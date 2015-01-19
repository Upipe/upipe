/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe module video continuity
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-modules/upipe_videocont.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** only accept pics */
#define EXPECTED_FLOW_DEF "pic."
/** default pts tolerance (late packets) */
#define DEFAULT_TOLERANCE (UCLOCK_FREQ / 25)

/** @internal @This is the private context of a ts join pipe. */
struct upipe_videocont {
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
    /** true if flow definition is up to date */
    bool flow_def_uptodate;
    /** true if subinput format has been copied to output flow def */
    bool flow_input_format;
    /** input flow definition packet */
    struct uref *flow_def_input;

    /** list of input subpipes */
    struct uchain subs;

    /** current input */
    struct upipe *input_cur;
    /** next input */
    char *input_name;

    /** pts tolerance */
    uint64_t tolerance;
    /** pts latency */
    uint64_t latency;
    /** last pts received from source */
    uint64_t last_pts;
    /** pointer value of last taken uref, for debug purposes */
    struct uref *last_uref;

    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_videocont, upipe, UPIPE_VIDEOCONT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_videocont, urefcount, upipe_videocont_free)
UPIPE_HELPER_VOID(upipe_videocont)
UPIPE_HELPER_OUTPUT(upipe_videocont, output, flow_def, output_state, request_list)

/** @internal @This is the private context of an input of a videocont pipe. */
struct upipe_videocont_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** temporary uref storage */
    struct uchain urefs;

    /** input flow definition packet */
    struct uref *flow_def;
    /** flow name in the latest flow definition packet */
    const char *flow_name;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_videocont_sub, upipe, UPIPE_VIDEOCONT_SUB_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_videocont_sub, urefcount, upipe_videocont_sub_dead)
UPIPE_HELPER_VOID(upipe_videocont_sub)

UPIPE_HELPER_SUBPIPE(upipe_videocont, upipe_videocont_sub, sub, sub_mgr,
                     subs, uchain)

/** @hidden */
static int upipe_videocont_switch_input(struct upipe *upipe,
                                        struct upipe *input);

/** @internal @This allocates an input subpipe of a videocont pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_videocont_sub_alloc(struct upipe_mgr *mgr,
                                               struct uprobe *uprobe,
                                               uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_videocont_sub_alloc_void(mgr,
                                     uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_videocont_sub *upipe_videocont_sub =
        upipe_videocont_sub_from_upipe(upipe);
    upipe_videocont_sub_init_urefcount(upipe);
    upipe_videocont_sub_init_sub(upipe);
    ulist_init(&upipe_videocont_sub->urefs);
    upipe_videocont_sub->flow_def = NULL;
    upipe_videocont_sub->flow_name = NULL;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_videocont_sub_input(struct upipe *upipe, struct uref *uref,
                                      struct upump **upump_p)
{
    struct upipe_videocont_sub *upipe_videocont_sub =
                                upipe_videocont_sub_from_upipe(upipe);

    uint64_t pts;
    if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &pts)))) {
        upipe_warn_va(upipe, "packet without pts");
        uref_free(uref);
        return;
    }
    struct upipe_videocont *upipe_videocont =
                            upipe_videocont_from_sub_mgr(upipe->mgr);
    upipe_verbose_va(upipe, "picture received (%"PRId64")", pts);
    if (pts + upipe_videocont->latency < upipe_videocont->last_pts) {
        upipe_warn_va(upipe, "late picture received (%"PRId64" ms)",
                      (upipe_videocont->last_pts - pts) / 27000);
    }

    if (unlikely(!uref->ubuf)) {
        upipe_warn_va(upipe, "received empty packet");
        uref_free(uref);
        return;
    }

    ulist_add(&upipe_videocont_sub->urefs, uref_to_uchain(uref));
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet (belongs to the callee)
 * @return an error code
 */
static void upipe_videocont_sub_handle_flow_def(struct upipe *upipe,
                                                struct uref *flow_def)
{
    struct upipe_videocont_sub *upipe_videocont_sub =
           upipe_videocont_sub_from_upipe(upipe);
    struct upipe_videocont *upipe_videocont =
                            upipe_videocont_from_sub_mgr(upipe->mgr);

    if (unlikely(upipe_videocont_sub->flow_def)) {
        uref_free(upipe_videocont_sub->flow_def);
    }
    upipe_videocont_sub->flow_def = flow_def;

    if (upipe_videocont->input_cur == upipe)
        upipe_videocont->flow_def_uptodate = false;
}

/** @internal @This receives the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_videocont_sub_set_flow_def(struct upipe *upipe,
                                            struct uref *flow_def)
{
    struct upipe_videocont_sub *upipe_videocont_sub =
           upipe_videocont_sub_from_upipe(upipe);
    struct upipe_videocont *upipe_videocont =
                            upipe_videocont_from_sub_mgr(upipe->mgr);

    if (flow_def == NULL) {
        return UBASE_ERR_INVALID;
    }
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    /* check flow against (next) grid input name */
    upipe_videocont_sub->flow_name = NULL;
    if ((upipe_videocont->input_name
        && likely(ubase_check(uref_flow_get_name(flow_def_dup,
                    &upipe_videocont_sub->flow_name)))
        && !strcmp(upipe_videocont->input_name,
            upipe_videocont_sub->flow_name))) {
        upipe_videocont_switch_input(upipe_videocont_to_upipe(upipe_videocont),
                                     upipe);
    }

    if (upipe_videocont_sub->flow_def != NULL)
        ulist_add(&upipe_videocont_sub->urefs, uref_to_uchain(flow_def_dup));
    else
        upipe_videocont_sub_handle_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @This sets a videocont subpipe as its superpipe input.
 *
 * @param upipe description structure of the (sub)pipe
 * @return an error code
 */
static inline int _upipe_videocont_sub_set_input(struct upipe *upipe)
{
    struct upipe_videocont *upipe_videocont =
                            upipe_videocont_from_sub_mgr(upipe->mgr);
    struct upipe *superpipe = upipe_videocont_to_upipe(upipe_videocont);
    free(upipe_videocont->input_name);
    upipe_videocont->input_name = NULL;
    return upipe_videocont_switch_input(superpipe, upipe);
}

/** @internal @This processes control commands on a subpipe of a videocont
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static int upipe_videocont_sub_control(struct upipe *upipe,
                                       int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            struct upipe_videocont *upipe_videocont =
                                    upipe_videocont_from_sub_mgr(upipe->mgr);
            return upipe_videocont_alloc_output_proxy(
                    upipe_videocont_to_upipe(upipe_videocont), request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            struct upipe_videocont *upipe_videocont =
                                    upipe_videocont_from_sub_mgr(upipe->mgr);
            return upipe_videocont_free_output_proxy(
                    upipe_videocont_to_upipe(upipe_videocont), request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_videocont_sub_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_videocont_sub_get_super(upipe, p);
        }

        case UPIPE_VIDEOCONT_SUB_SET_INPUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VIDEOCONT_SUB_SIGNATURE)
            return _upipe_videocont_sub_set_input(upipe);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This marks an input subpipe as dead.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_videocont_sub_dead(struct upipe *upipe)
{
    struct upipe_videocont_sub *upipe_videocont_sub =
                                upipe_videocont_sub_from_upipe(upipe);
    struct upipe_videocont *upipe_videocont =
                            upipe_videocont_from_sub_mgr(upipe->mgr);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_videocont_sub->urefs, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }
    if (upipe == upipe_videocont->input_cur) {
        upipe_videocont_switch_input(upipe_videocont_to_upipe(upipe_videocont),
                                     NULL);
    }

    if (likely(upipe_videocont_sub->flow_def)) {
        uref_free(upipe_videocont_sub->flow_def);
    }

    upipe_throw_dead(upipe);
    upipe_videocont_sub_clean_sub(upipe);
    upipe_videocont_sub_clean_urefcount(upipe);
    upipe_videocont_sub_free_void(upipe);
}

/** @internal @This initializes the input manager for a videocont pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_videocont_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_videocont->sub_mgr;
    sub_mgr->refcount = upipe_videocont_to_urefcount(upipe_videocont);
    sub_mgr->signature = UPIPE_VIDEOCONT_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_videocont_sub_alloc;
    sub_mgr->upipe_input = upipe_videocont_sub_input;
    sub_mgr->upipe_control = upipe_videocont_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a videocont pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_videocont_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_videocont_alloc_void(mgr,
                                 uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_videocont_init_urefcount(upipe);
    upipe_videocont_init_output(upipe);
    upipe_videocont_init_sub_mgr(upipe);
    upipe_videocont_init_sub_subs(upipe);

    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    upipe_videocont->input_cur = NULL;
    upipe_videocont->input_name = NULL;
    upipe_videocont->tolerance = DEFAULT_TOLERANCE;
    upipe_videocont->latency = 0;
    upipe_videocont->last_pts = 0;
    upipe_videocont->flow_def_input = NULL;
    upipe_videocont->flow_input_format = false;
    upipe_videocont->flow_def_uptodate = false;
    upipe_videocont->last_uref = NULL;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This copies format-related information from
 * input flow to output flow.
 *
 * @param upipe description structure of the pipe
 * @param out_flow destination flow
 * @param in_flow input flow
 * @return an error code
 */
static int upipe_videocont_switch_format(struct upipe *upipe,
                                         struct uref *out_flow,
                                         struct uref *in_flow)
{
    uint64_t hsize, vsize;
    struct urational sar;
    uref_pic_flow_clear_format(out_flow);
    uref_pic_flow_copy_format(out_flow, in_flow);
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
    if (likely(ubase_check(uref_pic_flow_get_overscan(in_flow)))) {
        uref_pic_flow_set_overscan(out_flow);
    } else {
        uref_pic_flow_delete_overscan(out_flow);
    }
    if (likely(ubase_check(uref_pic_get_progressive(in_flow)))) {
        uref_pic_set_progressive(out_flow);
    } else {
        uref_pic_delete_progressive(out_flow);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This switches to a new input.
 *
 * @param upipe description structure of the pipe
 * @param input description structure of the input pipe
 * @return an error code
 */
static int upipe_videocont_switch_input(struct upipe *upipe,
                                        struct upipe *input)
{
    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    char *name = upipe_videocont->input_name ?
                 upipe_videocont->input_name : "(noname)";
    upipe_videocont->input_cur = input;
    upipe_notice_va(upipe, "switched to input \"%s\" (%p)", name, input);

    upipe_videocont->flow_def_uptodate = false;

    return UBASE_ERR_NONE;
}

/** @internal @This processes reference ("clock") input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_videocont_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    struct uchain *uchain, *uchain_sub, *uchain_tmp;
    uint64_t next_pts = 0;
    bool sub_attached = false;

    if (unlikely(!upipe_videocont->flow_def_input)) {
        upipe_warn_va(upipe, "need to define flow def first");
        uref_free(uref);
        return;
    }

    if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &next_pts)))) {
        upipe_warn_va(upipe, "packet without pts");
        uref_free(uref);
        return;
    }

    /* clean old urefs first */
    int subs = 0;
    ulist_foreach(&upipe_videocont->subs, uchain_sub) {
        struct upipe_videocont_sub *sub =
               upipe_videocont_sub_from_uchain(uchain_sub);
        ulist_delete_foreach(&sub->urefs, uchain, uchain_tmp) {
            uint64_t pts = 0;
            struct uref *uref_uchain = uref_from_uchain(uchain);
            const char *def;
            if (ubase_check(uref_flow_get_def(uref_uchain, &def))) {
                ulist_delete(uchain);
                upipe_videocont_sub_handle_flow_def(
                        upipe_videocont_sub_to_upipe(sub), uref_uchain);
            } else {
                uref_clock_get_pts_sys(uref_uchain, &pts);
                if (pts + upipe_videocont->latency <
                        next_pts - upipe_videocont->tolerance) {
                    upipe_verbose_va(upipe, "(%d) deleted uref %p (%"PRIu64")",
                                     subs, uref_uchain, pts);
                    ulist_delete(uchain);
                    uref_free(uref_uchain);
                } else {
                    break;
                }
            }
        }
        subs++;
    }

    /* attach next ubuf from current input */
    if (unlikely(!upipe_videocont->input_cur)) {
        goto output;
    }
    struct upipe_videocont_sub *input = 
           upipe_videocont_sub_from_upipe(upipe_videocont->input_cur);
    if (unlikely(ulist_empty(&input->urefs))) {
        goto output;
    }

    struct uref *next_uref = uref_from_uchain(ulist_peek(&input->urefs));
    uint64_t pts = 0;
    uref_clock_get_pts_sys(next_uref, &pts);
    upipe_videocont->last_pts = next_pts - upipe_videocont->tolerance;

    if (pts + upipe_videocont->latency <
            next_pts + upipe_videocont->tolerance) {
        upipe_verbose_va(upipe, "attached ubuf %p (%"PRIu64") next %"PRIu64,
                         next_uref->ubuf, pts, next_pts);
        uref_attach_ubuf(uref, ubuf_dup(next_uref->ubuf));
        if (likely(ubase_check(uref_pic_get_progressive(next_uref)))) {
            uref_pic_set_progressive(uref);
        } else {
            uref_pic_delete_progressive(uref);
        }
        if (likely(ubase_check(uref_pic_get_tf(next_uref)))) {
            uref_pic_set_tf(uref);
        } else {
            uref_pic_delete_tf(uref);
        }
        if (likely(ubase_check(uref_pic_get_bf(next_uref)))) {
            uref_pic_set_bf(uref);
        } else {
            uref_pic_delete_bf(uref);
        }
        if (likely(ubase_check(uref_pic_get_tff(next_uref)))) {
            uref_pic_set_tff(uref);
        } else {
            uref_pic_delete_tff(uref);
        }
        if (upipe_videocont->last_uref == next_uref) {
            upipe_warn_va(upipe, "reusing the same picture %"PRIu64" %"PRIu64,
                      pts + upipe_videocont->latency, next_pts + upipe_videocont->tolerance);
        }
        upipe_videocont->last_uref = next_uref;
        sub_attached = true;
        next_uref = NULL;
        /* do NOT pop/free from list so that we can dup frame if needed */
    }

output:
    if (unlikely(!upipe_videocont->flow_def_uptodate
                 || (upipe_videocont->flow_input_format != sub_attached))) {
        struct uref *flow_def = uref_dup(upipe_videocont->flow_def_input);
        if (unlikely(!flow_def)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        if (sub_attached) {
            upipe_videocont_switch_format(upipe, flow_def,
                        upipe_videocont_sub_from_upipe(
                                upipe_videocont->input_cur)->flow_def);
        }
        upipe_videocont_store_flow_def(upipe, flow_def);
        upipe_videocont->flow_input_format = sub_attached;
        upipe_videocont->flow_def_uptodate = true;
    }
    upipe_verbose_va(upipe, "outputting picture %p (%"PRIu64")", uref, next_pts);
    upipe_videocont_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_videocont_set_flow_def(struct upipe *upipe,
                                        struct uref *flow_def)
{
    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    if (flow_def == NULL) {
        return UBASE_ERR_INVALID;
    }
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    /* local copy of input (ref) flow def */
    if (unlikely((flow_def = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    if (upipe_videocont->flow_def_input) {
        uref_free(upipe_videocont->flow_def_input);
    }
    upipe_videocont->flow_def_input = flow_def;

    upipe_videocont->flow_def_uptodate = false;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input by name.
 *
 * @param upipe description structure of the pipe
 * @param name input name
 * @return an error code
 */
static int _upipe_videocont_set_input(struct upipe *upipe, const char *name)
{
    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    char *name_dup = NULL;
    free(upipe_videocont->input_name);

    if (name) {
        name_dup = strdup(name);
        if (unlikely(!name_dup)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }

        upipe_videocont->input_name = name_dup;

        struct uchain *uchain;
        ulist_foreach(&upipe_videocont->subs, uchain) {
            struct upipe_videocont_sub *sub =
                       upipe_videocont_sub_from_uchain(uchain);
            const char *flow_name = NULL;
            if (sub->flow_def
                && likely(ubase_check(uref_flow_get_name(sub->flow_def, &flow_name)))
                && !strcmp(name_dup, flow_name)) {
                upipe_videocont_switch_input(upipe, upipe_videocont_sub_to_upipe(sub));
                break;
            }
        }
    } else {
        upipe_videocont->input_name = NULL;
        upipe_videocont_switch_input(upipe, NULL);
    }

    return UBASE_ERR_NONE;
}

/** @This returns the current input name if any.
 *
 * @param upipe description structure of the pipe
 * @param name_p filled with current input name pointer or NULL
 * @return an error code
 */
static int _upipe_videocont_get_current_input(struct upipe *upipe,
                                              const char **name_p)
{
    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    if (unlikely(!name_p)) {
        return UBASE_ERR_INVALID;
    }

    *name_p = NULL;
    if (upipe_videocont->input_cur) {
        struct upipe_videocont_sub *sub = 
               upipe_videocont_sub_from_upipe(upipe_videocont->input_cur);
        *name_p = sub->flow_name;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_videocont_control(struct upipe *upipe,
                                   int command, va_list args)
{
    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_videocont_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_videocont_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_videocont_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_videocont_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_videocont_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_videocont_set_output(upipe, output);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_videocont_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_videocont_iterate_sub(upipe, p);
        }

        case UPIPE_VIDEOCONT_SET_INPUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VIDEOCONT_SIGNATURE)
            const char *name = va_arg(args, const char*);
            return _upipe_videocont_set_input(upipe, name);
        }
        case UPIPE_VIDEOCONT_GET_INPUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VIDEOCONT_SIGNATURE)
            *va_arg(args, const char**) = upipe_videocont->input_name;
            return UBASE_ERR_NONE;
        }
        case UPIPE_VIDEOCONT_SET_TOLERANCE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VIDEOCONT_SIGNATURE)
            upipe_videocont->tolerance = va_arg(args, uint64_t);
            return UBASE_ERR_NONE;
        }
        case UPIPE_VIDEOCONT_GET_TOLERANCE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VIDEOCONT_SIGNATURE)
            *va_arg(args, uint64_t *) = upipe_videocont->tolerance;
            return UBASE_ERR_NONE;
        }
        case UPIPE_VIDEOCONT_SET_LATENCY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VIDEOCONT_SIGNATURE)
            upipe_videocont->latency = va_arg(args, uint64_t);
            return UBASE_ERR_NONE;
        }
        case UPIPE_VIDEOCONT_GET_LATENCY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VIDEOCONT_SIGNATURE)
            *va_arg(args, uint64_t *) = upipe_videocont->latency;
            return UBASE_ERR_NONE;
        }
        case UPIPE_VIDEOCONT_GET_CURRENT_INPUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VIDEOCONT_SIGNATURE)
            const char **name_p = va_arg(args, const char **);
            return _upipe_videocont_get_current_input(upipe, name_p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_videocont_free(struct upipe *upipe)
{
    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    upipe_throw_dead(upipe);

    free(upipe_videocont->input_name);
    if (likely(upipe_videocont->flow_def_input)) {
        uref_free(upipe_videocont->flow_def_input);
    }

    upipe_videocont_clean_sub_subs(upipe);
    upipe_videocont_clean_output(upipe);
    upipe_videocont_clean_urefcount(upipe);
    upipe_videocont_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_videocont_mgr = {
    .refcount = NULL,
    .signature = UPIPE_VIDEOCONT_SIGNATURE,

    .upipe_alloc = upipe_videocont_alloc,
    .upipe_input = upipe_videocont_input,
    .upipe_control = upipe_videocont_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all videocont pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_videocont_mgr_alloc(void)
{
    return &upipe_videocont_mgr;
}
