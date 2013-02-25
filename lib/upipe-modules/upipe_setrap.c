/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module setting systime_rap to urefs
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_setrap.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

/** @internal @This is the private context of a setrap pipe. */
struct upipe_setrap {
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** rap to set */
    uint64_t systime_rap;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_setrap, upipe)
UPIPE_HELPER_OUTPUT(upipe_setrap, output, flow_def, flow_def_sent)

/** @internal @This allocates a setrap pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_setrap_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe)
{
    struct upipe_setrap *upipe_setrap = malloc(sizeof(struct upipe_setrap));
    if (unlikely(upipe_setrap == NULL))
        return NULL;
    struct upipe *upipe = upipe_setrap_to_upipe(upipe_setrap);
    upipe_init(upipe, mgr, uprobe);
    upipe_setrap_init_output(upipe);
    upipe_setrap->systime_rap = UINT64_MAX;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_setrap_input(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_setrap *upipe_setrap = upipe_setrap_from_upipe(upipe);
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        upipe_setrap_store_flow_def(upipe, uref);
        return;
    }

    if (unlikely(uref_flow_get_end(uref))) {
        uref_free(uref);
        upipe_throw_need_input(upipe);
        return;
    }

    if (likely(upipe_setrap->systime_rap != UINT64_MAX))
        uref->systime_rap = upipe_setrap->systime_rap;
    upipe_setrap_output(upipe, uref, upump);
}

/** @internal @This returns the current systime_rap being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param rap_p filled with the current systime_rap
 * @return false in case of error
 */
static bool _upipe_setrap_get_rap(struct upipe *upipe, uint64_t *rap_p)
{
    struct upipe_setrap *upipe_setrap = upipe_setrap_from_upipe(upipe);
    *rap_p = upipe_setrap->systime_rap;
    return true;
}

/** @This sets the systime_rap to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param rap systime_rap to set
 * @return false in case of error
 */
static bool _upipe_setrap_set_rap(struct upipe *upipe, uint64_t rap)
{
    struct upipe_setrap *upipe_setrap = upipe_setrap_from_upipe(upipe);
    upipe_setrap->systime_rap = rap;
    return true;
}

/** @internal @This processes control commands on a setrap pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_setrap_control(struct upipe *upipe,
                                enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_setrap_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_setrap_set_output(upipe, output);
        }

        case UPIPE_SETRAP_GET_RAP: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_SETRAP_SIGNATURE);
            uint64_t *rap_p = va_arg(args, uint64_t *);
            return _upipe_setrap_get_rap(upipe, rap_p);
        }
        case UPIPE_SETRAP_SET_RAP: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_SETRAP_SIGNATURE);
            uint64_t rap = va_arg(args, uint64_t);
            return _upipe_setrap_set_rap(upipe, rap);
        }
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_setrap_free(struct upipe *upipe)
{
    struct upipe_setrap *upipe_setrap = upipe_setrap_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_setrap_clean_output(upipe);

    upipe_clean(upipe);
    free(upipe_setrap);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_setrap_mgr = {
    .signature = UPIPE_SETRAP_SIGNATURE,

    .upipe_alloc = upipe_setrap_alloc,
    .upipe_input = upipe_setrap_input,
    .upipe_control = upipe_setrap_control,
    .upipe_free = upipe_setrap_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all setrap pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_setrap_mgr_alloc(void)
{
    return &upipe_setrap_mgr;
}
