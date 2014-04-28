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
#include <upipe/uref_block_flow.h>
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
#define TOLERANCE (UCLOCK_FREQ / 1000)

/** @internal @This is the private context of a ts join pipe. */
struct upipe_videocont {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** list of input subpipes */
    struct uchain subs;

    /** current input */
    struct upipe *input_cur;
    /** next input */
    char *input_name;

    /** pts tolerance */
    uint64_t tolerance;

    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_videocont, upipe, UPIPE_VIDEOCONT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_videocont, urefcount, upipe_videocont_free)
UPIPE_HELPER_VOID(upipe_videocont)
UPIPE_HELPER_OUTPUT(upipe_videocont, output, flow_def, flow_def_sent)

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

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_videocont_sub, upipe, UPIPE_VIDEOCONT_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_videocont_sub, urefcount, upipe_videocont_sub_dead)
UPIPE_HELPER_VOID(upipe_videocont_sub)

UPIPE_HELPER_SUBPIPE(upipe_videocont, upipe_videocont_sub, sub, sub_mgr,
                     subs, uchain)

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
 * @param flow_def flow definition packet
 * @return an error code
 */
static enum ubase_err upipe_videocont_sub_set_flow_def(struct upipe *upipe,
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
    if (unlikely(upipe_videocont_sub->flow_def)) {
        uref_free(upipe_videocont_sub->flow_def);
    }
    upipe_videocont_sub->flow_def = flow_def_dup;

    /* check flow against (next) grid input name */
    const char *name = NULL;
    uref_flow_get_name(flow_def, &name);
    if (upipe_videocont->input_name
        && likely(ubase_check(uref_flow_get_name(flow_def, &name)))
        && !strcmp(upipe_videocont->input_name, name)) {
        upipe_videocont->input_cur = upipe;
        upipe_notice_va(upipe, "switched to input \"%s\" (%p)", name, upipe);
    }

    return UBASE_ERR_NONE;
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
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_videocont_sub_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_videocont_sub_get_super(upipe, p);
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
        upipe_videocont->input_cur = NULL;
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
    sub_mgr->signature = UPIPE_VIDEOCONT_INPUT_SIGNATURE;
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
    upipe_videocont->tolerance = TOLERANCE;

    upipe_throw_ready(upipe);

    return upipe;
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

    if (unlikely(!upipe_videocont->flow_def)) {
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
            uref_clock_get_pts_sys(uref_uchain, &pts);
            if (pts + upipe_videocont->tolerance < next_pts) {
                upipe_verbose_va(upipe, "(%d) deleted uref %p (%"PRIu64")",
                                 subs, uref_uchain, pts);
                ulist_delete(uchain);
                uref_free(uref_uchain);
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

    if (pts < next_pts + upipe_videocont->tolerance) {
        upipe_verbose_va(upipe, "attached ubuf %p (%"PRIu64")",
                         next_uref->ubuf, pts);
        uref_attach_ubuf(uref, uref_detach_ubuf(next_uref));
        next_uref = NULL;
        uref_free(uref_from_uchain(ulist_pop(&input->urefs)));
    }

output:
    upipe_videocont_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static enum ubase_err upipe_videocont_set_flow_def(struct upipe *upipe,
                                                   struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_videocont_store_flow_def(upipe, flow_def_dup);

    return UBASE_ERR_NONE;
}

/** @internal @This sets the input by name.
 *
 * @param upipe description structure of the pipe
 * @param name input name
 * @return an error code
 */
static enum ubase_err _upipe_videocont_set_input(struct upipe *upipe,
                                                 const char *name)
{
    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    char *name_dup = NULL;

    if (name) {
        name_dup = strdup(name);
        if (unlikely(!name_dup)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }

        struct uchain *uchain;
        ulist_foreach(&upipe_videocont->subs, uchain) {
            struct upipe_videocont_sub *sub =
                       upipe_videocont_sub_from_uchain(uchain);
            const char *flow_name = NULL;
            if (sub->flow_def
                && likely(ubase_check(uref_flow_get_name(sub->flow_def, &flow_name)))
                && !strcmp(name_dup, flow_name)) {
                upipe_videocont->input_cur = upipe_videocont_sub_to_upipe(sub);
                upipe_notice_va(upipe, "switched to input \"%s\" (%p)",
                                name_dup, upipe_videocont->input_cur);
                break;
            }
        }
    }

    free(upipe_videocont->input_name);
    upipe_videocont->input_name = name_dup;
    return UBASE_ERR_NONE;
}

/** @This returns the current input name if any.
 *
 * @param upipe description structure of the pipe
 * @param name_p filled with current input name pointer or NULL
 * @return an error code
 */
static inline enum ubase_err _upipe_videocont_get_current_input(
                       struct upipe *upipe, const char **name_p)
{
    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    if (unlikely(!name_p)) {
        return UBASE_ERR_INVALID;
    }

    *name_p = NULL;
    if (upipe_videocont->input_cur) {
        struct upipe_videocont_sub *sub = 
               upipe_videocont_sub_from_upipe(upipe_videocont->input_cur);
        if (sub->flow_def) {
            uref_flow_get_name(sub->flow_def, name_p);
        }
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
static int _upipe_videocont_control(struct upipe *upipe,
                                    int command, va_list args)
{
    struct upipe_videocont *upipe_videocont = upipe_videocont_from_upipe(upipe);
    switch (command) {
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
            assert(va_arg(args, int) == UPIPE_VIDEOCONT_SIGNATURE);
            const char *name = va_arg(args, const char*);
            return _upipe_videocont_set_input(upipe, name);
        }
        case UPIPE_VIDEOCONT_GET_INPUT: {
            assert(va_arg(args, int) == UPIPE_VIDEOCONT_SIGNATURE);
            *va_arg(args, const char**) = upipe_videocont->input_name;
            return UBASE_ERR_NONE;
        }
        case UPIPE_VIDEOCONT_SET_TOLERANCE: {
            assert(va_arg(args, int) == UPIPE_VIDEOCONT_SIGNATURE);
            upipe_videocont->tolerance = va_arg(args, uint64_t);
            return UBASE_ERR_NONE;
        }
        case UPIPE_VIDEOCONT_GET_TOLERANCE: {
            assert(va_arg(args, int) == UPIPE_VIDEOCONT_SIGNATURE);
            *va_arg(args, uint64_t *) = upipe_videocont->tolerance;
            return UBASE_ERR_NONE;
        }
        case UPIPE_VIDEOCONT_GET_CURRENT_INPUT: {
            assert(va_arg(args, int) == UPIPE_VIDEOCONT_SIGNATURE);
            const char **name_p = va_arg(args, const char **);
            return _upipe_videocont_get_current_input(upipe, name_p);
        }


        default:
            return UBASE_ERR_UNHANDLED;
    }
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
    UBASE_RETURN(_upipe_videocont_control(upipe, command, args))

    return UBASE_ERR_NONE;
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
