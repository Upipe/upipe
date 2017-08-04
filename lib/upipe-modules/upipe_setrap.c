/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe module setting rap_sys to urefs
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_setrap.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

/** @internal @This is the private context of a setrap pipe. */
struct upipe_setrap {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** rap to set */
    uint64_t rap_sys;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_setrap, upipe, UPIPE_SETRAP_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_setrap, urefcount, upipe_setrap_free)
UPIPE_HELPER_VOID(upipe_setrap)
UPIPE_HELPER_OUTPUT(upipe_setrap, output, flow_def, output_state, request_list)

/** @internal @This allocates a setrap pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_setrap_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_setrap_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_setrap *upipe_setrap = upipe_setrap_from_upipe(upipe);
    upipe_setrap_init_urefcount(upipe);
    upipe_setrap_init_output(upipe);
    upipe_setrap->rap_sys = UINT64_MAX;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_setrap_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_setrap *upipe_setrap = upipe_setrap_from_upipe(upipe);

    if (likely(upipe_setrap->rap_sys != UINT64_MAX))
        if (unlikely(!ubase_check(uref_clock_set_rap_sys(uref,
                            upipe_setrap->rap_sys))))
            upipe_dbg(upipe, "invalid clock ref for RAP");
    upipe_setrap_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_setrap_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    upipe_setrap_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current rap_sys being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param rap_sys_p filled with the current rap_sys
 * @return an error code
 */
static int _upipe_setrap_get_rap(struct upipe *upipe, uint64_t *rap_sys_p)
{
    struct upipe_setrap *upipe_setrap = upipe_setrap_from_upipe(upipe);
    *rap_sys_p = upipe_setrap->rap_sys;
    return UBASE_ERR_NONE;
}

/** @This sets the rap_sys to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param rap_sys rap_sys to set
 * @return an error code
 */
static int _upipe_setrap_set_rap(struct upipe *upipe, uint64_t rap_sys)
{
    struct upipe_setrap *upipe_setrap = upipe_setrap_from_upipe(upipe);
    upipe_setrap->rap_sys = rap_sys;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a setrap pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_setrap_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_setrap_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_setrap_set_flow_def(upipe, flow_def);
        }

        case UPIPE_SETRAP_GET_RAP: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SETRAP_SIGNATURE)
            uint64_t *rap_sys_p = va_arg(args, uint64_t *);
            return _upipe_setrap_get_rap(upipe, rap_sys_p);
        }
        case UPIPE_SETRAP_SET_RAP: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SETRAP_SIGNATURE)
            uint64_t rap_sys = va_arg(args, uint64_t);
            return _upipe_setrap_set_rap(upipe, rap_sys);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_setrap_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_setrap_clean_output(upipe);
    upipe_setrap_clean_urefcount(upipe);
    upipe_setrap_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_setrap_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SETRAP_SIGNATURE,

    .upipe_alloc = upipe_setrap_alloc,
    .upipe_input = upipe_setrap_input,
    .upipe_control = upipe_setrap_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all setrap pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_setrap_mgr_alloc(void)
{
    return &upipe_setrap_mgr;
}
