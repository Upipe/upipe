/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Rafaël Carré
 *          Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

#include <upipe/upipe.h>
#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/ubuf.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_flow_format.h>
#include <upipe-filters/upipe_audio_graph.h>
#include <upipe-filters/upipe_audio_max.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <math.h>

/** we only accept sound */
#define INPUT_FLOW_DEF "sound."
/** we only output pictures */
#define OUTPUT_FLOW_DEF "pic."

/** @hidden */
static bool upipe_agraph_handle(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p);
/** @hidden */
static int upipe_agraph_check_flow_format(struct upipe *upipe,
                                          struct uref *flow_format);
/** @hidden */
static int upipe_agraph_check_ubuf_mgr(struct upipe *upipe,
                                       struct uref *flow_format);

/** @internal @This is the private context of a agraph pipe */
struct upipe_agraph {
    /** refcount management structure */
    struct urefcount urefcount;

    /** configured flow_def */
    struct uref *flow_def_config;

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
    /** previous values */
    double *prev[255];

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** number of input channels */
    uint8_t channels;
    /** requested width */
    uint64_t hsize;
    /** requested height */
    uint64_t vsize;
    /** inferred channel width */
    uint64_t chan_width;
    /** inferred channel history */
    uint64_t chan_hist;
    /** inferred channel separation width */
    uint64_t sep_width;
    /** inferred padding at end of line */
    uint64_t pad_width;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_agraph, upipe, UPIPE_AUDIO_GRAPH_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_agraph, urefcount, upipe_agraph_free)
UPIPE_HELPER_FLOW(upipe_agraph, OUTPUT_FLOW_DEF)
UPIPE_HELPER_INPUT(upipe_agraph, urefs, nb_urefs, max_urefs, blockers,
                   upipe_agraph_handle)
UPIPE_HELPER_OUTPUT(upipe_agraph, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_FORMAT(upipe_agraph, request,
                         upipe_agraph_check_flow_format,
                         upipe_agraph_register_output_request,
                         upipe_agraph_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_agraph, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_agraph_check_ubuf_mgr,
                      upipe_agraph_register_output_request,
                      upipe_agraph_unregister_output_request)

/** @internal @This allocates a agraph pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_agraph_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_agraph_alloc_flow(mgr, uprobe, signature,
                                                  args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_agraph *upipe_agraph = upipe_agraph_from_upipe(upipe);
    upipe_agraph_init_urefcount(upipe);
    upipe_agraph_init_input(upipe);
    upipe_agraph_init_output(upipe);
    upipe_agraph_init_ubuf_mgr(upipe);
    upipe_agraph_init_flow_format(upipe);
    upipe_agraph->flow_def_config = flow_def;
    upipe_agraph->hsize = upipe_agraph->vsize =
        upipe_agraph->sep_width = upipe_agraph->pad_width = UINT64_MAX;

    for (int i = 0; i < 255; i++) {
        upipe_agraph->peak[i] = 0.;
        upipe_agraph->peak_date[i] = 0;
        upipe_agraph->prev[i] = NULL;
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
 * @param dst array of destination chromas (planar YUV422)
 * @param strides array of strides for each chroma
 * @param hsubs array of hsubs of each chroma
 * @param vsubs array of vsubs of each chroma
 * @param color 32-bit color code
 * @param row row number
 * @param col colon number
 * @param w size of the copy
 */
static void copy_color(uint8_t **dst, size_t *strides,
                       uint8_t *hsubs, uint8_t *vsubs, const uint8_t *color,
                       unsigned row, unsigned col, unsigned w)
{
    memset(&dst[0][(row / vsubs[0]) * strides[0] + col / hsubs[0]],
           color[0], w / hsubs[0]); // y8
    memset(&dst[1][(row / vsubs[1]) * strides[1] + col / hsubs[1]],
           color[1], w / hsubs[1]); // u8
    memset(&dst[2][(row / vsubs[2]) * strides[2] + col / hsubs[2]],
           color[2], w / hsubs[2]); // v8
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 * @return true if the packet was handled
 */
static bool upipe_agraph_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_agraph *upipe_agraph = upipe_agraph_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        UBASE_FATAL(upipe,
                uref_sound_flow_get_channels(uref, &upipe_agraph->channels))
        uref_sound_flow_clear_format(uref);
        UBASE_FATAL(upipe,
                uref_attr_import(uref, upipe_agraph->flow_def_config))
        uref_pic_flow_clear_format(uref);
        UBASE_FATAL(upipe, uref_pic_flow_set_planes(uref, 0))
        UBASE_FATAL(upipe, uref_pic_flow_set_macropixel(uref, 1))
        UBASE_FATAL(upipe, uref_pic_flow_add_plane(uref, 1, 1, 1, "y8"))
        UBASE_FATAL(upipe, uref_pic_flow_add_plane(uref, 2, 1, 1, "u8"))
        UBASE_FATAL(upipe, uref_pic_flow_add_plane(uref, 2, 1, 1, "v8"))
        UBASE_FATAL(upipe, uref_pic_set_progressive(uref))

        upipe_agraph->hsize = upipe_agraph->vsize =
            upipe_agraph->sep_width = upipe_agraph->pad_width = UINT64_MAX;
        upipe_agraph_require_flow_format(upipe, uref);
        return true;
    }

    if (!upipe_agraph->ubuf_mgr)
        return false;

    if (unlikely(upipe_agraph->hsize == UINT64_MAX))
        return false;

    struct ubuf *ubuf = ubuf_pic_alloc(upipe_agraph->ubuf_mgr,
                                       upipe_agraph->hsize,
                                       upipe_agraph->vsize);
    uref_attach_ubuf(uref, ubuf);

    uint8_t *dst[4];
    size_t strides[4];
    uint8_t hsubs[4];
    uint8_t vsubs[4];
    static const char *chroma[3] = { "y8", "u8", "v8" };
    for (int i = 0; i < 3; i++) {
        if (unlikely(!ubase_check(uref_pic_plane_write(uref, chroma[i],
                            0, 0, -1, -1, &dst[i])) ||
                     !ubase_check(uref_pic_plane_size(uref, chroma[i],
                             &strides[i], &hsubs[i], &vsubs[i], NULL)))) {
             upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
             uref_free(uref);
             return true;
        }
    }

    uint64_t h = upipe_agraph->vsize;
    const int hred = h - (iec_scale(-8.) * h);
    const int hyellow = h - (iec_scale(-18.) * h);
    uint8_t transparent[3] = { 0x10, 0x80, 0x80 };
    uint8_t black[3] = { 0x10, 0x80, 0x80 };
    uint8_t red[2][3] = { { 76, 85, 0xff }, { 37, 106, 191 } };
    uint8_t green[2][3] = { { 150, 44, 21 }, { 74, 85, 74 } };
    uint8_t yellow[2][3] = { { 226, 1, 148 }, { 112, 64, 138 } };

    uint64_t pts = 0;
    if (unlikely(!ubase_check(uref_clock_get_pts_prog(uref, &pts)))) {
        upipe_warn(upipe, "unable to read pts");
    }

    for (uint8_t chan = 0; chan < upipe_agraph->channels; chan++) {
        double amplitude = 0.;
        if (unlikely(!ubase_check(uref_amax_get_amplitude(uref, &amplitude,
                                                          chan))))
            upipe_warn_va(upipe, "unable to get amplitude for channel %"PRIu8", assuming silence", chan);

        double scale = log10(amplitude) * 20;

        // IEC-268-18 return time speed is 20dB per 1.7s (+/- .3)
        if (upipe_agraph->peak_date[chan])
            upipe_agraph->peak[chan] -= 20 * (pts - upipe_agraph->peak_date[chan]) / (1.7 * UCLOCK_FREQ);

        upipe_agraph->peak_date[chan] = pts;

        if (scale >= upipe_agraph->peak[chan]) /* higher than lowered peak */
            upipe_agraph->peak[chan] = scale;
        else /* Current amplitude can not go below the lowered peak value */
            scale = upipe_agraph->peak[chan];

        scale = iec_scale(scale);

        if (unlikely(upipe_agraph->prev[chan] == NULL)) {
            upipe_agraph->prev[chan] =
                malloc(upipe_agraph->chan_hist * sizeof(double));
            if (unlikely(upipe_agraph->prev[chan] == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                uref_free(uref);
                return true;
            }
            for (uint64_t i = 0; i < upipe_agraph->chan_hist - 1; i++)
                upipe_agraph->prev[chan][i] = 0.;
        } else {
            memmove(&upipe_agraph->prev[chan][0], &upipe_agraph->prev[chan][1],
                    (upipe_agraph->chan_hist - 1) * sizeof(double));
        }

        upipe_agraph->prev[chan][upipe_agraph->chan_hist - 1] = scale;

        for (uint64_t i = 0; i < upipe_agraph->chan_hist; i++) {
            scale = upipe_agraph->prev[chan][i];
            const int hmax = h - scale * h;
            for (int row = 0; row < h; row++) {
                bool bright = (i == upipe_agraph->chan_hist - 1);

                const uint8_t *color = row < hmax ? black :
                                       row < hred ? red[!bright] :
                                       row < hyellow ? yellow[!bright] :
                                       green[!bright];

                if (!i && upipe_agraph->sep_width)
                    copy_color(dst, strides, hsubs, vsubs, black, row,
                               chan * upipe_agraph->chan_width,
                               upipe_agraph->sep_width);
                copy_color(dst, strides, hsubs, vsubs, color, row,
                           upipe_agraph->sep_width +
                           chan * upipe_agraph->chan_width + 2 * i,
                           2);

                if (chan == upipe_agraph->channels - 1 &&
                    upipe_agraph->pad_width)
                    copy_color(dst, strides, hsubs, vsubs, transparent, row,
                               (chan + 1) * upipe_agraph->chan_width,
                               upipe_agraph->pad_width);
            }
        }
    }

    /* dB marks */
    for (int i = 1; i <= 6; i++) {
        int row = h - (iec_scale(-10 * i) * h);
        copy_color(dst, strides, hsubs, vsubs, black, row, 0,
                   upipe_agraph->hsize);
    }

    for (int i = 0; i < 3; i++)
        ubuf_pic_plane_unmap(ubuf, chroma[i], 0, 0, -1, -1);
    upipe_agraph_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_agraph_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    if (!upipe_agraph_check_input(upipe)) {
        upipe_agraph_hold_input(upipe, uref);
        upipe_agraph_block_input(upipe, upump_p);
    } else if (!upipe_agraph_handle(upipe, uref, upump_p)) {
        upipe_agraph_hold_input(upipe, uref);
        upipe_agraph_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This provides a flow_format request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_agraph_check_flow_format(struct upipe *upipe,
                                          struct uref *flow_format)
{
    struct upipe_agraph *upipe_agraph = upipe_agraph_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_agraph_require_ubuf_mgr(upipe, flow_format);

    return UBASE_ERR_NONE;
}

/** @internal @This provides a ubuf_mgr request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_agraph_check_ubuf_mgr(struct upipe *upipe,
                                       struct uref *flow_format)
{
    struct upipe_agraph *upipe_agraph = upipe_agraph_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_NONE;

    upipe_agraph_store_flow_def(upipe, flow_format);
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_format, &upipe_agraph->hsize))
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_format, &upipe_agraph->vsize))
    upipe_agraph->chan_width =
        upipe_agraph->hsize / upipe_agraph->channels;
    upipe_agraph->chan_width -= upipe_agraph->chan_width % 2;
    if (unlikely(upipe_agraph->chan_width < 16)) {
        upipe_warn_va(upipe,
                "channel width is too small to have a separation (%"PRIu64")",
                upipe_agraph->chan_width);
    }
    upipe_agraph->sep_width = 2;
    upipe_agraph->chan_hist =
        (upipe_agraph->chan_width - upipe_agraph->sep_width) / 2;
    upipe_agraph->pad_width = upipe_agraph->hsize -
        upipe_agraph->chan_width * upipe_agraph->channels;
    upipe_notice_va(upipe,
            "setting up chan %"PRIu64" sep %"PRIu64" pad %"PRIu64,
            upipe_agraph->chan_width, upipe_agraph->sep_width,
            upipe_agraph->pad_width);

    for (int i = 0; i < 255; i++) {
        free(upipe_agraph->prev[i]);
        upipe_agraph->prev[i] = NULL;
    }

    bool was_buffered = !upipe_agraph_check_input(upipe);
    upipe_agraph_output_input(upipe);
    upipe_agraph_unblock_input(upipe);
    if (was_buffered && upipe_agraph_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_agraph_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_agraph_set_flow_def(struct upipe *upipe,
                                     struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    uref_dump(flow_def, upipe->uprobe);
    UBASE_RETURN(uref_flow_match_def(flow_def, INPUT_FLOW_DEF))
    uint8_t channels;
    UBASE_RETURN(uref_sound_flow_get_channels(flow_def, &channels))

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;
    upipe_input(upipe, flow_def_dup, NULL);
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
static int upipe_agraph_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_agraph_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_agraph_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_agraph_set_flow_def(upipe, flow);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_agraph_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_agraph_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_agraph *upipe_agraph = upipe_agraph_from_upipe(upipe);
    for (int i = 0; i < 255; i++)
        free(upipe_agraph->prev[i]);
    uref_free(upipe_agraph->flow_def_config);
    upipe_agraph_clean_flow_format(upipe);
    upipe_agraph_clean_ubuf_mgr(upipe);
    upipe_agraph_clean_output(upipe);
    upipe_agraph_clean_input(upipe);
    upipe_agraph_clean_urefcount(upipe);
    upipe_agraph_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_agraph_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AUDIO_GRAPH_SIGNATURE,

    .upipe_alloc = upipe_agraph_alloc,
    .upipe_input = upipe_agraph_input,
    .upipe_control = upipe_agraph_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for agraph pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_agraph_mgr_alloc(void)
{
    return &upipe_agraph_mgr;
}
