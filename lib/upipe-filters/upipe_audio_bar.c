/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Rafaël Carré
 *          Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "upipe/upipe.h"
#include "upipe/uref.h"
#include "upipe/uref_dump.h"
#include "upipe/uref_sound_flow.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_flow.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_flow.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_flow_format.h"
#include "upipe-filters/upipe_audio_bar.h"
#include "upipe-filters/upipe_audio_max.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

/** we only accept sound */
#define INPUT_FLOW_DEF "sound."
/** we only output pictures */
#define OUTPUT_FLOW_DEF "pic."
/** default alpha channel */
#define DEFAULT_ALPHA 0x60

/** @hidden */
static int upipe_audiobar_check_flow_format(struct upipe *upipe,
                                            struct uref *flow_format);
/** @hidden */
static int upipe_audiobar_check_ubuf_mgr(struct upipe *upipe,
                                         struct uref *flow_format);

/** @internal @This is the private context of a audiobar pipe */
struct upipe_audiobar {
    /** refcount management structure */
    struct urefcount urefcount;

    /** configured flow_def */
    struct uref *flow_def_config;
    /** configured alpha channel */
    uint8_t alpha;

    /** flow format request */
    struct urequest request;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** output pipe */
    struct upipe *output;
    /** output flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** peak value */
    double peak[255];
    /* peak date */
    uint64_t peak_date[255];

    /** number of input channels */
    uint8_t channels;
    /** requested width */
    uint64_t hsize;
    /** requested height */
    uint64_t vsize;
    /** inferred channel width */
    uint64_t chan_width;
    /** inferred channel separation width */
    uint64_t sep_width;
    /** inferred padding at end of line */
    uint64_t pad_width;

    /** 10 bit output */
    bool tenbit;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_audiobar, upipe, UPIPE_AUDIO_BAR_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_audiobar, urefcount, upipe_audiobar_free)
UPIPE_HELPER_FLOW(upipe_audiobar, OUTPUT_FLOW_DEF)
UPIPE_HELPER_OUTPUT(upipe_audiobar, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_FORMAT(upipe_audiobar, request,
                         upipe_audiobar_check_flow_format,
                         upipe_audiobar_register_output_request,
                         upipe_audiobar_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_audiobar, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_audiobar_check_ubuf_mgr,
                      upipe_audiobar_register_output_request,
                      upipe_audiobar_unregister_output_request)

/** @internal @This allocates a audiobar pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_audiobar_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_audiobar_alloc_flow(mgr, uprobe, signature,
                                                    args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_audiobar *upipe_audiobar = upipe_audiobar_from_upipe(upipe);
    upipe_audiobar_init_urefcount(upipe);
    upipe_audiobar_init_output(upipe);
    upipe_audiobar_init_ubuf_mgr(upipe);
    upipe_audiobar_init_flow_format(upipe);
    upipe_audiobar->flow_def_config = flow_def;
    upipe_audiobar->alpha = DEFAULT_ALPHA;
    upipe_audiobar->hsize = upipe_audiobar->vsize =
        upipe_audiobar->sep_width = upipe_audiobar->pad_width = UINT64_MAX;

    for (int i = 0; i < 255; i++) {
        upipe_audiobar->peak[i] = 0.;
        upipe_audiobar->peak_date[i] = 0;
    }

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This converts dB to IEC 268-18 scale.
 *
 * @param dB amplitude in decibels
 * @return IEC-268-18 scale between 0. and 1.
 */
static double iec_scale(double dB)
{
    if (dB < -70.0)
        return 0.0;
    if (dB < -60.0)
        return (dB + 70.0) * 0.0025;
    if (dB < -50.0)
        return (dB + 60.0) * 0.005f + 0.025;
    if (dB < -40.0)
        return (dB + 50.0) * 0.0075f + 0.075;
    if (dB < -30.0)
        return (dB + 40.0) * 0.015f + 0.15;
    if (dB < -20.0)
        return (dB + 30.0) * 0.02f + 0.3;
    if (dB < -0.001f || dB > 0.001)  /* if (dB < 0.0) */
        return (dB + 20.0) * 0.025f + 0.5;
    return 1.0;
}

/** @internal @This copies pixels to the destination picture.
 *
 * @param dst array of destination chromas (planar YUV422P10LE)
 * @param strides array of strides for each chroma
 * @param hsubs array of hsubs of each chroma
 * @param vsubs array of vsubs of each chroma
 * @param color 16-bit color code
 * @param row row number
 * @param col column number
 * @param w size of the copy
 */
static void copy_color10(uint8_t **dst, const size_t *strides,
                       const uint8_t *hsubs, const uint8_t *vsubs, const uint16_t *color,
                       unsigned row, unsigned col, unsigned w)
{
    for (int i = 0; i < 3; i++) {
        if (dst[i] == NULL)
            continue;

        uint16_t *d = (uint16_t*)&dst[i][(row / vsubs[i]) * strides[i]];

        unsigned offset = col / hsubs[i];
        unsigned count = w / hsubs[i];
        for (unsigned j = 0; j < count; j++) {
            d[offset + j] = color[i] << 2;
        }
    }

    if (dst[3] != NULL)
        memset(&dst[3][(row / vsubs[3]) * strides[3] + col / hsubs[3]],
                color[3], w / hsubs[3]); // a8

    if (dst[4] != NULL) { // u10v10l
        uint16_t *d = (uint16_t*)&dst[4][(row / vsubs[4]) * strides[4] +
            2 * col / hsubs[4]];

        for (int i = 0; i < w / hsubs[4]; i++) {
            d[i * 2] = color[1];
            d[i * 2 + 1] = color[2];
        }
    }
}

/** @internal @This copies pixels to the destination picture.
 *
 * @param dst array of destination chromas (planar YUV422)
 * @param strides array of strides for each chroma
 * @param hsubs array of hsubs of each chroma
 * @param vsubs array of vsubs of each chroma
 * @param color 32-bit color code
 * @param row row number
 * @param col column number
 * @param w size of the copy
 */
static void copy_color8(uint8_t **dst, const size_t *strides,
                       const uint8_t *hsubs, const uint8_t *vsubs, const uint16_t *color,
                       unsigned row, unsigned col, unsigned w)
{
    if (dst[0] != NULL)
        memset(&dst[0][(row / vsubs[0]) * strides[0] + col / hsubs[0]],
               color[0], w / hsubs[0]); // y8
    if (dst[1] != NULL)
        memset(&dst[1][(row / vsubs[1]) * strides[1] + col / hsubs[1]],
               color[1], w / hsubs[1]); // u8
    if (dst[2] != NULL)
        memset(&dst[2][(row / vsubs[2]) * strides[2] + col / hsubs[2]],
               color[2], w / hsubs[2]); // v8
    if (dst[3] != NULL)
        memset(&dst[3][(row / vsubs[3]) * strides[3] + col / hsubs[3]],
               color[3], w / hsubs[3]); // a8
    if (dst[4] != NULL) { // u8v8
        uint8_t *p = &dst[4][(row / vsubs[4]) * strides[4] +
            2 * col / hsubs[4]];
        for (int i = 0; i < w / hsubs[4]; i++) {
            p[i * 2] = color[1];
            p[i * 2 + 1] = color[2];
        }
    }
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 * @return true if the packet was handled
 */
static void upipe_audiobar_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_audiobar *upipe_audiobar = upipe_audiobar_from_upipe(upipe);

    if (unlikely(!upipe_audiobar->ubuf_mgr || upipe_audiobar->hsize == UINT64_MAX)) {
        uref_free(uref);
        return;
    }

    struct ubuf *ubuf = ubuf_pic_alloc(upipe_audiobar->ubuf_mgr,
                                       upipe_audiobar->hsize,
                                       upipe_audiobar->vsize);
    uref_attach_ubuf(uref, ubuf);

    static const char *chroma8[] = { "y8", "u8", "v8", "a8", "u8v8" };
    static const char *chroma10[] = { "y10l", "u10l", "v10l", "a8", "u10v10l" };
#define NR_CHROMA UBASE_ARRAY_SIZE(chroma8)
    uint8_t *dst[NR_CHROMA];
    size_t strides[NR_CHROMA];
    uint8_t hsubs[NR_CHROMA];
    uint8_t vsubs[NR_CHROMA];

    const char **chroma = upipe_audiobar->tenbit ? chroma10 : chroma8;
    void (*copy_color)(uint8_t **dst, const size_t *strides,
            const uint8_t *hsubs, const uint8_t *vsubs, const uint16_t *color,
            unsigned row, unsigned col, unsigned w) = upipe_audiobar->tenbit ? copy_color10 : copy_color8;

    for (int i = 0; i < NR_CHROMA; i++) {
        if (unlikely(!ubase_check(uref_pic_plane_write(uref, chroma[i],
                            0, 0, -1, -1, &dst[i])) ||
                     !ubase_check(uref_pic_plane_size(uref, chroma[i],
                             &strides[i], &hsubs[i], &vsubs[i], NULL)))) {
             dst[i] = NULL;
        }
    }

    uint8_t alpha = upipe_audiobar->alpha;
    uint64_t h = upipe_audiobar->vsize;
    const int hred = h - (iec_scale(-8.) * h);
    const int hyellow = h - (iec_scale(-18.) * h);
    uint16_t transparent[4] = { 0x10, 0x80, 0x80, 0 };
    uint16_t black[4] = { 0x10, 0x80, 0x80, alpha };
    uint16_t red[2][4] = { { 76, 85, 0xff, alpha }, { 37, 106, 191, alpha } };
    uint16_t green[2][4] = { { 150, 44, 21, alpha }, { 74, 85, 74, alpha } };
    uint16_t yellow[2][4] = { { 226, 1, 148, alpha }, { 112, 64, 138, alpha } };

    uint64_t pts = 0;
    if (unlikely(!ubase_check(uref_clock_get_pts_prog(uref, &pts)))) {
        upipe_warn(upipe, "unable to read pts");
    }

    for (uint8_t chan = 0; chan < upipe_audiobar->channels; chan++) {
        double amplitude = 0.;
        if (unlikely(!ubase_check(uref_amax_get_amplitude(uref, &amplitude,
                                                          chan))))
            upipe_warn_va(upipe, "unable to get amplitude for channel %"PRIu8", assuming silence", chan);

        double scale = log10(amplitude) * 20;

        // IEC-268-18 return time speed is 20dB per 1.7s (+/- .3)
        if (upipe_audiobar->peak_date[chan])
            upipe_audiobar->peak[chan] -= 20 * (pts - upipe_audiobar->peak_date[chan]) / (1.7 * UCLOCK_FREQ);

        upipe_audiobar->peak_date[chan] = pts;

        if (scale >= upipe_audiobar->peak[chan]) /* higher than lowered peak */
            upipe_audiobar->peak[chan] = scale;
        else /* Current amplitude can not go below the lowered peak value */
            scale = upipe_audiobar->peak[chan];

        scale = iec_scale(scale);

        const int hmax = h - scale * h;
        for (int row = 0; row < h; row++) {
            bool bright = row > hmax;

            const uint16_t *color = row < hred ? red[!bright] :
                                   row < hyellow ? yellow[!bright] :
                                   green[!bright];

            copy_color(dst, strides, hsubs, vsubs, color, row,
                       chan * upipe_audiobar->chan_width,
                       upipe_audiobar->chan_width);
            if (chan && upipe_audiobar->sep_width)
                copy_color(dst, strides, hsubs, vsubs, black, row,
                           chan * upipe_audiobar->chan_width -
                           upipe_audiobar->sep_width / 2,
                           upipe_audiobar->sep_width);

            if (chan == upipe_audiobar->channels - 1 &&
                upipe_audiobar->pad_width)
                copy_color(dst, strides, hsubs, vsubs, transparent, row,
                           (chan + 1) * upipe_audiobar->chan_width,
                           upipe_audiobar->pad_width);
        }
    }

    /* dB marks */
    for (int i = 1; i <= 6; i++) {
        int row = h - (iec_scale(-10 * i) * h);
        copy_color(dst, strides, hsubs, vsubs, black, row, 0,
                   upipe_audiobar->hsize);
    }

    for (int i = 0; i < NR_CHROMA; i++)
        ubuf_pic_plane_unmap(ubuf, chroma[i], 0, 0, -1, -1);
    upipe_audiobar_output(upipe, uref, upump_p);
}

/** @internal @This provides a flow_format request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_audiobar_check_flow_format(struct upipe *upipe,
                                            struct uref *flow_format)
{
    if (flow_format != NULL)
        upipe_audiobar_require_ubuf_mgr(upipe, flow_format);

    return UBASE_ERR_NONE;
}

/** @internal @This provides a ubuf_mgr request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_audiobar_check_ubuf_mgr(struct upipe *upipe,
                                         struct uref *flow_format)
{
    struct upipe_audiobar *upipe_audiobar = upipe_audiobar_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_NONE;

    if (!ubase_check(uref_pic_flow_find_chroma(flow_format, "a8", NULL))) {
        upipe_err(upipe, "Missing alpha plane");
        return UBASE_ERR_INVALID;
    }

    if (!ubase_check(uref_pic_flow_find_chroma(flow_format, "y8", NULL))
            || ((!ubase_check(uref_pic_flow_find_chroma(flow_format, "u8", NULL))
                    || !ubase_check(uref_pic_flow_find_chroma(flow_format, "v8", NULL)))
                && !ubase_check(uref_pic_flow_find_chroma(flow_format, "u8v8", NULL)))) {
        if (!ubase_check(uref_pic_flow_find_chroma(flow_format, "y10l", NULL))
                || ((!ubase_check(uref_pic_flow_find_chroma(flow_format, "u10l", NULL)))
                && !ubase_check(uref_pic_flow_find_chroma(flow_format, "u10v10l", NULL)))) {
            upipe_err(upipe, "Unknown pixel format");
            uref_dump(flow_format, upipe->uprobe);
            return UBASE_ERR_INVALID;
        }
        upipe_audiobar->tenbit = true;
    } else
        upipe_audiobar->tenbit = false;

    upipe_audiobar_store_flow_def(upipe, flow_format);
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_format, &upipe_audiobar->hsize))
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_format, &upipe_audiobar->vsize))
    upipe_audiobar->chan_width =
        upipe_audiobar->hsize / upipe_audiobar->channels;
    upipe_audiobar->chan_width -= upipe_audiobar->chan_width % 2;
    if (unlikely(upipe_audiobar->chan_width < 16)) {
        upipe_warn_va(upipe,
                "channel width is too small to have a separation (%"PRIu64")",
                upipe_audiobar->chan_width);
    }
    upipe_audiobar->sep_width = upipe_audiobar->chan_width / 4;
    upipe_audiobar->sep_width -= upipe_audiobar->sep_width % 4;
    upipe_audiobar->pad_width = upipe_audiobar->hsize -
        upipe_audiobar->chan_width * upipe_audiobar->channels;
    upipe_notice_va(upipe,
            "setting up chan %"PRIu64" sep %"PRIu64" pad %"PRIu64,
            upipe_audiobar->chan_width, upipe_audiobar->sep_width,
            upipe_audiobar->pad_width);


    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_audiobar_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    struct upipe_audiobar *upipe_audiobar = upipe_audiobar_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    uref_dump(flow_def, upipe->uprobe);
    UBASE_RETURN(uref_flow_match_def(flow_def, INPUT_FLOW_DEF))
    uint8_t channels;
    UBASE_RETURN(uref_sound_flow_get_channels(flow_def, &channels))

    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(flow_def, &def)))) {
        flow_def = uref_dup(flow_def);
        if (flow_def == NULL)
            return UBASE_ERR_ALLOC;

        UBASE_FATAL(upipe,
                uref_sound_flow_get_channels(flow_def, &upipe_audiobar->channels))
        uref_sound_flow_clear_format(flow_def);
        UBASE_FATAL(upipe,
                uref_attr_import(flow_def, upipe_audiobar->flow_def_config))
        uref_pic_flow_clear_format(flow_def);
        UBASE_FATAL(upipe, uref_pic_flow_set_planes(flow_def, 0))
        UBASE_FATAL(upipe, uref_pic_flow_set_macropixel(flow_def, 1))
        UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"))
        UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 2, 1, 1, "u8"))
        UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 2, 1, 1, "v8"))
        UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 1, 1, 1, "a8"))
        UBASE_FATAL(upipe, uref_pic_set_progressive(flow_def, true))
        uref_dump(flow_def, upipe->uprobe);
        upipe_audiobar->hsize = upipe_audiobar->vsize =
            upipe_audiobar->sep_width = upipe_audiobar->pad_width = UINT64_MAX;

        upipe_audiobar_require_flow_format(upipe, flow_def);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This gets the value of the alpha channel.
 *
 * @param upipe description structure of the pipe
 * @param alpha_p filled in with the value of the alpha channel
 * (@see ubuf_pic_blit)
 * @return an error code
 */
static int _upipe_audiobar_get_alpha(struct upipe *upipe, uint8_t *alpha_p)
{
    struct upipe_audiobar *upipe_audiobar = upipe_audiobar_from_upipe(upipe);
    *alpha_p = upipe_audiobar->alpha;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the value of the alpha channel.
 *
 * @param upipe description structure of the pipe
 * @param alpha value of the alpha channel
 * @return an error code
 */
static int _upipe_audiobar_set_alpha(struct upipe *upipe, uint8_t alpha)
{
    struct upipe_audiobar *upipe_audiobar = upipe_audiobar_from_upipe(upipe);
    upipe_audiobar->alpha = alpha;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_audiobar_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_audiobar_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_audiobar_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_audiobar_set_flow_def(upipe, flow);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_audiobar_control_output(upipe, command, args);

        case UPIPE_AUDIOBAR_GET_ALPHA: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUDIO_BAR_SIGNATURE);
            uint8_t *alpha_p = va_arg(args, uint8_t *);
            return _upipe_audiobar_get_alpha(upipe, alpha_p);
        }
        case UPIPE_AUDIOBAR_SET_ALPHA: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUDIO_BAR_SIGNATURE);
            unsigned alpha = va_arg(args, unsigned);
            return _upipe_audiobar_set_alpha(upipe, alpha);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audiobar_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_audiobar *upipe_audiobar = upipe_audiobar_from_upipe(upipe);
    uref_free(upipe_audiobar->flow_def_config);
    upipe_audiobar_clean_flow_format(upipe);
    upipe_audiobar_clean_ubuf_mgr(upipe);
    upipe_audiobar_clean_output(upipe);
    upipe_audiobar_clean_urefcount(upipe);
    upipe_audiobar_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_audiobar_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AUDIO_BAR_SIGNATURE,

    .upipe_alloc = upipe_audiobar_alloc,
    .upipe_input = upipe_audiobar_input,
    .upipe_control = upipe_audiobar_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for audiobar pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_audiobar_mgr_alloc(void)
{
    return &upipe_audiobar_mgr;
}
