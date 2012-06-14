/*****************************************************************************
 * upipe_linear.h: common declarations of linear pipes
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

#ifndef _UPIPE_UPIPE_LINEAR_H_
/** @hidden */
#define _UPIPE_UPIPE_LINEAR_H_

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>

#include <stdarg.h>
#include <assert.h>

/** super-set of the upipe structure with additional members */
struct upipe_linear {
    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** pipe acting as output */
    struct upipe *output;

    /** flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** structure exported to application */
    struct upipe upipe;
};

/** @This returns the high-level upipe structure.
 *
 * @param upipe_linear pointer to the upipe_linear structure
 * @return pointer to the upipe structure
 */
static inline struct upipe *upipe_linear_to_upipe(struct upipe_linear *upipe_linear)
{
    return &upipe_linear->upipe;
}

/** @This returns the private upipe_linear structure.
 *
 * @param upipe description structure of the pipe
 * @return pointer to the upipe_linear structure
 */
static inline struct upipe_linear *upipe_linear_from_upipe(struct upipe *upipe)
{
    return container_of(upipe, struct upipe_linear, upipe);
}

UPIPE_STRUCT_TEMPLATE(linear, uref_mgr, struct uref_mgr *)
UPIPE_STRUCT_TEMPLATE(linear, ubuf_mgr, struct ubuf_mgr *)
UPIPE_STRUCT_TEMPLATE(linear, flow_def, struct uref *)

/** @This checks if the linear pipe is ready to process data.
 * This only checks uref_mgr and output as ubuf_mgr is not
 * mandatory to process data.
 *
 * @param upipe description structure of the pipe
 */
static inline bool upipe_linear_ready(struct upipe *upipe)
{
    struct upipe_linear *upipe_linear = upipe_linear_from_upipe(upipe);
    return upipe_linear->output != NULL && upipe_linear->uref_mgr != NULL;
}

/** @This initializes the common members of linear pipes.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_linear_init(struct upipe *upipe)
{
    struct upipe_linear *upipe_linear = upipe_linear_from_upipe(upipe);
    UPIPE_OBJ_INIT_TEMPLATE(upipe_linear, uref_mgr)
    UPIPE_OBJ_INIT_TEMPLATE(upipe_linear, ubuf_mgr)
    upipe_linear->output = NULL;
    upipe_linear->flow_def = NULL;
    upipe_linear->flow_def_sent = false;
}

/** @This outputs a flow deletion control packet.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_linear_flow_delete(struct upipe *upipe)
{
    struct upipe_linear *upipe_linear = upipe_linear_from_upipe(upipe);
    struct uref_mgr *uref_mgr = upipe_linear_uref_mgr(upipe);
    const char *flow_name;
    if (unlikely(uref_mgr == NULL ||
                 !uref_flow_get_name(upipe_linear->flow_def, &flow_name)))
        return;
    struct uref *uref = uref_flow_alloc_delete(uref_mgr, flow_name);
    if (unlikely(uref == NULL)) {
        ulog_aerror(upipe->ulog);
        return;
    }
    upipe_input(upipe_linear->output, uref);
    upipe_linear->flow_def_sent = false;
}

/** @internal @This outputs a flow definition control packet.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_linear_flow_definition(struct upipe *upipe)
{
    struct upipe_linear *upipe_linear = upipe_linear_from_upipe(upipe);
    struct uref_mgr *uref_mgr = upipe_linear_uref_mgr(upipe);
    if (unlikely(uref_mgr == NULL || upipe_linear->flow_def == NULL)) return;
    struct uref *uref = uref_dup(uref_mgr, upipe_linear->flow_def);
    if (unlikely(uref == NULL)) {
        ulog_aerror(upipe->ulog);
        return;
    }
    upipe_input(upipe_linear->output, uref);
    upipe_linear->flow_def_sent = true;
}

/** @This sets the flow definition of the output.
 *
 * @param upipe description structure of the pipe
 * @param flow_def control packet describing the output
 */
static inline void upipe_linear_set_flow_def(struct upipe *upipe,
                                             struct uref *flow_def)
{
    struct upipe_linear *upipe_linear = upipe_linear_from_upipe(upipe);
    if (unlikely(upipe_linear->flow_def != NULL)) {
        if (unlikely(upipe_linear->flow_def_sent))
            upipe_linear_flow_delete(upipe);
        uref_release(upipe_linear->flow_def);
    }
    upipe_linear->flow_def = flow_def;
}

/** @This outputs a uref to the output.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure to send
 */
static inline void upipe_linear_output(struct upipe *upipe, struct uref *uref)
{
    struct upipe_linear *upipe_linear = upipe_linear_from_upipe(upipe);
    if (unlikely(!upipe_linear->flow_def_sent))
        upipe_linear_flow_definition(upipe);
    if (unlikely(!upipe_linear->flow_def_sent)) {
        uref_release(uref);
        return;
    }

    const char *flow_name;
    if (unlikely(!upipe_linear->flow_def_sent ||
                 !uref_flow_get_name(upipe_linear->flow_def, &flow_name) ||
                 !uref_flow_set_name(&uref, flow_name))) {
        ulog_aerror(upipe->ulog);
        uref_release(uref);
        return;
    }
    upipe_input(upipe_linear->output, uref);
}

/** @internal @This handles the get_output control command.
 *
 * @param upipe description structure of the pipe
 * @param output_p filled in with a pointer to the output pipe
 * @return false in case of error
 */
static bool _upipe_linear_get_output(struct upipe *upipe,
                                     struct upipe **output_p)
{
    struct upipe_linear *upipe_linear = upipe_linear_from_upipe(upipe);
    assert(output_p != NULL);
    *output_p = upipe_linear->output;
    return true;
}

/** @internal @This handles the set_output control command, and properly
 * deletes and replays flows on old and new outputs.
 *
 * @param upipe description structure of the pipe
 * @param output new output pipe
 * @return false in case of error
 */
static bool _upipe_linear_set_output(struct upipe *upipe, struct upipe *output)
{
    struct upipe_linear *upipe_linear = upipe_linear_from_upipe(upipe);
    if (unlikely(upipe_linear->output != NULL)) {
        if (likely(upipe_linear->flow_def_sent))
            upipe_linear_flow_delete(upipe);
        upipe_release(upipe_linear->output);
    }
    upipe_linear->output = output;
    if (likely(upipe_linear->output != NULL))
        upipe_use(upipe_linear->output);
    return true;
}

/** @This processes common control commands on a linear pipe.
 *
 * @param upipe description structure of the pipe
 * @return true if the command has been correctly processed
 */
static inline bool upipe_linear_control(struct upipe *upipe,
                                        enum upipe_control control,
                                        va_list args)
{
    struct upipe_linear *upipe_linear = upipe_linear_from_upipe(upipe);
    switch (control) {
        UPIPE_OBJ_CONTROL_TEMPLATE(upipe_linear, UPIPE, uref_mgr, UREF_MGR, uref_mgr)
        UPIPE_OBJ_CONTROL_TEMPLATE(upipe_linear, UPIPE_LINEAR, ubuf_mgr, UBUF_MGR, ubuf_mgr)

        case UPIPE_LINEAR_GET_OUTPUT: {
            struct upipe **output_p = va_arg(args, struct upipe **);
            return _upipe_linear_get_output(upipe, output_p);
        }
        case UPIPE_LINEAR_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return _upipe_linear_set_output(upipe, output);
        }

        default:
            return false;
    }
}

/** @This cleans up the common members of linear pipes.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_linear_clean(struct upipe *upipe)
{
    struct upipe_linear *upipe_linear = upipe_linear_from_upipe(upipe);
    if (unlikely(upipe_linear->output != NULL)) {
        if (likely(upipe_linear->flow_def_sent))
            upipe_linear_flow_delete(upipe);
        upipe_release(upipe_linear->output);
    }
    if (likely(upipe_linear->flow_def != NULL))
        uref_release(upipe_linear->flow_def);
    UPIPE_OBJ_CLEAN_TEMPLATE(upipe_linear, uref_mgr, uref_mgr)
    UPIPE_OBJ_CLEAN_TEMPLATE(upipe_linear, ubuf_mgr, ubuf_mgr)
}

#endif
