/*
 * Copyright (C) 2017-2019 OpenHeadend S.A.R.L.
 * Copyright (C) 2025 EasyTools
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
 * @short Upipe module generating blank pictures for void urefs
 */

#include "upipe/ubase.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_flow.h"
#include "upipe/upipe_helper_input.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_flow_def.h"
#include "upipe/upipe_helper_flow_format.h"
#include "upipe/upipe_helper_ubuf_mgr.h"

#include "upipe/uref_pic_flow.h"
#include "upipe/uref_pic.h"
#include "upipe/ubuf_pic.h"
#include "upipe/uref_void_flow.h"

#include "upipe-modules/upipe_video_blank.h"

/** @internal @This is the private structure of a video blank pipe. */
struct upipe_vblk {
    /** public pipe structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** output pipe */
    struct upipe *output;
    /** output flow format */
    struct uref *flow_def;
    /** input flow definition */
    struct uref *input_flow_def;
    /** flow attributes */
    struct uref *flow_attr;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** output request list */
    struct uchain requests;
    /** flow format request */
    struct urequest flow_format_request;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** blank picture */
    struct ubuf *ubuf;
    /** flow format */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;
    /** buffered urefs */
    struct uchain urefs;
    /** number of buffered urefs */
    unsigned nb_urefs;
    /** maximum buffered urefs */
    unsigned max_urefs;
    /** blockers */
    struct uchain blockers;
    /** picture */
    struct ubuf *pic;
    /** picture attributes */
    struct uref *pic_attr;
};

/** @hidden */
static int upipe_vblk_check_flow_format(struct upipe *upipe,
                                        struct uref *flow_format);

/** @hidden */
static int upipe_vblk_check(struct upipe *upipe, struct uref *flow_format);

/** @hidden */
static bool upipe_vblk_try_output(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_vblk, upipe, UPIPE_VBLK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_vblk, urefcount, upipe_vblk_free);
UPIPE_HELPER_FLOW(upipe_vblk, UREF_PIC_FLOW_DEF);
UPIPE_HELPER_INPUT(upipe_vblk, urefs, nb_urefs, max_urefs, blockers,
                   upipe_vblk_try_output);
UPIPE_HELPER_OUTPUT(upipe_vblk, output, flow_def, output_state, requests);
UPIPE_HELPER_FLOW_DEF(upipe_vblk, input_flow_def, flow_attr);
UPIPE_HELPER_FLOW_FORMAT(upipe_vblk, flow_format_request,
                         upipe_vblk_check_flow_format,
                         upipe_vblk_register_output_request,
                         upipe_vblk_unregister_output_request);
UPIPE_HELPER_UBUF_MGR(upipe_vblk, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_vblk_check,
                      upipe_vblk_register_output_request,
                      upipe_vblk_unregister_output_request);

/** @internal @This frees a video blank pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_vblk_free(struct upipe *upipe)
{
    struct upipe_vblk *upipe_vblk = upipe_vblk_from_upipe(upipe);

    upipe_throw_dead(upipe);

    if (upipe_vblk->ubuf)
        ubuf_free(upipe_vblk->ubuf);
    if (upipe_vblk->pic)
        ubuf_free(upipe_vblk->pic);
    if (upipe_vblk->pic_attr)
        uref_free(upipe_vblk->pic_attr);

    upipe_vblk_clean_input(upipe);
    upipe_vblk_clean_ubuf_mgr(upipe);
    upipe_vblk_clean_flow_format(upipe);
    upipe_vblk_clean_flow_def(upipe);
    upipe_vblk_clean_output(upipe);
    upipe_vblk_clean_urefcount(upipe);

    upipe_vblk_free_flow(upipe);
}

/** @internal @This allocates a video blank pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe
 */
static struct upipe *upipe_vblk_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_vblk_alloc_flow(mgr, uprobe, signature, args,
                                                &flow_def);
    if (unlikely(!upipe)) {
        return NULL;
    }

    upipe_vblk_init_urefcount(upipe);
    upipe_vblk_init_output(upipe);
    upipe_vblk_init_flow_def(upipe);
    upipe_vblk_init_flow_format(upipe);
    upipe_vblk_init_ubuf_mgr(upipe);
    upipe_vblk_init_input(upipe);

    struct upipe_vblk *upipe_vblk = upipe_vblk_from_upipe(upipe);
    upipe_vblk->ubuf = NULL;
    upipe_vblk->pic = NULL;
    upipe_vblk->pic_attr = NULL;

    upipe_throw_ready(upipe);

    if (unlikely(!ubase_check(uref_flow_match_def(flow_def,
                                                  UREF_PIC_FLOW_DEF)))) {
        uref_free(flow_def);
        upipe_release(upipe);
        return NULL;
    }

    upipe_vblk_store_flow_def_attr(upipe, flow_def);

    return upipe;
}

/** @internal @This allocates a picture.
 *
 * @param upipe description structure of the pipe
 * @return a ubuf filled with a picture
 */
static struct ubuf *upipe_vblk_alloc_pic(struct upipe *upipe)
{
    struct upipe_vblk *upipe_vblk = upipe_vblk_from_upipe(upipe);

    if (upipe_vblk->pic)
        return ubuf_dup(upipe_vblk->pic);

    if (upipe_vblk->ubuf)
        return ubuf_dup(upipe_vblk->ubuf);

    struct uref *flow_def = upipe_vblk->flow_def;
    uint64_t hsize = 0, vsize = 0;
    uref_pic_flow_get_hsize(flow_def, &hsize);
    uref_pic_flow_get_vsize(flow_def, &vsize);
    bool full_range = ubase_check(uref_pic_flow_get_full_range(flow_def));

    upipe_verbose_va(upipe, "allocate blank %"PRIu64"x%"PRIu64" picture",
                     hsize, vsize);
    if (unlikely(!hsize || !vsize)) {
        upipe_warn(upipe, "no output size");
        return NULL;
    }

    struct ubuf *ubuf = ubuf_pic_alloc(upipe_vblk->ubuf_mgr, hsize, vsize);
    if (unlikely(!ubuf)) {
        upipe_err_va(upipe, "fail to allocate %"PRIu64"x%"PRIu64" picture",
                     hsize, vsize);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    int err = ubuf_pic_clear(ubuf, 0, 0, -1, -1, full_range);
    if (unlikely(!ubase_check(err))) {
        upipe_err(upipe, "fail to clear picture");
        ubuf_free(ubuf);
        return NULL;
    }

    upipe_vblk->ubuf = ubuf;
    return ubuf_dup(ubuf);
}

/** @internal @This tries to output a buffer.
 *
 * @param upipe description structure of the pipe
 * @param uref input uref
 * @param upump_p reference to pump that generated the buffer
 * @return true if the buffered was outputted
 */
static bool upipe_vblk_try_output(struct upipe *upipe,
                                  struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_vblk *upipe_vblk = upipe_vblk_from_upipe(upipe);

    if (uref->ubuf) {
        upipe_vblk_output(upipe, uref, upump_p);
        return true;
    }

    if (unlikely(!upipe_vblk->input_flow_def)) {
        upipe_warn(upipe, "no input flow definition");
        uref_free(uref);
        return true;
    }

    if (unlikely(!upipe_vblk->flow_def || !upipe_vblk->ubuf_mgr))
        return false;

    struct ubuf *ubuf = upipe_vblk_alloc_pic(upipe);
    if (unlikely(!ubuf)) {
        upipe_err(upipe, "fail to allocate picture");
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    uref_attach_ubuf(uref, ubuf);
    if (upipe_vblk->pic_attr)
        uref_attr_import(uref, upipe_vblk->pic_attr);
    if (ubase_check(uref_pic_get_progressive(upipe_vblk->flow_def)))
        uref_pic_set_progressive(uref);

    upipe_vblk_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This handles the input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref input uref
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_vblk_input(struct upipe *upipe,
                             struct uref *uref,
                             struct upump **upump_p)
{
    if (!upipe_vblk_check_input(upipe) ||
        !upipe_vblk_try_output(upipe, uref, upump_p)) {

        if (upipe_vblk_check_input(upipe))
            upipe_use(upipe);

        upipe_vblk_hold_input(upipe, uref);
        upipe_vblk_block_input(upipe, upump_p);
    }
}

/** @internal @This sets the input flow def.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to set
 * @return an error code
 */
static int upipe_vblk_set_flow_def(struct upipe *upipe,
                                   struct uref *flow_def)
{
    struct upipe_vblk *upipe_vblk = upipe_vblk_from_upipe(upipe);

    struct uref *input_flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(input_flow_def);

    struct uref *flow_format = NULL;
    if (ubase_check(uref_flow_match_def(flow_def, UREF_VOID_FLOW_DEF))) {
        flow_format = upipe_vblk_store_flow_def_input(upipe, input_flow_def);
    } else if (ubase_check(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF))) {
        flow_format = upipe_vblk_store_flow_def_attr(upipe, input_flow_def);
    } else {
        upipe_warn(upipe, "unsupported flow def");
        uref_free(input_flow_def);
        return UBASE_ERR_INVALID;
    }

    if (!upipe_vblk->ubuf_mgr || !flow_format ||
        !ubase_check(ubuf_mgr_check(upipe_vblk->ubuf_mgr, flow_format))) {
        ubuf_mgr_release(upipe_vblk->ubuf_mgr);
        upipe_vblk->ubuf_mgr = NULL;
        ubuf_free(upipe_vblk->ubuf);
        upipe_vblk->ubuf = NULL;
    }

    if (flow_format)
        upipe_vblk_require_flow_format(upipe, flow_format);

    return UBASE_ERR_NONE;
}

/** @internal @This sets the reference picture.
 *
 * @param upipe description structure of the pipe
 * @param uref picture buffer
 * @return an error code
 */
static int upipe_vblk_set_pic_real(struct upipe *upipe, struct uref *uref)
{
    struct upipe_vblk *upipe_vblk = upipe_vblk_from_upipe(upipe);

    if (upipe_vblk->pic) {
        ubuf_free(upipe_vblk->pic);
        upipe_vblk->pic = NULL;
    }

    if (upipe_vblk->pic_attr) {
        uref_free(upipe_vblk->pic_attr);
        upipe_vblk->pic_attr = NULL;
    }

    if (!uref)
        return UBASE_ERR_NONE;

    upipe_vblk->pic_attr = uref_sibling_alloc_control(uref);
    if (!upipe_vblk->pic_attr) {
        uref_free(uref);
        return UBASE_ERR_ALLOC;
    }
    int ret = uref_attr_import(upipe_vblk->pic_attr, uref);
    if (unlikely(!ubase_check(ret))) {
        uref_free(upipe_vblk->pic_attr);
        upipe_vblk->pic_attr = NULL;
        uref_free(uref);
        return ret;
    }
    upipe_vblk->pic = uref->ubuf;
    uref->ubuf = NULL;
    uref_free(uref);
    return UBASE_ERR_NONE;
}

/** @internal @This checks the provided flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_vblk_check_flow_format(struct upipe *upipe,
                                        struct uref *flow_format)
{
    struct upipe_vblk *upipe_vblk = upipe_vblk_from_upipe(upipe);
    uref_attr_import(flow_format, upipe_vblk->flow_attr);
    uref_pic_flow_delete_surface_type(flow_format);
    upipe_vblk_require_ubuf_mgr(upipe, flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This checks the ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_vblk_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_vblk *upipe_vblk = upipe_vblk_from_upipe(upipe);

    if (flow_format) {
        ubuf_free(upipe_vblk->ubuf);
        upipe_vblk->ubuf = NULL;
        upipe_vblk_store_flow_def(upipe, flow_format);
    }

    if (!upipe_vblk->flow_def)
        return UBASE_ERR_NONE;

    if (!upipe_vblk->ubuf_mgr &&
        urequest_get_opaque(&upipe_vblk->flow_format_request,
                            struct upipe *) != upipe) {
        upipe_vblk_require_flow_format(upipe, uref_dup(upipe_vblk->flow_def));
        return UBASE_ERR_NONE;
    }

    bool release = !upipe_vblk_check_input(upipe);
    bool unblock = upipe_vblk_output_input(upipe);
    if (unblock) {
        upipe_vblk_unblock_input(upipe);
        if (release)
            upipe_release(upipe);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This handles the pipe control commands.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_vblk_control_real(struct upipe *upipe,
                                   int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_vblk_control_output(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_vblk_set_flow_def(upipe, flow_def);
        }

        case UPIPE_VBLK_SET_PIC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VBLK_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            return upipe_vblk_set_pic_real(upipe, uref);
        }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This handles control commands and checks the status of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_vblk_control(struct upipe *upipe,
                              int command, va_list args)
{
    UBASE_RETURN(upipe_vblk_control_real(upipe, command, args));
    return upipe_vblk_check(upipe, NULL);
}

/** @internal @This is the static video blank pipe manager. */
static struct upipe_mgr upipe_vblk_mgr = {
    .refcount = NULL,
    .signature = UPIPE_VBLK_SIGNATURE,
    .upipe_alloc = upipe_vblk_alloc,
    .upipe_input = upipe_vblk_input,
    .upipe_control = upipe_vblk_control,
};

/** @This returns the video blank pipe manager.
 *
 * @return a pointer to the video blank pipe manager
 */
struct upipe_mgr *upipe_vblk_mgr_alloc(void)
{
    return &upipe_vblk_mgr;
}
