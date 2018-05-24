/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
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
 * @short Upipe avfilter module
 */

#include <upipe/upipe.h>
#include <upipe/uclock.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-av/upipe_avfilter.h>
#include <upipe-av/upipe_av_pixfmt.h>

#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>

/** upipe_avfilt structure */
struct upipe_avfilt {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output pipe */
    struct upipe *output;
    /** output flow def */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain requests;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** temporary uref storage */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;

    /** filter graph description */
    char *filters_desc;
    /** avfilter filter graph */
    AVFilterGraph *filter_graph;
    /** avfilter buffer source */
    AVFilterContext *buffersrc_ctx;
    /** avfilter buffer sink */
    AVFilterContext *buffersink_ctx;
    /** chroma map */
    const char *chroma_map[UPIPE_AV_MAX_PLANES];
    /** input pixel format */
    enum AVPixelFormat pix_fmt;
    /** input width */
    size_t width;
    /** input height */
    size_t height;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_avfilt_check(struct upipe *upipe, struct uref *flow_format);

/** @hidden */
static bool upipe_avfilt_handle(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_avfilt, upipe, UPIPE_AVFILT_SIGNATURE)
UPIPE_HELPER_VOID(upipe_avfilt)
UPIPE_HELPER_UREFCOUNT(upipe_avfilt, urefcount, upipe_avfilt_free)
UPIPE_HELPER_OUTPUT(upipe_avfilt, output, flow_def, output_state, requests)
UPIPE_HELPER_UBUF_MGR(upipe_avfilt, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_avfilt_check,
                      upipe_avfilt_register_output_request,
                      upipe_avfilt_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_avfilt, urefs, nb_urefs, max_urefs, blockers, upipe_avfilt_handle)

static void buffer_free_cb(void *opaque, uint8_t *data)
{
    struct uref *uref = opaque;

    uint64_t buffers;
    if (unlikely(!ubase_check(uref_attr_get_priv(uref, &buffers))))
        return;
    if (--buffers) {
        uref_attr_set_priv(uref, buffers);
        return;
    }

    const char *chroma;
    uref_pic_foreach_plane(uref, chroma) {
        uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
    }

    uref_free(uref);
}

static int upipe_avfilt_avframe_from_uref(struct upipe *upipe,
                                          struct uref *uref,
                                          AVFrame *frame)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);

    size_t hsize, vsize;
    if (unlikely(!ubase_check(uref_pic_size(uref, &hsize, &vsize, NULL)) ||
                 hsize != upipe_avfilt->width ||
                 vsize != upipe_avfilt->height))
        goto inval;

    for (int i = 0; i < UPIPE_AV_MAX_PLANES &&
         upipe_avfilt->chroma_map[i] != NULL; i++) {
        const uint8_t *data;
        size_t stride;
        uint8_t vsub;
        if (unlikely(!ubase_check(uref_pic_plane_read(uref, upipe_avfilt->chroma_map[i],
                                                      0, 0, -1, -1, &data)) ||
                     !ubase_check(uref_pic_plane_size(uref, upipe_avfilt->chroma_map[i],
                                                      &stride, NULL, &vsub, NULL))))
            goto inval;
        frame->data[i] = (uint8_t *)data;
        frame->linesize[i] = stride;
        frame->buf[i] = av_buffer_create(frame->data[i],
                                         stride * vsize / vsub,
                                         buffer_free_cb, uref,
                                         AV_BUFFER_FLAG_READONLY);
        if (frame->buf[i] == NULL) {
            uref_pic_plane_unmap(uref, upipe_avfilt->chroma_map[i],
                                 0, 0, -1, -1);
            goto inval;
        }

        /* use this as an avcodec refcount */
        uref_attr_set_priv(uref, i + 1);
    }

    frame->extended_data = frame->data;
    frame->width = hsize;
    frame->height = vsize;
    frame->key_frame = ubase_check(uref_pic_get_key(uref));
    frame->format = upipe_avfilt->pix_fmt;
    frame->interlaced_frame = !ubase_check(uref_pic_get_progressive(uref));
    frame->top_field_first = ubase_check(uref_pic_get_tff(uref));

    uint64_t number;
    if (ubase_check(uref_pic_get_number(uref, &number)))
        frame->coded_picture_number = number;

    uint64_t pts = UINT64_MAX;
    if (ubase_check(uref_clock_get_pts_prog(uref, &pts)))
        frame->pts = pts;

    uint64_t duration = UINT64_MAX;
    if (ubase_check(uref_clock_get_duration(uref, &duration)))
        frame->pkt_duration = duration;

    upipe_err_va(upipe, " input frame %d(%d) pts=%f duration=%f",
                 frame->display_picture_number,
                 frame->coded_picture_number,
                 (double) pts / UCLOCK_FREQ,
                 (double) duration / UCLOCK_FREQ);

    return UBASE_ERR_NONE;

inval:
    upipe_warn(upipe, "invalid buffer received");
    uref_free(uref);
    return UBASE_ERR_INVALID;
}

static void upipe_avfilt_output_frame(struct upipe *upipe, struct upump **upump_p,
                                      AVFrame *frame)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);

    struct uref *uref = uref_pic_alloc(upipe_avfilt->flow_def->mgr,
                                       upipe_avfilt->ubuf_mgr,
                                       frame->width,
                                       frame->height);
    if (unlikely(uref == NULL)) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return;
    }

    for (int i = 0; i < UPIPE_AV_MAX_PLANES &&
         upipe_avfilt->chroma_map[i] != NULL; i++) {
        uint8_t *data, hsub, vsub;
        size_t stride;
        if (unlikely(!ubase_check(uref_pic_plane_size(uref, upipe_avfilt->chroma_map[i],
                                                      &stride, &hsub, &vsub, NULL)) ||
                     !ubase_check(uref_pic_plane_write(uref, upipe_avfilt->chroma_map[i],
                                                       0, 0, -1, -1, &data))))
            goto err;
        for (int j = 0; j < frame->height / vsub; j++)
            memcpy(data + j * stride,
                   frame->data[i] + j * frame->linesize[i],
                   stride < frame->linesize[i] ? stride : frame->linesize[i]);
        uref_pic_plane_unmap(uref, upipe_avfilt->chroma_map[i], 0, 0, -1, -1);
    }

    uref_clock_set_pts_prog(uref, frame->pts);
    UBASE_ERROR(upipe, uref_clock_set_duration(uref, frame->pkt_duration))
    UBASE_ERROR(upipe, uref_pic_set_number(uref, frame->coded_picture_number))

    if (!frame->interlaced_frame)
        UBASE_ERROR(upipe, uref_pic_set_progressive(uref))
    else if (frame->top_field_first)
        UBASE_ERROR(upipe, uref_pic_set_tff(uref))

    if (frame->key_frame)
        UBASE_ERROR(upipe, uref_pic_set_key(uref))

    upipe_err_va(upipe, "output frame %d(%d) pts=%f duration=%f",
                 frame->display_picture_number,
                 frame->coded_picture_number,
                 (double) frame->pts / UCLOCK_FREQ,
                 (double) frame->pkt_duration / UCLOCK_FREQ);

    upipe_avfilt_output(upipe, uref, upump_p);
    return;

err:
    upipe_throw_error(upipe, UBASE_ERR_INVALID);
    uref_free(uref);
}

static int upipe_avfilt_init_filters(struct upipe *upipe,
                                     struct urational *sar,
                                     struct urational *fps)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);
    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    int ret, err;

    ret = UBASE_ERR_ALLOC;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    if (outputs == NULL || inputs == NULL) {
        upipe_err(upipe, "cannot allocate filter inputs/outputs");
        goto end;
    }

    upipe_avfilt->filter_graph = avfilter_graph_alloc();
    if (upipe_avfilt->filter_graph == NULL) {
        upipe_err(upipe, "cannot allocate filter graph");
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    char args[512];
    snprintf(args, sizeof (args),
             "video_size=%zux%zu:"
             "pix_fmt=%d:"
             "time_base=1/%"PRIu64":"
             "pixel_aspect=%"PRIu64"/%"PRIu64":"
             "frame_rate=%"PRIu64"/%"PRIu64,
             upipe_avfilt->width, upipe_avfilt->height,
             upipe_avfilt->pix_fmt,
             UCLOCK_FREQ,
             sar->num, sar->den,
             fps->num, fps->den);

    ret = UBASE_ERR_EXTERNAL;
    err = avfilter_graph_create_filter(&upipe_avfilt->buffersrc_ctx,
                                       buffersrc, "in",
                                       args, NULL,
                                       upipe_avfilt->filter_graph);
    if (err < 0) {
        upipe_err_va(upipe, "cannot create buffer source: %s",
                     av_err2str(err));
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    err = avfilter_graph_create_filter(&upipe_avfilt->buffersink_ctx,
                                       buffersink, "out",
                                       NULL, NULL,
                                       upipe_avfilt->filter_graph);
    if (err < 0) {
        upipe_err_va(upipe, "cannot create buffer sink: %s",
                     av_err2str(err));
        goto end;
    }

    enum AVPixelFormat pix_fmts[] = { upipe_avfilt->pix_fmt, AV_PIX_FMT_NONE };
    err = av_opt_set_int_list(upipe_avfilt->buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (err < 0) {
        upipe_err_va(upipe, "cannot set output pixel format: %s",
                     av_err2str(err));
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_desc.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_desc; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = upipe_avfilt->buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_desc; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = upipe_avfilt->buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((err = avfilter_graph_parse_ptr(upipe_avfilt->filter_graph,
                                        upipe_avfilt->filters_desc,
                                        &inputs, &outputs,
                                        NULL)) < 0) {
        upipe_err_va(upipe, "cannot parse filter graph: %s",
                     av_err2str(err));
        goto end;
    }

    if ((err = avfilter_graph_config(upipe_avfilt->filter_graph, NULL)) < 0) {
        upipe_err_va(upipe, "cannot configure filter graph: %s",
                     av_err2str(err));
        goto end;
    }

    ret = UBASE_ERR_NONE;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return ret;
}

static inline struct urational urational(AVRational v)
{
    return (struct urational){ .num = v.num, .den = v.den };
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 * @param flow_def_input description structure of the pipe
 * @return an error code
 */
static int upipe_avfilt_build_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);

    enum AVPixelFormat pix_fmt = av_buffersink_get_format(upipe_avfilt->buffersink_ctx);
    AVRational frame_rate = av_buffersink_get_frame_rate(upipe_avfilt->buffersink_ctx);
    int width = av_buffersink_get_w(upipe_avfilt->buffersink_ctx);
    int height = av_buffersink_get_h(upipe_avfilt->buffersink_ctx);
    AVRational sar = av_buffersink_get_sample_aspect_ratio(upipe_avfilt->buffersink_ctx);

    UBASE_RETURN(upipe_av_pixfmt_to_flow_def(pix_fmt, flow_def))
    UBASE_RETURN(uref_pic_flow_set_hsize(flow_def, width))
    UBASE_RETURN(uref_pic_flow_set_vsize(flow_def, height))
    UBASE_RETURN(uref_pic_flow_set_fps(flow_def, urational(frame_rate)))
    UBASE_RETURN(uref_pic_flow_set_sar(flow_def, urational(sar)))

    return UBASE_ERR_NONE;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_avfilt_handle(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);
    int ret, err;

    if (unlikely(ubase_check(uref_flow_get_def(uref, NULL)))) {
        struct uref *flow_def = uref;
        upipe_avfilt_store_flow_def(upipe, NULL);

        uint64_t hsize = 0, vsize = 0;
        uref_pic_flow_get_hsize(flow_def, &hsize);
        uref_pic_flow_get_vsize(flow_def, &vsize);
        upipe_avfilt->width = hsize;
        upipe_avfilt->height = vsize;

        upipe_avfilt->pix_fmt = upipe_av_pixfmt_from_flow_def(flow_def, NULL,
            upipe_avfilt->chroma_map);

        struct urational sar = { 1, 1 };
        uref_pic_flow_get_sar(flow_def, &sar);

        struct urational fps = { 1, 1 };
        uref_pic_flow_get_fps(flow_def, &fps);

        ret = upipe_avfilt_init_filters(upipe, &sar, &fps);
        if (unlikely(!ubase_check(ret))) {
            upipe_throw_fatal(upipe, ret);
            return true;
        }

        ret = upipe_avfilt_build_flow_def(upipe, flow_def);
        if (unlikely(!ubase_check(ret))) {
            upipe_throw_fatal(upipe, ret);
            return true;
        }

        upipe_avfilt_require_ubuf_mgr(upipe, flow_def);
        return true;
    }

    if (upipe_avfilt->flow_def == NULL)
        return false;

    AVFrame *frame = av_frame_alloc();
    AVFrame *filt_frame = av_frame_alloc();
    if (unlikely(frame == NULL || filt_frame == NULL)) {
        upipe_err_va(upipe, "cannot allocate av frame");
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        goto end;
    }

    ret = upipe_avfilt_avframe_from_uref(upipe, uref, frame);
    if (unlikely(!ubase_check(ret))) {
        upipe_throw_error(upipe, ret);
        goto end;
    }

    /* push the decoded frame into the filtergraph */
    if ((err = av_buffersrc_write_frame(upipe_avfilt->buffersrc_ctx,
                                        frame)) < 0) {
        upipe_err_va(upipe, "cannot write frame to filter graph: %s",
                     av_err2str(err));
        upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
        goto end;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        err = av_buffersink_get_frame(upipe_avfilt->buffersink_ctx, filt_frame);
        if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
            break;
        if (err < 0) {
            upipe_err_va(upipe, "cannot get frame from filter graph: %s",
                         av_err2str(err));
            upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
            goto end;
        }
        upipe_avfilt_output_frame(upipe, upump_p, filt_frame);
        av_frame_unref(filt_frame);
    }

end:
    av_frame_free(&frame);
    av_frame_free(&filt_frame);
    return true;
}

/** @internal @This handles input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_avfilt_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    if (!upipe_avfilt_check_input(upipe)) {
	upipe_avfilt_hold_input(upipe, uref);
	upipe_avfilt_block_input(upipe, upump_p);
    } else if (!upipe_avfilt_handle(upipe, uref, upump_p)) {
	upipe_avfilt_hold_input(upipe, uref);
	upipe_avfilt_block_input(upipe, upump_p);
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
static int upipe_avfilt_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);

    if (flow_format != NULL)
        upipe_avfilt_store_flow_def(upipe, flow_format);

    if (upipe_avfilt->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_avfilt_check_input(upipe);
    upipe_avfilt_output_input(upipe);
    upipe_avfilt_unblock_input(upipe);
    if (was_buffered && upipe_avfilt_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_avfilt_input. */
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
static int upipe_avfilt_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (unlikely(flow_def == NULL))
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, NULL))
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, NULL))

    const char *chroma_map[UPIPE_AV_MAX_PLANES];
    if (upipe_av_pixfmt_from_flow_def(flow_def, NULL,
                                      chroma_map) == AV_PIX_FMT_NONE) {
        upipe_err(upipe, "invalid pixel format");
        return UBASE_ERR_INVALID;
    }

    flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def);

    upipe_input(upipe, flow_def, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the filter graph description.
 *
 * @param upipe description structure of the pipe
 * @param filters_desc filter graph description
 * @return an error code
 */
static int _upipe_avfilt_set_filters_desc(struct upipe *upipe,
                                          const char *filters_desc)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);
    char *filters_desc_dup = strdup(filters_desc);
    UBASE_ALLOC_RETURN(filters_desc_dup);
    free(upipe_avfilt->filters_desc);
    upipe_avfilt->filters_desc = filters_desc_dup;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an avfilter pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_avfilt_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_avfilt_control_output(upipe, command, args))

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_avfilt_set_flow_def(upipe, flow_def);
        }
        case UPIPE_AVFILT_SET_FILTERS_DESC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFILT_SIGNATURE)
            const char *filters_desc = va_arg(args, const char *);
            return _upipe_avfilt_set_filters_desc(upipe, filters_desc);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates an avfilter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avfilt_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_avfilt_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_avfilt_init_urefcount(upipe);
    upipe_avfilt_init_ubuf_mgr(upipe);
    upipe_avfilt_init_output(upipe);
    upipe_avfilt_init_input(upipe);

    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);
    upipe_avfilt->filters_desc = NULL;
    upipe_avfilt->filter_graph = NULL;
    upipe_avfilt->buffersrc_ctx = NULL;
    upipe_avfilt->buffersink_ctx = NULL;
    upipe_avfilt->pix_fmt = AV_PIX_FMT_NONE;
    upipe_avfilt->width = 0;
    upipe_avfilt->height = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfilt_free(struct upipe *upipe)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);

    upipe_throw_dead(upipe);

    free(upipe_avfilt->filters_desc);
    avfilter_graph_free(&upipe_avfilt->filter_graph);

    upipe_avfilt_clean_input(upipe);
    upipe_avfilt_clean_output(upipe);
    upipe_avfilt_clean_ubuf_mgr(upipe);
    upipe_avfilt_clean_urefcount(upipe);
    upipe_avfilt_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_avfilt_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AVFILT_SIGNATURE,

    .upipe_alloc = upipe_avfilt_alloc,
    .upipe_input = upipe_avfilt_input,
    .upipe_control = upipe_avfilt_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for avfilter pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avfilt_mgr_alloc(void)
{
    return &upipe_avfilt_mgr;
}
