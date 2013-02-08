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
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_output.h>
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

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dup, upipe)

/** @internal @This returns the public output_mgr structure.
 *
 * @param upipe_dup pointer to the private upipe_dup structure
 * @return pointer to the public output_mgr structure
 */
static inline struct upipe_mgr *upipe_dup_to_output_mgr(struct upipe_dup *s)
{
    return &s->output_mgr;
}

/** @internal @This returns the private upipe_dup structure.
 *
 * @param output_mgr public output_mgr structure of the pipe
 * @return pointer to the private upipe_dup structure
 */
static inline struct upipe_dup *
    upipe_dup_from_output_mgr(struct upipe_mgr *output_mgr)
{
    return container_of(output_mgr, struct upipe_dup, output_mgr);
}

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

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dup_output, upipe)
UPIPE_HELPER_OUTPUT(upipe_dup_output, output, flow_def, flow_def_sent)

/** @This returns the high-level upipe_dup_output structure.
 *
 * @param uchain pointer to the uchain structure wrapped into the
 * upipe_dup_output
 * @return pointer to the upipe_dup_output structure
 */
static inline struct upipe_dup_output *
    upipe_dup_output_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct upipe_dup_output, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param upipe_dup_output upipe_dup_output structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *
    upipe_dup_output_to_uchain(struct upipe_dup_output *upipe_dup_output)
{
    return &upipe_dup_output->uchain;
}

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
    uchain_init(&upipe_dup_output->uchain);
    upipe_dup_output_init_output(upipe);
    urefcount_init(&upipe_dup_output->refcount);

    /* add the newly created output to the outputs list */
    struct upipe_dup *upipe_dup = upipe_dup_from_output_mgr(mgr);
    ulist_add(&upipe_dup->outputs,
              upipe_dup_output_to_uchain(upipe_dup_output));

    /* set flow definition if available */
    if (upipe_dup->flow_def != NULL) {
        struct uref *uref = uref_dup(upipe_dup->flow_def);
        if (unlikely(uref == NULL))
            upipe_throw_aerror(upipe);
        else
            upipe_dup_output_store_flow_def(upipe, uref);
    }
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

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_output_use(struct upipe *upipe)
{
    struct upipe_dup_output *upipe_dup_output =
        upipe_dup_output_from_upipe(upipe);
    urefcount_use(&upipe_dup_output->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_output_release(struct upipe *upipe)
{
    struct upipe_dup_output *upipe_dup_output =
        upipe_dup_output_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_dup_output->refcount))) {
        struct upipe_dup *upipe_dup = upipe_dup_from_output_mgr(upipe->mgr);
        upipe_throw_dead(upipe);

        /* remove output from the outputs list */
        struct uchain *uchain;
        ulist_delete_foreach(&upipe_dup->outputs, uchain) {
            if (upipe_dup_output_from_uchain(uchain) == upipe_dup_output) {
                ulist_delete(&upipe_dup->outputs, uchain);
                break;
            }
        }
        upipe_dup_output_clean_output(upipe);

        upipe_clean(upipe);
        urefcount_clean(&upipe_dup_output->refcount);
        free(upipe_dup_output);
    }
}

/** @This increments the reference count of a upipe manager.
 *
 * @param mgr pointer to upipe manager
 */
static void upipe_dup_output_mgr_use(struct upipe_mgr *mgr)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_output_mgr(mgr);
    upipe_use(upipe_dup_to_upipe(upipe_dup));
}

/** @This decrements the reference count of a upipe manager or frees it.
 *
 * @param mgr pointer to upipe manager.
 */
static void upipe_dup_output_mgr_release(struct upipe_mgr *mgr)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_output_mgr(mgr);
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
    output_mgr->upipe_use = upipe_dup_output_use;
    output_mgr->upipe_release = upipe_dup_output_release;
    output_mgr->upipe_mgr_use = upipe_dup_output_mgr_use;
    output_mgr->upipe_mgr_release = upipe_dup_output_mgr_release;
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
    ulist_init(&upipe_dup->outputs);
    upipe_dup->flow_def = NULL;
    urefcount_init(&upipe_dup->refcount);
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
        ulist_foreach(&upipe_dup->outputs, uchain) {
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

    if (unlikely(upipe_dup->flow_def == NULL)) {
        upipe_throw_flow_def_error(upipe, uref);
        uref_free(uref);
        return;
    }

    struct uchain *uchain;
    ulist_foreach (&upipe_dup->outputs, uchain) {
        struct upipe_dup_output *upipe_dup_output =
            upipe_dup_output_from_uchain(uchain);
        struct uref *new_uref = uref_dup(uref);
        if (unlikely(new_uref == NULL)) {
            uref_free(uref);
            upipe_throw_aerror(upipe);
            return;
        }
        upipe_dup_output_output(upipe_dup_output_to_upipe(upipe_dup_output),
                                new_uref, upump);
    }
    uref_free(uref);
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_use(struct upipe *upipe)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    urefcount_use(&upipe_dup->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_release(struct upipe *upipe)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_dup->refcount))) {
        upipe_throw_dead(upipe);
        /* we can only arrive here if there is no output anymore, so no
         * need to empty the outputs list */
        if (upipe_dup->flow_def != NULL)
            uref_free(upipe_dup->flow_def);
        upipe_clean(upipe);
        urefcount_clean(&upipe_dup->refcount);
        free(upipe_dup);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_dup_mgr = {
    .signature = UPIPE_DUP_SIGNATURE,

    .upipe_alloc = upipe_dup_alloc,
    .upipe_input = upipe_dup_input,
    .upipe_control = NULL,
    .upipe_use = upipe_dup_use,
    .upipe_release = upipe_dup_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all dup pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dup_mgr_alloc(void)
{
    return &upipe_dup_mgr;
}
