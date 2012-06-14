/*****************************************************************************
 * upipe_dup.c: upipe module allowing to duplicate buffers
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_split.h>
#include <upipe/upipe_flows.h>
#include <upipe-modules/upipe_dup.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** super-set of the upipe_split structure with additional local members */
struct upipe_dup {
    /** list of input flows */
    struct ulist flows;

    /** members common to split pipes */
    struct upipe_split upipe_split;
};

/** @internal @This returns the high-level upipe structure.
 *
 * @param upipe_dup pointer to the upipe_dup structure
 * @return pointer to the upipe structure
 */
static inline struct upipe *upipe_dup_to_upipe(struct upipe_dup *upipe_dup)
{
    return upipe_split_to_upipe(&upipe_dup->upipe_split);
}

/** @internal @This returns the private upipe_dup structure.
 *
 * @param upipe description structure of the pipe
 * @return pointer to the upipe_dup structure
 */
static inline struct upipe_dup *upipe_dup_from_upipe(struct upipe *upipe)
{
    struct upipe_split *upipe_split = upipe_split_from_upipe(upipe);
    return container_of(upipe_split, struct upipe_dup, upipe_split);
}

/** @This checks if the dup pipe is ready to process data.
 * Nota bene: we do not use upipe_split_ready() here because we are
 * more lenient in this module (we do not need a default output).
 *
 * @param upipe description structure of the pipe
 */
static bool upipe_dup_ready(struct upipe *upipe)
{
    return upipe_split_uref_mgr(upipe) != NULL;
}

/** @internal @This allocates a dup pipe.
 *
 * @param mgr common management structure
 * @return pointer to struct upipe or NULL in case of allocation error
 */
static struct upipe *upipe_dup_alloc(struct upipe_mgr *mgr)
{
    struct upipe_dup *upipe_dup = malloc(sizeof(struct upipe_dup));
    if (unlikely(upipe_dup == NULL)) return NULL;
    struct upipe *upipe = upipe_dup_to_upipe(upipe_dup);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_DUP_SIGNATURE;
    upipe_flows_init(&upipe_dup->flows);
    upipe_split_init(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_dup_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    struct ulist *outputs = upipe_split_outputs(upipe);
    struct uref_mgr *uref_mgr = upipe_split_uref_mgr(upipe);

    if (unlikely(!upipe_dup_ready(upipe))) {
        ulog_warning(upipe->ulog,
                     "received a buffer while the pipe is not ready");
        uref_release(uref);
        return false;
    }

    if (unlikely(!upipe_flows_input(&upipe_dup->flows, upipe->ulog, uref_mgr,
                                    uref))) {
        uref_release(uref);
        return false;
    }

    struct upipe_split_output *output;
    upipe_split_outputs_foreach (outputs, output) {
        struct uref *new_uref = uref_dup(uref_mgr, uref);
        if (likely(new_uref != NULL))
            upipe_split_output(upipe, new_uref, output->flow_suffix);
        else
            ulog_aerror(upipe->ulog);
    }
    uref_release(uref);
    return true;
}

/** @internal @This adds/deletes/changes an output.
 *
 * @param upipe description structure of the pipe
 * @param args pointer to output pipe and flow suffix
 * @return false in case of error
 */
static bool _upipe_dup_set_output(struct upipe *upipe, va_list args)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    struct ulist *outputs = upipe_split_outputs(upipe);
    struct uref_mgr *uref_mgr = upipe_split_uref_mgr(upipe);
    va_list args_copy;
    va_copy(args_copy, args);
    struct upipe *s = va_arg(args_copy, struct upipe *);
    const char *flow_suffix = va_arg(args_copy, const char *);
    va_end(args_copy);

    /* check if we need to add an output - this is a behavior specific to
     * dup pipes */
    struct upipe_split_output *output = upipe_split_outputs_get(outputs,
                                                                flow_suffix);
    if (likely(output == NULL)) {
        ulog_debug(upipe->ulog, "adding output: %s", flow_suffix);
        if (unlikely(upipe_split_outputs_add(outputs, flow_suffix) == NULL))
            return false;
    } else if (likely(uref_mgr != NULL)) {
        /* change of output, signal flow deletions on old output */
        upipe_flows_foreach_delete(&upipe_dup->flows, upipe->ulog, uref_mgr,
                    uref, upipe_split_output(upipe, uref, flow_suffix));
    }

    /* check if we need to delete an output - this is a behavior specific
     * to dup pipes */
    if (unlikely(s == NULL && flow_suffix != NULL)) {
        ulog_debug(upipe->ulog, "deleting output: %s", flow_suffix);
        return upipe_split_outputs_delete(outputs, flow_suffix);
    }

    bool ret = upipe_split_control(upipe, UPIPE_SPLIT_SET_OUTPUT, args);
    if (unlikely(!ret)) return ret;

    if (likely(uref_mgr != NULL)) {
        /* replay flow definitions */
        upipe_flows_foreach_replay(&upipe_dup->flows, upipe->ulog, uref_mgr,
                    uref, upipe_split_output(upipe, uref, flow_suffix));
    }
    return ret;
}

/** @internal @This processes control commands on a dup pipe.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_dup_control(struct upipe *upipe, enum upipe_control control,
                              va_list args)
{
    if (likely(control == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        return upipe_dup_input(upipe, uref);
    }
    switch (control) {
        case UPIPE_SPLIT_SET_OUTPUT:
            return _upipe_dup_set_output(upipe, args);
        default:
            return upipe_split_control(upipe, control, args);
    }
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_free(struct upipe *upipe)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    struct uref_mgr *uref_mgr = upipe_split_uref_mgr(upipe);
    if (likely(uref_mgr != NULL)) {
        struct ulist *outputs = upipe_split_outputs(upipe);
        struct upipe_split_output *output;
        upipe_split_outputs_foreach (outputs, output) {
            upipe_flows_foreach_delete(&upipe_dup->flows, upipe->ulog,
                                       uref_mgr, uref,
                                       upipe_split_output(upipe, uref,
                                                          output->flow_suffix));
        }
    }
    upipe_flows_clean(&upipe_dup->flows);
    upipe_split_clean(upipe);
    free(upipe_dup);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_dup_mgr = {
    /* no need to initialize refcount as we don't use it */

    .upipe_alloc = upipe_dup_alloc,
    .upipe_control = upipe_dup_control,
    .upipe_free = upipe_dup_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all dups
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dup_mgr_alloc(void)
{
    return &upipe_dup_mgr;
}
