/*
 * Copyright (C) 2012-2016 OpenHeadend S.A.R.L.
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
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
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
    /** refcount management structure */
    struct urefcount urefcount;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** rotate interval */
    uint64_t rotate;
    /** rotate offset */
    uint64_t rotate_offset;
    /** current index */
    uint64_t idx;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_multicat_probe, upipe, UPIPE_MULTICAT_PROBE_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_multicat_probe, urefcount,
                       upipe_multicat_probe_free)
UPIPE_HELPER_VOID(upipe_multicat_probe)
UPIPE_HELPER_OUTPUT(upipe_multicat_probe, output, flow_def, output_state, request_list);

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_multicat_probe_input(struct upipe *upipe, struct uref *uref,
                                       struct upump **upump_p)
{
    struct upipe_multicat_probe *upipe_multicat_probe = upipe_multicat_probe_from_upipe(upipe);
    uint64_t systime = 0;
    int64_t newidx;

    if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &systime)))) {
        upipe_warn(upipe, "uref has no systime");
    } else {
        newidx = (systime - upipe_multicat_probe->rotate_offset) /
                 upipe_multicat_probe->rotate;
        if (upipe_multicat_probe->idx != newidx) {
            upipe_throw(upipe, UPROBE_MULTICAT_PROBE_ROTATE,
                        UPIPE_MULTICAT_PROBE_SIGNATURE, uref, newidx);
            upipe_multicat_probe->idx = newidx;
        }
    }

    upipe_multicat_probe_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_multicat_probe_set_flow_def(struct upipe *upipe,
                                             struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    upipe_multicat_probe_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This changes the rotate interval.
 *
 * @param upipe description structure of the pipe
 * @param rotate new rotate interval
 * @param rotate_offset new rotate offset
 * @return an error code
 */
static int  _upipe_multicat_probe_set_rotate(struct upipe *upipe, uint64_t rotate, uint64_t rotate_offset)
{
    struct upipe_multicat_probe *upipe_multicat_probe = upipe_multicat_probe_from_upipe(upipe);
    if (unlikely(rotate < 1)) {
        upipe_warn_va(upipe, "invalid rotate interval (%"PRIu64" < 1)", rotate);
        return UBASE_ERR_INVALID;
    }
    upipe_multicat_probe->rotate = rotate;
    upipe_multicat_probe->rotate_offset = rotate_offset;
    upipe_notice_va(upipe, "setting rotate: %"PRIu64"+%"PRIu64, rotate, rotate_offset);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current rotate interval.
 *
 * @param upipe description structure of the pipe
 * @param rotate_p filled in with the current rotate interval
 * @param offset_p filled in with the current rotate offset
 * @return an error code
 */
static int _upipe_multicat_probe_get_rotate(struct upipe *upipe, uint64_t *rotate_p, uint64_t *offset_p)
{
    struct upipe_multicat_probe *upipe_multicat_probe = upipe_multicat_probe_from_upipe(upipe);
    assert(rotate_p != NULL);
    *rotate_p = upipe_multicat_probe->rotate;
    *offset_p = upipe_multicat_probe->rotate_offset;
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
static int upipe_multicat_probe_control(struct upipe *upipe,
                                        int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_multicat_probe_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_multicat_probe_set_flow_def(upipe, flow_def);
        }

        case UPIPE_MULTICAT_PROBE_SET_ROTATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_MULTICAT_PROBE_SIGNATURE)
            uint64_t rotate = va_arg(args, uint64_t);
            uint64_t rotate_offset = va_arg(args, uint64_t);
            return _upipe_multicat_probe_set_rotate(upipe, rotate, rotate_offset);
        }
        case UPIPE_MULTICAT_PROBE_GET_ROTATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_MULTICAT_PROBE_SIGNATURE)
            uint64_t *rotate_p = va_arg(args, uint64_t *);
            uint64_t *rotate_offset_p = va_arg(args, uint64_t *);
            return _upipe_multicat_probe_get_rotate(upipe, rotate_p, rotate_offset_p);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a multicat_probe pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_multicat_probe_alloc(struct upipe_mgr *mgr,
                                                struct uprobe *uprobe,
                                                uint32_t signature,
                                                va_list args)
{
    struct upipe *upipe = upipe_multicat_probe_alloc_void(mgr, uprobe,
                                                          signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_multicat_probe *upipe_multicat_probe =
        upipe_multicat_probe_from_upipe(upipe);
    upipe_multicat_probe_init_urefcount(upipe);
    upipe_multicat_probe_init_output(upipe);
    upipe_multicat_probe->rotate = UPIPE_MULTICAT_PROBE_DEF_ROTATE;
    upipe_multicat_probe->rotate_offset = UPIPE_MULTICAT_PROBE_DEF_ROTATE_OFFSET;
    upipe_multicat_probe->idx = 0;
    upipe_multicat_probe->flow_def = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_multicat_probe_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_multicat_probe_clean_output(upipe);
    upipe_multicat_probe_clean_urefcount(upipe);
    upipe_multicat_probe_free_void(upipe);
}

static struct upipe_mgr upipe_multicat_probe_mgr = {
    .refcount = NULL,
    .signature = UPIPE_MULTICAT_PROBE_SIGNATURE,

    .upipe_alloc = upipe_multicat_probe_alloc,
    .upipe_input = upipe_multicat_probe_input,
    .upipe_control = upipe_multicat_probe_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for multicat_probe pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_multicat_probe_mgr_alloc(void)
{
    return &upipe_multicat_probe_mgr;
}
