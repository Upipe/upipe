/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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
 * @short Upipe linear module converting from compressed NTSC (first line=23 tff)
 *        to baseband ntsc (first line=283 bff)
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_ntsc_prepend.h>
#include <upipe/uref_pic_flow.h>

#define UPIPE_NTSC_PREPEND_LINES 5
#define UPIPE_NTSC_APPEND_LINES 1

/** upipe_ntsc_prepend structure */
struct upipe_ntsc_prepend {
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

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ntsc_prepend, upipe, UPIPE_NTSC_PREPEND_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_ntsc_prepend, urefcount, upipe_ntsc_prepend_free)
UPIPE_HELPER_VOID(upipe_ntsc_prepend);
UPIPE_HELPER_OUTPUT(upipe_ntsc_prepend, output, flow_def, output_state, request_list)

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ntsc_prepend_input(struct upipe *upipe, struct uref *uref,
                                     struct upump **upump_p)
{
    struct upipe_ntsc_prepend *upipe_ntsc_prepend = upipe_ntsc_prepend_from_upipe(upipe);

    uref_pic_delete_tff(uref);
    UBASE_FATAL(upipe, uref_pic_resize(uref, 0, -UPIPE_NTSC_PREPEND_LINES, -1, 486))
    UBASE_FATAL(upipe, uref_pic_clear(uref, 0, 0, -1, UPIPE_NTSC_PREPEND_LINES, 0))

    upipe_ntsc_prepend_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ntsc_prepend_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))

    uint8_t vprepend;
    UBASE_RETURN(uref_pic_flow_get_vprepend(flow_def, &vprepend));
    if (vprepend < UPIPE_NTSC_PREPEND_LINES) {
        upipe_err_va(upipe, "incompatible input flow def, vprepend is %u",
                vprepend);
        return UBASE_ERR_INVALID;
    }

    uint8_t vappend;
    UBASE_RETURN(uref_pic_flow_get_vappend(flow_def, &vappend));
    if (vappend < UPIPE_NTSC_APPEND_LINES) {
        upipe_err_va(upipe, "incompatible input flow def, vappend is %u",
                vappend);
        return UBASE_ERR_INVALID;
    }

    uint64_t vsize, vsize_visible, hsize;
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &vsize));
    UBASE_RETURN(uref_pic_flow_get_vsize_visible(flow_def, &vsize_visible));
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &hsize));

    if (vsize != 480 || hsize != 720) {
        upipe_err_va(upipe,
                "incompatible input flow def, size is %"PRIu64"x%"PRIu64,
                hsize, vsize);
        return UBASE_ERR_INVALID;
    }

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL))
        return UBASE_ERR_ALLOC;

    vsize += UPIPE_NTSC_PREPEND_LINES + UPIPE_NTSC_APPEND_LINES;
    vsize_visible += UPIPE_NTSC_PREPEND_LINES + UPIPE_NTSC_APPEND_LINES;

    UBASE_FATAL(upipe, uref_pic_flow_set_vsize(flow_def_dup, vsize))
    UBASE_FATAL(upipe, uref_pic_flow_set_vsize_visible(flow_def_dup, vsize_visible))

    upipe_ntsc_prepend_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This requires a ubuf manager by proxy, and amends the flow
 * format.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_ntsc_prepend_amend_ubuf_mgr(struct upipe *upipe,
                                        struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);

    uint8_t vprepend, vappend;
    if (!ubase_check(uref_pic_flow_get_vprepend(flow_format, &vprepend)))
        vprepend = 0;
    if (!ubase_check(uref_pic_flow_get_vappend(flow_format, &vappend)))
        vappend = 0;

    vprepend += UPIPE_NTSC_PREPEND_LINES;
    vappend += UPIPE_NTSC_APPEND_LINES;

    uref_pic_flow_set_vappend(flow_format, vappend);
    uref_pic_flow_set_vprepend(flow_format, vprepend);

    struct urequest ubuf_mgr_request;
    urequest_set_opaque(&ubuf_mgr_request, request);
    urequest_init_ubuf_mgr(&ubuf_mgr_request, flow_format,
                           upipe_ntsc_prepend_provide_output_proxy, NULL);
    upipe_throw_provide_request(upipe, &ubuf_mgr_request);
    urequest_clean(&ubuf_mgr_request);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an ntsc_prepend pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ntsc_prepend_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_ntsc_prepend_amend_ubuf_mgr(upipe, request);
            return upipe_ntsc_prepend_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
                return UBASE_ERR_NONE;
            return upipe_ntsc_prepend_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ntsc_prepend_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_ntsc_prepend_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates an ntsc_prepend pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ntsc_prepend_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ntsc_prepend_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_ntsc_prepend_init_urefcount(upipe);
    upipe_ntsc_prepend_init_output(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ntsc_prepend_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ntsc_prepend_clean_output(upipe);
    upipe_ntsc_prepend_clean_urefcount(upipe);
    upipe_ntsc_prepend_free_void(upipe);
}

static struct upipe_mgr upipe_ntsc_prepend_mgr = {
    .refcount = NULL,
    .signature = UPIPE_NTSC_PREPEND_SIGNATURE,

    .upipe_alloc = upipe_ntsc_prepend_alloc,
    .upipe_input = upipe_ntsc_prepend_input,
    .upipe_control = upipe_ntsc_prepend_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for ntsc_prepend pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ntsc_prepend_mgr_alloc(void)
{
    return &upipe_ntsc_prepend_mgr;
}
