/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
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
 * @short Upipe module allowing to duplicate to several outputs
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-modules/upipe_dup.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

/** @internal @This is the private context of a dup pipe. */
struct upipe_dup {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** list of output subpipes */
    struct uchain outputs;
    /** flow definition packet */
    struct uref *flow_def;

    /** manager to create output subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dup, upipe, UPIPE_DUP_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_dup, urefcount, upipe_dup_no_input)
UPIPE_HELPER_UREFCOUNT_REAL(upipe_dup, urefcount_real, upipe_dup_free)
UPIPE_HELPER_VOID(upipe_dup)

/** @internal @This is the private context of an output of a dup pipe. */
struct upipe_dup_output {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dup_output, upipe, UPIPE_DUP_OUTPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_dup_output, urefcount, upipe_dup_output_free)
UPIPE_HELPER_VOID(upipe_dup_output);
UPIPE_HELPER_OUTPUT(upipe_dup_output, output, flow_def, output_state, request_list)

UPIPE_HELPER_SUBPIPE(upipe_dup, upipe_dup_output, output, sub_mgr, outputs,
                     uchain)

/** @internal @This allocates an output subpipe of a dup pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_dup_output_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    if (mgr->signature != UPIPE_DUP_OUTPUT_SIGNATURE)
        return NULL;

    struct upipe *upipe =
        upipe_dup_output_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(!upipe))
        return NULL;

    upipe_dup_output_init_urefcount(upipe);
    upipe_dup_output_init_output(upipe);
    upipe_dup_output_init_sub(upipe);

    upipe_throw_ready(upipe);

    struct upipe_dup *upipe_dup = upipe_dup_from_sub_mgr(mgr);
    struct uref *flow_def_dup = NULL;
    if (upipe_dup->flow_def != NULL &&
        (flow_def_dup = uref_dup(upipe_dup->flow_def)) == NULL) {
        upipe_release(upipe);
        return NULL;
    }

    upipe_dup_output_store_flow_def(upipe, flow_def_dup);

    return upipe;
}

/** @internal @This processes control commands on an output subpipe of a dup
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_dup_output_control(struct upipe *upipe,
                                    int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_dup_output_control_super(upipe, command, args));
    switch (command) {
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_dup_output_control_output(upipe, command, args);

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_output_free(struct upipe *upipe)
{
    struct upipe_dup_output *upipe_dup_output =
        upipe_dup_output_from_upipe(upipe);

    upipe_throw_dead(upipe);

    upipe_dup_output_clean_output(upipe);
    upipe_dup_output_clean_sub(upipe);
    upipe_dup_output_clean_urefcount(upipe);
    upipe_dup_output_free_void(upipe);
}

/** @internal @This initializes the output manager for a dup set pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_dup->sub_mgr;
    sub_mgr->refcount = upipe_dup_to_urefcount_real(upipe_dup);
    sub_mgr->signature = UPIPE_DUP_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_dup_output_alloc;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_dup_output_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a dup pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_dup_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_dup_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    upipe_dup_init_urefcount(upipe);
    upipe_dup_init_urefcount_real(upipe);
    upipe_dup_init_sub_mgr(upipe);
    upipe_dup_init_sub_outputs(upipe);
    upipe_dup->flow_def = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_dup_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    struct uchain *uchain;
    ulist_foreach (&upipe_dup->outputs, uchain) {
        struct upipe_dup_output *upipe_dup_output =
            upipe_dup_output_from_uchain(uchain);
        struct upipe *output = upipe_dup_output_to_upipe(upipe_dup_output);
        if (ulist_is_last(&upipe_dup->outputs, uchain)) {
            upipe_dup_output_output(output, uref, upump_p);
            uref = NULL;
        } else {
            struct uref *new_uref = uref_dup(uref);
            if (unlikely(new_uref == NULL)) {
                uref_free(uref);
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return;
            }
            upipe_dup_output_output(output, new_uref, upump_p);
        }
    }
    if (uref != NULL)
        uref_free(uref);
}

/** @internal @This changes the flow definition on all outputs.
 *
 * @param upipe description structure of the pipe
 * @param flow_def new flow definition
 * @return an error code
 */
static int upipe_dup_set_flow_def(struct upipe *upipe,
                                             struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;

    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    if (upipe_dup->flow_def != NULL)
        uref_free(upipe_dup->flow_def);
    upipe_dup->flow_def = flow_def_dup;

    struct uchain *uchain;
    ulist_foreach (&upipe_dup->outputs, uchain) {
        struct upipe_dup_output *upipe_dup_output =
            upipe_dup_output_from_uchain(uchain);
        flow_def_dup = uref_dup(flow_def);
        if (unlikely(flow_def_dup == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_dup_output_store_flow_def(
                upipe_dup_output_to_upipe(upipe_dup_output), flow_def_dup);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a dup pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_dup_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_dup_control_outputs(upipe, command, args));
    /* We do not pass through the requests; which output would we use? */
    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            return upipe_dup_set_flow_def(upipe, uref);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_dup_free(struct upipe *upipe)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);

    upipe_throw_dead(upipe);

    upipe_dup_clean_sub_outputs(upipe);
    if (upipe_dup->flow_def != NULL)
        uref_free(upipe_dup->flow_def);
    upipe_dup_clean_urefcount_real(upipe);
    upipe_dup_clean_urefcount(upipe);
    upipe_dup_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_no_input(struct upipe *upipe)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    upipe_dbg(upipe, "throw source end");
    upipe_dup_throw_sub_outputs(upipe, UPROBE_SOURCE_END);
    urefcount_release(upipe_dup_to_urefcount_real(upipe_dup));
}

/** dup module manager static descriptor */
static struct upipe_mgr upipe_dup_mgr = {
    .refcount = NULL,
    .signature = UPIPE_DUP_SIGNATURE,

    .upipe_alloc = upipe_dup_alloc,
    .upipe_input = upipe_dup_input,
    .upipe_control = upipe_dup_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all dup pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dup_mgr_alloc(void)
{
    return &upipe_dup_mgr;
}
