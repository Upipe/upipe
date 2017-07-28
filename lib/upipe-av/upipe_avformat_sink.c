/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe sink module libavformat wrapper
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow_def_check.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-framers/uref_mpga_flow.h>
#include <upipe-av/upipe_avformat_sink.h>

#include "upipe_av_internal.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>

#include <libavutil/dict.h>
#include <libavformat/avformat.h>

/** @internal @This is the private context of an avformat source pipe. */
struct upipe_avfsink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** list of subs */
    struct uchain subs;

    /** URI */
    char *uri;
    /** MIME type */
    char *mime;
    /** format name */
    char *format;

    /** avformat options */
    AVDictionary *options;
    /** avformat context opened from URL */
    AVFormatContext *context;
    /** true if the header has already been written */
    bool opened;
    /** offset between Upipe timestamp and avformat timestamp */
    uint64_t ts_offset;
    /** first DTS */
    uint64_t first_dts;
    /** highest DTS */
    uint64_t highest_next_dts;

    /** manager to create subs */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avfsink, upipe, UPIPE_AVFSINK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_avfsink, urefcount, upipe_avfsink_free)
UPIPE_HELPER_VOID(upipe_avfsink)

/** @internal @This is the private context of an output of an avformat source
 * pipe. */
struct upipe_avfsink_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;
    /** libavformat stream ID */
    int id;
    /** relevant flow definition attributes */
    struct uref *flow_def_check;
    /** sample aspect ratio */
    struct urational sar;

    /** buffered urefs */
    struct uchain urefs;
    /** next DTS that is supposed to be dequeued */
    uint64_t next_dts;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avfsink_sub, upipe, UPIPE_AVFSINK_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_avfsink_sub, urefcount, upipe_avfsink_sub_free)
UPIPE_HELPER_VOID(upipe_avfsink_sub)
UPIPE_HELPER_FLOW_DEF_CHECK(upipe_avfsink_sub, flow_def_check)

UPIPE_HELPER_SUBPIPE(upipe_avfsink, upipe_avfsink_sub, sub, sub_mgr,
                     subs, uchain)

/** @hidden */
static void upipe_avfsink_mux(struct upipe *upipe, struct upump **upump_p);

/** @internal @This allocates an output subpipe of an avfsink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avfsink_sub_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature, va_list args)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_sub_mgr(mgr);
    if (upipe_avfsink->opened)
        return NULL;

    struct upipe *upipe = upipe_avfsink_sub_alloc_void(mgr, uprobe, signature,
                                                       args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_avfsink_sub *upipe_avfsink_sub =
        upipe_avfsink_sub_from_upipe(upipe);
    upipe_avfsink_sub_init_urefcount(upipe);
    upipe_avfsink_sub_init_flow_def_check(upipe);
    upipe_avfsink_sub_init_sub(upipe);
    upipe_avfsink_sub->id = -1;
    ulist_init(&upipe_avfsink_sub->urefs);
    upipe_avfsink_sub->next_dts = UINT64_MAX;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This handles input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_avfsink_sub_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_avfsink_sub *upipe_avfsink_sub =
        upipe_avfsink_sub_from_upipe(upipe);

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    uint64_t dts;
    if (unlikely(!ubase_check(uref_clock_get_dts_prog(uref, &dts)))) {
        upipe_warn_va(upipe, "packet without DTS");
        uref_free(uref);
        return;
    }

    bool was_empty = ulist_empty(&upipe_avfsink_sub->urefs);
    ulist_add(&upipe_avfsink_sub->urefs, uref_to_uchain(uref));
    if (was_empty) {
        upipe_use(upipe);
        upipe_avfsink_sub->next_dts = dts;
    }

    struct upipe_avfsink *upipe_avfsink =
        upipe_avfsink_from_sub_mgr(upipe->mgr);
    upipe_avfsink_mux(upipe_avfsink_to_upipe(upipe_avfsink), upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_avfsink_sub_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_avfsink *upipe_avfsink =
        upipe_avfsink_from_sub_mgr(upipe->mgr);
    struct upipe_avfsink_sub *upipe_avfsink_sub =
        upipe_avfsink_sub_from_upipe(upipe);
    if (upipe_avfsink->opened && upipe_avfsink_sub->id == -1)
        return UBASE_ERR_UNHANDLED;

    const char *def;
    enum AVCodecID codec_id;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, "block.") ||
        !(codec_id = upipe_av_from_flow_def(def + strlen("block."))) ||
        codec_id >= AV_CODEC_ID_FIRST_SUBTITLE) {
        upipe_err(upipe, "bad codec");
        return UBASE_ERR_INVALID;
    }
    uint64_t octetrate = 0;
    uref_block_flow_get_octetrate(flow_def, &octetrate);

    struct urational fps = {}, sar;
    uint64_t width = 0, height = 0;
    uint8_t channels = 0;
    uint64_t rate = 0, samples = 0;
    if (codec_id < AV_CODEC_ID_FIRST_AUDIO) {
        if (unlikely(!ubase_check(uref_pic_flow_get_fps(flow_def, &fps)) ||
                     !fps.den ||
                     !ubase_check(uref_pic_flow_get_sar(flow_def, &sar)) ||
                     !ubase_check(uref_pic_flow_get_hsize(flow_def, &width)) ||
                     !ubase_check(uref_pic_flow_get_vsize(flow_def, &height)))) {
            upipe_err(upipe, "bad video parameters");
            return UBASE_ERR_INVALID;
        }
    } else {
        if (unlikely(!ubase_check(uref_sound_flow_get_channels(flow_def, &channels)) ||
                     !ubase_check(uref_sound_flow_get_rate(flow_def, &rate)) ||
                     !ubase_check(uref_sound_flow_get_samples(flow_def, &samples)))) {
            upipe_err(upipe, "bad audio parameters");
            return UBASE_ERR_INVALID;
        }
    }

    uint8_t *extradata_alloc = NULL;
    const uint8_t *extradata;
    size_t extradata_size = 0;
    if (ubase_check(uref_flow_get_headers(flow_def, &extradata, &extradata_size))) {
        extradata_alloc = malloc(extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (unlikely(extradata_alloc == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        memcpy(extradata_alloc, extradata, extradata_size);
        memset(extradata_alloc + extradata_size, 0,
               FF_INPUT_BUFFER_PADDING_SIZE);
    } else if (!ubase_ncmp(def, "block.h264.") ||
               !ubase_ncmp(def, "block.aac.")) {
        upipe_err(upipe, "global headers required");
        return UBASE_ERR_INVALID;
    }

    /* Extract relevant attributes to flow def check. */
    struct uref *flow_def_check =
        upipe_avfsink_sub_alloc_flow_def_check(upipe, flow_def);
    if (unlikely(flow_def_check == NULL)) {
        free(extradata_alloc);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (unlikely(!ubase_check(uref_flow_set_def(flow_def_check, def)) ||
                 (octetrate &&
                  !ubase_check(uref_block_flow_set_octetrate(flow_def_check, octetrate))) ||
                 (extradata_alloc != NULL &&
                  !ubase_check(uref_flow_set_headers(flow_def_check, extradata,
                                         extradata_size))))) {
        free(extradata_alloc);
        uref_free(flow_def_check);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    if (codec_id < AV_CODEC_ID_FIRST_AUDIO) {
        if (unlikely(!ubase_check(uref_pic_flow_set_fps(flow_def_check, fps)) ||
                     !ubase_check(uref_pic_flow_set_hsize(flow_def_check, width)) ||
                     !ubase_check(uref_pic_flow_set_vsize(flow_def_check, height)))) {
            free(extradata_alloc);
            uref_free(flow_def_check);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    } else {
        if (unlikely(!ubase_check(uref_sound_flow_set_channels(flow_def_check, channels)) ||
                     !ubase_check(uref_sound_flow_set_rate(flow_def_check, rate)) ||
                     !ubase_check(uref_sound_flow_set_samples(flow_def_check, samples)))) {
            free(extradata_alloc);
            uref_free(flow_def_check);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    }

    if (upipe_avfsink->opened) {
        /* Die if the attributes changed. */
        bool ret = upipe_avfsink_sub_check_flow_def_check(upipe,
                                                          flow_def_check);
        free(extradata_alloc);
        uref_free(flow_def_check);

        if (ret && codec_id < AV_CODEC_ID_FIRST_AUDIO &&
            urational_cmp(&sar, &upipe_avfsink_sub->sar)) {
            upipe_warn(upipe, "SAR is different");
        }
        return ret ? UBASE_ERR_NONE : UBASE_ERR_BUSY;
    }

    /* Open a new avformat stream. */
    upipe_avfsink_sub->id = upipe_avfsink->context->nb_streams;
    upipe_avfsink_sub_store_flow_def_check(upipe, flow_def_check);
    upipe_avfsink_sub->sar = sar;

    AVStream *stream = avformat_new_stream(upipe_avfsink->context, NULL);
    if (unlikely(stream == NULL)) {
        free(extradata_alloc);
        upipe_err(upipe, "couldn't allocate stream");
        upipe_throw_fatal(upipe, UBASE_ERR_EXTERNAL);
        return UBASE_ERR_EXTERNAL;
    }

    uint64_t id;
    if (likely(ubase_check(uref_flow_get_id(flow_def, &id)))) {
        stream->id = id;
    }
    stream->disposition = AV_DISPOSITION_DEFAULT;

    uint8_t languages;
    const char *lang;
    if (ubase_check(uref_flow_get_languages(flow_def, &languages)) &&
        languages && ubase_check(uref_flow_get_language(flow_def, &lang, 0)) &&
        lang) {
        av_dict_set(&stream->metadata, "language", lang, 0);
    }

    AVCodecContext *codec = stream->codec;
    codec->bit_rate = octetrate * 8;
    codec->codec_tag =
        av_codec_get_tag(upipe_avfsink->context->oformat->codec_tag, codec_id);
    codec->codec_id = codec_id;
    if (codec_id < AV_CODEC_ID_FIRST_AUDIO) {
        codec->codec_type = AVMEDIA_TYPE_VIDEO;
        codec->width = width;
        codec->height = height;
        stream->sample_aspect_ratio.num =
            codec->sample_aspect_ratio.num = sar.num;
        stream->sample_aspect_ratio.den =
            codec->sample_aspect_ratio.den = sar.den;
        stream->avg_frame_rate.num = 25;
        stream->avg_frame_rate.den = 1;
        stream->time_base.num = fps.den;
        stream->time_base.den = fps.num * 2;
        codec->ticks_per_frame = 2;
        codec->framerate.num = fps.num;
        codec->framerate.den = fps.den;
    } else {
        codec->codec_type = AVMEDIA_TYPE_AUDIO;
        codec->channels = channels;
        codec->sample_rate = rate;
        stream->time_base = (AVRational){ 1, codec->sample_rate };
        codec->frame_size = samples;
    }

    if (extradata_alloc != NULL) {
        codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
        codec->extradata_size = extradata_size;
        codec->extradata = extradata_alloc;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_avfsink_sub_provide_flow_format(struct upipe *upipe,
                                                 struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);
    /* we always require global headers */
    uref_flow_set_global(flow_format);
    const char *def;
    if (!ubase_check(uref_flow_get_def(flow_format, &def)))
        return urequest_provide_flow_format(request, flow_format);

    struct upipe_avfsink *upipe_avfsink =
        upipe_avfsink_from_sub_mgr(upipe->mgr);
    if (!ubase_ncmp(def, "block.aac.") && upipe_avfsink->format != NULL &&
        (!strcmp(upipe_avfsink->format, "mp4") ||
         !strcmp(upipe_avfsink->format, "mov") ||
         !strcmp(upipe_avfsink->format, "m4a") ||
         !strcmp(upipe_avfsink->format, "flv") ||
         !strcmp(upipe_avfsink->format, "adts") ||
         !strcmp(upipe_avfsink->format, "aac")))
        uref_mpga_flow_set_encaps(flow_format, UREF_MPGA_ENCAPS_RAW);
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This processes control commands on an output subpipe of an
 * avfsink pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_avfsink_sub_control(struct upipe *upipe,
                                     int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_avfsink_sub_provide_flow_format(upipe, request);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_avfsink_sub_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_avfsink_sub_get_super(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsink_sub_free(struct upipe *upipe)
{
    struct upipe_avfsink *upipe_avfsink =
        upipe_avfsink_from_sub_mgr(upipe->mgr);
    upipe_throw_dead(upipe);

    upipe_avfsink_sub_clean_flow_def_check(upipe);
    upipe_avfsink_sub_clean_sub(upipe);
    upipe_avfsink_mux(upipe_avfsink_to_upipe(upipe_avfsink), NULL);
    upipe_avfsink_sub_clean_urefcount(upipe);
    upipe_avfsink_sub_free_void(upipe);
}

/** @internal @This initializes the output manager for an avfsink pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsink_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_avfsink->sub_mgr;
    sub_mgr->refcount = upipe_avfsink_to_urefcount(upipe_avfsink);
    sub_mgr->signature = UPIPE_AVFSINK_INPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_avfsink_sub_alloc;
    sub_mgr->upipe_input = upipe_avfsink_sub_input;
    sub_mgr->upipe_control = upipe_avfsink_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates an avfsink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avfsink_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe =
        upipe_avfsink_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    upipe_avfsink_init_urefcount(upipe);
    upipe_avfsink_init_sub_mgr(upipe);
    upipe_avfsink_init_sub_subs(upipe);

    upipe_avfsink->uri = NULL;
    upipe_avfsink->mime = NULL;
    upipe_avfsink->format = NULL;

    upipe_avfsink->options = NULL;
    upipe_avfsink->context = NULL;
    upipe_avfsink->opened = false;
    upipe_avfsink->ts_offset = 0;
    upipe_avfsink->first_dts = 0;
    upipe_avfsink->highest_next_dts = 0;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This finds the input with the lowest DTS.
 *
 * @param upipe description structure of the pipe
 * @return a pointer to the sub pipe, or NULL if not all inputs have packets
 */
static struct upipe_avfsink_sub *upipe_avfsink_find_input(struct upipe *upipe)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    struct uchain *uchain;
    uint64_t earliest_dts = UINT64_MAX;
    struct upipe_avfsink_sub *earliest_input = NULL;
    ulist_foreach (&upipe_avfsink->subs, uchain) {
        struct upipe_avfsink_sub *input = upipe_avfsink_sub_from_uchain(uchain);
        if (input->next_dts == UINT64_MAX)
            return NULL;
        if (input->next_dts < earliest_dts) {
            earliest_dts = input->next_dts;
            earliest_input = input;
        }
    }
    if (earliest_input != NULL) {
        uchain = ulist_peek(&earliest_input->urefs);
        if (uchain == NULL)
            return NULL; /* wait for the incoming packet */
    }
    return earliest_input;
}

/** @internal @This asks avformat to multiplex some data.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the last buffer
 */
static void upipe_avfsink_mux(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    struct upipe_avfsink_sub *input;
    while ((input = upipe_avfsink_find_input(upipe)) != NULL) {
        if (unlikely(!upipe_avfsink->opened)) {
            upipe_dbg(upipe, "writing header");
            /* avformat dts for formats other than mpegts should start at 0 */
            if (strcmp(upipe_avfsink->context->oformat->name, "mpegts")) {
                upipe_avfsink->ts_offset = input->next_dts;
            }
            upipe_avfsink->first_dts = input->next_dts;
            if (!(upipe_avfsink->context->oformat->flags & AVFMT_NOFILE)) {
                AVDictionary *options = NULL;
                av_dict_copy(&options, upipe_avfsink->options, 0);
                int error = avio_open2(&upipe_avfsink->context->pb,
                                       upipe_avfsink->context->filename,
                                       AVIO_FLAG_WRITE, NULL, &options);
                av_dict_free(&options);
                if (error < 0) {
                    upipe_av_strerror(error, buf);
                    upipe_err_va(upipe, "couldn't open file %s (%s)",
                                 upipe_avfsink->context->filename, buf);
                    upipe_throw_fatal(upipe, UBASE_ERR_EXTERNAL);
                    while (!ulist_empty(&input->urefs)) {
                        uref_free(uref_from_uchain(ulist_pop(&input->urefs)));
                    }
                    upipe_release(upipe_avfsink_sub_to_upipe(input));
                    return;
                }
            }

            AVDictionary *options = NULL;
            av_dict_copy(&options, upipe_avfsink->options, 0);
            int error = avformat_write_header(upipe_avfsink->context, &options);
            if (unlikely(error < 0)) {
                upipe_av_strerror(error, buf);
                upipe_err_va(upipe, "couldn't write header (%s)", buf);
                upipe_throw_fatal(upipe, UBASE_ERR_EXTERNAL);
                while (!ulist_empty(&input->urefs)) {
                    uref_free(uref_from_uchain(ulist_pop(&input->urefs)));
                }
                upipe_release(upipe_avfsink_sub_to_upipe(input));
                return;
            }
            AVDictionaryEntry *e = NULL;
            while ((e = av_dict_get(options, "", e, AV_DICT_IGNORE_SUFFIX)))
                upipe_warn_va(upipe, "unknown option \"%s\"", e->key);
            av_dict_free(&options);
            upipe_avfsink->opened = true;
        }

        AVStream *stream = upipe_avfsink->context->streams[input->id];
        struct uchain *uchain = ulist_pop(&input->urefs);
        struct uref *uref = uref_from_uchain(uchain);

        upipe_use(upipe_avfsink_sub_to_upipe(input));
        if (ulist_empty(&input->urefs)) {
            uint64_t duration;
            if (ubase_check(uref_clock_get_duration(uref, &duration)))
                input->next_dts += duration;
            else
                input->next_dts = UINT64_MAX;
            upipe_release(upipe_avfsink_sub_to_upipe(input));
        } else {
            uchain = ulist_peek(&input->urefs);
            struct uref *next_uref = uref_from_uchain(uchain);
            uref_clock_get_dts_prog(next_uref, &input->next_dts);
        }

        AVPacket avpkt;
        memset(&avpkt, 0, sizeof(AVPacket));
        av_init_packet(&avpkt);
        avpkt.stream_index = input->id;
        if (ubase_check(uref_flow_get_random(uref)))
            avpkt.flags |= AV_PKT_FLAG_KEY;

        uint64_t dts;
        if (ubase_check(uref_clock_get_dts_prog(uref, &dts)))
            avpkt.dts = ((dts - upipe_avfsink->ts_offset) *
                         stream->time_base.den + UCLOCK_FREQ / 2) /
                        UCLOCK_FREQ / stream->time_base.num;
        uint64_t pts;
        if (ubase_check(uref_clock_get_pts_prog(uref, &pts)))
            avpkt.pts = ((pts - upipe_avfsink->ts_offset) *
                         stream->time_base.den + UCLOCK_FREQ / 2) /
                        UCLOCK_FREQ / stream->time_base.num;

        size_t size = 0;
        uref_block_size(uref, &size);
        if (unlikely(!size)) {
            upipe_warn(upipe, "Received packet with size 0, dropping");
            uref_free(uref);
            upipe_release(upipe_avfsink_sub_to_upipe(input));
            continue;
        }
        avpkt.size = size;

        /* TODO replace with umem */
        avpkt.data = malloc(avpkt.size);
        if (unlikely(avpkt.data == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            upipe_release(upipe_avfsink_sub_to_upipe(input));
            return;
        }
        uref_block_extract(uref, 0, avpkt.size, avpkt.data); 
        uref_free(uref);

        if (input->next_dts > upipe_avfsink->highest_next_dts) {
            upipe_avfsink->highest_next_dts = input->next_dts;
        }

        upipe_release(upipe_avfsink_sub_to_upipe(input));

        int error = av_write_frame(upipe_avfsink->context, &avpkt);
        free(avpkt.data);
        if (unlikely(error < 0)) {
            upipe_av_strerror(error, buf);
            upipe_warn_va(upipe, "write error to %s (%s)", upipe_avfsink->uri, buf);
            upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
            return;
        }
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_avfsink_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))
    return UBASE_ERR_NONE;
}

/** @internal @This returns the content of an avformat option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content_p filled in with the content of the option
 * @return an error code
 */
static int upipe_avfsink_get_option(struct upipe *upipe,
                                    const char *option, const char **content_p)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    assert(option != NULL);
    assert(content_p != NULL);
    AVDictionaryEntry *entry = av_dict_get(upipe_avfsink->options, option,
                                           NULL, 0);
    if (unlikely(entry == NULL))
        return UBASE_ERR_EXTERNAL;
    *content_p = entry->value;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the content of an avformat option. It only take effect
 * after the next call to @ref upipe_set_uri.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return an error code
 */
static int upipe_avfsink_set_option(struct upipe *upipe,
                                    const char *option, const char *content)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    assert(option != NULL);
    int error = av_dict_set(&upipe_avfsink->options, option, content, 0);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't set option %s:%s (%s)", option, content,
                     buf);
        return UBASE_ERR_EXTERNAL;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the currently configured MIME type.
 *
 * @param upipe description structure of the pipe
 * @param mime_p filled in with the currently configured MIME type
 * @return an error code
 */
static int _upipe_avfsink_get_mime(struct upipe *upipe, const char **mime_p)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    assert(mime_p != NULL);
    *mime_p = upipe_avfsink->mime;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the MIME type. It only takes effect after the next
 * call to @ref upipe_set_uri.
 *
 * @param upipe description structure of the pipe
 * @param mime MIME type
 * @return an error code
 */
static int _upipe_avfsink_set_mime(struct upipe *upipe, const char *mime)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    free(upipe_avfsink->mime);
    if (mime != NULL) {
        upipe_avfsink->mime = strdup(mime);
        if (upipe_avfsink->mime == NULL) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    } else
        upipe_avfsink->mime = NULL;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the currently configured format name.
 *
 * @param upipe description structure of the pipe
 * @param format_p filled in with the currently configured format name
 * @return an error code
 */
static int _upipe_avfsink_get_format(struct upipe *upipe, const char **format_p)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    assert(format_p != NULL);
    *format_p = upipe_avfsink->format;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the format name. It only takes effect after the next
 * call to @ref upipe_set_uri.
 *
 * @param upipe description structure of the pipe
 * @param format format name
 * @return an error code
 */
static int _upipe_avfsink_set_format(struct upipe *upipe, const char *format)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    free(upipe_avfsink->format);
    if (format != NULL) {
        upipe_avfsink->format = strdup(format);
        if (upipe_avfsink->format == NULL) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    } else
        upipe_avfsink->format = NULL;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current duration.
 *
 * @param upipe description structure of the pipe
 * @param duration_p filled in with the current duration
 * @return an error code
 */
static int _upipe_avfsink_get_duration(struct upipe *upipe,
                                       uint64_t *duration_p)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    assert(duration_p != NULL);
    if (upipe_avfsink->highest_next_dts == 0) {
        *duration_p = 0;
    } else {
        *duration_p = upipe_avfsink->highest_next_dts
                      - upipe_avfsink->first_dts;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the currently opened URI.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the URI
 * @return an error code
 */
static int upipe_avfsink_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_avfsink->uri;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given URI.
 *
 * @param upipe description structure of the pipe
 * @param uri URI to open
 * @return an error code
 */
static int upipe_avfsink_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);

    if (unlikely(upipe_avfsink->context != NULL)) {
        if (likely(upipe_avfsink->uri != NULL))
            upipe_notice_va(upipe, "closing URI %s", upipe_avfsink->uri);
        if (upipe_avfsink->opened) {
            upipe_dbg(upipe, "writing trailer");
            av_write_trailer(upipe_avfsink->context);
            if (!(upipe_avfsink->context->oformat->flags & AVFMT_NOFILE))
                avio_close(upipe_avfsink->context->pb);
        }
        avformat_free_context(upipe_avfsink->context);
    }
    ubase_clean_str(&upipe_avfsink->uri);

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    AVOutputFormat *format = NULL;
    format = av_guess_format(upipe_avfsink->format, uri, upipe_avfsink->mime);
    if (unlikely(format == NULL))
        return UBASE_ERR_INVALID;

    upipe_avfsink->context = avformat_alloc_context();
    if (unlikely(upipe_avfsink->context == NULL)) {
        upipe_err_va(upipe, "can't allocate context (URI %s)", uri);
        return UBASE_ERR_EXTERNAL;
    }
    upipe_avfsink->context->oformat = format;
    strncpy(upipe_avfsink->context->filename, uri,
            sizeof(upipe_avfsink->context->filename));
    upipe_avfsink->context->filename[sizeof(upipe_avfsink->context->filename) - 1] = '\0';

    upipe_avfsink->uri = strdup(uri);
    upipe_avfsink->opened = false;
    upipe_notice_va(upipe, "opening URI %s", upipe_avfsink->uri);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an avformat source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_avfsink_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_avfsink_control_subs(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_avfsink_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char **content_p = va_arg(args, const char **);
            return upipe_avfsink_get_option(upipe, option, content_p);
        }
        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return upipe_avfsink_set_option(upipe, option, content);
        }
        case UPIPE_AVFSINK_GET_MIME: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFSINK_SIGNATURE)
            const char **mime_p = va_arg(args, const char **);
            return _upipe_avfsink_get_mime(upipe, mime_p);
        }
        case UPIPE_AVFSINK_SET_MIME: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFSINK_SIGNATURE)
            const char *mime = va_arg(args, const char *);
            return _upipe_avfsink_set_mime(upipe, mime);
        }
        case UPIPE_AVFSINK_GET_FORMAT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFSINK_SIGNATURE)
            const char **format_p = va_arg(args, const char **);
            return _upipe_avfsink_get_format(upipe, format_p);
        }
        case UPIPE_AVFSINK_SET_FORMAT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFSINK_SIGNATURE)
            const char *format = va_arg(args, const char *);
            return _upipe_avfsink_set_format(upipe, format);
        }
        case UPIPE_AVFSINK_GET_DURATION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFSINK_SIGNATURE)
            uint64_t *duration_p = va_arg(args, uint64_t *);
            return _upipe_avfsink_get_duration(upipe, duration_p);
        }

        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_avfsink_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_avfsink_set_uri(upipe, uri);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsink_free(struct upipe *upipe)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    upipe_avfsink_clean_sub_subs(upipe);

    upipe_avfsink_set_uri(upipe, NULL);
    upipe_throw_dead(upipe);

    free(upipe_avfsink->mime);
    free(upipe_avfsink->format);

    av_dict_free(&upipe_avfsink->options);

    upipe_avfsink_clean_urefcount(upipe);
    upipe_avfsink_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_avfsink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AVFSINK_SIGNATURE,

    .upipe_alloc = upipe_avfsink_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_avfsink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all avformat sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avfsink_mgr_alloc(void)
{
    return &upipe_avfsink_mgr;
}
