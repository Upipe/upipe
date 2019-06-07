/*
 * Copyright (C) 2017-2019 OpenHeadend S.A.R.L.
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

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_flow_format.h>
#include <upipe/upipe_helper_ubuf_mgr.h>

#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/ubuf_pic.h>
#include <upipe/uref_void_flow.h>

#include <upipe-modules/upipe_video_blank.h>

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
    /** picture attributes */
    struct uref *pic_attr;
};

/** @hidden */
static int upipe_vblk_check_flow_format(struct upipe *upipe,
                                        struct uref *flow_format);

/** @hidden */
static int upipe_vblk_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_vblk, upipe, UPIPE_VBLK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_vblk, urefcount, upipe_vblk_free);
UPIPE_HELPER_FLOW(upipe_vblk, UREF_PIC_FLOW_DEF);
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
    if (upipe_vblk->pic_attr)
        uref_free(upipe_vblk->pic_attr);

    upipe_vblk_clean_ubuf_mgr(upipe);
    upipe_vblk_clean_flow_format(upipe);
    upipe_vblk_clean_flow_def(upipe);
    upipe_vblk_clean_output(upipe);
    upipe_vblk_clean_urefcount(upipe);

    upipe_vblk_free_flow(upipe);
}

/** @internal @This checks a flow definition validity.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition to check
 * @return an error code
 */
static int upipe_vblk_check_flow_def(struct upipe *upipe,
                                     struct uref *flow_def)
{
    uint64_t hsize, vsize;
    UBASE_RETURN(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF));
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &hsize));
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &vsize));
    return UBASE_ERR_NONE;
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

    struct upipe_vblk *upipe_vblk = upipe_vblk_from_upipe(upipe);
    upipe_vblk->ubuf = NULL;
    upipe_vblk->pic_attr = NULL;

    upipe_throw_ready(upipe);

    if (unlikely(!ubase_check(uref_flow_match_def(flow_def,
                                                  UREF_PIC_FLOW_DEF)))) {
        uref_free(flow_def);
        upipe_release(upipe);
        return NULL;
    }

    upipe_vblk_store_flow_def(upipe, flow_def);

    return upipe;
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
    struct upipe_vblk *upipe_vblk = upipe_vblk_from_upipe(upipe);
    struct uref *flow_def = upipe_vblk->flow_def;
    struct uref *input_flow_def = upipe_vblk->input_flow_def;

    if (uref->ubuf) {
        upipe_vblk_output(upipe, uref, upump_p);
        return;
    }

    if (unlikely(!input_flow_def)) {
        upipe_warn(upipe, "no input flow definition");
        uref_free(uref);
        return;
    }

    if (unlikely(!flow_def)) {
        upipe_warn(upipe, "no output flow definition");
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_vblk->ubuf_mgr)) {
        upipe_warn(upipe, "no ubuf manager set");
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_vblk->ubuf)) {
        upipe_verbose(upipe, "allocate blank picture");

        uint64_t hsize, vsize;
        ubase_assert(uref_pic_flow_get_hsize(upipe_vblk->flow_def, &hsize));
        ubase_assert(uref_pic_flow_get_vsize(upipe_vblk->flow_def, &vsize));

        upipe_vblk->ubuf = ubuf_pic_alloc(upipe_vblk->ubuf_mgr, hsize, vsize);
        if (unlikely(!upipe_vblk->ubuf)) {
            upipe_err_va(upipe, "fail to allocate %"PRIu64"x%"PRIu64" picture",
                         hsize, vsize);
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        ubuf_pic_clear(upipe_vblk->ubuf, 0, 0, -1, -1,
                ubase_check(uref_pic_flow_get_full_range(upipe_vblk->flow_def)));
    }

    struct ubuf *ubuf = ubuf_dup(upipe_vblk->ubuf);
    if (unlikely(!ubuf)) {
        upipe_err(upipe, "fail to duplicate blank picture");
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uref_attach_ubuf(uref, ubuf);
    if (upipe_vblk->pic_attr)
        uref_attr_import(uref, upipe_vblk->pic_attr);
    if (ubase_check(uref_pic_get_progressive(flow_def)))
        uref_pic_set_progressive(uref);

    upipe_vblk_output(upipe, uref, upump_p);
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

    if (!ubase_check(uref_flow_match_def(flow_def, UREF_VOID_FLOW_DEF)) &&
        !ubase_check(upipe_vblk_check_flow_def(upipe, flow_def)))
        return UBASE_ERR_INVALID;

    struct uref *input_flow_def = uref_dup(flow_def);
    if (unlikely(!input_flow_def)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_vblk_store_flow_def_input(upipe, input_flow_def);

    if (ubase_check(uref_flow_match_def(flow_def, UREF_VOID_FLOW_DEF)))
        return UBASE_ERR_NONE;

    uint64_t hsize = 0, vsize = 0;
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &hsize));
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &vsize));

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(!flow_def_dup)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    struct urational sar;
    uref_pic_flow_clear_format(flow_def_dup);
    uref_pic_flow_copy_format(flow_def_dup, flow_def);
    uref_pic_flow_set_hsize(flow_def_dup, hsize);
    uref_pic_flow_set_vsize(flow_def_dup, vsize);
    if (likely(ubase_check(uref_pic_flow_get_sar(flow_def, &sar)))) {
        uref_pic_flow_set_sar(flow_def_dup, sar);
    } else {
        uref_pic_flow_delete_sar(flow_def_dup);
    }
    bool overscan;
    if (likely(ubase_check(uref_pic_flow_get_overscan(flow_def, &overscan)))) {
        uref_pic_flow_set_overscan(flow_def_dup, overscan);
    } else {
        uref_pic_flow_delete_overscan(flow_def_dup);
    }
    if (likely(ubase_check(uref_pic_get_progressive(flow_def)))) {
        uref_pic_set_progressive(flow_def_dup);
    } else {
        uref_pic_delete_progressive(flow_def_dup);
    }

    if (upipe_vblk->ubuf) {
        ubuf_free(upipe_vblk->ubuf);
        upipe_vblk->ubuf = NULL;
    }

    if (upipe_vblk->ubuf_mgr &&
        !ubase_check(ubuf_mgr_check(upipe_vblk->ubuf_mgr, flow_def_dup))) {
        ubuf_mgr_release(upipe_vblk->ubuf_mgr);
        upipe_vblk->ubuf_mgr = NULL;
        upipe_vblk_require_flow_format(upipe, flow_def_dup);
    }
    else
        upipe_vblk_store_flow_def(upipe, flow_def_dup);

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
    if (upipe_vblk->ubuf)
        ubuf_free(upipe_vblk->ubuf);
    if (upipe_vblk->pic_attr)
        uref_free(upipe_vblk->pic_attr);
    if (!uref)
        return UBASE_ERR_NONE;

    if (!uref->mgr) {
        uref_free(uref);
        return UBASE_ERR_INVALID;
    }
    upipe_vblk->pic_attr = uref_alloc_control(uref->mgr);
    if (!upipe_vblk->pic_attr)
        return UBASE_ERR_ALLOC;
    int ret = uref_attr_import(upipe_vblk->pic_attr, uref);
    if (unlikely(!ubase_check(ret))) {
        uref_free(upipe_vblk->pic_attr);
        upipe_vblk->pic_attr = NULL;
        uref_free(uref);
        return ret;
    }
    upipe_vblk->ubuf = uref->ubuf;
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

    if (flow_format)
        upipe_vblk_store_flow_def(upipe, flow_format);

    if (!upipe_vblk->flow_def)
        return UBASE_ERR_NONE;

    if (!upipe_vblk->ubuf_mgr &&
        urequest_get_opaque(&upipe_vblk->flow_format_request,
                            struct upipe *) != upipe) {
        upipe_vblk_require_flow_format(upipe, uref_dup(upipe_vblk->flow_def));
        return UBASE_ERR_NONE;
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
