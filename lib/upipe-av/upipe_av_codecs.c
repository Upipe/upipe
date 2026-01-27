/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *          Benjamin Cohen     <bencoh@notk.org>
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short upipe-av flow def/libavcodec codec_id translation
 */

#include <string.h>
#include "upipe_av_internal.h"

/** @This allows to convert from avcodec ID to flow definition codec.
 *
 * @param id avcodec ID
 * @return flow definition codec, or "unknown" if not found
 */
const char *upipe_av_to_flow_def_codec(enum AVCodecID id)
{
    const AVCodecDescriptor *desc = avcodec_descriptor_get(id);
    return desc ? desc->name : "unknown";
}

/** @This allows to convert from avcodec ID to flow definition type.
 *
 * @param id avcodec ID
 * @return flow definition type
 */
const char *upipe_av_to_flow_def_type(enum AVCodecID id)
{
    switch (avcodec_get_type(id)) {
        case AVMEDIA_TYPE_VIDEO:
            return "pic.";
        case AVMEDIA_TYPE_AUDIO:
            return "sound.";
        case AVMEDIA_TYPE_SUBTITLE:
            return "pic.sub.";
        default:
            return "unknown.";
    }
}

/** @This allows to convert to avcodec ID from flow definition.
 *
 * @param flow_def flow definition
 * @return avcodec ID, or 0 if not found
 */
enum AVCodecID upipe_av_from_flow_def(const char *flow_def)
{
    const char *t = strchr(flow_def, '.');
    if (!t)
        return AV_CODEC_ID_NONE;
    size_t len = t - flow_def;
    char name[len + 1];
    memcpy(name, flow_def, len);
    name[len] = '\0';

    enum AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
    if (!ubase_ncmp(t, ".pic.sub."))
        type = AVMEDIA_TYPE_SUBTITLE;
    else if (!ubase_ncmp(t, ".sound."))
        type = AVMEDIA_TYPE_AUDIO;
    else if (!ubase_ncmp(t, ".pic."))
        type = AVMEDIA_TYPE_VIDEO;

    const AVCodecDescriptor *desc = avcodec_descriptor_get_by_name(name);
    if (!desc || desc->type != type)
        return AV_CODEC_ID_NONE;

    return desc->id;
}
