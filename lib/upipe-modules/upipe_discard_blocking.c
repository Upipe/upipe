/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe module discarding input uref when the output pipe is blocking
 */

#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_discard_blocking.h>

/** @internal @This is the private structure of a discard blocking pipe. */
struct upipe_disblo {
    /** public structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** upump */
    struct upump *upump;
    /** output pipe */
    struct upipe *output;
    /** output flow def */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of registered request */
    struct uchain requests;
    /** last received uref */
    struct uref *uref;
};

UPIPE_HELPER_UPIPE(upipe_disblo, upipe, UPIPE_DISBLO_SIGNATURE);
UPIPE_HELPER_VOID(upipe_disblo);
UPIPE_HELPER_UREFCOUNT(upipe_disblo, urefcount, upipe_disblo_free);
UPIPE_HELPER_UPUMP_MGR(upipe_disblo, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_disblo, upump, upump_mgr);
UPIPE_HELPER_OUTPUT(upipe_disblo, output, flow_def, output_state, requests);

/** @internal @This allocates a discard blocking pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe or NULL
 */
static struct upipe *upipe_disblo_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature,
                                        va_list args)
{
    struct upipe *upipe = upipe_disblo_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(!upipe))
        return NULL;

    upipe_disblo_init_urefcount(upipe);
    upipe_disblo_init_upump_mgr(upipe);
    upipe_disblo_init_upump(upipe);
    upipe_disblo_init_output(upipe);

    struct upipe_disblo *upipe_disblo = upipe_disblo_from_upipe(upipe);
    upipe_disblo->uref = NULL;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This frees a discard blocking pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_disblo_free(struct upipe *upipe)
{
    struct upipe_disblo *upipe_disblo = upipe_disblo_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_disblo->uref);
    upipe_disblo_clean_output(upipe);
    upipe_disblo_clean_upump(upipe);
    upipe_disblo_clean_upump_mgr(upipe);
    upipe_disblo_clean_urefcount(upipe);
    upipe_disblo_free_void(upipe);
}

/** @internal @This handles the input buffers.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer to handle
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_disblo_input(struct upipe *upipe,
                               struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_disblo *upipe_disblo = upipe_disblo_from_upipe(upipe);

    if (upipe_disblo->uref) {
        upipe_warn(upipe, "dropping uref");
        uref_free(upipe_disblo->uref);
    }

    if (upipe_disblo->upump) {
        upipe_disblo->uref = uref;
        upump_start(upipe_disblo->upump);
    }
    else {
        upipe_warn(upipe, "no idler, dropping uref");
        uref_free(uref);
    }
}

/** @internal @This handles the pipe control commands.
 *
 * @param upipe description structure of the pipe
 * @param cmd type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_disblo_control_real(struct upipe *upipe, int cmd, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_disblo_control_output(upipe, cmd, args));
    switch (cmd) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            struct uref *flow_def_dup = uref_dup(flow_def);
            if (unlikely(!flow_def_dup))
                return UBASE_ERR_ALLOC;
            upipe_disblo_store_flow_def(upipe, flow_def_dup);
            return UBASE_ERR_NONE;
        }
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_disblo_set_upump(upipe, NULL);
            return upipe_disblo_attach_upump_mgr(upipe);
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the idler pump callback.
 *
 * @param upump description structure of the pump
 */
static void upipe_disblo_idle(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_disblo *upipe_disblo = upipe_disblo_from_upipe(upipe);
    if (upipe_disblo->uref) {
        struct uref *uref = upipe_disblo->uref;
        upipe_disblo->uref = NULL;
        upipe_disblo_output(upipe, uref, &upipe_disblo->upump);
    }
    upump_stop(upipe_disblo->upump);
}

/** @internal @This checks the internal state.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_disblo_check(struct upipe *upipe)
{
    struct upipe_disblo *upipe_disblo = upipe_disblo_from_upipe(upipe);

    UBASE_RETURN(upipe_disblo_check_upump_mgr(upipe));
    if (upipe_disblo->upump_mgr) {
        if (!upipe_disblo->upump) {
            struct upump *upump = upump_alloc_idler(upipe_disblo->upump_mgr,
                                                    upipe_disblo_idle, upipe,
                                                    upipe->refcount);
            if (unlikely(!upump))
                return UBASE_ERR_UPUMP;

            upipe_disblo_set_upump(upipe, upump);
        }
    }
    return UBASE_ERR_NONE;
}

/** @internal @This handles the pipe control commands and check the internal
 * state.
 *
 * @param upipe description structure of the pipe
 * @param cmd type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_disblo_control(struct upipe *upipe, int cmd, va_list args)
{
    UBASE_RETURN(upipe_disblo_control_real(upipe, cmd, args));
    return upipe_disblo_check(upipe);
}

/** @internal @This is the static management structure for the discard blocking
 * pipes. */
static struct upipe_mgr upipe_disblo_mgr = {
    .refcount = NULL,
    .signature = UPIPE_DISBLO_SIGNATURE,
    .upipe_err_str = NULL,
    .upipe_command_str = NULL,
    .upipe_event_str = NULL,
    .upipe_alloc = upipe_disblo_alloc,
    .upipe_input = upipe_disblo_input,
    .upipe_control = upipe_disblo_control,
    .upipe_mgr_control = NULL,
};

/** @This returns the management structure for discard blocking pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_disblo_mgr_alloc(void)
{
    return &upipe_disblo_mgr;
}
