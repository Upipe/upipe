/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short Upipe avcodec decode module
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/ubuf.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_block.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-av/upipe_avcodec_dec_vid.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <libavcodec/avcodec.h>
#include "upipe_av_internal.h"

#define ALIVE() { printf("# ALIVE: %s %s - %d\n", __FILE__, __func__, __LINE__); } // FIXME - debug - remove this

/** upipe_avcdv structure with avcdv parameters */ 
struct upipe_avcdv {
    /** input flow */
    struct uref *input_flow;
    /** output flow */
    struct uref *output_flow;
    /** true if the flow definition has already been sent */
    bool output_flow_sent;

    /** output pipe */
    struct upipe *output;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** uref manager */
    struct uref_mgr *uref_mgr;

    /** frame counter */
    uint64_t counter;
    /** latest incoming uref */
    struct uref *uref;

    /** avcodec codec */
    AVCodec *codec;
    /** avcodec context */
    AVCodecContext *context;
    /** avcodec frame */
    AVFrame *frame;

    /** true if we have thrown the ready event */
    bool ready;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};


UPIPE_HELPER_UPIPE(upipe_avcdv, upipe);
UPIPE_HELPER_UREF_MGR(upipe_avcdv, uref_mgr);
UPIPE_HELPER_OUTPUT(upipe_avcdv, output, output_flow, output_flow_sent)
UPIPE_HELPER_UBUF_MGR(upipe_avcdv, ubuf_mgr);

/** @internal
 */
enum plane_action {
    UNMAP,
    READ,
    WRITE
};

/** @internal @This fetches chroma from uref
 *  
 * @param ubuf ubuf structure
 * @param str name of the chroma
 * @param strides strides array
 * @param slices array of pointers to data plans
 * @param idx index of the chroma in slices[]/strides[]
 */
static void inline upipe_avcdv_fetch_chroma(struct ubuf *ubuf, const char *str, int *strides, uint8_t **slices, size_t idx, enum plane_action action)
{
    size_t stride = 0;
    switch(action) {

    case READ:
        ubuf_pic_plane_read(ubuf, str, 0, 0, -1, -1, (const uint8_t**)slices+idx);
        break;
    case WRITE:
        ubuf_pic_plane_write(ubuf, str, 0, 0, -1, -1, slices+idx);
        break;
    case UNMAP:
        ubuf_pic_plane_unmap(ubuf, str, 0, 0, -1, -1);
        slices[idx] = NULL;
        return;
    }
    ubuf_pic_plane_size(ubuf, str, &stride, NULL, NULL, NULL);
    strides[idx] = (int) stride;
}

/** @internal
 */
static void upipe_avcdv_map_frame(struct ubuf *ubuf, int *strides, uint8_t **slices, enum plane_action action)
{
    // FIXME - hardcoded YUV chroma fetch
    upipe_avcdv_fetch_chroma(ubuf, "y8", strides, slices, 0, action);
    upipe_avcdv_fetch_chroma(ubuf, "u8", strides, slices, 1, action);
    upipe_avcdv_fetch_chroma(ubuf, "v8", strides, slices, 2, action);
}

/* Documentation from libavcodec.h (get_buffer) :
 * The function will set AVFrame.data[], AVFrame.linesize[].
 * AVFrame.extended_data[] must also be set, but it should be the same as
 * AVFrame.data[] except for planar audio with more channels than can fit
 * in AVFrame.data[].  In that case, AVFrame.data[] shall still contain as
 * many data pointers as it can hold.  if CODEC_CAP_DR1 is not set then
 * get_buffer() must call avcodec_default_get_buffer() instead of providing
 * buffers allocated by some other means.
 * 
 * AVFrame.data[] should be 32- or 16-byte-aligned unless the CPU doesn't
 * need it.  avcodec_default_get_buffer() aligns the output buffer
 * properly, but if get_buffer() is overridden then alignment
 * considerations should be taken into account.
 * 
 * If pic.reference is set then the frame will be read later by libavcodec.
 * avcodec_align_dimensions2() should be used to find the required width
 * and height, as they normally need to be rounded up to the next multiple
 * of 16.
 * 
 * If frame multithreading is used and thread_safe_callbacks is set, it may
 * be called from a different thread, but not from more than one at once.
 * Does not need to be reentrant.
 */

/** @internal @This is called by avcodec when allocating a new frame
 * @param context current avcodec context
 * @param frame avframe handler entering avcodec black magic box
 */
static int upipe_avcdv_get_buffer(struct AVCodecContext *context, AVFrame *frame)
{
    struct upipe *upipe = context->opaque;
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    struct ubuf *ubuf_pic;
    int width_aligned, height_aligned;

    frame->opaque = uref_dup(upipe_avcdv->uref);

    uint64_t framenum = 0; // DEBUG
    uref_attr_get_unsigned(frame->opaque, &framenum, "avcdv.framenum");
    ulog_debug(upipe->ulog, "Allocating frame for %u (%p) - %ux%u",
                framenum, frame->opaque, frame->width, frame->height);

    if (frame->format != PIX_FMT_YUV420P) { // TODO: support different frame format
        ulog_error(upipe->ulog, "Frame format != yuv420p");
        return 0;
    }

    // Direct Rendering - allocate ubuf pic
    if (upipe_avcdv->codec->capabilities & CODEC_CAP_DR1) {
        width_aligned = frame->width;
        height_aligned = frame->height;

        // Use avcodec width/height alignement, then resize pic
        avcodec_align_dimensions(context, &width_aligned, &height_aligned);
        ubuf_pic = ubuf_pic_alloc(upipe_avcdv->ubuf_mgr, width_aligned, height_aligned);

        if (likely(ubuf_pic)) {
            ubuf_pic_resize(ubuf_pic, 0, 0, frame->width, frame->height);
            upipe_avcdv_map_frame(ubuf_pic, frame->linesize, frame->data, WRITE);
            uref_attach_ubuf(frame->opaque, ubuf_pic);
            frame->data[3] = NULL;
            frame->linesize[3] = 0;
            frame->extended_data = frame->data;
            frame->type = FF_BUFFER_TYPE_USER;
            
            return 1; // success
        } else {
            ulog_debug(upipe->ulog, "ubuf_pic_alloc(%d, %d) failed, fallback", width_aligned, height_aligned);
        }
    }

    // Default : DR failed or not available
    return avcodec_default_get_buffer(context, frame);
}

/** @internal
 * @param context current avcodec context
 * @param frame avframe handler released by avcodec black magic box
 */
static void upipe_avcdv_release_buffer(struct AVCodecContext *context, AVFrame *frame)
{
    struct upipe *upipe = context->opaque;
    struct uref *uref = frame->opaque;

    uint64_t framenum = 0; // DEBUG
    uref_attr_get_unsigned(uref, &framenum, "avcdv.framenum");
    ulog_debug(upipe->ulog, "Releasing frame %u (%p)", (uint64_t) framenum, uref);

    if (likely(uref->ubuf)) {
        upipe_avcdv_map_frame(uref->ubuf, frame->linesize, frame->data, UNMAP);
    } else {
        avcodec_default_release_buffer(context, frame);
    }
    uref_free(uref);
}

/** @internal @This opens a new avcodec context
 * @param upipe description structure of the pipe
 * @param codec_def avcodec codec flow definition string
 * @return false in case of error
 */
static bool upipe_avcdv_set_context(struct upipe *upipe, const char *codec_def)
{
    AVCodecContext *context;
    AVCodec *codec;
    int codec_id;
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);

    if (unlikely(upipe_avcdv->context)) { // Close previously opened context
        ulog_debug(upipe->ulog, "avcodec context closed");
        avcodec_close(upipe_avcdv->context);
        if (upipe_avcdv->context->extradata_size > 0) {
            free(upipe_avcdv->context->extradata);
        }
        av_free(upipe_avcdv->context);
        upipe_avcdv->context = NULL;
    }

    if (!codec_def) { // null pointer, nothing else to be done
        return true;
    }
    codec_id = upipe_av_from_flow_def(codec_def);
    if (!codec_id) {
        ulog_debug(upipe->ulog, "codec %s not found", codec_def);
        return false;
    }

    codec = avcodec_find_decoder(codec_id);
    if (unlikely(!codec)) {
        ulog_warning(upipe->ulog, "codec %d not found", codec_id);
        return false;
    }
    context = avcodec_alloc_context3(codec);
    if (unlikely(!context)) {
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return false;
    }
    context->opaque = upipe;
    context->get_buffer = upipe_avcdv_get_buffer;
    context->release_buffer = upipe_avcdv_release_buffer;
    context->flags |= CODEC_FLAG_EMU_EDGE;
    context->extradata = NULL;
    context->extradata_size = 0;

    if (unlikely(avcodec_open2(context, codec, NULL) < 0)) {
        ulog_warning(upipe->ulog, "could not open codec");
        av_free(context);
        return false;
    }

    upipe_avcdv->context = context;
    upipe_avcdv->codec = codec;
    upipe_avcdv->counter = 0;
    return true;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param frame AVFrame structure
 */
static void upipe_avcdv_output_frame(struct upipe *upipe, AVFrame *frame,
                                     struct upump *upump)
{
    struct ubuf *ubuf;
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    struct uref *uref = uref_dup(frame->opaque);

    uint8_t *data, *src, hsub, vsub;
    const char *chroma = NULL; 
    size_t sstride, dstride;
    int i, j;

    // if uref has no attached ubuf (ie DR not supported)
    if (unlikely(!uref->ubuf)) {
        ubuf = ubuf_pic_alloc(upipe_avcdv->ubuf_mgr, frame->width, frame->height);
        if (!ubuf) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            return;
        }

        i = j = 0;
        while (ubuf_pic_plane_iterate(ubuf, &chroma) && chroma && i < 3) {
            ubuf_pic_plane_write(ubuf, chroma, 0, 0, -1, -1, &data);
            ubuf_pic_plane_size(ubuf, chroma, &dstride, &hsub, &vsub, NULL);
            src = frame->data[i];
            sstride = frame->linesize[i];
            for (j = 0; j < frame->height/vsub; j++) {
                memcpy(data, src, frame->width/hsub);
                data += dstride;
                src += sstride;
            }
            ubuf_pic_plane_unmap(ubuf, chroma, 0, 0, -1, -1);
            i++;
        }

        uref_attach_ubuf(uref, ubuf);
    }
    upipe_avcdv_output(upipe, uref, upump);
}


/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static void upipe_avcdv_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    AVPacket avpkt;
    AVFrame *frame; 
    uint8_t *inbuf;
    size_t insize;
    int gotframe = 0, len;
    uint64_t framenum = 0;

    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);

    
    /* avcodec input buffer needs to be at least 4-byte aligned and
       FF_INPUT_BUFFER_PADDING_SIZE larger than actual input size.
       Thus, extract ubuf content in a properly allocated buffer.
       Padding must be zeroed. */
    uref_block_size(uref, &insize);
    ulog_debug(upipe->ulog, "Received packet %u - size : %u", upipe_avcdv->counter, insize);
    inbuf = malloc(insize + FF_INPUT_BUFFER_PADDING_SIZE);
    if (unlikely(!inbuf)) {
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return;
    }
    memset(inbuf, 0, insize + FF_INPUT_BUFFER_PADDING_SIZE);
    uref_block_extract(uref, 0, insize, inbuf); 
    ubuf_free(uref_detach_ubuf(uref));

    // Init avcodec packed and attach input buffer
    av_init_packet(&avpkt);
    avpkt.size = insize;
    avpkt.data = inbuf;

    frame = upipe_avcdv->frame;
    uref_attr_set_unsigned(uref, upipe_avcdv->counter, "avcdv.framenum"); // DEBUG
    upipe_avcdv->uref = uref;
    len = avcodec_decode_video2(upipe_avcdv->context, frame, &gotframe, &avpkt);
    if (len < 0) {
        ulog_warning(upipe->ulog, "Error while decoding frame");
    }

    // Copy frame to ubuf_pic if any frame has been decoded
    if (gotframe) {

        uref_attr_get_unsigned(frame->opaque, &framenum, "avcdv.framenum"); // DEBUG
        ulog_debug(upipe->ulog, "%u\t - Picture decoded ! %dx%d - %u",
                upipe_avcdv->counter, frame->width, frame->height, (uint64_t) framenum);

        // FIXME DEVEL
        if (!upipe_avcdv->output_flow) {
            struct uref *outflow = uref_pic_flow_alloc_def(upipe_avcdv->uref_mgr, 1);
            upipe_avcdv_store_flow_def(upipe, outflow);
        }

        upipe_avcdv_output_frame(upipe, frame, upump);
    }

    uref_free(uref);
    free(inbuf);
    upipe_avcdv->counter++;
}

/** @internal @This handles extradata
 *
 * @param upipe description structure of the pipe
 * @param extradata pointer to extradata buffer
 * @param size extradata size
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_avcdv_copy_extradata(struct upipe *upipe, const uint8_t *extradata, int size)
{
    uint8_t *buf;
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    AVCodecContext *context = upipe_avcdv->context;

    if (unlikely( (!extradata) || (size <= 0) )) {
        ulog_warning(upipe->ulog, "Received invalid or empty extradata");
        return false;
    }
    if (unlikely(!context)) {
        ulog_warning(upipe->ulog, "Received extradata before opening context");
        return false;
    }

    buf = malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!buf) {
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return false;
    }

    memset(buf+size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(buf, extradata, size);

    if (context->extradata_size) {
        free(context->extradata);
        ulog_debug(upipe->ulog, "Freeing current extradata");
    }
    context->extradata = buf;
    context->extradata_size = size;

    avcodec_close(context); // extradata must be present *before* opening codec
    avcodec_open2(context, upipe_avcdv->codec, NULL);

    ulog_debug(upipe->ulog, "Received extradata (%d bytes) - codec closed/reopened", size);
    return true;
}


/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */

static bool _upipe_avcdv_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    switch (command) {
        // generic linear stuff
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_avcdv_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_avcdv_set_uref_mgr(upipe, uref_mgr);
        }
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_avcdv_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_avcdv_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_avcdv_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_avcdv_set_output(upipe, output);
        }

        // specific avcdv
#if 0
        case UPIPE_AVCDV_GET_CODEC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCDV_SIGNATURE);
            const char **url_p = va_arg(args, const char **);
            return _upipe_AVCDV_get_CODEC(upipe, url_p);
        }
#endif
        case UPIPE_AVCDV_SET_CODEC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCDV_SIGNATURE);
            const char *codec = va_arg(args, const char *);
            return upipe_avcdv_set_context(upipe, codec);
        }
        case UPIPE_AVCDV_SET_EXTRADATA: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCDV_SIGNATURE);
            const uint8_t *extradata = va_arg(args, const uint8_t *);
            int size = va_arg(args, int);
            return upipe_avcdv_copy_extradata(upipe, extradata, size);
        }

        default:
            return false;
    }
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */

static bool upipe_avcdv_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    int ret = _upipe_avcdv_control(upipe, command, args);
   
    // FIXME - check context
    if (upipe_avcdv->output && upipe_avcdv->ubuf_mgr) {
        if (!upipe_avcdv->ready) {
            upipe_avcdv->ready = true;
            upipe_throw_ready(upipe);
        }
    }
    return ret;
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdv_use(struct upipe *upipe)
{
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    urefcount_use(&upipe_avcdv->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdv_release(struct upipe *upipe)
{
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_avcdv->refcount))) {
        ulog_debug(upipe->ulog, "Releasing pipe (%p)", upipe);

        if (upipe_avcdv->context) {
            upipe_avcdv_set_context(upipe, NULL);
        }
        if (upipe_avcdv->frame) {
            av_free(upipe_avcdv->frame);
        }

        if (upipe_avcdv->input_flow) {
            uref_free(upipe_avcdv->input_flow);
        }

        upipe_avcdv_clean_output(upipe);
        upipe_avcdv_clean_ubuf_mgr(upipe);
        upipe_avcdv_clean_uref_mgr(upipe);

        upipe_clean(upipe);
        urefcount_clean(&upipe_avcdv->refcount);
        free(upipe_avcdv);
    }
}

/** @internal @This allocates a avcdv pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param ulog structure used to output logs
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avcdv_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe, struct ulog *ulog)
{
    struct upipe_avcdv *upipe_avcdv = malloc(sizeof(struct upipe_avcdv));
    if (unlikely(upipe_avcdv == NULL))
        return NULL;
    struct upipe *upipe = upipe_avcdv_to_upipe(upipe_avcdv);
    upipe_init(upipe, mgr, uprobe, ulog);

    urefcount_init(&upipe_avcdv->refcount);
    upipe_avcdv_init_uref_mgr(upipe);
    upipe_avcdv_init_ubuf_mgr(upipe);
    upipe_avcdv_init_output(upipe);
    upipe_avcdv->input_flow = NULL;
    upipe_avcdv->codec = NULL;
    upipe_avcdv->context = NULL;
    upipe_avcdv->frame = avcodec_alloc_frame();

    upipe_avcdv->ready = false;
    return upipe;
}

/** module manager static descriptor */
static struct upipe_mgr upipe_avcdv_mgr = {
    .signature = UPIPE_AVCDV_SIGNATURE,

    .upipe_alloc = upipe_avcdv_alloc,
    .upipe_input = upipe_avcdv_input,
    .upipe_control = upipe_avcdv_control,
    .upipe_use = upipe_avcdv_use,
    .upipe_release = upipe_avcdv_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for avcdv pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avcdv_mgr_alloc(void)
{
    return &upipe_avcdv_mgr;
}
