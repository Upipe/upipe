/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short Upipe source module libavformat wrapper
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
//#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_split_outputs.h>
#include <upipe/upipe_helper_split_ubuf_mgr.h>
#include <upipe-av/upipe_avformat_source.h>

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
struct upipe_avfsrc {
    /** uref manager */
    struct uref_mgr *uref_mgr;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;

    /** list of outputs */
    struct ulist outputs;

    /** flow name */
    char *flow_name;
    /** URL */
    char *url;

    /** avcodec initialization watcher */
    struct upump *upump_av_deal;
    /** avformat options */
    AVDictionary *options;
    /** avformat context opened from URL */
    AVFormatContext *context;
    /** true if the URL has already been probed by avformat */
    bool probed;
    /** true if we have thrown the ready event */
    bool ready;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avfsrc, upipe)
UPIPE_HELPER_UREF_MGR(upipe_avfsrc, uref_mgr)

UPIPE_HELPER_UPUMP_MGR(upipe_avfsrc, upump_mgr, upump)
UPIPE_HELPER_UCLOCK(upipe_avfsrc, uclock)

/** @internal @This is the private context of an output of an avformat source
 * pipe. */
struct upipe_avfsrc_output {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** pipe acting as output */
    struct upipe *output;
    /** suffix added to every flow on this output */
    char *flow_suffix;
    /** flow definition */
    struct uref *flow_def;
    /** true if the flow definition has been sent */
    bool flow_def_sent;
    /** ubuf manager for this output */
    struct ubuf_mgr *ubuf_mgr;
};

UPIPE_HELPER_SPLIT_OUTPUT(upipe_avfsrc, upipe_avfsrc_output, uchain, output,
                          flow_suffix, flow_def, flow_def_sent, uref_mgr)
UPIPE_HELPER_SPLIT_OUTPUTS(upipe_avfsrc, outputs, upipe_avfsrc_output)
UPIPE_HELPER_SPLIT_UBUF_MGR(upipe_avfsrc, upipe_avfsrc_output, ubuf_mgr)
UPIPE_HELPER_SPLIT_UBUF_MGRS(upipe_avfsrc, upipe_avfsrc_output)

/** @internal @This allocates and initializes a new output-specific
 * substructure.
 *
 * @param upipe description structure of the pipe
 * @param flow_suffix flow suffix
 * @return pointer to allocated substructure
 */
static struct upipe_avfsrc_output *
    upipe_avfsrc_output_alloc(struct upipe *upipe, const char *flow_suffix)
{
    assert(flow_suffix != NULL);
    struct upipe_avfsrc_output *output = malloc(sizeof(struct upipe_avfsrc_output));
    if (unlikely(output == NULL))
        return NULL;
    if (unlikely(!upipe_avfsrc_output_init(upipe, output, flow_suffix))) {
        free(output);
        return NULL;
    }
    upipe_avfsrc_output_init_ubuf_mgr(upipe, output);
    return output;
}

/** @internal @This allocates and initializes a new output-specific
 * substructure, with printf-style name generation.
 *
 * @param upipe description structure of the pipe
 * @param format printf-style format of the flow suffix, followed by a variable
 * list of arguments
 * @return pointer to allocated substructure
 */
static struct upipe_avfsrc_output *
    upipe_avfsrc_output_alloc_va(struct upipe *upipe, const char *format, ...)
                   __attribute__ ((format(printf, 2, 3)));
static struct upipe_avfsrc_output *
    upipe_avfsrc_output_alloc_va(struct upipe *upipe, const char *format, ...)
{
    UBASE_VARARG(upipe_avfsrc_output_alloc(upipe, string))
}

/** @internal @This frees up an output-specific substructure.
 *
 * @param upipe description structure of the pipe
 * @param output substructure to free
 */
static void upipe_avfsrc_output_free(struct upipe *upipe,
                                     struct upipe_avfsrc_output *output)
{
    upipe_avfsrc_output_clean_ubuf_mgr(upipe, output);
    upipe_avfsrc_output_clean(upipe, output);
    free(output);
}

/** @internal @This allocates a file source pipe.
 *
 * @param mgr common management structure
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avfsrc_alloc(struct upipe_mgr *mgr)
{
    struct upipe_avfsrc *upipe_avfsrc = malloc(sizeof(struct upipe_avfsrc));
    if (unlikely(upipe_avfsrc == NULL)) return NULL;
    struct upipe *upipe = upipe_avfsrc_to_upipe(upipe_avfsrc);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_AVFSRC_SIGNATURE;
    urefcount_init(&upipe_avfsrc->refcount);
    upipe_avfsrc_init_uref_mgr(upipe);
    upipe_avfsrc_init_upump_mgr(upipe);
    upipe_avfsrc_init_uclock(upipe);
    upipe_avfsrc_init_outputs(upipe);

    upipe_avfsrc->flow_name = NULL;
    upipe_avfsrc->url = NULL;

    upipe_avfsrc->upump_av_deal = NULL;
    upipe_avfsrc->options = NULL;
    upipe_avfsrc->context = NULL;
    upipe_avfsrc->probed = false;
    upipe_avfsrc->ready = false;
    return upipe;
}

/** @This aborts and frees an existing upump watching for exclusive access to
 * avcodec_open().
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_abort_av_deal(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    if (unlikely(upipe_avfsrc->upump_av_deal != NULL)) {
        upipe_av_deal_abort(upipe_avfsrc->upump_av_deal);
        upump_free(upipe_avfsrc->upump_av_deal);
        upipe_avfsrc->upump_av_deal = NULL;
    }
}

/** @internal @This reads data from the source and outputs it.
 * It is called either when the idler triggers (permanent storage mode) or
 * when data is available on the file descriptor (live stream mode).
 *
 * @param upump description structure of the read watcher
 */
static void upipe_avfsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    AVPacket pkt;

    int error = av_read_frame(upipe_avfsrc->context, &pkt);
    if (unlikely(error < 0)) {
        ulog_error(upipe->ulog, "read error from %s (%s)", upipe_avfsrc->url,
                   upipe_av_ulog_strerror(upipe->ulog, error));
        upipe_avfsrc_set_upump(upipe, NULL);
        upipe_throw_read_end(upipe, upipe_avfsrc->url);
        return;
    }

    struct upipe_avfsrc_output *output =
        upipe_avfsrc_find_output_va(upipe, "%d", pkt.stream_index);
    if (unlikely(output == NULL || output->output == NULL ||
                 output->ubuf_mgr == NULL)) {
        av_free_packet(&pkt);
        return;
    }

    struct uref *uref = uref_block_alloc(upipe_avfsrc->uref_mgr,
                                         output->ubuf_mgr, pkt.size);
    if (unlikely(uref == NULL)) {
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return;
    }
    uint64_t systime = upipe_avfsrc->uclock != NULL ?
                       uclock_now(upipe_avfsrc->uclock) : 0;
    uint8_t *buffer;
    int read_size = -1;
    if (unlikely(!uref_block_write(uref, 0, &read_size, &buffer))) {
        uref_free(uref);
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return;
    }
    assert(read_size == pkt.size);
    memcpy(buffer, pkt.data, pkt.size);
    uref_block_unmap(uref, 0, read_size);

    if (upipe_avfsrc->uclock != NULL)
        uref_clock_set_systime(uref, systime);
    /* FIXME: fix and set PTS */
    upipe_avfsrc_output_output(upipe, output, uref);
}

/** @internal @This starts the worker.
 *
 * @param upipe description structure of the pipe
 */
static bool upipe_avfsrc_start(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    struct upump *upump = upump_alloc_idler(upipe_avfsrc->upump_mgr,
                                            upipe_avfsrc_worker, upipe, true);
    if (unlikely(upump == NULL)) {
        ulog_error(upipe->ulog, "can't create worker");
        upipe_throw_upump_error(upipe);
        return false;
    }
    upipe_avfsrc_set_upump(upipe, upump);
    upump_start(upump);

    if (likely(!upipe_avfsrc->ready)) {
        upipe_avfsrc->ready = true;
        upipe_throw_ready(upipe);
    }
    return true;
}

/** @hidden */
#define CHK(x)                                                              \
    if (unlikely(!x))                                                       \
        return NULL;

/** @internal @This returns a flow definition for a raw audio media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_raw_audio_def(struct uref_mgr *uref_mgr,
                                        AVCodecContext *codec)
{
    if (unlikely(codec->bits_per_coded_sample % 8))
        return NULL;

    struct uref *flow_def =
        uref_sound_flow_alloc_def(uref_mgr, codec->channels,
                                  codec->bits_per_coded_sample / 8);
    if (unlikely(flow_def == NULL))
        return NULL;

    CHK(uref_sound_flow_set_rate(flow_def, codec->sample_rate))
    if (codec->block_align)
        CHK(uref_sound_flow_set_samples(flow_def,
                                        codec->block_align /
                                        (codec->bits_per_coded_sample / 8) /
                                        codec->channels))
    return flow_def;
}

/** @internal @This returns a flow definition for a coded audio media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_audio_def(struct uref_mgr *uref_mgr,
                                    AVCodecContext *codec)
{
    const char *def = upipe_av_to_flow_def(codec->codec_id);
    if (unlikely(def == NULL))
        return NULL;

    struct uref *flow_def =
        uref_block_flow_alloc_def_va(uref_mgr, "sound.%s", def);
    if (unlikely(flow_def == NULL))
        return NULL;

    if (codec->bit_rate)
        CHK(uref_block_flow_set_octetrate(flow_def, (codec->bit_rate + 7) / 8))

    CHK(uref_sound_flow_set_channels(flow_def, codec->channels))
    CHK(uref_sound_flow_set_rate(flow_def, codec->sample_rate))
    if (codec->block_align)
        CHK(uref_block_flow_set_size(flow_def, codec->block_align))
    return flow_def;
}

/** @internal @This returns a flow definition for a raw video media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_raw_video_def(struct uref_mgr *uref_mgr,
                                        AVCodecContext *codec)
{
    /* TODO */
    return NULL;
}

/** @internal @This returns a flow definition for a coded video media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_video_def(struct uref_mgr *uref_mgr,
                                    AVCodecContext *codec)
{
    const char *def = upipe_av_to_flow_def(codec->codec_id);
    if (unlikely(def == NULL))
        return NULL;

    struct uref *flow_def = uref_block_flow_alloc_def_va(uref_mgr, "pic.%s",
                                                         def);
    if (unlikely(flow_def == NULL))
        return NULL;

    if (codec->bit_rate)
        CHK(uref_block_flow_set_octetrate(flow_def, (codec->bit_rate + 7) / 8))

    CHK(uref_pic_flow_set_hsize(flow_def, codec->width))
    CHK(uref_pic_flow_set_vsize(flow_def, codec->height))
    int ticks = codec->ticks_per_frame ? codec->ticks_per_frame : 1;
    if (codec->time_base.den)
        CHK(uref_pic_flow_set_fps(flow_def, codec->time_base.num * ticks /
                                            codec->time_base.den))
    return flow_def;
}

/** @internal @This returns a flow definition for a subtitles media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_subtitles_def(struct uref_mgr *uref_mgr,
                                        AVCodecContext *codec)
{
    /* TODO */
    /* FIXME extradata */
    return NULL;
}

/** @internal @This returns a flow definition for a data media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_data_def(struct uref_mgr *uref_mgr,
                                   AVCodecContext *codec)
{
    /* TODO */
    return NULL;
}

#undef CHK

/** @internal @This probes all flows from the source.
 *
 * @param upump description structure of the dealer
 */
static void upipe_avfsrc_probe(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    AVFormatContext *context = upipe_avfsrc->context;

    if (unlikely(!upipe_av_deal_grab()))
        return;

    AVDictionary *options[context->nb_streams];
    for (unsigned i = 0; i < context->nb_streams; i++) {
        options[i] = NULL;
        av_dict_copy(&options[i], upipe_avfsrc->options, 0);
    }
    int error = avformat_find_stream_info(context, options);
    for (unsigned i = 0; i < context->nb_streams; i++)
        av_dict_free(&options[i]);

    if (unlikely(!upipe_av_deal_yield(upump))) {
        upump_free(upipe_avfsrc->upump_av_deal);
        upipe_avfsrc->upump_av_deal = NULL;
        ulog_error(upipe->ulog, "can't stop dealer");
        upipe_throw_upump_error(upipe);
        return;
    }
    upump_free(upipe_avfsrc->upump_av_deal);
    upipe_avfsrc->upump_av_deal = NULL;
    upipe_avfsrc->probed = true;

    if (unlikely(error < 0)) {
        ulog_error(upipe->ulog, "can't probe URL %s (%s)", upipe_avfsrc->url,
                   upipe_av_ulog_strerror(upipe->ulog, error));
        if (likely(upipe_avfsrc->url != NULL))
            ulog_notice(upipe->ulog, "closing URL %s", upipe_avfsrc->url);
        avformat_close_input(&upipe_avfsrc->context);
        upipe_avfsrc->context = NULL;
        free(upipe_avfsrc->url);
        upipe_avfsrc->url = NULL;
        return;
    }

    for (int i = 0; i < context->nb_streams; i++) {
        AVStream *stream = context->streams[i];
        AVCodecContext *codec = stream->codec;
        struct upipe_avfsrc_output *output =
            upipe_avfsrc_output_alloc_va(upipe, "%d", stream->index);
        struct uref *flow_def;
        if (unlikely(output == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            return;
        }
        upipe_avfsrc_add_output(upipe, output);

        switch (codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (codec->codec_id >= CODEC_ID_FIRST_AUDIO &&
                    codec->codec_id < CODEC_ID_ADPCM_IMA_QT)
                    flow_def = alloc_raw_audio_def(upipe_avfsrc->uref_mgr,
                                                   codec);
                else
                    flow_def = alloc_audio_def(upipe_avfsrc->uref_mgr, codec);
                break;
            case AVMEDIA_TYPE_VIDEO:
                if (codec->codec_id == CODEC_ID_RAWVIDEO)
                    flow_def = alloc_raw_video_def(upipe_avfsrc->uref_mgr, codec);
                else
                    flow_def = alloc_video_def(upipe_avfsrc->uref_mgr, codec);
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                flow_def = alloc_subtitles_def(upipe_avfsrc->uref_mgr, codec);
                break;
            default:
                flow_def = alloc_data_def(upipe_avfsrc->uref_mgr, codec);
                break;
        }

        if (unlikely(flow_def == NULL)) {
            ulog_warning(upipe->ulog, "unsupported track type (%u:%u)",
                         codec->codec_type, codec->codec_id);
            continue;
        }

        bool ret;
        if (unlikely(upipe_avfsrc->flow_name == NULL))
            ret = uref_flow_set_name(flow_def, output->flow_suffix);
        else
            ret = uref_flow_set_name_va(flow_def, "%s.%s",
                                        upipe_avfsrc->flow_name,
                                        output->flow_suffix);
        if (unlikely(!ret)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            return;
        }

        AVDictionaryEntry *lang = av_dict_get(stream->metadata, "language",
                                              NULL, 0);
        if (lang != NULL && lang->value != NULL)
            uref_flow_set_lang(flow_def, lang->value);

        upipe_avfsrc_output_set_flow_def(upipe, output, flow_def);
        upipe_throw_new_flow(upipe, output->flow_suffix, flow_def);
    }

    upipe_avfsrc_start(upipe);
}

/** @internal @This sets the upump_mgr and deals with the upump_av_deal.
 *
 * @param upipe description structure of the pipe
 * @param upump_mgr new upump_mgr
 * @return false in case of error
 */
static inline bool _upipe_avfsrc_set_upump_mgr(struct upipe *upipe,
                                               struct upump_mgr *upump_mgr)
{
    upipe_avfsrc_abort_av_deal(upipe);
    return upipe_avfsrc_set_upump_mgr(upipe, upump_mgr);
}

/** @internal @This returns the source flow name.
 *
 * @param upipe description structure of the pipe
 * @param url_p filled in with the URL
 * @return false in case of error
 */
static bool upipe_avfsrc_get_flow_name(struct upipe *upipe, const char **p)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(p != NULL);
    *p = upipe_avfsrc->flow_name;
    return true;
}

/** @internal @This sets the content of an avformat option. It only take effect
 * after the next call to @ref upipe_avfsrc_set_url.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return false in case of error
 */
static bool upipe_avfsrc_set_flow_name(struct upipe *upipe,
                                       const char *flow_name)
{
    assert(flow_name != NULL);
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    free(upipe_avfsrc->flow_name);
    if (likely(flow_name != NULL)) {
        upipe_avfsrc->flow_name = strdup(flow_name);
        if (unlikely(upipe_avfsrc->flow_name == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            return false;
        }
    } else
        upipe_avfsrc->flow_name = NULL;

    struct uchain *uchain;
    ulist_foreach (&upipe_avfsrc->outputs, uchain) {
        struct upipe_avfsrc_output *output =
            upipe_avfsrc_output_from_uchain(uchain);
        if (likely(output->flow_def != NULL)) {
            struct uref *flow_def = uref_dup(output->flow_def);
            if (unlikely(flow_def == NULL)) {
                ulog_aerror(upipe->ulog);
                upipe_throw_aerror(upipe);
                return false;
            }

            bool ret;
            if (unlikely(upipe_avfsrc->flow_name == NULL))
                ret = uref_flow_set_name(flow_def, output->flow_suffix);
            else
                ret = uref_flow_set_name_va(flow_def, "%s.%s",
                                            upipe_avfsrc->flow_name,
                                            output->flow_suffix);
            if (unlikely(!ret)) {
                ulog_aerror(upipe->ulog);
                upipe_throw_aerror(upipe);
                return false;
            }

            upipe_avfsrc_output_set_flow_def(upipe, output, flow_def);
        }
    }
    return true;
}

/** @internal @This returns the content of an avformat option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content_p filled in with the content of the option
 * @return false in case of error
 */
static bool _upipe_avfsrc_get_option(struct upipe *upipe, const char *option,
                                     const char **content_p)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(option != NULL);
    assert(content_p != NULL);
    AVDictionaryEntry *entry = av_dict_get(upipe_avfsrc->options, option,
                                           NULL, 0);
    if (unlikely(entry == NULL))
        return false;
    *content_p = entry->value;
    return true;
}

/** @internal @This sets the content of an avformat option. It only take effect
 * after the next call to @ref upipe_avfsrc_set_url.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return false in case of error
 */
static bool _upipe_avfsrc_set_option(struct upipe *upipe, const char *option,
                                     const char *content)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(option != NULL);
    int error = av_dict_set(&upipe_avfsrc->options, option, content, 0);
    if (unlikely(error < 0)) {
        ulog_error(upipe->ulog, "can't set option %s:%s (%s)", option, content,
                   upipe_av_ulog_strerror(upipe->ulog, error));
        return false;
    }
    return true;
}

/** @internal @This returns the currently opened URL.
 *
 * @param upipe description structure of the pipe
 * @param url_p filled in with the URL
 * @return false in case of error
 */
static bool _upipe_avfsrc_get_url(struct upipe *upipe, const char **url_p)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(url_p != NULL);
    *url_p = upipe_avfsrc->url;
    return true;
}

/** @internal @This asks to open the given URL.
 *
 * @param upipe description structure of the pipe
 * @param url URL to open
 * @return false in case of error
 */
static bool _upipe_avfsrc_set_url(struct upipe *upipe, const char *url)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);

    if (unlikely(upipe_avfsrc->context != NULL)) {
        if (likely(upipe_avfsrc->url != NULL))
            ulog_notice(upipe->ulog, "closing URL %s", upipe_avfsrc->url);
        avformat_close_input(&upipe_avfsrc->context);
        upipe_avfsrc->context = NULL;
        upipe_avfsrc_set_upump(upipe, NULL);
        upipe_avfsrc_abort_av_deal(upipe);
    }
    free(upipe_avfsrc->url);
    upipe_avfsrc->url = NULL;

    if (likely(url != NULL)) {
        AVDictionary *options = NULL;
        av_dict_copy(&options, upipe_avfsrc->options, 0);
        int error = avformat_open_input(&upipe_avfsrc->context, url, NULL,
                                        &options);
        av_dict_free(&options);
        if (unlikely(error < 0)) {
            ulog_error(upipe->ulog, "can't open URL %s (%s)", url,
                       upipe_av_ulog_strerror(upipe->ulog, error));
            return false;
        }

        upipe_avfsrc->url = strdup(url);
        ulog_notice(upipe->ulog, "opening URL %s", upipe_avfsrc->url);
    }
    return true;
}

/** @internal @This returns the time of the currently opened URL.
 *
 * @param upipe description structure of the pipe
 * @param time_p filled in with the reading time, in clock units
 * @return false in case of error
 */
static bool _upipe_avfsrc_get_time(struct upipe *upipe, uint64_t *time_p)
{
    //struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(time_p != NULL);
    return false;
}

/** @internal @This asks to read at the given time.
 *
 * @param upipe description structure of the pipe
 * @param time new reading time, in clock units
 * @return false in case of error
 */
static bool _upipe_avfsrc_set_time(struct upipe *upipe, uint64_t time)
{
    //struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    return false;
}

/** @internal @This processes control commands on an avformat source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_avfsrc_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_avfsrc_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_avfsrc_set_uref_mgr(upipe, uref_mgr);
        }

        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_avfsrc_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            return _upipe_avfsrc_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_avfsrc_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            return upipe_avfsrc_set_uclock(upipe, uclock);
        }
        case UPIPE_SOURCE_GET_FLOW_NAME: {
            const char **p = va_arg(args, const char **);
            return upipe_avfsrc_get_flow_name(upipe, p);
        }
        case UPIPE_SOURCE_SET_FLOW_NAME: {
            const char *flow_name = va_arg(args, const char *);
            return upipe_avfsrc_set_flow_name(upipe, flow_name);
        }

        case UPIPE_SPLIT_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_avfsrc_get_output(upipe, p, flow_suffix);
        }
        case UPIPE_SPLIT_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_avfsrc_set_output(upipe, output, flow_suffix);
        }
        case UPIPE_SPLIT_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_avfsrc_get_ubuf_mgr(upipe, p, flow_suffix);
        }
        case UPIPE_SPLIT_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_avfsrc_set_ubuf_mgr(upipe, ubuf_mgr, flow_suffix);
        }

        case UPIPE_AVFSRC_GET_OPTION: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSRC_SIGNATURE);
            const char *option = va_arg(args, const char *);
            const char **content_p = va_arg(args, const char **);
            return _upipe_avfsrc_get_option(upipe, option, content_p);
        }
        case UPIPE_AVFSRC_SET_OPTION: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSRC_SIGNATURE);
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return _upipe_avfsrc_set_option(upipe, option, content);
        }
        case UPIPE_AVFSRC_GET_URL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSRC_SIGNATURE);
            const char **url_p = va_arg(args, const char **);
            return _upipe_avfsrc_get_url(upipe, url_p);
        }
        case UPIPE_AVFSRC_SET_URL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSRC_SIGNATURE);
            const char *url = va_arg(args, const char *);
            return _upipe_avfsrc_set_url(upipe, url);
        }
        case UPIPE_AVFSRC_GET_TIME: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSRC_SIGNATURE);
            uint64_t *time_p = va_arg(args, uint64_t *);
            return _upipe_avfsrc_get_time(upipe, time_p);
        }
        case UPIPE_AVFSRC_SET_TIME: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSRC_SIGNATURE);
            uint64_t time = va_arg(args, uint64_t);
            return _upipe_avfsrc_set_time(upipe, time);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on an avformat source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_avfsrc_control(struct upipe *upipe,
                                 enum upipe_command command, va_list args)
{
    if (unlikely(!_upipe_avfsrc_control(upipe, command, args)))
        return false;

    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    if (unlikely(upipe_avfsrc->uref_mgr != NULL &&
                 upipe_avfsrc->upump_mgr != NULL &&
                 upipe_avfsrc->url != NULL)) {
        if (unlikely(upipe_avfsrc->upump != NULL))
            return true;

        if (unlikely(upipe_avfsrc->probed))
            return upipe_avfsrc_start(upipe);

        if (unlikely(upipe_avfsrc->upump_av_deal != NULL))
            return true;

        struct upump *upump_av_deal =
            upipe_av_deal_upump_alloc(upipe_avfsrc->upump_mgr,
                                      upipe_avfsrc_probe, upipe);
        if (unlikely(upump_av_deal == NULL)) {
            ulog_error(upipe->ulog, "can't create dealer");
            upipe_throw_upump_error(upipe);
            return false;
        }
        upipe_avfsrc->upump_av_deal = upump_av_deal;
        upipe_av_deal_start(upump_av_deal);

    } else {
        upipe_avfsrc_set_upump(upipe, NULL);
        upipe_avfsrc_abort_av_deal(upipe);
        upipe_avfsrc->ready = false;

        if (unlikely(upipe_avfsrc->url != NULL)) {
            if (unlikely(upipe_avfsrc->uref_mgr == NULL))
                upipe_throw_need_uref_mgr(upipe);
            else if (unlikely(upipe_avfsrc->upump_mgr == NULL))
                upipe_throw_need_upump_mgr(upipe);
        }
    }

    return true;
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_use(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    urefcount_use(&upipe_avfsrc->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_release(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_avfsrc->refcount))) {
        upipe_avfsrc_abort_av_deal(upipe);
        if (likely(upipe_avfsrc->context != NULL)) {
            if (likely(upipe_avfsrc->url != NULL))
                ulog_notice(upipe->ulog, "closing URL %s", upipe_avfsrc->url);
            avformat_close_input(&upipe_avfsrc->context);
        }
        av_dict_free(&upipe_avfsrc->options);

        free(upipe_avfsrc->url);
        free(upipe_avfsrc->flow_name);

        upipe_avfsrc_clean_outputs(upipe, upipe_avfsrc_output_free);
        upipe_avfsrc_clean_uclock(upipe);
        upipe_avfsrc_clean_upump_mgr(upipe);
        upipe_avfsrc_clean_uref_mgr(upipe);

        upipe_clean(upipe);
        urefcount_clean(&upipe_avfsrc->refcount);
        free(upipe_avfsrc);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_avfsrc_mgr = {
    .upipe_alloc = upipe_avfsrc_alloc,
    .upipe_control = upipe_avfsrc_control,
    .upipe_use = upipe_avfsrc_use,
    .upipe_release = upipe_avfsrc_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all avformat sources
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avfsrc_mgr_alloc(void)
{
    return &upipe_avfsrc_mgr;
}
