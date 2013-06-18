/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-modules/upipe_dup.h>
#include <upipe-modules/upipe_proxy.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a dup pipe. */
struct upipe_dup {
    /** list of output subpipes */
    struct ulist outputs;
    /** flow definition packet */
    struct uref *flow_def;

    /** manager to create output subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dup, upipe)
UPIPE_HELPER_FLOW(upipe_dup, NULL)

/** @internal @This is the private context of an output of a dup pipe. */
struct upipe_dup_output {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dup_output, upipe)
UPIPE_HELPER_OUTPUT(upipe_dup_output, output, flow_def, flow_def_sent)

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
    if (signature != UPIPE_VOID_SIGNATURE ||
        mgr->signature != UPIPE_DUP_OUTPUT_SIGNATURE)
        return NULL;
    struct upipe_dup *upipe_dup = upipe_dup_from_sub_mgr(mgr);
    struct uref *flow_def_dup = NULL;
    if (upipe_dup->flow_def != NULL &&
        (flow_def_dup = uref_dup(upipe_dup->flow_def)) == NULL)
        return NULL;

    struct upipe_dup_output *upipe_dup_output =
        malloc(sizeof(struct upipe_dup_output));
    if (unlikely(upipe_dup_output == NULL)) {
        if (flow_def_dup != NULL)
            uref_free(flow_def_dup);
        return NULL;
    }
    struct upipe *upipe = upipe_dup_output_to_upipe(upipe_dup_output);
    upipe_init(upipe, mgr, uprobe);
    upipe_dup_output_init_output(upipe);
    upipe_dup_output_init_sub(upipe);

    upipe_dup_output_store_flow_def(upipe, flow_def_dup);
    upipe_use(upipe_dup_to_upipe(upipe_dup));
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This processes control commands on an output subpipe of a dup
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_dup_output_control(struct upipe *upipe,
                                     enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_dup_output_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_dup_output_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_dup_output_set_output(upipe, output);
        }

        default:
            return false;
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
    struct upipe_dup *upipe_dup =
        upipe_dup_from_sub_mgr(upipe->mgr);
    upipe_throw_dead(upipe);

    upipe_dup_output_clean_output(upipe);
    upipe_dup_output_clean_sub(upipe);

    upipe_clean(upipe);
    free(upipe_dup_output);

    upipe_release(upipe_dup_to_upipe(upipe_dup));
}

/** @internal @This initializes the output manager for a dup set pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_dup->sub_mgr;
    sub_mgr->signature = UPIPE_DUP_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_dup_output_alloc;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_dup_output_control;
    sub_mgr->upipe_free = upipe_dup_output_free;
    sub_mgr->upipe_mgr_free = NULL;
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
    struct uref *flow_def;
    struct upipe *upipe = upipe_dup_alloc_flow(mgr, uprobe, signature, args,
                                               &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    upipe_dup_init_sub_mgr(upipe);
    upipe_dup_init_sub_outputs(upipe);
    upipe_dup->flow_def = flow_def;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_dup_input(struct upipe *upipe, struct uref *uref,
                            struct upump *upump)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    struct uchain *uchain;
    ulist_foreach (&upipe_dup->outputs, uchain) {
        struct upipe_dup_output *upipe_dup_output =
            upipe_dup_output_from_uchain(uchain);
        if (uchain->next == NULL) {
            upipe_dup_output_output(upipe_dup_output_to_upipe(upipe_dup_output),
                                    uref, upump);
            uref = NULL;
        } else {
            struct uref *new_uref = uref_dup(uref);
            if (unlikely(new_uref == NULL)) {
                uref_free(uref);
                upipe_throw_aerror(upipe);
                return;
            }
            upipe_dup_output_output(upipe_dup_output_to_upipe(upipe_dup_output),
                                    new_uref, upump);
        }
    }
}

/** @internal @This changes the flow definition on all outputs.
 *
 * @param upipe description structure of the pipe
 * @param flow_def new flow definition
 * @return false in case of error
 */
static bool upipe_dup_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return false;
    struct uref *flow_def_dup = NULL;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return false;

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
            upipe_throw_aerror(upipe);
            return false;
        }
        upipe_dup_output_store_flow_def(
                upipe_dup_output_to_upipe(upipe_dup_output), flow_def_dup);
    }
    return true;
}

/** @internal @This processes control commands on a dup pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_dup_control(struct upipe *upipe,
                              enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            return upipe_dup_set_flow_def(upipe, uref);
        }

        default:
            return false;
    }
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_free(struct upipe *upipe)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_dup_clean_sub_outputs(upipe);
    if (upipe_dup->flow_def != NULL)
        uref_free(upipe_dup->flow_def);
    upipe_clean(upipe);
    free(upipe_dup);
}

/** dup module manager static descriptor */
static struct upipe_mgr upipe_dup_mgr = {
    .signature = UPIPE_DUP_SIGNATURE,

    .upipe_alloc = upipe_dup_alloc,
    .upipe_input = upipe_dup_input,
    .upipe_control = upipe_dup_control,
    .upipe_free = upipe_dup_free,

    .upipe_mgr_free = NULL
};

/** @This is called when the proxy is released.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_proxy_released(struct upipe *upipe)
{
    upipe_dup_throw_sub_outputs(upipe, UPROBE_SOURCE_END);
}

/** @This returns the management structure for all dup pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dup_mgr_alloc(void)
{
    return upipe_proxy_mgr_alloc(&upipe_dup_mgr, upipe_dup_proxy_released);
}
