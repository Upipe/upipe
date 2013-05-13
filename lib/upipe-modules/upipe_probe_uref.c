/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_probe_uref, upipe);
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
    struct upipe_probe_uref *upipe_probe_uref = upipe_probe_uref_from_upipe(upipe);
    const char *def;

    if (unlikely(uref_flow_get_def(uref, &def))) {
        upipe_dbg_va(upipe, "flow definition %s", def);
        upipe_probe_uref_store_flow_def(upipe, uref);
        return;
    }

    if (unlikely(uref_flow_get_end(uref))) {
        uref_free(uref);
        upipe_throw_need_input(upipe);
        return;
    }

    if (unlikely(upipe_probe_uref->flow_def == NULL)) {
        upipe_throw_flow_def_error(upipe, uref);
        uref_free(uref);
        return;
    }

    upipe_throw(upipe, UPROBE_PROBE_UREF, UPIPE_PROBE_UREF_SIGNATURE, uref);
    upipe_probe_uref_output(upipe, uref, upump);
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_probe_uref_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    switch (command) {
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_probe_uref_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_probe_uref_set_output(upipe, output);
        }

        default:
            return false;
    }
}

/** @internal @This allocates a probe pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_probe_uref_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe)
{
    struct upipe_probe_uref *upipe_probe_uref = malloc(sizeof(struct upipe_probe_uref));
    if (unlikely(upipe_probe_uref == NULL))
        return NULL;
    struct upipe *upipe = upipe_probe_uref_to_upipe(upipe_probe_uref);
    upipe_init(upipe, mgr, uprobe);
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
    struct upipe_probe_uref *upipe_probe_uref = upipe_probe_uref_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_probe_uref_clean_output(upipe);
    upipe_clean(upipe);
    free(upipe_probe_uref);
}

static struct upipe_mgr upipe_probe_uref_mgr = {
    .signature = UPIPE_PROBE_UREF_SIGNATURE,

    .upipe_alloc = upipe_probe_uref_alloc,
    .upipe_input = upipe_probe_uref_input,
    .upipe_control = upipe_probe_uref_control,
    .upipe_free = upipe_probe_uref_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for probe pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_probe_uref_mgr_alloc(void)
{
    return &upipe_probe_uref_mgr;
}
