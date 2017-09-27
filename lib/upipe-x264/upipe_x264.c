/*
 * Copyright (C) 2013-2017 OpenHeadend S.A.R.L.
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
 * @short Upipe x264 module
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
#include <upipe-x264/upipe_x264.h>
#include <upipe-framers/uref_h26x_flow.h>
#include <upipe-framers/uref_h26x.h>
#include <upipe-framers/uref_h264.h>
#include <upipe-framers/uref_mpgv.h>
#include <upipe-framers/upipe_h26x_common.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>

#include <x264.h>
#include <bitstream/mpeg/h264.h>
#include <bitstream/mpeg/mp2v.h>

#define EXPECTED_FLOW "pic."
#define OUT_FLOW "block.h264.pic."
#define OUT_FLOW_MPEG2 "block.mpeg2video.pic."

/** @internal upipe_x264 private structure */
struct upipe_x264 {
    /** refcount management structure */
    struct urefcount urefcount;

    /** x264 encoder */
    x264_t *encoder;
    /** x264 params */
    x264_param_t params;
    /** latency in the input flow */
    uint64_t input_latency;
    /** supposed latency of the packets when leaving the encoder */
    uint64_t initial_latency;
    /** latency introduced by speedcontrol */
    uint64_t sc_latency;
    /** true if the existing slice types must be enforced */
    bool slice_type_enforce;

    /** x264 "PTS" */
    uint64_t x264_ts;

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

    /** input SAR */
    struct urational sar;
    /** input overscan */
    int overscan;
    /** MPEG-2 aspect ratio information */
    uint8_t mpeg2_ar;

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

    /** public structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_x264_check_ubuf_mgr(struct upipe *upipe,
                                     struct uref *flow_format);
/** @hidden */
static int upipe_x264_check_flow_format(struct upipe *upipe,
                                        struct uref *flow_format);
/** @hidden */
static bool upipe_x264_handle(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_x264, upipe, UPIPE_X264_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_x264, urefcount, upipe_x264_free)
UPIPE_HELPER_VOID(upipe_x264);
UPIPE_HELPER_OUTPUT(upipe_x264, output, flow_def, output_state, request_list)
UPIPE_HELPER_INPUT(upipe_x264, urefs, nb_urefs, max_urefs, blockers, upipe_x264_handle)
UPIPE_HELPER_FLOW_FORMAT(upipe_x264, flow_format_request,
                         upipe_x264_check_flow_format,
                         upipe_x264_register_output_request,
                         upipe_x264_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_x264, flow_def_input, flow_def_attr)
UPIPE_HELPER_FLOW_DEF_CHECK(upipe_x264, flow_def_check)
UPIPE_HELPER_UBUF_MGR(upipe_x264, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_x264_check_ubuf_mgr,
                      upipe_x264_register_output_request,
                      upipe_x264_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_x264, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

/** @internal loglevel map from x264 to uprobe_log */
static const enum uprobe_log_level loglevel_map[] = {
    [X264_LOG_ERROR] = UPROBE_LOG_ERROR,
    [X264_LOG_WARNING] = UPROBE_LOG_WARNING,
    [X264_LOG_INFO] = UPROBE_LOG_NOTICE,
    [X264_LOG_DEBUG] = UPROBE_LOG_VERBOSE
};

/** @internal @This sends x264 logs to uprobe_log
 * @param upipe description structure of the pipe
 * @param loglevel x264 loglevel
 * @param format string format
 * @param args arguments
 */
static void upipe_x264_log(void *_upipe, int loglevel,
                           const char *format, va_list args)
{
    struct upipe *upipe = _upipe;
    char *string = NULL, *end = NULL;
    if (unlikely(loglevel < 0 || loglevel > X264_LOG_DEBUG)) {
        return;
    }

    int ret = vasprintf(&string, format, args);
    if (unlikely(ret < 0) || unlikely(!string)) {
        return;
    }
    end = string + strlen(string) - 1;
    if (isspace(*end)) {
        *end = '\0';
    }
    upipe_log(upipe, loglevel_map[loglevel], string);
    free(string);
}

/** @internal @This checks whether mpeg2 encoding is enabled
 * @param upipe description structure of the pipe
 * @return true if mpeg2 enabled
 */
static inline bool upipe_x264_mpeg2_enabled(struct upipe *upipe)
{
#ifdef HAVE_X264_MPEG2
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    return !!upipe_x264->params.b_mpeg2;
#else
    return false;
#endif
}

/** @internal @This reconfigures encoder with updated parameters
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int _upipe_x264_reconfigure(struct upipe *upipe)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    int ret;
    if (unlikely(!upipe_x264->encoder)) {
        return UBASE_ERR_UNHANDLED;
    }
    ret = x264_encoder_reconfig(upipe_x264->encoder, &upipe_x264->params);
    return ( (ret < 0) ? UBASE_ERR_EXTERNAL : UBASE_ERR_NONE );
}

/** @internal @This reset parameters to default
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int _upipe_x264_set_default(struct upipe *upipe)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    x264_param_default(&upipe_x264->params);
    return UBASE_ERR_NONE;
}

/** @internal @This resets parameters to mpeg2 default
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int _upipe_x264_set_default_mpeg2(struct upipe *upipe)
{
#ifndef HAVE_X264_MPEG2
    return UBASE_ERR_EXTERNAL;
#else
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    x264_param_default_mpeg2(&upipe_x264->params);
    return UBASE_ERR_NONE;
#endif
}

/** @internal @This sets default parameters for specified preset.
 *
 * @param upipe description structure of the pipe
 * @param preset x264 preset
 * @param tuning x264 tuning
 * @return an error code
 */
static int _upipe_x264_set_default_preset(struct upipe *upipe,
                                const char *preset, const char *tune)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    int ret;
    ret = x264_param_default_preset(&upipe_x264->params, preset, tune);
    return ( (ret < 0) ? UBASE_ERR_EXTERNAL : UBASE_ERR_NONE );
}

/** @internal @This enforces profile.
 *
 * @param upipe description structure of the pipe
 * @param profile x264 profile
 * @return an error code
 */
static int _upipe_x264_set_profile(struct upipe *upipe, const char *profile)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    int ret;
    ret = x264_param_apply_profile(&upipe_x264->params, profile);
    return ( (ret < 0) ? UBASE_ERR_EXTERNAL : UBASE_ERR_NONE );
}

/** @internal @This sets the content of an x264 option.
 * upipe_x264_reconfigure must be called to apply changes.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option
 * @return an error code
 */
static int upipe_x264_set_option(struct upipe *upipe,
                                 const char *option, const char *content)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    int ret = x264_param_parse(&upipe_x264->params, option, content);
    if (unlikely(ret < 0)) {
        upipe_err_va(upipe, "can't set option %s:%s (%d)",
                     option, content, ret);
        return UBASE_ERR_EXTERNAL;
    }
    return UBASE_ERR_NONE;
}

/** @This switches x264 into speedcontrol mode, with the given latency (size
 * of sc buffer).
 *
 * @param upipe description structure of the pipe
 * @param latency size (in units of a 27 MHz) of the speedcontrol buffer
 * @return an error code
 */
static int _upipe_x264_set_sc_latency(struct upipe *upipe, uint64_t sc_latency)
{
#ifndef HAVE_X264_OBE
    return UBASE_ERR_EXTERNAL;
#else
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    upipe_x264->sc_latency = sc_latency;
    upipe_dbg_va(upipe, "activating speed control with latency %"PRIu64" ms",
                 sc_latency * 1000 / UCLOCK_FREQ);
    return UBASE_ERR_NONE;
#endif
}

/** @This sets the slice type enforcement mode (true or false).
 *
 * @param upipe description structure of the pipe
 * @param enforce true if the incoming slice types must be enforced
 * @return an error code
 */
static int _upipe_x264_set_slice_type_enforce(struct upipe *upipe, bool enforce)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    upipe_x264->slice_type_enforce = enforce;
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
static struct upipe *upipe_x264_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_x264_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);

    upipe_x264->encoder = NULL;
    _upipe_x264_set_default(upipe);
    upipe_x264->input_latency = 0;
    upipe_x264->initial_latency = 0;
    upipe_x264->sc_latency = 0;
    upipe_x264->slice_type_enforce = false;
    upipe_x264->x264_ts = 0;

    upipe_x264_init_urefcount(upipe);
    upipe_x264_init_ubuf_mgr(upipe);
    upipe_x264_init_uclock(upipe);
    upipe_x264_init_output(upipe);
    upipe_x264_init_input(upipe);
    upipe_x264_init_flow_format(upipe);
    upipe_x264_init_flow_def(upipe);
    upipe_x264_init_flow_def_check(upipe);
    upipe_x264->flow_def_requested = NULL;
    upipe_x264->headers_requested = false;
    upipe_x264->encaps_requested = UREF_H26X_ENCAPS_ANNEXB;
    upipe_x264->sar.num = upipe_x264->sar.den = 1;
    upipe_x264->overscan = 0; /* undef */
    upipe_x264->mpeg2_ar = 1;

    upipe_x264->last_dts = UINT64_MAX;
    upipe_x264->last_dts_sys = UINT64_MAX;
    upipe_x264->drift_rate.num = upipe_x264->drift_rate.den = 1;
    upipe_x264->input_pts = UINT64_MAX;
    upipe_x264->input_pts_sys = UINT64_MAX;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This opens x264 encoder.
 *
 * @param upipe description structure of the pipe
 * @param width image width
 * @param height image height
 */
static bool upipe_x264_open(struct upipe *upipe, int width, int height)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    struct urational fps = {0, 0};
    x264_param_t *params = &upipe_x264->params;

    params->pf_log = upipe_x264_log;
    params->p_log_private = upipe;
    params->i_log_level = X264_LOG_DEBUG;
    if (likely(ubase_check(uref_pic_flow_get_fps(upipe_x264->flow_def_input,
                           &fps)))) {
        params->b_vfr_input = 0;
        params->i_fps_num = fps.num;
        params->i_fps_den = fps.den;
        params->i_timebase_num = fps.den;
        params->i_timebase_den = fps.num;
    }

#ifdef HAVE_X264_MPEG2
    if (upipe_x264_mpeg2_enabled(upipe)) {
        params->vui.i_aspect_ratio_information = upipe_x264->mpeg2_ar;
    } else
#endif
    {
        params->vui.i_sar_width = upipe_x264->sar.num;
        params->vui.i_sar_height = upipe_x264->sar.den;
        params->vui.i_overscan = upipe_x264->overscan;
    }
    params->i_width = width;
    params->i_height = height;
    if (!ubase_check(uref_pic_get_progressive(upipe_x264->flow_def_input)))
        params->b_interlaced = true;

    const char *content;
    int ret;
    if (ubase_check(uref_pic_flow_get_video_format(
                    upipe_x264->flow_def_input, &content)) &&
        (ret = x264_param_parse(&upipe_x264->params, "videoformat",
                                content)) < 0)
        upipe_err_va(upipe, "can't set option %s:%s (%d)",
                     "videoformat", content, ret);
    content =
        ubase_check(uref_pic_flow_get_full_range(upipe_x264->flow_def_input)) ?
        "1" : "0";
    if ((ret = x264_param_parse(&upipe_x264->params, "fullrange", content)) < 0)
        upipe_err_va(upipe, "can't set option %s:%s (%d)",
                     "fullrange", content, ret);
    if (ubase_check(uref_pic_flow_get_colour_primaries(
                    upipe_x264->flow_def_input, &content)) &&
        (ret = x264_param_parse(&upipe_x264->params, "colorprim", content)) < 0)
        upipe_err_va(upipe, "can't set option %s:%s (%d)",
                     "colorprim", content, ret);
    if (ubase_check(uref_pic_flow_get_transfer_characteristics(
                    upipe_x264->flow_def_input, &content)) &&
        (ret = x264_param_parse(&upipe_x264->params, "transfer", content)) < 0)
        upipe_err_va(upipe, "can't set option %s:%s (%d)",
                     "transfer", content, ret);
    if (ubase_check(uref_pic_flow_get_matrix_coefficients(
                    upipe_x264->flow_def_input, &content)) &&
        (ret = x264_param_parse(&upipe_x264->params, "colormatrix",
                                content)) < 0)
        upipe_err_va(upipe, "can't set option %s:%s (%d)",
                     "colormatrix", content, ret);

    /* reconfigure encoder with new parameters and return */
    if (unlikely(upipe_x264->encoder)) {
        if (!ubase_check(_upipe_x264_reconfigure(upipe)))
            return false;
    } else {
        /* open encoder */
        upipe_x264->encoder = x264_encoder_open(params);
        if (unlikely(!upipe_x264->encoder))
            return false;
    }

    /* sync pipe parameters with internal copy */
    x264_encoder_parameters(upipe_x264->encoder, params);

    /* flow definition */
    struct uref *flow_def_attr = upipe_x264_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def_attr == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    const char *def = OUT_FLOW;
    if (upipe_x264_mpeg2_enabled(upipe)) {
        def = OUT_FLOW_MPEG2;
    }
    if (unlikely(!ubase_check(uref_flow_set_def(flow_def_attr, def)))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    UBASE_FATAL(upipe, uref_flow_set_complete(flow_def_attr))

    /* set octetrate for CBR streams */
    if (params->rc.i_bitrate > 0) {
        uref_block_flow_set_octetrate(flow_def_attr,
                                      (uint64_t)params->rc.i_bitrate * 125);
        if (params->rc.i_vbv_buffer_size > 0)
            uref_block_flow_set_buffer_size(flow_def_attr,
                (uint64_t)params->rc.i_vbv_buffer_size * 125);

        uint64_t max_octetrate, max_bs;
#ifdef HAVE_X264_MPEG2
        if (upipe_x264_mpeg2_enabled(upipe)) {
            switch (params->i_level_idc) {
                case X264_MPEG2_LEVEL_LOW:
                    max_octetrate = 4000000 / 8;
                    max_bs = 475136 / 8;
                    break;
                default:
                    upipe_warn_va(upipe, "unknown level %"PRIu8,
                                  params->i_level_idc);
                    /* intended fall-through */
                case X264_MPEG2_LEVEL_MAIN:
                    max_octetrate = 15000000 / 8;
                    max_bs = 1835008 / 8;
                    break;
                case X264_MPEG2_LEVEL_HIGH_1440:
                    max_octetrate = 60000000 / 8;
                    max_bs = 7340032 / 8;
                    break;
                case X264_MPEG2_LEVEL_HIGH:
                    max_octetrate = 80000000 / 8;
                    max_bs = 9781248 / 8;
                    break;
                case X264_MPEG2_LEVEL_HIGHP:
                    /* ISO/IEC JTC1/SC29/WG11 MPEG2007/m14868 */
                    max_octetrate = 120000000 / 8;
                    max_bs = 14671872 / 8;
                    break;
            }
        } else
#endif
        {
            switch (params->i_level_idc) {
                case 10:
                    max_octetrate = 64000 / 8;
                    max_bs = 175000 / 8;
                    break;
                case 11:
                    max_octetrate = 192000 / 8;
                    max_bs = 500000 / 8;
                    break;
                case 12:
                    max_octetrate = 384000 / 8;
                    max_bs = 1000000 / 8;
                    break;
                case 13:
                    max_octetrate = 768000 / 8;
                    max_bs = 2000000 / 8;
                    break;
                case 20:
                    max_octetrate = 2000000 / 8;
                    max_bs = 2000000 / 8;
                    break;
                case 21:
                case 22:
                    max_octetrate = 4000000 / 8;
                    max_bs = 4000000 / 8;
                    break;
                case 30:
                    max_octetrate = 10000000 / 8;
                    max_bs = 10000000 / 8;
                    break;
                case 31:
                    max_octetrate = 14000000 / 8;
                    max_bs = 14000000 / 8;
                    break;
                case 32:
                case 40:
                    max_octetrate = 20000000 / 8;
                    max_bs = 20000000 / 8;
                    break;
                case 41:
                case 42:
                    max_octetrate = 50000000 / 8;
                    max_bs = 62500000 / 8;
                    break;
                case 50:
                    max_octetrate = 135000000 / 8;
                    max_bs = 135000000 / 8;
                    break;
                default:
                    upipe_warn_va(upipe, "unknown level %"PRIu8,
                                  params->i_level_idc);
                    /* fallthrough */
                case 51:
                case 52:
                    max_octetrate = 240000000 / 8;
                    max_bs = 240000000 / 8;
                    break;
            }
        }
        UBASE_FATAL(upipe, uref_block_flow_set_max_octetrate(flow_def_attr,
                    max_octetrate))
        UBASE_FATAL(upipe, uref_block_flow_set_max_buffer_size(flow_def_attr,
                    max_bs))
    }

    /* Find out if flow def attributes have changed. */
    if (!upipe_x264_check_flow_def_attr(upipe, flow_def_attr)) {
        upipe_x264_store_flow_def(upipe, NULL);
        uref_free(upipe_x264->flow_def_requested);
        upipe_x264->flow_def_requested = NULL;
        struct uref *flow_def =
            upipe_x264_store_flow_def_attr(upipe, flow_def_attr);
        if (flow_def != NULL) {
            uref_pic_flow_clear_format(flow_def);
            upipe_x264_require_flow_format(upipe, flow_def);
        }
    } else
        uref_free(flow_def_attr);

    return true;
}

/** @internal @This closes x264 encoder.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_x264_close(struct upipe *upipe)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    if (upipe_x264->encoder) {
        while(x264_encoder_delayed_frames(upipe_x264->encoder)) {
            upipe_x264_handle(upipe, NULL, NULL);
        }

        upipe_notice(upipe, "closing encoder");
        x264_encoder_close(upipe_x264->encoder);
    }
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_x264_build_flow_def(struct upipe *upipe)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    assert(upipe_x264->flow_def_requested != NULL);

    struct uref *flow_def = uref_dup(upipe_x264->flow_def_requested);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* find latency */
    uint64_t latency = upipe_x264->input_latency;
    int delayed = x264_encoder_maximum_delayed_frames(upipe_x264->encoder);
    if (delayed >= 0)
        latency += (uint64_t)delayed * UCLOCK_FREQ
                     * upipe_x264->params.i_fps_den
                     / upipe_x264->params.i_fps_num;
    /* add one frame for the time of encoding the current frame */
    latency += UCLOCK_FREQ * upipe_x264->params.i_fps_den /
               upipe_x264->params.i_fps_num;
    upipe_x264->initial_latency = latency;

    latency += upipe_x264->sc_latency;
    uref_clock_set_latency(flow_def, latency);

    /* global headers (extradata) */
    if (upipe_x264->headers_requested) {
        int i, ret, nal_num, size = 0;
        x264_nal_t *nals;
        ret = x264_encoder_headers(upipe_x264->encoder, &nals, &nal_num);
        if (unlikely(ret < 0)) {
            upipe_warn(upipe, "unable to get encoder headers");
        } else {
            for (i=0; i < nal_num; i++) {
                size += nals[i].i_payload;
            }
            UBASE_FATAL(upipe,
                uref_flow_set_headers(flow_def, nals[0].p_payload, size))
        }
    }
    UBASE_FATAL(upipe,
            uref_h26x_flow_set_encaps(flow_def, upipe_x264->encaps_requested))

    upipe_x264_store_flow_def(upipe, flow_def);
}

/** @internal @This checks incoming pic against cached parameters.
 *
 * @param upipe description structure of the pipe
 * @param width image width
 * @param height image height
 * @return true if parameters update needed
 */
static inline bool upipe_x264_need_update(struct upipe *upipe,
                                          int width, int height)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    x264_param_t *params = &upipe_x264_from_upipe(upipe)->params;
#ifdef HAVE_X264_MPEG2
    if (upipe_x264_mpeg2_enabled(upipe))
        return (params->i_width != width ||
                params->i_height != height ||
                params->vui.i_aspect_ratio_information != upipe_x264->mpeg2_ar);
#endif
    return (params->i_width != width ||
            params->i_height != height ||
            params->vui.i_sar_width != upipe_x264->sar.num ||
            params->vui.i_sar_height != upipe_x264->sar.den ||
            params->vui.i_overscan != upipe_x264->overscan);
}

/** @internal @This processes pictures.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 * @return true if the packet was handled
 */
static bool upipe_x264_handle(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    const char *def;
    if (unlikely(uref != NULL && ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_x264->input_latency = 0;
        uref_clock_get_latency(uref, &upipe_x264->input_latency);
        upipe_x264_store_flow_def(upipe, NULL);
        uref_free(upipe_x264->flow_def_requested);
        upipe_x264->flow_def_requested = NULL;

        if (upipe_x264_mpeg2_enabled(upipe)) {
            struct urational dar;
            dar.num = 4;
            dar.den = 3;
            uref_pic_flow_infer_dar(uref, &dar);
            if (dar.num == 4 && dar.den == 3)
                upipe_x264->mpeg2_ar = 2;
            else if (dar.num == 16 && dar.den == 9)
                upipe_x264->mpeg2_ar = 3;
            else if (dar.num == 221 && dar.den == 100)
                upipe_x264->mpeg2_ar = 4;
            else {
                upipe_warn_va(upipe,
                        "unrecognized aspect ratio %"PRId64"/%"PRIu64", using square",
                        dar.num, dar.den);
                upipe_x264->mpeg2_ar = 1;
            }
        } else {
            upipe_x264->sar.num = upipe_x264->sar.den = 1;
            uref_pic_flow_get_sar(uref, &upipe_x264->sar);
            bool overscan;
            if (!ubase_check(uref_pic_flow_get_overscan(uref, &overscan)))
                upipe_x264->overscan = 0; /* undef */
            else
                upipe_x264->overscan = overscan ? 2 : 1;
        }

        uref = upipe_x264_store_flow_def_input(upipe, uref);
        if (uref != NULL) {
            uref_pic_flow_clear_format(uref);
            upipe_x264_require_flow_format(upipe, uref);
        }
        return true;
    }

    static const char *const chromas[] = {"y8", "u8", "v8"};
    size_t width, height;
    x264_picture_t pic;
    x264_nal_t *nals;
    int i, nals_num, size = 0, header_size = 0;
    struct ubuf *ubuf_block;
    uint8_t *buf = NULL;
    x264_param_t curparams;
    bool needopen = false;
    int ret = 0;

    /* init x264 picture */
    x264_picture_init(&pic);

    if (likely(uref)) {
        pic.opaque = uref;
        pic.img.i_csp = X264_CSP_I420;

        uref_pic_size(uref, &width, &height, NULL);

        /* open encoder if not already opened or if update needed */
        if (unlikely(!upipe_x264->encoder)) {
            needopen = true;
        } else if (unlikely(upipe_x264_need_update(upipe, width, height))) {
            x264_param_t *params = &upipe_x264_from_upipe(upipe)->params;
            upipe_notice_va(upipe, "Flow parameters changed, reconfiguring encoder (%d:%zu, %d:%zu, %d:%"PRId64", %d:%"PRIu64", %d:%d)",
                params->i_width, width, params->i_height, height,
                params->vui.i_sar_width, upipe_x264->sar.num,
                params->vui.i_sar_height, upipe_x264->sar.den,
                params->vui.i_overscan, upipe_x264->overscan);
            needopen = true;
        }
        if (unlikely(needopen)) {
            if (unlikely(!upipe_x264_open(upipe, width, height))) {
                upipe_err(upipe, "Could not open encoder");
                uref_free(uref);
                return true;
            }
        }
        if (upipe_x264->flow_def_requested == NULL)
            return false;

        x264_encoder_parameters(upipe_x264->encoder, &curparams);

        /* set pts in x264 timebase */
        pic.i_pts = upipe_x264->x264_ts;
        upipe_x264->x264_ts++;
        uref_clock_get_rate(uref, &upipe_x264->drift_rate);
        uref_clock_get_pts_prog(uref, &upipe_x264->input_pts);
        uref_clock_get_pts_sys(uref, &upipe_x264->input_pts_sys);

        pic.i_type = X264_TYPE_AUTO;
        if (upipe_x264->slice_type_enforce) {
            uint8_t type;
            if (ubase_check(uref_h264_get_type(uref, &type))) {
                switch (type) {
                    case H264SLI_TYPE_P:
                        pic.i_type = X264_TYPE_P;
                        break;
                    case H264SLI_TYPE_B:
                        pic.i_type = X264_TYPE_B;
                        break;
                    case H264SLI_TYPE_I:
                        pic.i_type = X264_TYPE_KEYFRAME;
                        break;
                    case H264SLI_TYPE_SP:
                    case H264SLI_TYPE_SI:
                    default:
                        break;
                }
            } else if (ubase_check(uref_mpgv_get_type(uref, &type))) {
                switch (type) {
                    case MP2VPIC_TYPE_P:
                        pic.i_type = X264_TYPE_P;
                        break;
                    case MP2VPIC_TYPE_B:
                        pic.i_type = X264_TYPE_B;
                        break;
                    case MP2VPIC_TYPE_I:
                        pic.i_type = X264_TYPE_KEYFRAME;
                        break;
                    case MP2VPIC_TYPE_D:
                    default:
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
            pic.img.i_stride[i] = stride;
            /* cast needed because of x264 API */
            pic.img.plane[i] = (uint8_t *)plane;
        }
        pic.img.i_plane = i;

        /* encode frame ! */
        ret = x264_encoder_encode(upipe_x264->encoder,
                                  &nals, &nals_num, &pic, &pic);

        /* unmap */
        for (i = 0; i < 3; i++) {
            uref_pic_plane_unmap(uref, chromas[i], 0, 0, -1, -1);
        }
        ubuf_free(uref_detach_ubuf(uref));

    } else {
        /* NULL uref, flushing delayed frame */
        ret = x264_encoder_encode(upipe_x264->encoder,
                                  &nals, &nals_num, NULL, &pic);
        x264_encoder_parameters(upipe_x264->encoder, &curparams);
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
    uref = pic.opaque;
    assert(uref);

    for (i = 0; i < nals_num; i++) {
        size += nals[i].i_payload;
        if (nals[i].i_type == NAL_SPS || nals[i].i_type == NAL_PPS ||
            nals[i].i_type == NAL_AUD || nals[i].i_type == NAL_FILLER ||
            nals[i].i_type == NAL_UNKNOWN)
            header_size += nals[i].i_payload;
    }

    /* alloc ubuf, map, copy, unmap */
    ubuf_block = ubuf_block_alloc(upipe_x264->ubuf_mgr, size);
    if (unlikely(ubuf_block == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }
    ubuf_block_write(ubuf_block, 0, &size, &buf);
    memcpy(buf, nals[0].p_payload, size);
    ubuf_block_unmap(ubuf_block, 0);
    uref_attach_ubuf(uref, ubuf_block);
    uref_block_set_header_size(uref, header_size);

    if (!upipe_x264_mpeg2_enabled(upipe)) {
        /* NAL offsets */
        uint64_t offset = 0;
        for (i = 0; i < nals_num - 1; i++) {
            offset += nals[i].i_payload;
            uref_h26x_set_nal_offset(uref, offset, i);
        }

        /* optionally convert NAL encapsulation */
        enum uref_h26x_encaps encaps = upipe_x264->params.b_annexb ?
            UREF_H26X_ENCAPS_ANNEXB : UREF_H26X_ENCAPS_LENGTH4;
        /* no need for annex B header because if annexb is requested, there
         * will be no conversion */
        int err = upipe_h26xf_convert_frame(uref,
                encaps, upipe_x264->encaps_requested, upipe_x264->ubuf_mgr,
                NULL);
        if (!ubase_check(err)) {
            upipe_warn(upipe, "invalid NAL encapsulation conversion");
            upipe_throw_error(upipe, err);
        }
    }

    /* set dts */
    uint64_t dts_pts_delay = (uint64_t)(pic.i_pts - pic.i_dts) * UCLOCK_FREQ
                              * curparams.i_timebase_num
                              / curparams.i_timebase_den;
    uref_clock_set_dts_pts_delay(uref, dts_pts_delay);
    uref_clock_delete_cr_dts_delay(uref);

    /* rebase to dts as we're in encoded domain now */
    uint64_t dts = UINT64_MAX;
    if ((!ubase_check(uref_clock_get_dts_prog(uref, &dts)) ||
         dts < upipe_x264->last_dts) &&
        upipe_x264->last_dts != UINT64_MAX) {
        upipe_warn_va(upipe, "DTS prog in the past, resetting (%"PRIu64" ms)",
                      (upipe_x264->last_dts - dts) * 1000 / UCLOCK_FREQ);
        dts = upipe_x264->last_dts + 1;
        uref_clock_set_dts_prog(uref, dts);
    } else
        uref_clock_rebase_dts_prog(uref);

    uint64_t dts_sys = UINT64_MAX;
    if (dts != UINT64_MAX &&
        upipe_x264->input_pts != UINT64_MAX &&
        upipe_x264->input_pts_sys != UINT64_MAX) {
        dts_sys = (int64_t)upipe_x264->input_pts_sys +
            ((int64_t)dts - (int64_t)upipe_x264->input_pts) *
            (int64_t)upipe_x264->drift_rate.num /
            (int64_t)upipe_x264->drift_rate.den;
        uref_clock_set_dts_sys(uref, dts_sys);
    } else if (!ubase_check(uref_clock_get_dts_sys(uref, &dts_sys)) ||
        (upipe_x264->last_dts_sys != UINT64_MAX &&
               dts_sys < upipe_x264->last_dts_sys)) {
        upipe_warn_va(upipe,
                      "DTS sys in the past, resetting (%"PRIu64" ms)",
                      (upipe_x264->last_dts_sys - dts_sys) * 1000 /
                      UCLOCK_FREQ);
        dts_sys = upipe_x264->last_dts_sys + 1;
        uref_clock_set_dts_sys(uref, dts_sys);
    } else
        uref_clock_rebase_dts_sys(uref);

    uref_clock_rebase_dts_orig(uref);
    uref_clock_set_rate(uref, upipe_x264->drift_rate);

    upipe_x264->last_dts = dts;
    upipe_x264->last_dts_sys = dts_sys;

#ifdef HAVE_X264_OBE
    /* speedcontrol */
    if (dts_sys != UINT64_MAX && upipe_x264->uclock != NULL &&
        upipe_x264->sc_latency) {
        uint64_t systime = uclock_now(upipe_x264->uclock);
        int64_t buffer_state = dts_sys + upipe_x264->initial_latency +
                               upipe_x264->sc_latency - systime;
        float buffer_fill = (float)buffer_state /
                            (float)upipe_x264->sc_latency;
        x264_speedcontrol_sync(upipe_x264->encoder, buffer_fill, 0, 1);
    }
#endif

    if (pic.b_keyframe) {
        uref_flow_set_random(uref);
    }

    if (upipe_x264->flow_def == NULL)
        upipe_x264_build_flow_def(upipe);

    upipe_x264_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_x264_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    if (!upipe_x264_check_input(upipe)) {
        upipe_x264_hold_input(upipe, uref);
        upipe_x264_block_input(upipe, upump_p);
    } else if (!upipe_x264_handle(upipe, uref, upump_p)) {
        upipe_x264_hold_input(upipe, uref);
        upipe_x264_block_input(upipe, upump_p);
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
static int upipe_x264_check_flow_format(struct upipe *upipe,
                                        struct uref *flow_format)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_INVALID;

    upipe_x264->headers_requested =
        ubase_check(uref_flow_get_global(flow_format));
    upipe_x264->encaps_requested = uref_h26x_flow_infer_encaps(flow_format);
    bool annexb = upipe_x264->encaps_requested == UREF_H26X_ENCAPS_ANNEXB;
    if (upipe_x264->params.b_annexb != annexb) {
        upipe_x264->params.b_annexb = annexb;
        _upipe_x264_reconfigure(upipe);
    }

    uref_free(upipe_x264->flow_def_requested);
    upipe_x264->flow_def_requested = NULL;
    upipe_x264_require_ubuf_mgr(upipe, flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_x264_check_ubuf_mgr(struct upipe *upipe,
                                     struct uref *flow_format)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_NONE; /* should not happen */

    uref_free(upipe_x264->flow_def_requested);
    upipe_x264->flow_def_requested = flow_format;
    upipe_x264_build_flow_def(upipe);

    bool was_buffered = !upipe_x264_check_input(upipe);
    upipe_x264_output_input(upipe);
    upipe_x264_unblock_input(upipe);
    if (was_buffered && upipe_x264_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_x264_input. */
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
static int upipe_x264_set_flow_def(struct upipe *upipe,
                                   struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    /* We only accept YUV420P for the moment. */
    uint8_t macropixel;
    if (unlikely(!ubase_check(uref_flow_match_def(flow_def, EXPECTED_FLOW)) ||
                 !ubase_check(uref_pic_flow_get_macropixel(flow_def, &macropixel)) ||
                 macropixel != 1 ||
                 !ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) ||
                 !ubase_check(uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "u8")) ||
                 !ubase_check(uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "v8"))))
        return UBASE_ERR_INVALID;

    /* Extract relevant attributes to flow def check. */
    struct uref *flow_def_check =
        upipe_x264_alloc_flow_def_check(upipe, flow_def);
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

    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);

    if (upipe_x264->flow_def_check != NULL) {
        /* Die if the attributes changed. */
        if (!upipe_x264_check_flow_def_check(upipe, flow_def_check)) {
            uref_free(flow_def_check);
            return UBASE_ERR_BUSY;
        }
        uref_free(flow_def_check);

    } else {
#ifdef HAVE_X264_OBE
        if (upipe_x264->sc_latency) {
            upipe_x264->params.sc.i_buffer_size =
                upipe_x264->sc_latency * fps.num / fps.den / UCLOCK_FREQ;
            upipe_x264->params.sc.f_speed = 1.0;
            upipe_x264->params.sc.f_buffer_init = 0.0;
            upipe_x264->params.sc.b_alt_timer = 1;
            uint64_t height;
            if (ubase_check(uref_pic_flow_get_hsize(flow_def, &height)) && height >= 720)
                upipe_x264->params.sc.max_preset = 7;
            else
                upipe_x264->params.sc.max_preset = 10;
        }
#endif

        upipe_x264_store_flow_def_check(upipe, flow_def_check);
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
static int _upipe_x264_provide_flow_format(struct upipe *upipe,
                                           struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);
    uref_pic_flow_clear_format(flow_format);
    uref_pic_flow_set_macropixel(flow_format, 1);
    uref_pic_flow_set_planes(flow_format, 0);
    uref_pic_flow_add_plane(flow_format, 1, 1, 1, "y8");
    uref_pic_flow_add_plane(flow_format, 2, 2, 1, "u8");
    uref_pic_flow_add_plane(flow_format, 2, 2, 1, "v8");
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_x264_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UCLOCK:
            upipe_x264_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_throw_provide_request(upipe, request);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return _upipe_x264_provide_flow_format(upipe, request);
            return upipe_x264_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_x264_free_output_proxy(upipe, request);
        }

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_x264_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_GET_FLOW_DEF:
            return upipe_x264_control_output(upipe, command, args);

        case UPIPE_X264_RECONFIG: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X264_SIGNATURE)
            return _upipe_x264_reconfigure(upipe);
        }
        case UPIPE_X264_SET_DEFAULT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X264_SIGNATURE)
            return _upipe_x264_set_default(upipe);
        }
        case UPIPE_X264_SET_DEFAULT_MPEG2: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X264_SIGNATURE)
            return _upipe_x264_set_default_mpeg2(upipe);
        }
        case UPIPE_X264_SET_DEFAULT_PRESET: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X264_SIGNATURE)
            const char *preset = va_arg(args, const char *);
            const char *tune = va_arg(args, const char *);
            return _upipe_x264_set_default_preset(upipe, preset, tune);
        }
        case UPIPE_X264_SET_PROFILE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X264_SIGNATURE)
            const char *profile = va_arg(args, const char *);
            return _upipe_x264_set_profile(upipe, profile);
        }
        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return upipe_x264_set_option(upipe, option, content);
        }
        case UPIPE_X264_SET_SC_LATENCY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X264_SIGNATURE)
            uint64_t sc_latency = va_arg(args, uint64_t);
            return _upipe_x264_set_sc_latency(upipe, sc_latency);
        }
        case UPIPE_X264_SET_SLICE_TYPE_ENFORCE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X264_SIGNATURE)
            bool enforce = !(va_arg(args, int) == 0);
            return _upipe_x264_set_slice_type_enforce(upipe, enforce);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_x264_free(struct upipe *upipe)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    upipe_x264_close(upipe);

    upipe_throw_dead(upipe);
    upipe_x264_clean_uclock(upipe);
    upipe_x264_clean_ubuf_mgr(upipe);
    upipe_x264_clean_input(upipe);
    upipe_x264_clean_output(upipe);
    uref_free(upipe_x264->flow_def_requested);
    upipe_x264_clean_flow_format(upipe);
    upipe_x264_clean_flow_def(upipe);
    upipe_x264_clean_flow_def_check(upipe);
    upipe_x264_clean_urefcount(upipe);
    upipe_x264_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_x264_mgr = {
    .refcount = NULL,
    .signature = UPIPE_X264_SIGNATURE,
    .upipe_alloc = upipe_x264_alloc,
    .upipe_input = upipe_x264_input,
    .upipe_control = upipe_x264_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for x264 pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_x264_mgr_alloc(void)
{
    return &upipe_x264_mgr;
}
