/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
#include <upipe/upipe_helper_upump_mgr.h>
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

#define EXPECTED_FLOW "block."

/** @internal @This are the parameters passed to avcodec_open2 by
 * upipe_avcodec_open_cb()
 */
struct upipe_avcodec_open_params {
    AVCodec *codec;
    uint8_t *extradata;
    int extradata_size;
};

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
    /** upump mgr */
    struct upump_mgr *upump_mgr;

    /** avcodec_open watcher */
    struct upump *upump_av_deal;
    // FIXME: use a list of saved uref/upump ?
    /** incoming upump kept in memory because of open_codec waiting */
    #if 0 // upump are not refcounted and therefore cannot be stored
    struct upump *saved_upump;
    #else // saving upump_mgr instead
    struct upump_mgr *saved_upump_mgr;
    #endif
    /** incoming uref kept in memory because of open_codec waiting */
    struct uref *saved_uref;

    /** frame counter */
    uint64_t counter;
    /** latest incoming uref */
    struct uref *uref;

    /** avcodec context */
    AVCodecContext *context;
    /** avcodec frame */
    AVFrame *frame;
    /** avcodec_open parameters */
    struct upipe_avcodec_open_params open_params;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};


UPIPE_HELPER_UPIPE(upipe_avcdv, upipe);
UPIPE_HELPER_UREF_MGR(upipe_avcdv, uref_mgr);
UPIPE_HELPER_OUTPUT(upipe_avcdv, output, output_flow, output_flow_sent)
UPIPE_HELPER_UBUF_MGR(upipe_avcdv, ubuf_mgr);
UPIPE_HELPER_UPUMP_MGR(upipe_avcdv, upump_mgr, upump_av_deal)

/** @internal
 */
enum plane_action {
    UNMAP,
    READ,
    WRITE
};

/** @internal */
static void upipe_avcdv_use(struct upipe *upipe);
/** @internal */
static void upipe_avcdv_release(struct upipe *upipe);
/** @internal */
static bool upipe_avcdv_process_buf(struct upipe *upipe, uint8_t *buf,
                                    size_t size, struct upump *upump);

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

    upipe_dbg_va(upipe, "Allocating frame for %u (%p) - %ux%u",
                 framenum, frame->opaque, frame->width, frame->height);

    if (context->pix_fmt != PIX_FMT_YUV420P) { // TODO: support different frame format
        upipe_err_va(upipe, "Frame format != yuv420p (%d)", context->pix_fmt);
        return 0;
    }

    // Direct Rendering - allocate ubuf pic
    if (upipe_avcdv->context->codec->capabilities & CODEC_CAP_DR1) {
        width_aligned = context->width;
        height_aligned = context->height;

        // Use avcodec width/height alignement, then resize pic
        avcodec_align_dimensions(context, &width_aligned, &height_aligned);
        ubuf_pic = ubuf_pic_alloc(upipe_avcdv->ubuf_mgr, width_aligned, height_aligned);

        if (likely(ubuf_pic)) {
            ubuf_pic_resize(ubuf_pic, 0, 0, context->width, context->height);
            upipe_avcdv_map_frame(ubuf_pic, frame->linesize, frame->data, WRITE);
            uref_attach_ubuf(frame->opaque, ubuf_pic);
            frame->data[3] = NULL;
            frame->linesize[3] = 0;
            frame->extended_data = frame->data;
            frame->type = FF_BUFFER_TYPE_USER;
            
            return 1; // success
        } else {
            upipe_dbg_va(upipe, "ubuf_pic_alloc(%d, %d) failed, fallback", width_aligned, height_aligned);
        }
    }

    // Default : DR failed or not available
    return avcodec_default_get_buffer(context, frame);
}

/** @internal @This is called by avcodec when releasing a frame
 * @param context current avcodec context
 * @param frame avframe handler released by avcodec black magic box
 */
static void upipe_avcdv_release_buffer(struct AVCodecContext *context, AVFrame *frame)
{
    struct upipe *upipe = context->opaque;
    struct uref *uref = frame->opaque;

    uint64_t framenum = 0; // DEBUG
    uref_attr_get_unsigned(uref, &framenum, "avcdv.framenum");

    upipe_dbg_va(upipe, "Releasing frame %u (%p)", (uint64_t) framenum, uref);

    if (likely(uref->ubuf)) {
        upipe_avcdv_map_frame(uref->ubuf, frame->linesize, frame->data, UNMAP);
    } else {
        avcodec_default_release_buffer(context, frame);
    }
    uref_free(uref);
}

/** @This aborts and frees an existing upump watching for exclusive access to
 * avcodec_open().
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdv_abort_av_deal(struct upipe *upipe)
{
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    if (unlikely(upipe_avcdv->upump_av_deal != NULL)) {
        upipe_av_deal_abort(upipe_avcdv->upump_av_deal);
        upump_free(upipe_avcdv->upump_av_deal);
        upipe_avcdv->upump_av_deal = NULL;
        if (upipe_avcdv->open_params.extradata) {
            free(upipe_avcdv->open_params.extradata);
        }
        memset(&upipe_avcdv->open_params, 0, sizeof(struct upipe_avcodec_open_params));
    }
}

static bool upipe_avcdv_open_codec(struct upipe *upipe, AVCodec *codec,
                                   uint8_t *extradata, int extradata_size)
{
    AVCodecContext *context = NULL;
    bool ret = true;
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    assert(upipe);
    struct upump *upump_av_deal = upipe_avcdv->upump_av_deal;

    if (upump_av_deal) {
        if (unlikely(!upipe_av_deal_grab())) {
            upipe_dbg(upipe, "could not grab resource, return");
            return false;
        }
        upipe_avcdv->upump_av_deal = NULL;
    }

    if (codec) {
        context = avcodec_alloc_context3(codec);
        if (unlikely(!context)) {
            upipe_throw_aerror(upipe);
            ret = false;
        }
        context->opaque = upipe;
        context->get_buffer = upipe_avcdv_get_buffer;
        context->release_buffer = upipe_avcdv_release_buffer;
        context->flags |= CODEC_FLAG_EMU_EDGE;
        context->extradata = extradata;
        context->extradata_size = extradata_size;
    }

    if (unlikely(upipe_avcdv->context)) { // Close previously opened context
        // First send empty packet to send/release retained frames
        upipe_dbg(upipe, "flushing frames in decoder");
        while (upipe_avcdv_process_buf(upipe, NULL, 0, NULL));

        // Now close codec and free extradata if any
        upipe_notice_va(upipe, "avcodec context (%s) closed (%d)",
                    upipe_avcdv->context->codec->name, upipe_avcdv->counter);
        avcodec_close(upipe_avcdv->context);
        if (upipe_avcdv->context->extradata_size > 0) {
            free(upipe_avcdv->context->extradata);
        }
        av_free(upipe_avcdv->context);
        upipe_avcdv->context = NULL;
    }

    if (context) { // Open new context
        if (unlikely(avcodec_open2(context, codec, NULL) < 0)) {
            upipe_warn(upipe, "could not open codec");
            // FIXME: send probe (?)
            av_free(context);
            ret = false;
        }
    }

    if (upump_av_deal) {
        if (unlikely(!upipe_av_deal_yield(upump_av_deal))) {
            upump_free(upump_av_deal);
            upump_av_deal = NULL;
            upipe_err(upipe, "can't stop dealer");
            upipe_throw_upump_error(upipe);
            if (context) {
                if (ret) {
                    avcodec_close(context);
                }
                av_free(context);
            }
            return false;
        }
        upump_free(upump_av_deal);
        upump_av_deal = NULL;
    }

    if (context && ret) {
        upipe_avcdv->context = context;
        upipe_avcdv->counter = 0;
        upipe_notice_va(upipe, "codec %s (%s) %d opened", codec->name, 
                   codec->long_name, codec->id);


        #if 0
        if (upipe_avcdv->saved_upump) {
            upump_start(upipe_avcdv->saved_upump);
            upipe_avcdv->saved_upump = NULL;
        }
        #else
        if (upipe_avcdv->saved_upump_mgr) {
            upipe_dbg(upipe, "unblocking saved upump_mgr");
            upump_mgr_sink_unblock(upipe_avcdv->saved_upump_mgr);
            upump_mgr_release(upipe_avcdv->saved_upump_mgr);
            upipe_avcdv->saved_upump_mgr = NULL;
        }
        #endif
    }
    upipe_release(upipe);
    return ret;
}

static void upipe_avcdv_open_codec_cb(struct upump *upump)
{
    assert(upump);
    struct upipe *upipe = upump_get_opaque(upump, struct upipe*);
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    struct upipe_avcodec_open_params *params = &upipe_avcdv->open_params;
    upipe_avcdv_open_codec(upipe, params->codec, params->extradata, params->extradata_size);
}

/** @internal @This copies extradata
 *
 * @param upipe description structure of the pipe
 * @param extradata pointer to extradata buffer
 * @param size extradata size
 * @return false if the buffer couldn't be accepted
 */
static uint8_t *upipe_avcdv_copy_extradata(struct upipe *upipe, const uint8_t *extradata, int size)
{
    uint8_t *buf;
    if (!extradata || size <= 0) {
        return NULL;
    }

    buf = malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!buf) {
        upipe_throw_aerror(upipe);
        return NULL;
    }

    memset(buf+size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(buf, extradata, size);

    upipe_dbg_va(upipe, "Received extradata (%d bytes)", size);
    return buf;
}

static bool upipe_avcdv_set_context(struct upipe *upipe, const char *codec_def,
                                    uint8_t *extradata, int extradata_size)
{
    AVCodec *codec = NULL;
    int codec_id = 0;
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    struct upipe_avcodec_open_params *params = &upipe_avcdv->open_params;
    uint8_t *extradata_padded = NULL;

    if (codec_def) {
        codec_id = upipe_av_from_flow_def(codec_def);
        if (unlikely(!codec_id)) {
            upipe_warn_va(upipe, "codec %s not found", codec_def);
        }
        codec = avcodec_find_decoder(codec_id);
        if (unlikely(!codec)) {
            upipe_warn_va(upipe, "codec %d not found", codec_id);
        }
    }

    if (extradata && extradata_size > 0) {
        extradata_padded = upipe_avcdv_copy_extradata(upipe, extradata, extradata_size);
        if (!extradata_padded) {
            extradata_size = 0;
        }
    }

    if (upipe_avcdv->upump_mgr) {
        upipe_dbg(upipe, "upump_mgr present, using udeal");
        if (unlikely(upipe_avcdv->upump_av_deal)) {
            upipe_dbg(upipe, "previous upump_av_deal still running, cleaning first");
            upipe_avcdv_abort_av_deal(upipe);
        } else {
            upipe_avcdv_use(upipe);
        }
        struct upump *upump_av_deal = upipe_av_deal_upump_alloc(upipe_avcdv->upump_mgr,
                                                        upipe_avcdv_open_codec_cb, upipe);
        if (unlikely(!upump_av_deal)) {
            upipe_err(upipe, "can't create dealer");
            upipe_throw_upump_error(upipe);
            return false;
        }
        upipe_avcdv->upump_av_deal = upump_av_deal;

        memset(params, 0, sizeof(struct upipe_avcodec_open_params));
        params->codec = codec;
        params->extradata = extradata_padded;
        params->extradata_size = extradata_size;

        // Fire
        upipe_av_deal_start(upump_av_deal);

        return true;
    } else {
        upipe_dbg(upipe, "no upump_mgr present, direct call to avcdv_open");
        upipe_avcdv_use(upipe);
        return upipe_avcdv_open_codec(upipe, codec, extradata_padded, extradata_size);
    }
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
    struct urational aspect;

    // if uref has no attached ubuf (ie DR not supported)
    if (unlikely(!uref->ubuf)) {
        ubuf = ubuf_pic_alloc(upipe_avcdv->ubuf_mgr, frame->width, frame->height);
        if (!ubuf) {
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

    // Set aspect-ratio
    aspect.den = 0; // null denom is invalid
    if (upipe_avcdv->context->sample_aspect_ratio.den) {
        aspect.num = upipe_avcdv->context->sample_aspect_ratio.num;
        aspect.den = upipe_avcdv->context->sample_aspect_ratio.den;
    } else if (frame->sample_aspect_ratio.den) {
        aspect.num = frame->sample_aspect_ratio.num;
        aspect.den = frame->sample_aspect_ratio.den;
    }
    if (aspect.den) {
        urational_simplify(&aspect);
        uref_pic_set_aspect(uref, aspect);
    }

    upipe_avcdv_output(upipe, uref, upump);
}

/** @internal @This handles buffers once stripped from uref.
 *
 * @param upipe description structure of the pipe
 * @param buf buffer containing packet
 * @param size buffer size before padding
 * @param upump upump structure
 */
static bool upipe_avcdv_process_buf(struct upipe *upipe, uint8_t *buf,
                                    size_t size, struct upump *upump)
{
    int gotframe = 0, len;
    AVPacket avpkt;
    AVFrame *frame; 
    uint64_t framenum = 0;

    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    assert(upipe);

    // Init avcodec packed and attach input buffer
    av_init_packet(&avpkt);
    avpkt.size = size;
    avpkt.data = buf;

    frame = upipe_avcdv->frame;
    len = avcodec_decode_video2(upipe_avcdv->context, frame, &gotframe, &avpkt);
    if (len < 0) {
        upipe_warn(upipe, "Error while decoding frame");
    }

    // Copy frame to ubuf_pic if any frame has been decoded
    if (gotframe) {
        uref_attr_get_unsigned(frame->opaque, &framenum, "avcdv.framenum"); // DEBUG

        upipe_dbg_va(upipe, "%u\t - Picture decoded ! %dx%d - %u",
                upipe_avcdv->counter, frame->width, frame->height, (uint64_t) framenum);

        // FIXME DEVEL
        if (!upipe_avcdv->output_flow) {
            struct uref *outflow = uref_pic_flow_alloc_def(upipe_avcdv->uref_mgr, 1);
            upipe_avcdv_store_flow_def(upipe, outflow);
        }

        upipe_avcdv_output_frame(upipe, frame, upump);
        return true;
    } else {
        return false;
    }
}

/** @internal @This handles packets.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_avcdv_input_packet(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    uint8_t *inbuf;
    size_t insize;

    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    assert(upipe);
    assert(uref);

    if (upipe_avcdv->upump_av_deal) { // open_codec callback is waiting
        #if 1
        upipe_dbg(upipe, "Received packet while open_codec waiting");
        if (upump) {
            #if 0
            upump_stop(upump);
            upipe_avcdv->saved_upump = upump;
            #else
            upump_mgr_sink_block(upump->mgr);
            upump_mgr_use(upump->mgr);
            upipe_avcdv->saved_upump_mgr = upump->mgr;
            #endif
        }
        if (upipe_avcdv->saved_uref) {
            upipe_warn(upipe, "Dropping previously saved packet !");
            uref_free(upipe_avcdv->saved_uref);
        }
        upipe_avcdv->saved_uref = uref;
        return;
    } else if (upipe_avcdv->saved_uref) {
        upipe_dbg(upipe, "Processing previously saved packet");
        struct uref *prev_uref = upipe_avcdv->saved_uref;
        upipe_avcdv->saved_uref = NULL;
        #if 0
        upipe_avcdv_input_packet(upipe, prev_uref, prev_upump);
        #else
        upipe_avcdv_input_packet(upipe, prev_uref, upump); // Not a typo, using the current upump here
        #endif
        #else
        upipe_dbg(upipe, "Received packet while open_codec waiting");
        uref_free(uref);
        return;
        #endif
    }

    if (!upipe_avcdv->context) {
        uref_free(uref);
        upipe_warn(upipe, "Received packet but decoder is not initialized");
        return;
    }

    /* avcodec input buffer needs to be at least 4-byte aligned and
       FF_INPUT_BUFFER_PADDING_SIZE larger than actual input size.
       Thus, extract ubuf content in a properly allocated buffer.
       Padding must be zeroed. */
    uref_block_size(uref, &insize);

    upipe_dbg_va(upipe, "Received packet %u - size : %u", upipe_avcdv->counter, insize);
    inbuf = malloc(insize + FF_INPUT_BUFFER_PADDING_SIZE);
    if (unlikely(!inbuf)) {
        upipe_throw_aerror(upipe);
        return;
    }
    memset(inbuf, 0, insize + FF_INPUT_BUFFER_PADDING_SIZE);
    uref_block_extract(uref, 0, insize, inbuf); 
    ubuf_free(uref_detach_ubuf(uref));

    uref_attr_set_unsigned(uref, upipe_avcdv->counter, "avcdv.framenum"); // DEBUG

    /* Track current uref in pipe structure - required for buffer allocation
     * in upipe_avcdv_get_buffer */
    upipe_avcdv->uref = uref;

    upipe_avcdv_process_buf(upipe, inbuf, insize, upump);

    free(inbuf);
    uref_free(uref);
    upipe_avcdv->counter++;
}

/** @internal @This handles input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_avcdv_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    const char *def = NULL;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(ubase_ncmp(def, EXPECTED_FLOW))) {
            upipe_throw_flow_def_error(upipe, uref);
            uref_free(uref);
            upipe_avcdv_set_context(upipe, NULL, NULL, 0);
            return;
        }

        upipe_dbg_va(upipe, "flow definition %s", def);
        def += strlen(EXPECTED_FLOW);
        upipe_avcdv_set_context(upipe, def, NULL, 0);
        uref_free(uref);
        return;
    }

    upipe_avcdv_input_packet(upipe, uref, upump);
}

/* @internal @This defines a new upump_mgr after aborting av_deal
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool _upipe_avcdv_set_upump_mgr(struct upipe *upipe,
                                       struct upump_mgr *upump_mgr)
{
    upipe_avcdv_abort_av_deal(upipe);
    return upipe_avcdv_set_upump_mgr(upipe, upump_mgr);
}

/** @internal @This returns the current codec definition string
 */
static bool _upipe_avcdv_get_codec(struct upipe *upipe, const char **codec_p)
{
    struct upipe_avcdv *upipe_avcdv = upipe_avcdv_from_upipe(upipe);
    assert(codec_p);
    if (upipe_avcdv->context && upipe_avcdv->context->codec) {
        *codec_p = upipe_av_to_flow_def(upipe_avcdv->context->codec->id);
    } else {
        *codec_p = NULL;
        return false;
    }
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
static bool upipe_avcdv_control(struct upipe *upipe, enum upipe_command command,
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
        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_avcdv_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            return _upipe_avcdv_set_upump_mgr(upipe, upump_mgr);
        }


        case UPIPE_AVCDV_GET_CODEC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCDV_SIGNATURE);
            const char **url_p = va_arg(args, const char **);
            return _upipe_avcdv_get_codec(upipe, url_p);
        }
        case UPIPE_AVCDV_SET_CODEC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCDV_SIGNATURE);
            const char *codec = va_arg(args, const char *);
            uint8_t *extradata = va_arg(args, uint8_t *);
            int size = va_arg(args, int);
            return upipe_avcdv_set_context(upipe, codec, extradata, size);
        }

        default:
            return false;
    }
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
        if (upipe_avcdv->context) {
            upipe_avcdv_set_context(upipe, NULL, NULL, 0);
            return; // set_context() calls _use()/_release()
        }

        upipe_throw_dead(upipe);

        if (upipe_avcdv->frame) {
            av_free(upipe_avcdv->frame);
        }

        if (upipe_avcdv->input_flow) {
            uref_free(upipe_avcdv->input_flow);
        }

        if (upipe_avcdv->saved_uref) {
            uref_free(upipe_avcdv->saved_uref);
        }
        #if 0
        if (upipe_avcdv->saved_upump) {
            upump_start(upipe_avcdv->saved_upump);
        }
        #else
        if (upipe_avcdv->saved_upump_mgr) {
            upump_mgr_sink_unblock(upipe_avcdv->saved_upump_mgr);
            upump_mgr_release(upipe_avcdv->saved_upump_mgr);
        }
        #endif

        upipe_avcdv_abort_av_deal(upipe);
        upipe_avcdv_clean_output(upipe);
        upipe_avcdv_clean_ubuf_mgr(upipe);
        upipe_avcdv_clean_uref_mgr(upipe);
        upipe_avcdv_clean_upump_mgr(upipe);

        upipe_clean(upipe);
        urefcount_clean(&upipe_avcdv->refcount);
        free(upipe_avcdv);
    }
}

/** @internal @This allocates a avcdv pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avcdv_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe)
{
    struct upipe_avcdv *upipe_avcdv = malloc(sizeof(struct upipe_avcdv));
    if (unlikely(upipe_avcdv == NULL))
        return NULL;
    struct upipe *upipe = upipe_avcdv_to_upipe(upipe_avcdv);
    upipe_init(upipe, mgr, uprobe);

    urefcount_init(&upipe_avcdv->refcount);
    upipe_avcdv_init_uref_mgr(upipe);
    upipe_avcdv_init_ubuf_mgr(upipe);
    upipe_avcdv_init_upump_mgr(upipe);
    upipe_avcdv_init_output(upipe);
    upipe_avcdv->input_flow = NULL;
    upipe_avcdv->context = NULL;
    upipe_avcdv->upump_av_deal = NULL;
    upipe_avcdv->saved_uref = NULL;
    #if 0
    upipe_avcdv->saved_upump = NULL;
    #else
    upipe_avcdv->saved_upump_mgr = NULL;
    #endif
    upipe_avcdv->frame = avcodec_alloc_frame();

    upipe_throw_ready(upipe);
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
