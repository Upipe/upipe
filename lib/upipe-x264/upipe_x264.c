/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
#include <upipe/uref_block_flow.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_flow_def_check.h>
#include <upipe-x264/upipe_x264.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>

#include <x264.h>

#define EXPECTED_FLOW "pic."
#define OUT_FLOW "block.h264.pic."

/** @internal upipe_x264 private structure */
struct upipe_x264 {
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

    /** x264 "PTS" */
    uint64_t x264_ts;

    /** uclock */
    struct uclock *uclock;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** input flow */
    struct uref *flow_def_input;
    /** attributes added by the pipe */
    struct uref *flow_def_attr;
    /** structure to check input flow def */
    struct uref *flow_def_check;
    /** output flow */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;
    /** output pipe */
    struct upipe *output;

    /** public structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_x264, upipe, UPIPE_X264_SIGNATURE);
UPIPE_HELPER_VOID(upipe_x264);
UPIPE_HELPER_UBUF_MGR(upipe_x264, ubuf_mgr);
UPIPE_HELPER_UCLOCK(upipe_x264, uclock);
UPIPE_HELPER_OUTPUT(upipe_x264, output, flow_def, flow_def_sent)
UPIPE_HELPER_FLOW_DEF(upipe_x264, flow_def_input, flow_def_attr)
UPIPE_HELPER_FLOW_DEF_CHECK(upipe_x264, flow_def_check)

static void upipe_x264_input(struct upipe *upipe, struct uref *uref, struct upump *upump);

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

    vasprintf(&string, format, args);
    if (unlikely(!string)) {
        return;
    }
    end = string + strlen(string) - 1;
    if (isspace(*end)) {
        *end = '\0';
    }
    upipe_log(upipe, loglevel_map[loglevel], string);
    free(string);
}

/** @internal @This reconfigures encoder with updated parameters
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool _upipe_x264_reconfigure(struct upipe *upipe)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    int ret;
    if (unlikely(!upipe_x264->encoder)) {
        return false;
    }
    ret = x264_encoder_reconfig(upipe_x264->encoder, &upipe_x264->params);
    return ( (ret < 0) ? false : true );
}

/** @internal @This reset parameters to default
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool _upipe_x264_set_default(struct upipe *upipe)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    x264_param_default(&upipe_x264->params);
    return true;
}

/** @internal @This sets default parameters for specified preset.
 *
 * @param upipe description structure of the pipe
 * @param preset x264 preset
 * @param tuning x264 tuning
 * @return false in case of error
 */
static bool _upipe_x264_set_default_preset(struct upipe *upipe,
                                const char *preset, const char *tune)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    int ret;
    ret = x264_param_default_preset(&upipe_x264->params, preset, tune);
    return ( (ret < 0) ? false : true );
}

/** @internal @This enforces profile.
 *
 * @param upipe description structure of the pipe
 * @param profile x264 profile
 * @return false in case of error
 */
static bool _upipe_x264_set_profile(struct upipe *upipe, const char *profile)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    int ret;
    ret = x264_param_apply_profile(&upipe_x264->params, profile);
    return ( (ret < 0) ? false : true );
}

/** @internal @This sets the content of an x264 option.
 * upipe_x264_reconfigure must be called to apply changes.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option
 * @return false in case of error
 */
static bool _upipe_x264_set_option(struct upipe *upipe, const char *option,
                                   const char *content)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    int ret = x264_param_parse(&upipe_x264->params, option, content);
    if (unlikely(ret < 0)) {
        upipe_err_va(upipe, "can't set option %s:%s (%d)",
                     option, content, ret);
        return false;
    }
    return true;
}

/** @This switches x264 into speedcontrol mode, with the given latency (size
 * of sc buffer).
 *
 * @param upipe description structure of the pipe
 * @param latency size (in units of a 27 MHz) of the speedcontrol buffer
 * @return false in case of error
 */
static bool _upipe_x264_set_sc_latency(struct upipe *upipe, uint64_t sc_latency)
{
#ifndef HAVE_X264_OBE
    return false;
#else
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    upipe_x264->sc_latency = sc_latency;
    return true;
#endif
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
    upipe_x264->x264_ts = 0;

    upipe_x264_init_ubuf_mgr(upipe);
    upipe_x264_init_uclock(upipe);
    upipe_x264_init_output(upipe);
    upipe_x264_init_flow_def(upipe);
    upipe_x264_init_flow_def_check(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This opens x264 encoder.
 *
 * @param upipe description structure of the pipe
 * @param width image width
 * @param height image height
 * @param sar SAR
 */
static bool upipe_x264_open(struct upipe *upipe, int width, int height,
                            struct urational *sar)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    struct urational fps = {0, 0};
    x264_param_t *params = &upipe_x264->params;

    params->pf_log = upipe_x264_log;
    params->p_log_private = upipe;
    params->i_log_level = X264_LOG_DEBUG;
    if (likely(uref_pic_flow_get_fps(upipe_x264->flow_def_input, &fps))) {
        params->i_fps_num = fps.num;
        params->i_fps_den = fps.den;
        //params->b_vfr_input = 0;
    }

    params->vui.i_sar_width = sar->num;
    params->vui.i_sar_height = sar->den;
    params->i_width = width;
    params->i_height = height;

    /* reconfigure encoder with new parameters and return */
    if (unlikely(upipe_x264->encoder)) {
        if (!_upipe_x264_reconfigure(upipe))
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
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }

    if (unlikely(!uref_flow_set_def(flow_def_attr, OUT_FLOW))) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }
    /* set octetrate for CBR streams */
    if (params->rc.i_bitrate) {
        uref_block_flow_set_octetrate(flow_def_attr,
                                      params->rc.i_bitrate * 125);
        if (params->rc.i_vbv_buffer_size)
            uref_block_flow_set_cpb_buffer(flow_def_attr,
                                           params->rc.i_vbv_buffer_size * 125);
    }

    /* find latency */
    int delayed = x264_encoder_maximum_delayed_frames(upipe_x264->encoder);
    if (delayed >= 0) {
        uint64_t latency = upipe_x264->input_latency +
                           (uint64_t)delayed * UCLOCK_FREQ
                            * params->i_fps_den
                            / params->i_fps_num;
        upipe_x264->initial_latency = latency;

        if (params->rc.i_bitrate)
            latency += (uint64_t)params->rc.i_vbv_buffer_size * UCLOCK_FREQ /
                       params->rc.i_bitrate;
        latency += upipe_x264->sc_latency;
        uref_clock_set_latency(flow_def_attr, latency);
    }

    /* Find out if flow def attributes have changed. */
    if (!upipe_x264_check_flow_def_attr(upipe, flow_def_attr)) {
        struct uref *flow_def =
            upipe_x264_store_flow_def_attr(upipe, flow_def_attr);
        if (flow_def != NULL) {
            uref_pic_flow_clear_format(flow_def);
            upipe_x264_store_flow_def(upipe, flow_def);
        }
    }

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
            upipe_x264_input(upipe, NULL, NULL);
        }

        upipe_notice(upipe, "closing encoder");
        x264_encoder_close(upipe_x264->encoder);
    }
}

/** @internal @This checks incoming pic against cached parameters
 *
 * @param upipe description structure of the pipe
 * @param width image width
 * @param height image height
 * @param sar SAR
 * @return true if parameters update needed
 */
static inline bool upipe_x264_need_update(struct upipe *upipe, int width, int height,
                            struct urational *sar)
{
    x264_param_t *params = &upipe_x264_from_upipe(upipe)->params;
    if ( params->i_width != width ||
         params->i_height != height ||
         params->vui.i_sar_width != sar->num ||
         params->vui.i_sar_height != sar->den )
    {
        return true;
    }
    return false;
}

/** @internal @This processes pictures.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_x264_input(struct upipe *upipe, struct uref *uref,
                             struct upump *upump)
{
    static const char *const chromas[] = {"y8", "u8", "v8"};
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    size_t width, height;
    x264_picture_t pic;
    x264_nal_t *nals;
    int i, nals_num, size = 0;
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

        struct urational sar;
        sar.den = sar.num = 1;
        uref_pic_flow_get_sar(upipe_x264->flow_def_input, &sar);
        urational_simplify(&sar);
        uref_pic_size(uref, &width, &height, NULL);

        /* open encoder if not already opened or if update needed */
        if (unlikely(!upipe_x264->encoder)) {
            needopen = true;
        } else if (unlikely(upipe_x264_need_update(upipe, width, height, &sar))) {
            needopen = true;
            upipe_notice(upipe, "Flow parameters changed, reconfiguring encoder");
        }
        if (unlikely(needopen)) {
            if (unlikely(!upipe_x264_open(upipe, width, height, &sar))) {
                upipe_err(upipe, "Could not open encoder");
                uref_free(uref);
                return;
            }
        }
        x264_encoder_parameters(upipe_x264->encoder, &curparams);

        if (unlikely(upipe_x264->ubuf_mgr == NULL)) {
            upipe_throw_need_ubuf_mgr(upipe, upipe_x264->flow_def_attr);
            if (unlikely(upipe_x264->ubuf_mgr == NULL)) {
                uref_free(uref);
                return;
            }
        }

        /* set pts in x264 timebase */
        pic.i_pts = upipe_x264->x264_ts;
        upipe_x264->x264_ts++;

        /* map */
        for (i = 0; i < 3; i++) {
            size_t stride;
            const uint8_t *plane;
            if (unlikely(!uref_pic_plane_size(uref, chromas[i], &stride,
                                              NULL, NULL, NULL) ||
                         !uref_pic_plane_read(uref, chromas[i], 0, 0, -1, -1,
                                              &plane))) {
                upipe_err_va(upipe, "Could not read origin chroma %s",
                             chromas[i]);
                uref_free(uref);
                return;
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
        return;
    } else if (unlikely(ret == 0)) {
        upipe_verbose(upipe, "No nal units returned");
        return;
    }

    /* get uref back */
    uref = pic.opaque;
    assert(uref);

    for (i=0; i < nals_num; i++) {
        size += nals[i].i_payload;
    }

    /* alloc ubuf, map, copy, unmap */
    ubuf_block = ubuf_block_alloc(upipe_x264->ubuf_mgr, size);
    if (unlikely(ubuf_block == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }
    ubuf_block_write(ubuf_block, 0, &size, &buf);
    memcpy(buf, nals[0].p_payload, size);
    ubuf_block_unmap(ubuf_block, 0);
    uref_attach_ubuf(uref, ubuf_block);

    /* set dts */
    uint64_t dts_pts_delay = (uint64_t)(pic.i_pts - pic.i_dts) * UCLOCK_FREQ
                              * curparams.i_timebase_num
                              / curparams.i_timebase_den;
    uref_clock_set_dts_pts_delay(uref, dts_pts_delay);

    /* rebase to dts as we're in encoded domain now */
    uref_clock_rebase_dts_sys(uref);
    uref_clock_rebase_dts_prog(uref);
    uref_clock_rebase_dts_orig(uref);

#ifdef HAVE_X264_OBE
    /* speedcontrol */
    uint64_t dts;
    if (uref_clock_get_dts_sys(uref, &dts)) {
        if (upipe_x264->uclock != NULL && upipe_x264->sc_latency) {
            uint64_t systime = uclock_now(upipe_x264->uclock);
            float buffer_fill =
                (float)(systime + upipe_x264->sc_latency -
                        (dts + upipe_x264->initial_latency)) /
                (float)upipe_x264->sc_latency;
            if (buffer_fill > 1.0)
                buffer_fill = 1.0;
            if (buffer_fill < 0.0)
                buffer_fill = 0.0;
            x264_speedcontrol_sync(upipe_x264->encoder, buffer_fill,
                                   upipe_x264->params.sc.i_buffer_size, 1 );
        }
    }
#endif

    if (pic.b_keyframe) {
        uref_flow_set_random(uref);
    }

    if (pic.hrd_timing.cpb_final_arrival_time)
        uref_clock_set_cr_dts_delay(uref,
#ifndef HAVE_X264_OBE
                UCLOCK_FREQ *
#endif
                (pic.hrd_timing.cpb_removal_time -
                 pic.hrd_timing.cpb_final_arrival_time));

    upipe_x264_output(upipe, uref, upump);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the flow definition is not handled
 */
static bool upipe_x264_set_flow_def(struct upipe *upipe,
                                    struct uref *flow_def)
{
    if (flow_def == NULL)
        return false;

    /* We only accept YUV420P for the moment. */
    uint8_t macropixel;
    if (unlikely(!uref_flow_match_def(flow_def, EXPECTED_FLOW) ||
                 !uref_pic_flow_get_macropixel(flow_def, &macropixel) ||
                 macropixel != 1 ||
                 !uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8") ||
                 !uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "u8") ||
                 !uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "v8")))
        return false;

    /* Extract relevant attributes to flow def check. */
    struct uref *flow_def_check =
        upipe_x264_alloc_flow_def_check(upipe, flow_def);
    if (unlikely(flow_def_check == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }

    struct urational fps;
    if (!uref_pic_flow_get_fps(flow_def, &fps)) {
        upipe_err(upipe, "incompatible flow def");
        uref_free(flow_def_check);
        return false;
    }

    if (unlikely(!uref_pic_flow_copy_format(flow_def_check, flow_def) ||
                 !uref_pic_flow_set_fps(flow_def_check, fps))) {
        uref_free(flow_def_check);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }

    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);

    if (upipe_x264->encoder != NULL) {
        /* Die if the attributes changed. */
        /* NB: this supposes that all attributes are in the udict, and that
         * the udict is never empty. */
        if (!upipe_x264_check_flow_def_check(upipe, flow_def_check)) {
            uref_free(flow_def_check);
            return false;
        }
        uref_free(flow_def_check);

    } else {
#ifdef HAVE_X264_OBE
        if (upipe_x264->sc_latency) {
            upipe_x264->params.sc.i_buffer_size =
                upipe_x264->sc_latency * fps.num / fps.den / UCLOCK_FREQ;
            upipe_x264->params.sc.f_speed = 1.0;
            upipe_x264->params.sc.f_buffer_init = 1.0;
            upipe_x264->params.sc.b_alt_timer = 1;
            uint64_t height;
            if (uref_pic_flow_get_hsize(flow_def, &height) && height >= 720)
                upipe_x264->params.sc.max_preset = 7;
            else
                upipe_x264->params.sc.max_preset = 10;
        }
#endif

        upipe_x264_store_flow_def_check(upipe, flow_def_check);
    }

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }
    flow_def = upipe_x264_store_flow_def_input(upipe, flow_def);
    if (flow_def != NULL) {
        uref_pic_flow_clear_format(flow_def);
        upipe_x264_store_flow_def(upipe, flow_def);
    }

    upipe_x264->input_latency = 0;
    uref_clock_get_latency(upipe_x264->flow_def_input,
                           &upipe_x264->input_latency);
    return true;
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_x264_control(struct upipe *upipe,
                               enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_x264_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_x264_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_x264_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            return upipe_x264_set_uclock(upipe, uclock);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_x264_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_x264_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_x264_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_x264_set_output(upipe, output);
        }

        case UPIPE_X264_RECONFIG: {
            int signature = va_arg(args, int);
            assert(signature == UPIPE_X264_SIGNATURE);
            return _upipe_x264_reconfigure(upipe);
        }
        case UPIPE_X264_SET_DEFAULT: {
            int signature = va_arg(args, int);
            assert(signature == UPIPE_X264_SIGNATURE);
            return _upipe_x264_set_default(upipe);
        }
        case UPIPE_X264_SET_DEFAULT_PRESET: {
            int signature = va_arg(args, int);
            assert(signature == UPIPE_X264_SIGNATURE);
            const char *preset = va_arg(args, const char *);
            const char *tune = va_arg(args, const char *);
            return _upipe_x264_set_default_preset(upipe, preset, tune);
        }
        case UPIPE_X264_SET_PROFILE: {
            int signature = va_arg(args, int);
            assert(signature == UPIPE_X264_SIGNATURE);
            const char *profile = va_arg(args, const char *);
            return _upipe_x264_set_profile(upipe, profile);
        }
        case UPIPE_X264_SET_OPTION: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_X264_SIGNATURE);
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return _upipe_x264_set_option(upipe, option, content);
        }
        case UPIPE_X264_SET_SC_LATENCY: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_X264_SIGNATURE);
            uint64_t sc_latency = va_arg(args, uint64_t);
            return _upipe_x264_set_sc_latency(upipe, sc_latency);
        }
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_x264_free(struct upipe *upipe)
{
    upipe_x264_close(upipe);

    upipe_throw_dead(upipe);
    upipe_x264_clean_uclock(upipe);
    upipe_x264_clean_ubuf_mgr(upipe);
    upipe_x264_clean_output(upipe);
    upipe_x264_clean_flow_def(upipe);
    upipe_x264_clean_flow_def_check(upipe);
    upipe_x264_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_x264_mgr = {
    .signature = UPIPE_X264_SIGNATURE,
    .upipe_alloc = upipe_x264_alloc,
    .upipe_input = upipe_x264_input,
    .upipe_control = upipe_x264_control,
    .upipe_free = upipe_x264_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for glx_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_x264_mgr_alloc(void)
{
    return &upipe_x264_mgr;
}
