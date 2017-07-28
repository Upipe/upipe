/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe module - skip
 * Skip arbitrary length of data in blocks.
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_skip.h>

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

#define EXPECTED_FLOW_DEF "block."

/** upipe_skip structure */ 
struct upipe_skip {
    /** refcount management structure */
    struct urefcount urefcount;

    /** skip offset */
    size_t offset;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_skip, upipe, UPIPE_SKIP_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_skip, urefcount, upipe_skip_free)
UPIPE_HELPER_VOID(upipe_skip);
UPIPE_HELPER_OUTPUT(upipe_skip, output, flow_def, output_state, request_list);

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static inline void upipe_skip_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_skip *upipe_skip = upipe_skip_from_upipe(upipe);

    // skip given length
    uref_block_resize(uref, upipe_skip->offset, -1);

    upipe_skip_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_skip_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;
    upipe_skip_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a skip pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_skip_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_skip_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_skip_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SKIP_SET_OFFSET: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SKIP_SIGNATURE)
            struct upipe_skip *upipe_skip = upipe_skip_from_upipe(upipe);
            upipe_skip->offset = va_arg(args, size_t);
            return UBASE_ERR_NONE;
        }
        case UPIPE_SKIP_GET_OFFSET: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SKIP_SIGNATURE)
            struct upipe_skip *upipe_skip = upipe_skip_from_upipe(upipe);
            size_t *offset_p = va_arg(args, size_t *);
            if (unlikely(!offset_p)) {
                return UBASE_ERR_INVALID;
            }
            upipe_skip->offset = *offset_p;
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a skip pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_skip_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_skip_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_skip *upipe_skip = upipe_skip_from_upipe(upipe);
    upipe_skip_init_urefcount(upipe);
    upipe_skip_init_output(upipe);

    upipe_skip->offset = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_skip_free(struct upipe *upipe)
{
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);

    upipe_skip_clean_output(upipe);
    upipe_skip_clean_urefcount(upipe);
    upipe_skip_free_void(upipe);
}

static struct upipe_mgr upipe_skip_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SKIP_SIGNATURE,

    .upipe_alloc = upipe_skip_alloc,
    .upipe_input = upipe_skip_input,
    .upipe_control = upipe_skip_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for skip pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_skip_mgr_alloc(void)
{
    return &upipe_skip_mgr;
}
