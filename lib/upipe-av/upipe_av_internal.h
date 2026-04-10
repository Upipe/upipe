/*
 * Copyright (C) 2012-2018 OpenHeadend S.A.R.L.
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

#endif
