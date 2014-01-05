/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe module - probe uref
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_probe_uref.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <assert.h>
#include <sys/param.h>

/** upipe_probe_uref structure */ 
struct upipe_probe_uref {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_probe_uref, upipe, UPIPE_PROBE_UREF_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_probe_uref, urefcount, upipe_probe_uref_free)
UPIPE_HELPER_VOID(upipe_probe_uref)
UPIPE_HELPER_OUTPUT(upipe_probe_uref, output, flow_def, flow_def_sent);

/** @internal @This handles urefs (data & flows).
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_probe_uref_input(struct upipe *upipe, struct uref *uref,
                                   struct upump *upump)
{
    upipe_throw(upipe, UPROBE_PROBE_UREF, UPIPE_PROBE_UREF_SIGNATURE, uref);
    upipe_probe_uref_output(upipe, uref, upump);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static enum ubase_err upipe_probe_uref_set_flow_def(struct upipe *upipe,
                                                    struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    upipe_probe_uref_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static enum ubase_err upipe_probe_uref_control(struct upipe *upipe,
                                               enum upipe_command command,
                                               va_list args)
{
    switch (command) {
        case UPIPE_AMEND_FLOW_FORMAT: {
            struct uref *flow_format = va_arg(args, struct uref *);
            return upipe_throw_new_flow_format(upipe, flow_format, NULL);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_probe_uref_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_probe_uref_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_probe_uref_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_probe_uref_set_output(upipe, output);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a probe pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_probe_uref_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_probe_uref_alloc_void(mgr, uprobe, signature,
                                                      args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_probe_uref_init_urefcount(upipe);
    upipe_probe_uref_init_output(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_probe_uref_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_probe_uref_clean_output(upipe);
    upipe_probe_uref_clean_urefcount(upipe);
    upipe_probe_uref_free_void(upipe);
}

static struct upipe_mgr upipe_probe_uref_mgr = {
    .refcount = NULL,
    .signature = UPIPE_PROBE_UREF_SIGNATURE,

    .upipe_alloc = upipe_probe_uref_alloc,
    .upipe_input = upipe_probe_uref_input,
    .upipe_control = upipe_probe_uref_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for probe pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_probe_uref_mgr_alloc(void)
{
    return &upipe_probe_uref_mgr;
}
