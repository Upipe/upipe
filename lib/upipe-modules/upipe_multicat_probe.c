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
 * @short Upipe module - multicat probe
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref_clock.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_multicat_probe.h>

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

/** upipe_multicat_probe structure */ 
struct upipe_multicat_probe {
    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** rotate interval */
    uint64_t rotate;
    /** current index */
    uint64_t idx;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_multicat_probe, upipe);
UPIPE_HELPER_OUTPUT(upipe_multicat_probe, output, flow_def, flow_def_sent);

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void _upipe_multicat_probe_input(struct upipe *upipe, struct uref *uref,
                                       struct upump *upump)
{
    struct upipe_multicat_probe *upipe_multicat_probe = upipe_multicat_probe_from_upipe(upipe);
    uint64_t systime = 0;
    int64_t newidx;

    if (unlikely(!uref_clock_get_systime(uref, &systime))) {
        upipe_warn(upipe, "uref has no systime");
    }
    newidx = (systime/upipe_multicat_probe->rotate);
    if (upipe_multicat_probe->idx != newidx) {
        upipe_throw(upipe, UPROBE_MULTICAT_PROBE_ROTATE,
                    UPIPE_MULTICAT_PROBE_SIGNATURE, uref, newidx);
        upipe_multicat_probe->idx = newidx;
    }

    upipe_multicat_probe_output(upipe, uref, upump);
}

/** @internal @This handles urefs (data & flows).
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_multicat_probe_input(struct upipe *upipe, struct uref *uref,
                                      struct upump *upump)
{
    struct upipe_multicat_probe *upipe_multicat_probe = upipe_multicat_probe_from_upipe(upipe);
    const char *def;

    if (unlikely(uref_flow_get_def(uref, &def))) {
        upipe_dbg_va(upipe, "flow definition %s", def);
        upipe_multicat_probe_store_flow_def(upipe, uref);
        return;
    }

    if (unlikely(uref_flow_get_end(uref))) {
        uref_free(uref);
        upipe_throw_need_input(upipe);
        return;
    }

    if (unlikely(upipe_multicat_probe->flow_def == NULL)) {
        upipe_throw_flow_def_error(upipe, uref);
        uref_free(uref);
        return;
    }

    return _upipe_multicat_probe_input(upipe, uref, upump);
}

/** @internal @This changes the rotate interval
 *
 * @param upipe description structure of the pipe
 * @param rotate new rotate interval
 * @return false in case of error
 */
static bool  _upipe_multicat_probe_set_rotate(struct upipe *upipe, uint64_t rotate)
{
    struct upipe_multicat_probe *upipe_multicat_probe = upipe_multicat_probe_from_upipe(upipe);
    if (unlikely(rotate < 1)) {
        upipe_warn_va(upipe, "invalid rotate interval (%"PRIu64" < 1)", rotate);
        return false;
    }
    upipe_multicat_probe->rotate = rotate;
    upipe_notice_va(upipe, "setting rotate: %"PRIu64, rotate);
    return true;
}

/** @internal @This returns the current rotate interval
 *
 * @param upipe description structure of the pipe
 * @param rotate_p filled in with the current rotate interval
 * @return false in case of error
 */
static bool _upipe_multicat_probe_get_rotate(struct upipe *upipe, uint64_t *rotate_p)
{
    struct upipe_multicat_probe *upipe_multicat_probe = upipe_multicat_probe_from_upipe(upipe);
    assert(rotate_p != NULL);
    *rotate_p = upipe_multicat_probe->rotate;
    return true;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_multicat_probe_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    switch (command) {
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_multicat_probe_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_multicat_probe_set_output(upipe, output);
        }

        case UPIPE_MULTICAT_PROBE_SET_ROTATE: {
			unsigned int signature = va_arg(args, unsigned int); 
			assert(signature == UPIPE_MULTICAT_PROBE_SIGNATURE);
            return _upipe_multicat_probe_set_rotate(upipe, va_arg(args, uint64_t));
        }
        case UPIPE_MULTICAT_PROBE_GET_ROTATE: {
			unsigned int signature = va_arg(args, unsigned int); 
			assert(signature == UPIPE_MULTICAT_PROBE_SIGNATURE);
            return _upipe_multicat_probe_get_rotate(upipe, va_arg(args, uint64_t*));
        }
        default:
            return false;
    }
}

/** @internal @This allocates a multicat_probe pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_multicat_probe_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe)
{
    struct upipe_multicat_probe *upipe_multicat_probe = malloc(sizeof(struct upipe_multicat_probe));
    if (unlikely(upipe_multicat_probe == NULL))
        return NULL;
    struct upipe *upipe = upipe_multicat_probe_to_upipe(upipe_multicat_probe);
    upipe_init(upipe, mgr, uprobe);
    upipe_multicat_probe_init_output(upipe);
    upipe_multicat_probe->rotate = UPIPE_MULTICAT_PROBE_DEF_ROTATE;
    upipe_multicat_probe->idx = 0;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_multicat_probe_free(struct upipe *upipe)
{
    struct upipe_multicat_probe *upipe_multicat_probe = upipe_multicat_probe_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_multicat_probe_clean_output(upipe);
    upipe_clean(upipe);
    free(upipe_multicat_probe);
}

static struct upipe_mgr upipe_multicat_probe_mgr = {
    .signature = UPIPE_MULTICAT_PROBE_SIGNATURE,

    .upipe_alloc = upipe_multicat_probe_alloc,
    .upipe_input = upipe_multicat_probe_input,
    .upipe_control = upipe_multicat_probe_control,
    .upipe_free = upipe_multicat_probe_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for multicat_probe pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_multicat_probe_mgr_alloc(void)
{
    return &upipe_multicat_probe_mgr;
}
