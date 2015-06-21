/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
 * @short Upipe swscale (ffmpeg) module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/upipe.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-swscale/upipe_sws.h>
#include <upipe-av/upipe_av_pixfmt.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <libavutil/opt.h>
#include <libswscale/swscale.h>

/** @hidden */
static bool upipe_sws_handle(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p);
/** @hidden */
static int upipe_sws_check(struct upipe *upipe, struct uref *flow_format);

/** upipe_sws structure with swscale parameters */
struct upipe_sws {
    /** refcount management structure */
    struct urefcount urefcount;

    /** input flow */
    struct uref *flow_def_input;
    /** attributes added by the pipe */
    struct uref *flow_def_attr;
    /** output pipe */
    struct upipe *output;
    /** output flow */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** swscale flags */
    int flags;
    /** swscale image conversion context [0] for progressive, [1,2] interlaced */
    struct SwsContext *convert_ctx[3];
    /** input pixel format */
    enum PixelFormat input_pix_fmt;
    /** requested output pixel format */
    enum PixelFormat output_pix_fmt;
    /** input chroma map */
    const char *input_chroma_map[UPIPE_AV_MAX_PLANES];
    /** output chroma map */
    const char *output_chroma_map[UPIPE_AV_MAX_PLANES];
    /** input colorspace */
    int input_colorspace;
    /** output colorspace */
    int output_colorspace;
    /** input color range */
    int input_color_range;
    /** output color range */
    int output_color_range;
    /** true if the we already tried to set the colorspace, but failed at it */
    bool colorspace_invalid;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_sws, upipe, UPIPE_SWS_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_sws, urefcount, upipe_sws_free)
UPIPE_HELPER_FLOW(upipe_sws, "pic.");
UPIPE_HELPER_OUTPUT(upipe_sws, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_sws, flow_def_input, flow_def_attr)
UPIPE_HELPER_UBUF_MGR(upipe_sws, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_sws_check,
                      upipe_sws_register_output_request,
                      upipe_sws_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_sws, urefs, nb_urefs, max_urefs, blockers, upipe_sws_handle)

/** @internal @This converts Upipe color space to sws color space.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return sws color space
 */
static int upipe_sws_convert_color(struct upipe *upipe, struct uref *flow_def)
{
    int colorspace = -1;
    const char *matrix_coefficients;
    if (ubase_check(uref_pic_flow_get_matrix_coefficients(flow_def,
                    &matrix_coefficients))) {
        if (!strcmp(matrix_coefficients, "bt709"))
            colorspace = SWS_CS_ITU709;
        else if (!strcmp(matrix_coefficients, "fcc"))
            colorspace = SWS_CS_FCC;
        else if (!strcmp(matrix_coefficients, "smpte170m"))
            colorspace = SWS_CS_SMPTE170M;
        else if (!strcmp(matrix_coefficients, "smpte240m"))
            colorspace = SWS_CS_SMPTE240M;
        else
            upipe_warn_va(upipe, "unknown color space %s", matrix_coefficients);
    }
    return colorspace;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_sws_handle(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_sws_store_flow_def(upipe, NULL);
        uref = upipe_sws_store_flow_def_input(upipe, uref);
        struct urational dar;
        if (ubase_check(uref_pic_flow_get_dar(uref, &dar)))
            uref_pic_flow_infer_sar(uref, dar);
        upipe_sws_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_sws->flow_def == NULL)
        return false;

    size_t input_hsize, input_vsize;
    if (!ubase_check(uref_pic_size(uref, &input_hsize, &input_vsize, NULL))) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return true;
    }

    int progressive = ubase_check(uref_pic_get_progressive(uref)) ? 1 : 0;

    uint64_t output_hsize, output_vsize;
    if (!ubase_check(uref_pic_flow_get_hsize(upipe_sws->flow_def_attr, &output_hsize)) ||
        !ubase_check(uref_pic_flow_get_vsize(upipe_sws->flow_def_attr, &output_vsize))) {
        /* comes handy in case of format conversion with no rescaling */
        output_hsize = input_hsize;
        output_vsize = input_vsize;
    }

    int i;
    for (i = 0; i < 3; i++) {
        upipe_sws->convert_ctx[i] = sws_getCachedContext(upipe_sws->convert_ctx[i],
                    input_hsize, input_vsize >> !!i, upipe_sws->input_pix_fmt,
                    output_hsize, output_vsize >> !!i, upipe_sws->output_pix_fmt,
                    upipe_sws->flags, NULL, NULL, NULL);

        if (unlikely(upipe_sws->convert_ctx[i] == NULL)) {
            upipe_err(upipe, "sws_getContext failed");
            uref_free(uref);
            return true;
        }

        if (upipe_sws->colorspace_invalid)
            continue;

        int in_full, out_full, brightness, contrast, saturation;
        const int *inv_table, *table;

        if (unlikely(sws_getColorspaceDetails(upipe_sws->convert_ctx[i],
                        (int **)&inv_table, &in_full, (int **)&table, &out_full,
                        &brightness, &contrast, &saturation) < 0)) {
            upipe_warn(upipe, "unable to set color space data");
            upipe_sws->colorspace_invalid = true;
            continue;
        }

        if (upipe_sws->input_colorspace != -1)
            inv_table = sws_getCoefficients(upipe_sws->input_colorspace);
        if (upipe_sws->input_color_range != -1)
            in_full = upipe_sws->input_color_range;
        if (upipe_sws->output_colorspace != -1)
            table = sws_getCoefficients(upipe_sws->output_colorspace);
        if (upipe_sws->output_color_range != -1)
            out_full = upipe_sws->output_color_range;

        if (unlikely(sws_setColorspaceDetails(upipe_sws->convert_ctx[i],
                        inv_table, in_full, table, out_full,
                        brightness, contrast, saturation) < 0)) {
            upipe_warn(upipe, "unable to set color space data");
            upipe_sws->colorspace_invalid = true;
        }
    }

    upipe_verbose_va(upipe, "%s -> %s",
        av_get_pix_fmt_name(upipe_sws->input_pix_fmt),
        av_get_pix_fmt_name(upipe_sws->output_pix_fmt));

    /* map input */
    const uint8_t *input_planes[UPIPE_AV_MAX_PLANES + 1];
    int input_strides[UPIPE_AV_MAX_PLANES + 1];
    for (i = 0; i < UPIPE_AV_MAX_PLANES &&
                upipe_sws->input_chroma_map[i] != NULL; i++) {
        const uint8_t *data;
        size_t stride;
        if (unlikely(!ubase_check(uref_pic_plane_read(uref,
                                          upipe_sws->input_chroma_map[i],
                                          0, 0, -1, -1, &data)) ||
                     !ubase_check(uref_pic_plane_size(uref,
                                          upipe_sws->input_chroma_map[i],
                                          &stride, NULL, NULL, NULL)))) {
            upipe_warn(upipe, "invalid buffer received");
            uref_free(uref);
            return true;
        }
        input_planes[i] = data;
        input_strides[i] = stride * (1+!progressive);
        upipe_verbose_va(upipe, "input_stride[%d] %d",
                         i, input_strides[i]);
    }
    input_planes[i] = NULL;
    input_strides[i] = 0;

    /* allocate dest ubuf */
    struct ubuf *ubuf = ubuf_pic_alloc(upipe_sws->ubuf_mgr,
                                       output_hsize, output_vsize);
    if (unlikely(ubuf == NULL)) {
        for (i = 0; i < UPIPE_AV_MAX_PLANES &&
                    upipe_sws->input_chroma_map[i] != NULL; i++)
            uref_pic_plane_unmap(uref, upipe_sws->input_chroma_map[i],
                                 0, 0, -1, -1);
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    /* map output */
    uint8_t *output_planes[UPIPE_AV_MAX_PLANES + 1];
    int output_strides[UPIPE_AV_MAX_PLANES + 1];
    for (i = 0; i < UPIPE_AV_MAX_PLANES &&
                upipe_sws->output_chroma_map[i] != NULL; i++) {
        uint8_t *data;
        size_t stride;
        if (unlikely(!ubase_check(ubuf_pic_plane_write(ubuf,
                                           upipe_sws->output_chroma_map[i],
                                           0, 0, -1, -1, &data)) ||
                     !ubase_check(ubuf_pic_plane_size(ubuf,
                                          upipe_sws->output_chroma_map[i],
                                          &stride, NULL, NULL, NULL)))) {
            upipe_warn(upipe, "invalid buffer received");
            ubuf_free(ubuf);
            uref_free(uref);
            return true;
        }
        output_planes[i] = data;
        output_strides[i] = stride * (1+!progressive);
        upipe_verbose_va(upipe, "output_stride[%d] %d",
                         i, output_strides[i]);
    }
    output_planes[i] = NULL;
    output_strides[i] = 0;

    /* fire ! */
    int ret = 0, ret2 = 1;
    if (progressive) {
        ret = sws_scale(upipe_sws->convert_ctx[0],
                        input_planes, input_strides, 0, input_vsize,
                        output_planes, output_strides);
    }
    else {
        ret = sws_scale(upipe_sws->convert_ctx[1],
                        input_planes, input_strides, 0, (input_vsize+1)/2,
                        output_planes, output_strides);

        for (i = 0; i < UPIPE_AV_MAX_PLANES && input_planes[i]; i++) {
                input_planes[i] += input_strides[i] >> 1;
        }
        for (i = 0; i < UPIPE_AV_MAX_PLANES && output_planes[i]; i++) {
                output_planes[i] += output_strides[i] >> 1;
        }

        ret2 = sws_scale(upipe_sws->convert_ctx[2],
                         input_planes, input_strides, 0, input_vsize/2,
                         output_planes, output_strides);
    }

    /* unmap pictures */
    for (i = 0; i < UPIPE_AV_MAX_PLANES &&
                upipe_sws->input_chroma_map[i] != NULL; i++)
        uref_pic_plane_unmap(uref, upipe_sws->input_chroma_map[i],
                             0, 0, -1, -1);
    for (i = 0; i < UPIPE_AV_MAX_PLANES &&
                upipe_sws->output_chroma_map[i] != NULL; i++)
        ubuf_pic_plane_unmap(ubuf, upipe_sws->output_chroma_map[i],
                             0, 0, -1, -1);

    /* clean and attach */
    if (unlikely(ret <= 0) || unlikely(ret2 <= 0)) {
        upipe_warn(upipe, "error during sws conversion");
        ubuf_free(ubuf);
        uref_free(uref);
        return true;
    }
    uref_attach_ubuf(uref, ubuf);
    upipe_sws_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_sws_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    if (!upipe_sws_check_input(upipe)) {
        upipe_sws_hold_input(upipe, uref);
        upipe_sws_block_input(upipe, upump_p);
    } else if (!upipe_sws_handle(upipe, uref, upump_p)) {
        upipe_sws_hold_input(upipe, uref);
        upipe_sws_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_sws_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_sws_store_flow_def(upipe, flow_format);

    if (upipe_sws->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_sws_check_input(upipe);
    upipe_sws_output_input(upipe);
    upipe_sws_unblock_input(upipe);
    if (was_buffered && upipe_sws_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_sws_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This requires a ubuf manager by proxy, and amends the flow
 * format.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_sws_amend_ubuf_mgr(struct upipe *upipe,
                                    struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);

    uint64_t align;
    if (!ubase_check(uref_pic_flow_get_align(flow_format, &align)) || !align)
        uref_pic_flow_set_align(flow_format, 16);

    if (align % 16) {
        align = align * 16 / ubase_gcd(align, 16);
        uref_pic_flow_set_align(flow_format, align);
    }

    struct urequest ubuf_mgr_request;
    urequest_set_opaque(&ubuf_mgr_request, request);
    urequest_init_ubuf_mgr(&ubuf_mgr_request, flow_format,
                           upipe_sws_provide_output_proxy, NULL);
    upipe_throw_provide_request(upipe, &ubuf_mgr_request);
    urequest_clean(&ubuf_mgr_request);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_sws_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))

    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    if ((upipe_sws->input_pix_fmt =
                upipe_av_pixfmt_from_flow_def(flow_def, NULL,
                            upipe_sws->input_chroma_map)) == AV_PIX_FMT_NONE ||
        !sws_isSupportedInput(upipe_sws->input_pix_fmt)) {
        upipe_err(upipe, "incompatible flow def");
        uref_dump(flow_def, upipe->uprobe);
        return UBASE_ERR_EXTERNAL;
    }
    upipe_sws->input_colorspace = upipe_sws_convert_color(upipe, flow_def);
    upipe_sws->input_color_range =
        ubase_check(uref_pic_flow_get_full_range(flow_def)) ? 1 : 0;

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uint64_t input_hsize, input_vsize, output_hsize, output_vsize;
    if (ubase_check(uref_pic_flow_get_hsize(flow_def, &input_hsize)) &&
        ubase_check(uref_pic_flow_get_vsize(flow_def, &input_vsize)) &&
        ubase_check(uref_pic_flow_get_hsize(upipe_sws->flow_def_attr,
                                            &output_hsize)) &&
        ubase_check(uref_pic_flow_get_vsize(upipe_sws->flow_def_attr,
                                            &output_vsize)) &&
        (input_hsize != output_hsize || input_vsize != output_vsize)) {

        uint64_t hsize_visible;
        if (input_hsize != output_hsize &&
            ubase_check(uref_pic_flow_get_hsize_visible(flow_def,
                                                        &hsize_visible))) {
            hsize_visible *= output_hsize;
            hsize_visible /= input_hsize;
            UBASE_FATAL(upipe, uref_pic_flow_set_hsize_visible(flow_def,
                        hsize_visible))
                upipe_err_va(upipe, "meuh %"PRIu64, hsize_visible);
        }

        uint64_t vsize_visible;
        if (input_vsize != output_vsize &&
            ubase_check(uref_pic_flow_get_vsize_visible(flow_def,
                                                        &vsize_visible))) {
            vsize_visible *= output_vsize;
            vsize_visible /= input_vsize;
            UBASE_FATAL(upipe, uref_pic_flow_set_vsize_visible(flow_def,
                        vsize_visible))
        }

        struct urational sar;
        if (!ubase_check(uref_pic_flow_get_sar(upipe_sws->flow_def_attr,
                                               &sar)) &&
            ubase_check(uref_pic_flow_get_sar(flow_def, &sar))) {
            sar.num *= input_hsize * output_vsize;
            sar.den *= input_vsize * output_hsize;
            urational_simplify(&sar);
            UBASE_FATAL(upipe, uref_pic_flow_set_sar(flow_def, sar))
        }
    }

    if (upipe_sws->input_pix_fmt == AV_PIX_FMT_YUV420P) {
        av_opt_set_int(upipe_sws->convert_ctx[0], "src_v_chr_pos", 128, 0);
        av_opt_set_int(upipe_sws->convert_ctx[1], "src_v_chr_pos", 64, 0);
        av_opt_set_int(upipe_sws->convert_ctx[2], "src_v_chr_pos", 192, 0);
    }

    if (upipe_sws->output_pix_fmt == AV_PIX_FMT_YUV420P) {
        av_opt_set_int(upipe_sws->convert_ctx[0], "dst_v_chr_pos", 128, 0);
        av_opt_set_int(upipe_sws->convert_ctx[1], "dst_v_chr_pos", 64, 0);
        av_opt_set_int(upipe_sws->convert_ctx[2], "dst_v_chr_pos", 192, 0);
    }
    upipe_sws->colorspace_invalid = false;

    upipe_input(upipe, flow_def, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the swscale flags.
 *
 * @param upipe description structure of the pipe
 * @param flags_p filled in with the swscale flags
 * @return an error code
 */
static int _upipe_sws_get_flags(struct upipe *upipe, int *flags_p)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    *flags_p = upipe_sws->flags;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the swscale flags.
 *
 * @param upipe description structure of the pipe
 * @param flags swscale flags
 * @return an error code
 */
static int _upipe_sws_set_flags(struct upipe *upipe, int flags)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    upipe_sws->flags = flags;
    upipe_dbg_va(upipe, "setting flags to %d", flags);
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
static int upipe_sws_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_sws_amend_ubuf_mgr(upipe, request);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_sws_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_sws_free_output_proxy(upipe, request);
        }

        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sws_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_sws_set_output(upipe, output);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_sws_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_sws_set_flow_def(upipe, flow);
        }

        /* specific commands */
        case UPIPE_SWS_GET_FLAGS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SWS_SIGNATURE)
            int *flags_p = va_arg(args, int *);
            return _upipe_sws_get_flags(upipe, flags_p);
        }
        case UPIPE_SWS_SET_FLAGS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SWS_SIGNATURE)
            int flags = va_arg(args, int);
            return _upipe_sws_set_flags(upipe, flags);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a swscale pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_sws_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_sws_alloc_flow(mgr, uprobe, signature,
                                               args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    upipe_sws->output_pix_fmt = upipe_av_pixfmt_from_flow_def(flow_def, NULL,
                                                 upipe_sws->output_chroma_map);
    if (upipe_sws->output_pix_fmt == AV_PIX_FMT_NONE ||
        !sws_isSupportedOutput(upipe_sws->output_pix_fmt)) {
        uref_free(flow_def);
        upipe_sws_free_flow(upipe);
        return NULL;
    }

    upipe_sws_init_urefcount(upipe);
    upipe_sws_init_ubuf_mgr(upipe);
    upipe_sws_init_output(upipe);
    upipe_sws_init_flow_def(upipe);
    upipe_sws_init_input(upipe);
    upipe_sws->colorspace_invalid = false;

    memset(upipe_sws->convert_ctx, 0, sizeof(upipe_sws->convert_ctx));
    for (int i = 0; i < 3; i++) {
        upipe_sws->convert_ctx[i] = sws_alloc_context();
        if (!upipe_sws->convert_ctx[i]) {
            uref_free(flow_def);
            upipe_sws_free_flow(upipe);
            goto fail;
        }
    }

    upipe_sws->flags = SWS_FULL_CHR_H_INP | SWS_ACCURATE_RND | SWS_LANCZOS;

    upipe_throw_ready(upipe);

    upipe_sws->output_colorspace = upipe_sws_convert_color(upipe, flow_def);
    upipe_sws->output_color_range =
        ubase_check(uref_pic_flow_get_full_range(flow_def)) ? 1 : 0;
    UBASE_FATAL(upipe, uref_pic_flow_set_align(flow_def, 16))
    upipe_sws_store_flow_def_attr(upipe, flow_def);
    return upipe;

fail:
    for (int i = 0; i < 3; i++) {
        if (likely(upipe_sws->convert_ctx[i]))
            sws_freeContext(upipe_sws->convert_ctx[i]);
        upipe_sws->convert_ctx[i] = NULL;
    }

    return NULL;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_sws_free(struct upipe *upipe)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    for (int i = 0; i < 3; i++) {
        if (likely(upipe_sws->convert_ctx[i]))
            sws_freeContext(upipe_sws->convert_ctx[i]);
        upipe_sws->convert_ctx[i] = NULL;
    }

    upipe_throw_dead(upipe);
    upipe_sws_clean_input(upipe);
    upipe_sws_clean_output(upipe);
    upipe_sws_clean_flow_def(upipe);
    upipe_sws_clean_ubuf_mgr(upipe);
    upipe_sws_clean_urefcount(upipe);
    upipe_sws_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_sws_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SWS_SIGNATURE,

    .upipe_alloc = upipe_sws_alloc,
    .upipe_input = upipe_sws_input,
    .upipe_control = upipe_sws_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for swscale pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sws_mgr_alloc(void)
{
    return &upipe_sws_mgr;
}

