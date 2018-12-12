/*
 * Copyright (C) 2017-2018 OpenHeadend S.A.R.L.
 *
 * Authors: Clément Vasseur
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
 * @short Upipe x265 module
 */

#define _GNU_SOURCE

#include <upipe/uclock.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/udict.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_dump.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic_flow_formats.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/ubuf_block.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_flow_format.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_flow_def_check.h>
#include <upipe-x265/upipe_x265.h>
#include <upipe-framers/uref_h26x_flow.h>
#include <upipe-framers/uref_h26x.h>
#include <upipe-framers/uref_h265.h>
#include <upipe-framers/uref_mpgv.h>
#include <upipe-framers/upipe_h26x_common.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>

/* fix undef warnings in x265.h */
#ifndef ENABLE_LIBVMAF
# define ENABLE_LIBVMAF 0
#endif
#ifndef _MSC_VER
# define _MSC_VER 0
#endif
#ifndef X265_DEPTH
# define X265_DEPTH 0
#endif

#include <x265.h>
#include <bitstream/itu/h265.h>

#define EXPECTED_FLOW "pic."
#define OUT_FLOW "block.hevc.pic."

// speed control presets
//     ultrafast
//   0 superfast
//   1 veryfast
//   2 faster
//   3 fast
//   4 medium (default)
//   5 slow
//   6 slower
//   7 veryslow
//     placebo

struct option {
    char *name;
    char *value;
    struct uchain uchain;
};

UBASE_FROM_TO(option, uchain, uchain, uchain);

/** @internal upipe_x265 private structure */
struct upipe_x265 {
    /** refcount management structure */
    struct urefcount urefcount;

    /** api function pointers for the current bit depth */
    const x265_api *api;
    /** x265 encoder */
    x265_encoder *encoder;
    /** x265 params */
    x265_param params;
    /** configured bit depth */
    int bit_depth;
    /** configured preset */
    char *preset;
    /** configured tune */
    char *tune;
    /** configured profile */
    char *profile;
    /** configured options */
    struct uchain options;
    /** latency in the input flow */
    uint64_t input_latency;
    /** buffered frames count */
    int latency_frames;
    /** supposed latency of the packets when leaving the encoder */
    uint64_t initial_latency;
    /** true if the existing slice types must be enforced */
    bool slice_type_enforce;
    /** true if delayed frames are available */
    bool delayed_frames;

    /** uclock */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** flow format request */
    struct urequest flow_format_request;
    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** input flow */
    struct uref *flow_def_input;
    /** attributes added by the pipe */
    struct uref *flow_def_attr;
    /** structure to check input flow def */
    struct uref *flow_def_check;
    /** requested flow */
    struct uref *flow_def_requested;
    /** requested headers */
    bool headers_requested;
    /** requested encaps */
    enum uref_h26x_encaps encaps_requested;
    /** output flow */
    struct uref *flow_def;
    /** output pipe */
    struct upipe *output;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** input pixel format */
    enum pixel_format {
        PIX_FMT_YUV420P,
        PIX_FMT_YUV422P,
        PIX_FMT_YUV444P,
        PIX_FMT_YUV420P10LE,
        PIX_FMT_YUV422P10LE,
        PIX_FMT_YUV444P10LE,
        PIX_FMT_YUV420P12LE,
        PIX_FMT_YUV422P12LE,
        PIX_FMT_YUV444P12LE,
    } pixel_format;

    /** input width */
    int width;
    /** input height */
    int height;
    /** input aspect ratio idc (0 = unspecified) */
    int aspect_ratio_idc;
    /** input sar width (if aspect_ratio_idc == X265_EXTENDED_SAR) */
    int sar_width;
    /** input sar height (if aspect_ratio_idc == X265_EXTENDED_SAR) */
    int sar_height;
    /** input overscan */
    enum {
        OVERSCAN_UNDEF,
        OVERSCAN_SHOW,
        OVERSCAN_CROP,
    } overscan;
    /** color space */
    int color_space;

    /** last DTS */
    uint64_t last_dts;
    /** last DTS (system time) */
    uint64_t last_dts_sys;
    /** drift rate */
    struct urational drift_rate;
    /** last input PTS */
    uint64_t input_pts;
    /** last input PTS (system time) */
    uint64_t input_pts_sys;

    /** latency introduced by speedcontrol */
    uint64_t sc_latency;
    /** current speedcontrol preset (0-7) */
    int sc_preset;
    /** maximum speedcontrol preset (0-7) */
    int sc_max_preset;
    /** speedcontrol buffer size */
    int64_t sc_buffer_size;
    /** speedcontrol buffer fullness */
    int64_t sc_buffer_fill;

    /** public structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_x265_check_ubuf_mgr(struct upipe *upipe,
                                     struct uref *flow_format);
/** @hidden */
static int upipe_x265_check_flow_format(struct upipe *upipe,
                                        struct uref *flow_format);
/** @hidden */
static bool upipe_x265_handle(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_x265, upipe, UPIPE_X265_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_x265, urefcount, upipe_x265_free)
UPIPE_HELPER_VOID(upipe_x265);
UPIPE_HELPER_OUTPUT(upipe_x265, output, flow_def, output_state, request_list)
UPIPE_HELPER_INPUT(upipe_x265, urefs, nb_urefs, max_urefs, blockers, upipe_x265_handle)
UPIPE_HELPER_FLOW_FORMAT(upipe_x265, flow_format_request,
                         upipe_x265_check_flow_format,
                         upipe_x265_register_output_request,
                         upipe_x265_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_x265, flow_def_input, flow_def_attr)
UPIPE_HELPER_FLOW_DEF_CHECK(upipe_x265, flow_def_check)
UPIPE_HELPER_UBUF_MGR(upipe_x265, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_x265_check_ubuf_mgr,
                      upipe_x265_register_output_request,
                      upipe_x265_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_x265, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

/** @internal @This describes the supported pixel formats. */
static const struct uref_pic_flow_format *pixel_format_desc[] = {
    [PIX_FMT_YUV420P] = &uref_pic_flow_format_yuv420p,
    [PIX_FMT_YUV422P] = &uref_pic_flow_format_yuv422p,
    [PIX_FMT_YUV444P] = &uref_pic_flow_format_yuv444p,
    [PIX_FMT_YUV420P10LE] = &uref_pic_flow_format_yuv420p10le,
    [PIX_FMT_YUV422P10LE] = &uref_pic_flow_format_yuv422p10le,
    [PIX_FMT_YUV444P10LE] = &uref_pic_flow_format_yuv444p10le,
    [PIX_FMT_YUV420P12LE] = &uref_pic_flow_format_yuv420p12le,
    [PIX_FMT_YUV422P12LE] = &uref_pic_flow_format_yuv422p12le,
    [PIX_FMT_YUV444P12LE] = &uref_pic_flow_format_yuv444p12le,
};

/** @internal @This gets the pixel format from the flow definition.
 *
 * @param flow_def flow definition
 * @param pixel_format pointer filled with the pixel format
 * @return an error code
 */
static int get_pixel_format(struct uref *flow_def,
                            enum pixel_format *pixel_format)
{
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(pixel_format_desc); i++)
        if (ubase_check(uref_pic_flow_check_format(flow_def,
                                                   pixel_format_desc[i]))) {
            *pixel_format = i;
            return UBASE_ERR_NONE;
        }
    return UBASE_ERR_INVALID;
}

/** @internal @This get the bit depth from a pixel format.
 *
 * @param pixel_format pixel format
 * @return the bit depth or -1
 */
static int pixel_format_to_bit_depth(enum pixel_format pixel_format)
{
    return pixel_format_desc[pixel_format]->planes[0].mpixel_bits;
}

/** @internal @This gets the color space from pixel format.
 *
 * @param pixel_format pixel format
 * @return the color spave
 */
static int pixel_format_to_color_space(enum pixel_format pixel_format)
{
    const struct uref_pic_flow_format *fmt = pixel_format_desc[pixel_format];
    if (fmt->planes[1].hsub == 1 && fmt->planes[2].hsub == 1)
        return X265_CSP_I444;
    else if (fmt->planes[1].vsub == 1 && fmt->planes[2].vsub == 1)
        return X265_CSP_I422;
    return X265_CSP_I420;
}

/** @interal @This returns the pixel format corresponding to the given bit
 * depth and color space.
 *
 * @param bit_depth bit depth
 * @param color_space color spave
 * @return a pixel format
 */
static enum pixel_format pixel_format_find(int bit_depth, int color_space)
{
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(pixel_format_desc); i++)
        if (pixel_format_to_bit_depth(i) == bit_depth &&
            pixel_format_to_color_space(i) == color_space)
            return i;
    return PIX_FMT_YUV420P;
}

/** @internal @This sets the content of an x265 option.
 * upipe_x265_reconfigure must be called to apply changes.
 *
 * @param upipe description structure of the pipe
 * @param params params structure where the option should be set
 * @param name name of the option
 * @param value content of the option
 * @return an error code
 */
static int upipe_x265_set_option(struct upipe *upipe,
                                 x265_param *params,
                                 const char *name,
                                 const char *value)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);

    if (unlikely(upipe_x265->api == NULL || name == NULL))
        return UBASE_ERR_INVALID;

    int ret = upipe_x265->api->param_parse(params, name, value);
    if (unlikely(ret < 0)) {
        const char *reason = "";
        if (ret == X265_PARAM_BAD_NAME)
            reason = ": bad name";
        else if (ret == X265_PARAM_BAD_VALUE)
            reason = ": bad value";
        upipe_err_va(upipe, "cannot set option %s=%s%s",
                     name, value ?: "true", reason);
        return UBASE_ERR_EXTERNAL;
    }

    return UBASE_ERR_NONE;
}

static const char *overscan_to_str(int overscan)
{
    switch (overscan) {
        case OVERSCAN_UNDEF: return "undef";
        case OVERSCAN_SHOW: return "show";
        case OVERSCAN_CROP: return "crop";
    }
    return NULL;
}

static void apply_params(struct upipe *upipe, x265_param *params)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    struct uref *flow_def = upipe_x265->flow_def_input;
    const char *value;

    if (flow_def == NULL)
        return;

    params->logLevel = X265_LOG_DEBUG;

    struct urational fps = {0, 0};
    if (likely(ubase_check(uref_pic_flow_get_fps(flow_def, &fps)))) {
        params->fpsNum = fps.num;
        params->fpsDenom = fps.den;
    }

    params->vui.aspectRatioIdc = upipe_x265->aspect_ratio_idc;
    if (params->vui.aspectRatioIdc == X265_EXTENDED_SAR) {
        params->vui.sarWidth = upipe_x265->sar_width;
        params->vui.sarHeight = upipe_x265->sar_height;
    }

    upipe_x265_set_option(upipe, params, "overscan",
                          overscan_to_str(upipe_x265->overscan));

    params->sourceWidth = upipe_x265->width;
    params->sourceHeight = upipe_x265->height;
    params->internalCsp = upipe_x265->color_space;

    params->interlaceMode =
        !ubase_check(uref_pic_get_progressive(flow_def));

    upipe_x265_set_option(upipe, params, "range",
                          ubase_check(uref_pic_flow_get_full_range(flow_def)) ?
                          "full" : "limited");

    if (ubase_check(uref_pic_flow_get_video_format(flow_def, &value)))
        upipe_x265_set_option(upipe, params, "videoformat", value);

    if (ubase_check(uref_pic_flow_get_colour_primaries(flow_def, &value)))
        upipe_x265_set_option(upipe, params, "colorprim", value);

    if (ubase_check(uref_pic_flow_get_transfer_characteristics(flow_def, &value)))
        upipe_x265_set_option(upipe, params, "transfer", value);

    if (ubase_check(uref_pic_flow_get_matrix_coefficients(flow_def, &value)))
        upipe_x265_set_option(upipe, params, "colormatrix", value);
}

static int setup_encoder_params(struct upipe *upipe)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    const struct x265_api *api = upipe_x265->api;
    x265_param params;

    if (unlikely(api == NULL))
        return UBASE_ERR_NONE;

    if (unlikely(api->param_default_preset(&params,
                                           upipe_x265->preset,
                                           upipe_x265->tune) < 0)) {
        upipe_err_va(upipe, "cannot set default preset=%s tune=%s",
                     upipe_x265->preset, upipe_x265->tune);
        return UBASE_ERR_EXTERNAL;
    }

    apply_params(upipe, &params);

    struct uchain *uchain;
    ulist_foreach(&upipe_x265->options, uchain) {
        struct option *option = option_from_uchain(uchain);
        int ret = upipe_x265_set_option(upipe, &params,
                                        option->name,
                                        option->value);
        if (unlikely(!ubase_check(ret)))
            return ret;
    }

    if (unlikely(api->param_apply_profile(&params, upipe_x265->profile) < 0)) {
        upipe_err_va(upipe, "cannot apply profile %s", upipe_x265->profile);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_x265->params = params;
    return UBASE_ERR_NONE;
}

/** @internal @This reconfigures encoder with updated parameters
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int _upipe_x265_reconfigure(struct upipe *upipe)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);

    /* encoder not initialized yet, params will be applied later */
    if (unlikely(upipe_x265->encoder == NULL))
        return UBASE_ERR_NONE;

    int ret = upipe_x265->api->encoder_reconfig(upipe_x265->encoder,
                                                &upipe_x265->params);
    return ret != 0 ? UBASE_ERR_EXTERNAL : UBASE_ERR_NONE;
}

/** @internal @This sets default parameters for specified preset.
 *
 * @param upipe description structure of the pipe
 * @param preset x265 preset
 * @param tune x265 tuning
 * @return an error code
 */
static int _upipe_x265_set_default_preset(struct upipe *upipe,
                                          const char *preset,
                                          const char *tune)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);

    if (unlikely(upipe_x265->api == NULL))
        return UBASE_ERR_INVALID;
    int ret = upipe_x265->api->param_default_preset(&upipe_x265->params,
                                                    preset, tune);
    return unlikely(ret < 0) ? UBASE_ERR_EXTERNAL : UBASE_ERR_NONE;
}

/** @This switches upipe-x265 into speedcontrol mode, with the given latency
 * (size of sc buffer).
 *
 * @param upipe description structure of the pipe
 * @param latency size (in units of a 27 MHz) of the speedcontrol buffer
 * @return an error code
 */
static int _upipe_x265_set_sc_latency(struct upipe *upipe, uint64_t sc_latency)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    upipe_x265->sc_latency = sc_latency;
    upipe_dbg_va(upipe, "activating speed control with latency %"PRIu64" ms",
                 sc_latency * 1000 / UCLOCK_FREQ);
    return UBASE_ERR_NONE;
}

/** @This sets the slice type enforcement mode (true or false).
 *
 * @param upipe description structure of the pipe
 * @param enforce true if the incoming slice types must be enforced
 * @return an error code
 */
static int _upipe_x265_set_slice_type_enforce(struct upipe *upipe, bool enforce)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    upipe_x265->slice_type_enforce = enforce;
    upipe_dbg_va(upipe, "%sactivating slice type enforcement",
                 enforce ? "" : "de");
    return UBASE_ERR_NONE;
}

/** @internal @This allocates a filter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_x265_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature,
                                      va_list args)
{
    struct upipe *upipe = upipe_x265_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);

    upipe_x265->api = NULL;
    upipe_x265->encoder = NULL;
    upipe_x265->bit_depth = 0;
    upipe_x265->preset = NULL;
    upipe_x265->tune = NULL;
    upipe_x265->profile = NULL;
    ulist_init(&upipe_x265->options);
    upipe_x265->input_latency = 0;
    upipe_x265->latency_frames = 3;
    upipe_x265->initial_latency = 0;
    upipe_x265->sc_latency = 0;
    upipe_x265->slice_type_enforce = false;
    upipe_x265->delayed_frames = true;

    upipe_x265_init_urefcount(upipe);
    upipe_x265_init_ubuf_mgr(upipe);
    upipe_x265_init_uclock(upipe);
    upipe_x265_init_output(upipe);
    upipe_x265_init_input(upipe);
    upipe_x265_init_flow_format(upipe);
    upipe_x265_init_flow_def(upipe);
    upipe_x265_init_flow_def_check(upipe);
    upipe_x265->flow_def_requested = NULL;
    upipe_x265->headers_requested = false;
    upipe_x265->encaps_requested = UREF_H26X_ENCAPS_ANNEXB;
    upipe_x265->aspect_ratio_idc = 0;
    upipe_x265->overscan = OVERSCAN_UNDEF;

    upipe_x265->last_dts = UINT64_MAX;
    upipe_x265->last_dts_sys = UINT64_MAX;
    upipe_x265->drift_rate.num = upipe_x265->drift_rate.den = 1;
    upipe_x265->input_pts = UINT64_MAX;
    upipe_x265->input_pts_sys = UINT64_MAX;

    upipe_throw_ready(upipe);
    return upipe;
}

static void speedcontrol_update(struct upipe *upipe)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    int set;

    float filled = (float) upipe_x265->sc_buffer_fill /
        upipe_x265->sc_buffer_size;

    if (filled < 0.25)
        set = 0;
    else if (filled > 1)
        set = upipe_x265->sc_max_preset;
    else
        set = upipe_x265->sc_max_preset * (filled - 0.25) / 0.75;

    if (set < 0)
        set = 0;
    if (set > upipe_x265->sc_max_preset)
        set = upipe_x265->sc_max_preset;

    if (set != upipe_x265->sc_preset) {
        const char *preset = x265_preset_names[set + 1];

        upipe_verbose_va(upipe, "apply speedcontrol preset %s", preset);

        if (!ubase_check(_upipe_x265_set_default_preset(upipe, preset,
                                                        upipe_x265->tune)))
            upipe_err_va(upipe, "x265 set_default_preset failed");

        apply_params(upipe, &upipe_x265->params);

        struct uchain *uchain;
        ulist_foreach(&upipe_x265->options, uchain) {
            struct option *option = option_from_uchain(uchain);
            upipe_x265_set_option(upipe, &upipe_x265->params,
                                  option->name, option->value);
        }

        if (ubase_check(_upipe_x265_reconfigure(upipe)))
            upipe_x265->sc_preset = set;
    }
}

/** @internal @This opens x265 encoder.
 *
 * @param upipe description structure of the pipe
 * @param width image width
 * @param height image height
 */
static bool upipe_x265_open(struct upipe *upipe, int width, int height)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    x265_param *params = &upipe_x265->params;

    upipe_x265->width = width;
    upipe_x265->height = height;

    if (!ubase_check(setup_encoder_params(upipe)))
        return false;

    /* reconfigure encoder with new parameters and return */
    if (unlikely(upipe_x265->encoder)) {
        if (!ubase_check(_upipe_x265_reconfigure(upipe)))
            return false;
    } else {
        /* open encoder */
        upipe_x265->encoder = upipe_x265->api->encoder_open(params);
        if (unlikely(!upipe_x265->encoder))
            return false;
    }

    /* sync pipe parameters with internal copy */
    upipe_x265->api->encoder_parameters(upipe_x265->encoder, params);

    /* flow definition */
    struct uref *flow_def_attr = upipe_x265_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def_attr == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    const char *def = OUT_FLOW;
    if (unlikely(!ubase_check(uref_flow_set_def(flow_def_attr, def)))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    UBASE_FATAL(upipe, uref_flow_set_complete(flow_def_attr))

    /* set octetrate for CBR streams */
    if (params->rc.bitrate > 0) {
        uref_block_flow_set_octetrate(flow_def_attr,
                                      (uint64_t)params->rc.bitrate * 125);
        if (params->rc.vbvBufferSize > 0)
            uref_block_flow_set_buffer_size(flow_def_attr,
                (uint64_t)params->rc.vbvBufferSize * 125);

        uint64_t max_octetrate, max_bs;
        int tier = params->bHighTier;
        switch (params->levelIdc) {
            case 10:
                max_octetrate = 128000 / 8;
                max_bs = 350000 / 8;
                break;
            case 20:
                max_octetrate = max_bs = 1500000 / 8;
                break;
            case 21:
                max_octetrate = max_bs = 3000000 / 8;
                break;
            case 30:
                max_octetrate = max_bs = 6000000 / 8;
                break;
            case 31:
                max_octetrate = max_bs = 10000000 / 8;
                break;
            case 40:
                max_octetrate = max_bs = tier ? (30000000 / 8) : (12000000 / 8);
                break;
            case 41:
                max_octetrate = max_bs = tier ? (50000000 / 8) : (20000000 / 8);
                break;
            case 50:
                max_octetrate = max_bs = tier ? (100000000 / 8) : (25000000 / 8);
                break;
            case 51:
                max_octetrate = max_bs = tier ? (160000000 / 8) : (40000000 / 8);
                break;
            case 52:
                max_octetrate = max_bs = tier ? (240000000 / 8) : (60000000 / 8);
                break;
            case 60:
                max_octetrate = max_bs = tier ? (240000000 / 8) : (60000000 / 8);
                break;
            case 61:
                max_octetrate = max_bs = tier ? (480000000 / 8) : (120000000 / 8);
                break;
            default:
                upipe_warn_va(upipe, "unknown level %d", params->levelIdc);
                /* fallthrough */
            case 62:
                max_octetrate = max_bs = tier ? (800000000 / 8) : (240000000 / 8);
                break;
        }
        UBASE_FATAL(upipe, uref_block_flow_set_max_octetrate(flow_def_attr,
                    max_octetrate))
        UBASE_FATAL(upipe, uref_block_flow_set_max_buffer_size(flow_def_attr,
                    max_bs))
    }

    /* Find out if flow def attributes have changed. */
    if (!upipe_x265_check_flow_def_attr(upipe, flow_def_attr)) {
        upipe_x265_store_flow_def(upipe, NULL);
        uref_free(upipe_x265->flow_def_requested);
        upipe_x265->flow_def_requested = NULL;
        struct uref *flow_def =
            upipe_x265_store_flow_def_attr(upipe, flow_def_attr);
        if (flow_def != NULL) {
            uref_pic_flow_clear_format(flow_def);
            upipe_x265_require_flow_format(upipe, flow_def);
        }
    } else
        uref_free(flow_def_attr);

    return true;
}

/** @internal @This closes x265 encoder.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_x265_close(struct upipe *upipe)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    if (upipe_x265->encoder) {
        while (upipe_x265->delayed_frames)
            upipe_x265_handle(upipe, NULL, NULL);

        upipe_notice(upipe, "closing encoder");
        upipe_x265->api->encoder_close(upipe_x265->encoder);
    }
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_x265_build_flow_def(struct upipe *upipe)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    assert(upipe_x265->flow_def_requested != NULL);

    struct uref *flow_def = uref_dup(upipe_x265->flow_def_requested);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* find latency */
    upipe_notice_va(upipe, "latency: %d frames", upipe_x265->latency_frames);
    uint64_t latency = upipe_x265->input_latency +
            (uint64_t)upipe_x265->latency_frames * UCLOCK_FREQ *
            upipe_x265->params.fpsDenom / upipe_x265->params.fpsNum;

    upipe_x265->initial_latency = latency;
    latency += upipe_x265->sc_latency;
    uref_clock_set_latency(flow_def, latency);

    /* global headers (extradata) */
    if (upipe_x265->headers_requested) {
        uint32_t nal_num;
        x265_nal *nals;
        int ret = upipe_x265->api->encoder_headers(upipe_x265->encoder, &nals, &nal_num);
        if (unlikely(ret < 0)) {
            upipe_warn(upipe, "unable to get encoder headers");
        } else {
            UBASE_FATAL(upipe,
                uref_flow_set_headers(flow_def, nals[0].payload, ret));
        }
    }
    UBASE_FATAL(upipe,
            uref_h26x_flow_set_encaps(flow_def, upipe_x265->encaps_requested));

    upipe_x265_store_flow_def(upipe, flow_def);
}

static int params_overscan(x265_param *params)
{
    if (!params->vui.bEnableOverscanInfoPresentFlag)
        return OVERSCAN_UNDEF;
    return params->vui.bEnableOverscanAppropriateFlag ?
        OVERSCAN_CROP : OVERSCAN_SHOW;
}

/** @internal @This checks incoming pic against cached parameters.
 *
 * @param upipe description structure of the pipe
 * @param width image width
 * @param height image height
 * @return true if parameters update needed
 */
static inline bool upipe_x265_need_update(struct upipe *upipe,
                                          int width, int height)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    x265_param *params = &upipe_x265->params;
    return upipe_x265->width != width ||
           upipe_x265->height != height ||
           params->vui.aspectRatioIdc != upipe_x265->aspect_ratio_idc ||
           (params->vui.aspectRatioIdc == X265_EXTENDED_SAR &&
            (params->vui.sarWidth != upipe_x265->sar_width ||
             params->vui.sarHeight != upipe_x265->sar_height)) ||
           params_overscan(params) != upipe_x265->overscan ||
           params->internalCsp != upipe_x265->color_space;
}

/** @internal @This fetches aspect ratio information from flow def.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 */
static void upipe_x265_get_aspect_ratio(struct upipe *upipe,
                                        struct uref *flow_def)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    struct urational sar;

    static const struct {
        int idc;
        int num;
        int den;
    } sar_to_idc[] = {
        {  1,   1,  1, },
        {  2,  12, 11, },
        {  3,  10, 11, },
        {  4,  16, 11, },
        {  5,  40, 33, },
        {  6,  24, 11, },
        {  7,  20, 11, },
        {  8,  32, 11, },
        {  9,  80, 33, },
        { 10,  18, 11, },
        { 11,  15, 11, },
        { 12,  64, 33, },
        { 13, 160, 99, },
        { 14,   4,  3, },
        { 15,   3,  2, },
        { 16,   2,  1, },
    };

    if (!ubase_check(uref_pic_flow_get_sar(flow_def, &sar))) {
        // unspecified aspect ratio
        upipe_x265->aspect_ratio_idc = 0;
        return;
    }

    // look for predefined aspect ratio
    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(sar_to_idc); i++)
        if (sar.num == sar_to_idc[i].num &&
            sar.den == sar_to_idc[i].den) {
            upipe_x265->aspect_ratio_idc = sar_to_idc[i].idc;
            return;
        }

    // extended aspect ratio
    upipe_x265->aspect_ratio_idc = X265_EXTENDED_SAR;
    upipe_x265->sar_width = sar.num;
    upipe_x265->sar_height = sar.den;
}

/** @internal @This processes pictures.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 * @return true if the packet was handled
 */
static bool upipe_x265_handle(struct upipe *upipe,
                              struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);

    if (unlikely(uref != NULL && ubase_check(uref_flow_get_def(uref, NULL)))) {
        upipe_x265->input_latency = 0;
        uref_clock_get_latency(uref, &upipe_x265->input_latency);
        upipe_x265_store_flow_def(upipe, NULL);
        uref_free(upipe_x265->flow_def_requested);
        upipe_x265->flow_def_requested = NULL;

        upipe_x265_get_aspect_ratio(upipe, uref);

        bool overscan;
        if (!ubase_check(uref_pic_flow_get_overscan(uref, &overscan)))
            upipe_x265->overscan = OVERSCAN_UNDEF;
        else
            upipe_x265->overscan = overscan ?
                OVERSCAN_CROP : OVERSCAN_SHOW;

        uint64_t hsize, vsize;
        if (ubase_check(uref_pic_flow_get_hsize(uref, &hsize)) &&
            ubase_check(uref_pic_flow_get_vsize(uref, &vsize))) {
            upipe_x265->width = hsize;
            upipe_x265->height = vsize;
        }

        uref = upipe_x265_store_flow_def_input(upipe, uref);
        if (uref != NULL) {
            uref_pic_flow_clear_format(uref);
            upipe_x265_require_flow_format(upipe, uref);
        }

        /* setup encoder params */
        int bit_depth = upipe_x265->bit_depth;
        int ret = get_pixel_format(upipe_x265->flow_def_input,
                                   &upipe_x265->pixel_format);
        if (unlikely(!ubase_check(ret)))
            goto err_invalid;
        if (bit_depth == 0)
            bit_depth = pixel_format_to_bit_depth(upipe_x265->pixel_format);
        upipe_x265->color_space =
            pixel_format_to_color_space(upipe_x265->pixel_format);

        upipe_x265->api = x265_api_get(bit_depth);
        if (unlikely(upipe_x265->api == NULL))
            goto err_invalid;

        ret = setup_encoder_params(upipe);
        if (unlikely(!ubase_check(ret)))
            goto err_invalid;

        return true;

err_invalid:
        upipe_throw_fatal(upipe, UBASE_ERR_INVALID);
        return true;
    }

    static const char *const chromas_list[][3] = {
        [PIX_FMT_YUV420P]     = {"y8", "u8", "v8"},
        [PIX_FMT_YUV422P]     = {"y8", "u8", "v8"},
        [PIX_FMT_YUV444P]     = {"y8", "u8", "v8"},
        [PIX_FMT_YUV420P10LE] = {"y10l", "u10l", "v10l"},
        [PIX_FMT_YUV422P10LE] = {"y10l", "u10l", "v10l"},
        [PIX_FMT_YUV444P10LE] = {"y10l", "u10l", "v10l"},
        [PIX_FMT_YUV420P12LE] = {"y12l", "u12l", "v12l"},
        [PIX_FMT_YUV422P12LE] = {"y12l", "u12l", "v12l"},
        [PIX_FMT_YUV444P12LE] = {"y12l", "u12l", "v12l"},
    };
    const char * const *chromas = chromas_list[upipe_x265->pixel_format];
    size_t width, height;
    x265_picture pic;
    x265_nal *nals = NULL;
    int i, size = 0, header_size = 0;
    uint32_t nals_num = 0;
    struct ubuf *ubuf_block;
    uint8_t *buf = NULL;
    bool needopen = false;
    int ret = 0;

    /* init x265 picture */
    upipe_x265->api->picture_init(&upipe_x265->params, &pic);

    if (upipe_x265->sc_latency &&
        likely(upipe_x265->encoder))
        speedcontrol_update(upipe);

    if (likely(uref)) {
        pic.userData = uref;
        pic.bitDepth = pixel_format_to_bit_depth(upipe_x265->pixel_format);
        pic.colorSpace = pixel_format_to_color_space(upipe_x265->pixel_format);

        uref_pic_size(uref, &width, &height, NULL);

        /* open encoder if not already opened or if update needed */
        if (unlikely(!upipe_x265->encoder)) {
            needopen = true;
        } else if (unlikely(upipe_x265_need_update(upipe, width, height))) {
            x265_param *params = &upipe_x265->params;
            upipe_notice_va(upipe, "Flow parameters changed, reconfiguring encoder "
                            "(%d:%zu, %d:%zu, %d/%d/%d:%d/%d/%d, %s:%s)",
                upipe_x265->width, width,
                upipe_x265->height, height,
                params->vui.aspectRatioIdc,
                params->vui.aspectRatioIdc == X265_EXTENDED_SAR ? params->vui.sarWidth : 0,
                params->vui.aspectRatioIdc == X265_EXTENDED_SAR ? params->vui.sarHeight : 0,
                upipe_x265->aspect_ratio_idc,
                upipe_x265->aspect_ratio_idc == X265_EXTENDED_SAR ? upipe_x265->sar_width : 0,
                upipe_x265->aspect_ratio_idc == X265_EXTENDED_SAR ? upipe_x265->sar_height : 0,
                overscan_to_str(params_overscan(params)),
                overscan_to_str(upipe_x265->overscan));
            needopen = true;
        }
        if (unlikely(needopen)) {
            if (unlikely(!upipe_x265_open(upipe, width, height))) {
                upipe_err(upipe, "Could not open encoder");
                uref_free(uref);
                return true;
            }
        }
        if (upipe_x265->flow_def_requested == NULL)
            return false;

        uref_clock_get_rate(uref, &upipe_x265->drift_rate);
        uref_clock_get_pts_prog(uref, &upipe_x265->input_pts);
        uref_clock_get_pts_sys(uref, &upipe_x265->input_pts_sys);

        pic.pts = upipe_x265->input_pts;

        pic.sliceType = X265_TYPE_AUTO;
        if (upipe_x265->slice_type_enforce) {
            uint8_t type;
            if (ubase_check(uref_h265_get_type(uref, &type))) {
                switch (type) {
                    case H265SLI_TYPE_P:
                        pic.sliceType = X265_TYPE_P;
                        break;
                    case H265SLI_TYPE_B:
                        pic.sliceType = X265_TYPE_B;
                        break;
                    case H265SLI_TYPE_I:
                        pic.sliceType = upipe_x265->params.bOpenGOP ?
                            X265_TYPE_I :
                            X265_TYPE_IDR;
                        break;
                }
            }
        }

        /* map */
        for (i = 0; i < 3; i++) {
            size_t stride;
            const uint8_t *plane;
            if (unlikely(!ubase_check(uref_pic_plane_size(uref, chromas[i], &stride,
                                              NULL, NULL, NULL)) ||
                         !ubase_check(uref_pic_plane_read(uref, chromas[i], 0, 0, -1, -1,
                                              &plane)))) {
                upipe_err_va(upipe, "Could not read origin chroma %s",
                             chromas[i]);
                uref_free(uref);
                return true;
            }
            pic.stride[i] = stride;
            pic.planes[i] = (void *)plane;
        }

        /* encode frame */
        ret = upipe_x265->api->encoder_encode(upipe_x265->encoder,
                                              &nals, &nals_num,
                                              &pic, &pic);

        /* unmap */
        for (i = 0; i < 3; i++)
            uref_pic_plane_unmap(uref, chromas[i], 0, 0, -1, -1);

        ubuf_free(uref_detach_ubuf(uref));

        /* delayed frame, increase latency */
        if (unlikely(ret == 0))
                upipe_x265->latency_frames++;

    } else {
        /* NULL uref, flushing delayed frame */
        ret = upipe_x265->api->encoder_encode(upipe_x265->encoder,
                                              &nals, &nals_num,
                                              NULL, &pic);
        if (ret <= 0)
            upipe_x265->delayed_frames = false;
    }

    if (unlikely(ret < 0)) {
        upipe_warn(upipe, "Error encoding frame");
        uref_free(uref);
        return true;
    } else if (unlikely(ret == 0)) {
        upipe_verbose(upipe, "No nal units returned");
        return true;
    }

    /* get uref back */
    uref = pic.userData;
    assert(uref);

    for (i = 0; i < nals_num; i++) {
        size += nals[i].sizeBytes;
        if (nals[i].type == NAL_UNIT_VPS ||
            nals[i].type == NAL_UNIT_SPS ||
            nals[i].type == NAL_UNIT_PPS ||
            nals[i].type == NAL_UNIT_ACCESS_UNIT_DELIMITER ||
            nals[i].type == NAL_UNIT_FILLER_DATA)
            header_size += nals[i].sizeBytes;
    }

    /* alloc ubuf, map, copy, unmap */
    ubuf_block = ubuf_block_alloc(upipe_x265->ubuf_mgr, size);
    if (unlikely(ubuf_block == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }
    ubuf_block_write(ubuf_block, 0, &size, &buf);
    memcpy(buf, nals[0].payload, size);
    ubuf_block_unmap(ubuf_block, 0);
    uref_attach_ubuf(uref, ubuf_block);
    uref_block_set_header_size(uref, header_size);

    /* NAL offsets */
    uint64_t offset = 0;
    for (i = 0; i < nals_num - 1; i++) {
        offset += nals[i].sizeBytes;
        uref_h26x_set_nal_offset(uref, offset, i);
    }

    /* optionally convert NAL encapsulation */
    enum uref_h26x_encaps encaps = upipe_x265->params.bAnnexB ?
        UREF_H26X_ENCAPS_ANNEXB : UREF_H26X_ENCAPS_LENGTH4;
    /* no need for annex B header because if annexb is requested, there
     * will be no conversion */
    int err = upipe_h26xf_convert_frame(uref,
                                        encaps,
                                        upipe_x265->encaps_requested,
                                        upipe_x265->ubuf_mgr,
                                        NULL);
    if (!ubase_check(err)) {
        upipe_warn(upipe, "invalid NAL encapsulation conversion");
        upipe_throw_error(upipe, err);
    }

    /* set dts */
    uint64_t dts_pts_delay = pic.pts - pic.dts;
    uref_clock_set_dts_pts_delay(uref, dts_pts_delay);
    uref_clock_delete_cr_dts_delay(uref);

    /* rebase to dts as we're in encoded domain now */
    uint64_t dts = UINT64_MAX;
    if ((!ubase_check(uref_clock_get_dts_prog(uref, &dts)) ||
         dts < upipe_x265->last_dts) &&
        upipe_x265->last_dts != UINT64_MAX) {
        upipe_warn_va(upipe, "DTS prog in the past, resetting (%"PRIu64" ms)",
                      (upipe_x265->last_dts - dts) * 1000 / UCLOCK_FREQ);
        dts = upipe_x265->last_dts + 1;
        uref_clock_set_dts_prog(uref, dts);
    } else
        uref_clock_rebase_dts_prog(uref);

    uint64_t dts_sys = UINT64_MAX;
    if (dts != UINT64_MAX &&
        upipe_x265->input_pts != UINT64_MAX &&
        upipe_x265->input_pts_sys != UINT64_MAX) {
        dts_sys = (int64_t)upipe_x265->input_pts_sys +
            ((int64_t)dts - (int64_t)upipe_x265->input_pts) *
            (int64_t)upipe_x265->drift_rate.num /
            (int64_t)upipe_x265->drift_rate.den;
        uref_clock_set_dts_sys(uref, dts_sys);
    } else if (!ubase_check(uref_clock_get_dts_sys(uref, &dts_sys)) ||
        (upipe_x265->last_dts_sys != UINT64_MAX &&
               dts_sys < upipe_x265->last_dts_sys)) {
        upipe_warn_va(upipe,
                      "DTS sys in the past, resetting (%"PRIu64" ms)",
                      (upipe_x265->last_dts_sys - dts_sys) * 1000 /
                      UCLOCK_FREQ);
        dts_sys = upipe_x265->last_dts_sys + 1;
        uref_clock_set_dts_sys(uref, dts_sys);
    } else
        uref_clock_rebase_dts_sys(uref);

    uref_clock_rebase_dts_orig(uref);
    uref_clock_set_rate(uref, upipe_x265->drift_rate);

    upipe_x265->last_dts = dts;
    upipe_x265->last_dts_sys = dts_sys;

    if (dts_sys != UINT64_MAX &&
        upipe_x265->uclock != NULL &&
        upipe_x265->sc_latency) {
        /* speedcontrol sync */
        upipe_x265->sc_buffer_fill = dts_sys +
            upipe_x265->initial_latency +
            upipe_x265->sc_latency -
            uclock_now(upipe_x265->uclock);
    }

    if (IS_X265_TYPE_I(pic.sliceType))
        uref_flow_set_random(uref);

    if (upipe_x265->flow_def == NULL)
        upipe_x265_build_flow_def(upipe);

    upipe_x265_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_x265_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    if (!upipe_x265_check_input(upipe)) {
        upipe_x265_hold_input(upipe, uref);
        upipe_x265_block_input(upipe, upump_p);
    } else if (!upipe_x265_handle(upipe, uref, upump_p)) {
        upipe_x265_hold_input(upipe, uref);
        upipe_x265_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This receives the result of a flow format request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_x265_check_flow_format(struct upipe *upipe,
                                        struct uref *flow_format)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_INVALID;

    upipe_x265->headers_requested =
        ubase_check(uref_flow_get_global(flow_format));
    upipe_x265->encaps_requested = uref_h26x_flow_infer_encaps(flow_format);
    bool annexb = upipe_x265->encaps_requested == UREF_H26X_ENCAPS_ANNEXB;
    if (upipe_x265->params.bAnnexB != annexb) {
        upipe_x265->params.bAnnexB = annexb;
        _upipe_x265_reconfigure(upipe);
    }

    upipe_x265_store_flow_def(upipe, NULL);
    uref_free(upipe_x265->flow_def_requested);
    upipe_x265->flow_def_requested = NULL;
    upipe_x265_require_ubuf_mgr(upipe, flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_x265_check_ubuf_mgr(struct upipe *upipe,
                                     struct uref *flow_format)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_NONE; /* should not happen */

    uref_free(upipe_x265->flow_def_requested);
    upipe_x265->flow_def_requested = flow_format;

    bool was_buffered = !upipe_x265_check_input(upipe);
    upipe_x265_output_input(upipe);
    upipe_x265_unblock_input(upipe);
    if (was_buffered && upipe_x265_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_x265_input. */
        upipe_release(upipe);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the flow definition is not handled
 */
static int upipe_x265_set_flow_def(struct upipe *upipe,
                                   struct uref *flow_def)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);

    if (unlikely(flow_def == NULL))
        return UBASE_ERR_INVALID;

    uint8_t macropixel;
    if (unlikely(!ubase_check(uref_flow_match_def(flow_def, EXPECTED_FLOW)) ||
                 !ubase_check(uref_pic_flow_get_macropixel(flow_def, &macropixel)) ||
                 macropixel != 1))
        return UBASE_ERR_INVALID;

    /* check bit depth */
    enum pixel_format pixel_format;
    int ret = get_pixel_format(flow_def, &pixel_format);
    if (unlikely(!ubase_check(ret)))
        return ret;
    int bit_depth = upipe_x265->bit_depth;
    int input_bit_depth = pixel_format_to_bit_depth(pixel_format);
    if (bit_depth == 0)
        bit_depth = input_bit_depth;
    else if (bit_depth != input_bit_depth)
        return UBASE_ERR_INVALID;

    const struct x265_api *api = x265_api_get(bit_depth);
    if (unlikely(api == NULL)) {
        upipe_err_va(upipe, "unsupported bit depth %d", bit_depth);
        return UBASE_ERR_INVALID;
    }

    /* Extract relevant attributes to flow def check. */
    struct uref *flow_def_check =
        upipe_x265_alloc_flow_def_check(upipe, flow_def);
    if (unlikely(flow_def_check == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    struct urational fps;
    uint64_t hsize, vsize;
    if (!ubase_check(uref_pic_flow_get_fps(flow_def, &fps) ||
        !ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize)) ||
        !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize)))) {
        upipe_err(upipe, "incompatible flow def");
        uref_free(flow_def_check);
        return UBASE_ERR_INVALID;
    }

    if (unlikely(!ubase_check(uref_pic_flow_copy_format(flow_def_check, flow_def)) ||
                 !ubase_check(uref_pic_flow_set_fps(flow_def_check, fps)) ||
                 !ubase_check(uref_pic_flow_set_hsize(flow_def_check, hsize)) ||
                 !ubase_check(uref_pic_flow_set_vsize(flow_def_check, vsize)))) {
        uref_free(flow_def_check);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (upipe_x265->flow_def_check != NULL) {
        /* Die if the attributes changed. */
        if (!upipe_x265_check_flow_def_check(upipe, flow_def_check)) {
            uref_free(flow_def_check);
            return UBASE_ERR_BUSY;
        }
        uref_free(flow_def_check);

    } else {
        if (upipe_x265->sc_latency) {
            /* init speedcontrol */
            upipe_x265->sc_buffer_size = upipe_x265->sc_latency;
            upipe_x265->sc_buffer_fill = 0;
            upipe_x265->sc_max_preset = 4;
        }

        upipe_x265_store_flow_def_check(upipe, flow_def_check);
    }

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_input(upipe, flow_def, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int _upipe_x265_provide_flow_format(struct upipe *upipe,
                                           struct urequest *request)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);

    enum pixel_format pixel_format;
    if (unlikely(!ubase_check(get_pixel_format(flow_format, &pixel_format)))) {
        uref_pic_flow_set_yuv420p(flow_format);
        return urequest_provide_flow_format(request, flow_format);
    }

    int bit_depth = pixel_format_to_bit_depth(pixel_format);
    int color_space = pixel_format_to_color_space(pixel_format);
    if (upipe_x265->bit_depth == 0 || upipe_x265->bit_depth == bit_depth)
        return urequest_provide_flow_format(request, flow_format);

    pixel_format = pixel_format_find(upipe_x265->bit_depth, color_space);
    const struct uref_pic_flow_format *fmt = pixel_format_desc[pixel_format];
    uref_pic_flow_set_format(flow_format, fmt);
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_x265_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);

    switch (command) {
        case UPIPE_ATTACH_UCLOCK:
            upipe_x265_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_throw_provide_request(upipe, request);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return _upipe_x265_provide_flow_format(upipe, request);
            return upipe_x265_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_x265_free_output_proxy(upipe, request);
        }

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_x265_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_GET_FLOW_DEF:
            return upipe_x265_control_output(upipe, command, args);

        case UPIPE_X265_RECONFIG: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X265_SIGNATURE)
            int ret = setup_encoder_params(upipe);
            if (unlikely(!ubase_check(ret)))
                return ret;
            return _upipe_x265_reconfigure(upipe);
        }
        case UPIPE_X265_SET_DEFAULT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X265_SIGNATURE)
            int bit_depth = va_arg(args, int);
            upipe_x265->bit_depth = bit_depth;
            return UBASE_ERR_NONE;
        }
        case UPIPE_X265_SET_DEFAULT_PRESET: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X265_SIGNATURE)
            const char *preset = va_arg(args, const char *);
            const char *tune = va_arg(args, const char *);
            char *preset_dup = preset ? strdup(preset) : NULL;
            char *tune_dup = tune ? strdup(tune) : NULL;
            if ((preset && !preset_dup) || (tune && !tune_dup)) {
                free(preset_dup);
                free(tune_dup);
                return UBASE_ERR_ALLOC;
            }
            free(upipe_x265->preset);
            upipe_x265->preset = preset_dup;
            free(upipe_x265->tune);
            upipe_x265->tune = tune_dup;
            return UBASE_ERR_NONE;
        }
        case UPIPE_X265_SET_PROFILE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X265_SIGNATURE)
            const char *profile = va_arg(args, const char *);
            char *profile_dup = profile ? strdup(profile) : NULL;
            if (profile && !profile_dup)
                return UBASE_ERR_ALLOC;
            free(upipe_x265->profile);
            upipe_x265->profile = profile_dup;
            return UBASE_ERR_NONE;
        }
        case UPIPE_SET_OPTION: {
            const char *name = va_arg(args, const char *);
            const char *value = va_arg(args, const char *);
            struct option *option = NULL;
            struct uchain *uchain;
            ulist_foreach(&upipe_x265->options, uchain) {
                struct option *opt = option_from_uchain(uchain);
                if (!strcmp(opt->name, name)) {
                    option = opt;
                    break;
                }
            }
            char *value_dup = value ? strdup(value) : NULL;
            if (value && !value_dup)
                return UBASE_ERR_ALLOC;
            if (option == NULL) {
                option = malloc(sizeof (*option));
                if (option == NULL) {
                    free(value_dup);
                    return UBASE_ERR_ALLOC;
                }
                option->name = strdup(name);
                if (option->name == NULL) {
                    free(value_dup);
                    free(option);
                    return UBASE_ERR_ALLOC;
                }
            } else {
                ulist_delete(uchain);
                free(option->value);
            }
            option->value = value_dup;
            uchain_init(&option->uchain);
            ulist_add(&upipe_x265->options, &option->uchain);
            upipe_dbg_va(upipe, "set %s=%s", name, value ?: "true");
            return UBASE_ERR_NONE;
        }
        case UPIPE_X265_SET_SC_LATENCY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X265_SIGNATURE)
            uint64_t sc_latency = va_arg(args, uint64_t);
            return _upipe_x265_set_sc_latency(upipe, sc_latency);
        }
        case UPIPE_X265_SET_SLICE_TYPE_ENFORCE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X265_SIGNATURE)
            bool enforce = va_arg(args, int);
            return _upipe_x265_set_slice_type_enforce(upipe, enforce);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees all recorded options
 *
 * @param upipe description structure of the pipe
 */
static void upipe_x265_free_options(struct upipe *upipe)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);
    struct uchain *uchain, *uchain_tmp;

    ulist_delete_foreach(&upipe_x265->options, uchain, uchain_tmp) {
        struct option *option = option_from_uchain(uchain);
        ulist_delete(uchain);
        free(option->name);
        free(option->value);
        free(option);
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_x265_free(struct upipe *upipe)
{
    struct upipe_x265 *upipe_x265 = upipe_x265_from_upipe(upipe);

    upipe_x265_close(upipe);
    upipe_x265_free_options(upipe);
    free(upipe_x265->preset);
    free(upipe_x265->tune);
    free(upipe_x265->profile);
    upipe_throw_dead(upipe);
    upipe_x265_clean_uclock(upipe);
    upipe_x265_clean_ubuf_mgr(upipe);
    upipe_x265_clean_input(upipe);
    upipe_x265_clean_output(upipe);
    uref_free(upipe_x265->flow_def_requested);
    upipe_x265_clean_flow_format(upipe);
    upipe_x265_clean_flow_def(upipe);
    upipe_x265_clean_flow_def_check(upipe);
    upipe_x265_clean_urefcount(upipe);
    upipe_x265_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_x265_mgr = {
    .refcount = NULL,
    .signature = UPIPE_X265_SIGNATURE,
    .upipe_alloc = upipe_x265_alloc,
    .upipe_input = upipe_x265_input,
    .upipe_control = upipe_x265_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for x265 pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_x265_mgr_alloc(void)
{
    return &upipe_x265_mgr;
}

/** @This frees process globals
 */
void upipe_x265_cleanup(void)
{
    x265_cleanup();
}
