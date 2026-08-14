/*
 * Copyright (C) 2012-2018 OpenHeadend S.A.R.L.
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short internal interface to av managers
 */

#ifndef _UPIPE_AV_INTERNAL_H_
/** @hidden */
#define _UPIPE_AV_INTERNAL_H_

#include <stdbool.h>

#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavcodec/avcodec.h>

/** @hidden */
struct uref;
/** @hidden */
struct upipe;

/** @hidden */
enum AVCodecID;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(54, 51, 100)
/** @hidden */
enum CodecID;
/** @hidden */
#define AVCodecID CodecID
#define AV_CODEC_ID_FIRST_SUBTITLE CODEC_ID_FIRST_SUBTITLE
#define AV_CODEC_ID_FIRST_AUDIO CODEC_ID_FIRST_AUDIO
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
/** @This returns the list of configurations of a given type supported by a
 * codec.
 *
 * @param context codec context to take into account, or NULL for defaults
 * @param codec codec to query, or NULL to use the context codec
 * @param config type of configuration to query
 * @return a terminated list of values, or NULL if all values are supported
 */
static inline const void *
upipe_av_codec_get_supported_config(const AVCodecContext *context,
                                    const AVCodec *codec,
                                    enum AVCodecConfig config)
{
    const void *configs;
    if (avcodec_get_supported_config(context, codec, config, 0,
                                     &configs, NULL) < 0)
        return NULL;
    return configs;
}
#endif

/** @This returns the list of pixel formats supported by a codec.
 *
 * @param context codec context to take into account, or NULL for defaults
 * @param codec codec to query, or NULL to use the context codec
 * @return a list terminated by AV_PIX_FMT_NONE, or NULL if all are supported
 */
static inline const enum AVPixelFormat *
upipe_av_codec_get_pix_fmts(const AVCodecContext *context,
                            const AVCodec *codec)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    return upipe_av_codec_get_supported_config(context, codec,
                                               AV_CODEC_CONFIG_PIX_FORMAT);
#else
    return codec != NULL ? codec->pix_fmts : context->codec->pix_fmts;
#endif
}

/** @This returns the list of frame rates supported by a codec.
 *
 * @param context codec context to take into account, or NULL for defaults
 * @param codec codec to query, or NULL to use the context codec
 * @return a list terminated by {0, 0}, or NULL if all are supported
 */
static inline const AVRational *
upipe_av_codec_get_frame_rates(const AVCodecContext *context,
                               const AVCodec *codec)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    return upipe_av_codec_get_supported_config(context, codec,
                                               AV_CODEC_CONFIG_FRAME_RATE);
#else
    return codec != NULL ? codec->supported_framerates :
                           context->codec->supported_framerates;
#endif
}

/** @This returns the list of sample formats supported by a codec.
 *
 * @param context codec context to take into account, or NULL for defaults
 * @param codec codec to query, or NULL to use the context codec
 * @return a list terminated by AV_SAMPLE_FMT_NONE, or NULL if all are
 * supported
 */
static inline const enum AVSampleFormat *
upipe_av_codec_get_sample_fmts(const AVCodecContext *context,
                               const AVCodec *codec)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    return upipe_av_codec_get_supported_config(context, codec,
                                               AV_CODEC_CONFIG_SAMPLE_FORMAT);
#else
    return codec != NULL ? codec->sample_fmts : context->codec->sample_fmts;
#endif
}

/** @This returns the list of sample rates supported by a codec.
 *
 * @param context codec context to take into account, or NULL for defaults
 * @param codec codec to query, or NULL to use the context codec
 * @return a list terminated by 0, or NULL if all are supported
 */
static inline const int *
upipe_av_codec_get_sample_rates(const AVCodecContext *context,
                                const AVCodec *codec)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    return upipe_av_codec_get_supported_config(context, codec,
                                               AV_CODEC_CONFIG_SAMPLE_RATE);
#else
    return codec != NULL ? codec->supported_samplerates :
                           context->codec->supported_samplerates;
#endif
}

/** @This returns the list of channel layouts supported by a codec.
 *
 * @param context codec context to take into account, or NULL for defaults
 * @param codec codec to query, or NULL to use the context codec
 * @return a list terminated by {0}, or NULL if all are supported
 */
static inline const AVChannelLayout *
upipe_av_codec_get_ch_layouts(const AVCodecContext *context,
                              const AVCodec *codec)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    return upipe_av_codec_get_supported_config(context, codec,
                                               AV_CODEC_CONFIG_CHANNEL_LAYOUT);
#else
    return codec != NULL ? codec->ch_layouts : context->codec->ch_layouts;
#endif
}

/** @This allows to convert from avcodec ID to flow definition codec.
 *
 * @param id avcodec ID
 * @return flow definition codec, or "unknown" if not found
 */
const char *upipe_av_to_flow_def_codec(enum AVCodecID id);

/** @This allows to convert from avcodec ID to flow definition type.
 *
 * @param id avcodec ID
 * @return flow definition type
 */
const char *upipe_av_to_flow_def_type(enum AVCodecID id);

/** @This allows to convert to avcodec ID from flow definition.
 *
 * @param flow_def flow definition
 * @return avcodec ID, or 0 if not found
 */
enum AVCodecID upipe_av_from_flow_def(const char *flow_def);

/** @This sets frame properties from flow definition and uref packets.
 *
 * @param upipe upipe used for logging
 * @param frame av frame to setup
 * @param flow_def flow definition packet
 * @param uref uref structure
 * @return an error code
 */
int upipe_av_set_frame_properties(struct upipe *upipe,
                                  AVFrame *frame,
                                  struct uref *flow_def,
                                  struct uref *uref);

/** @This sets flow definition from a frame.
 *
 * @param flow_def flow definition packet
 * @param frame av frame to setup
 * @param current_flow_def current flow definition packet or NULL
 * @return an error code
 */
int upipe_av_get_frame_properties(struct uref *flow_def,
                                  const AVFrame *frame,
                                  struct uref *current_flow_def);

#endif
