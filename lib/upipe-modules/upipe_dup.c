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
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-modules/upipe_dup.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a dup pipe. */
struct upipe_dup {
    /** list of outputs */
    struct ulist outputs;
    /** flow definition packet */
    struct uref *flow_def;

    /** manager to create outputs */
    struct upipe_mgr output_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dup, upipe)

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

UPIPE_HELPER_SUBPIPE(upipe_dup, upipe_dup_output, output, output_mgr, outputs,
                     uchain)

/** @internal @This allocates an output subpipe of a dup pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_dup_output_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe)
{
    struct upipe_dup_output *upipe_dup_output =
        malloc(sizeof(struct upipe_dup_output));
    if (unlikely(upipe_dup_output == NULL))
        return NULL;
    struct upipe *upipe = upipe_dup_output_to_upipe(upipe_dup_output);
    upipe_init(upipe, mgr, uprobe);
    upipe_dup_output_init_output(upipe);
    upipe_dup_output_init_sub(upipe);

    /* set flow definition if available */
    struct upipe_dup *upipe_dup = upipe_dup_from_output_mgr(mgr);
    if (upipe_dup->flow_def != NULL) {
        struct uref *uref = uref_dup(upipe_dup->flow_def);
        if (unlikely(uref == NULL))
            upipe_throw_aerror(upipe);
        else
            upipe_dup_output_store_flow_def(upipe, uref);
    }
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
    struct upipe_dup *upipe_dup = upipe_dup_from_output_mgr(upipe->mgr);
    upipe_throw_dead(upipe);

    upipe_dup_output_clean_output(upipe);
    upipe_dup_output_clean_sub(upipe);

    upipe_clean(upipe);
    free(upipe_dup_output);

    upipe_release(upipe_dup_to_upipe(upipe_dup));
}

/** @internal @This initializes the output manager for a dup pipe.
 *
 * @param upipe description structure of the pipe
 * @return pointer to output upipe manager
 */
static struct upipe_mgr *upipe_dup_init_output_mgr(struct upipe *upipe)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    struct upipe_mgr *output_mgr = &upipe_dup->output_mgr;
    output_mgr->signature = UPIPE_DUP_OUTPUT_SIGNATURE;
    output_mgr->upipe_alloc = upipe_dup_output_alloc;
    output_mgr->upipe_input = NULL;
    output_mgr->upipe_control = upipe_dup_output_control;
    output_mgr->upipe_free = upipe_dup_output_free;
    output_mgr->upipe_mgr_free = NULL;
    return output_mgr;
}

/** @internal @This allocates a dup pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_dup_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe)
{
    struct upipe_dup *upipe_dup = malloc(sizeof(struct upipe_dup));
    if (unlikely(upipe_dup == NULL))
        return NULL;
    struct upipe *upipe = upipe_dup_to_upipe(upipe_dup);
    upipe_split_init(upipe, mgr, uprobe, upipe_dup_init_output_mgr(upipe));
    upipe_dup_init_sub_outputs(upipe);
    upipe_dup->flow_def = NULL;
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
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (upipe_dup->flow_def != NULL)
            uref_free(upipe_dup->flow_def);
        upipe_dup->flow_def = uref;
        upipe_dbg_va(upipe, "flow definition %s", def);

        /* also set it for every output */
        struct uchain *uchain;
        ulist_foreach (&upipe_dup->outputs, uchain) {
            struct upipe_dup_output *upipe_dup_output =
                upipe_dup_output_from_uchain(uchain);
            uref = uref_dup(upipe_dup->flow_def);
            if (unlikely(uref == NULL)) {
                upipe_throw_aerror(upipe);
                return;
            }
            upipe_dup_output_store_flow_def(
                    upipe_dup_output_to_upipe(upipe_dup_output), uref);
        }
        return;
    }

    if (unlikely(uref_flow_get_end(uref))) {
        uref_free(uref);
        upipe_throw_need_input(upipe);
        upipe_dup_throw_sub_outputs(upipe, UPROBE_NEED_INPUT);
        return;
    }

    if (unlikely(upipe_dup->flow_def == NULL)) {
        uref_free(uref);
        upipe_throw_flow_def_error(upipe, uref);
        return;
    }

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

/** module manager static descriptor */
static struct upipe_mgr upipe_dup_mgr = {
    .signature = UPIPE_DUP_SIGNATURE,

    .upipe_alloc = upipe_dup_alloc,
    .upipe_input = upipe_dup_input,
    .upipe_control = NULL,
    .upipe_free = upipe_dup_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all dup pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dup_mgr_alloc(void)
{
    return &upipe_dup_mgr;
}
