/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short upipe/avutil sampleformat conversion
 * This is also used in swresample.
 */

#ifndef _UPIPE_AV_UPIPE_AV_SAMPLEFMT_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AV_SAMPLEFMT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref_sound_flow.h>
#include <libavutil/samplefmt.h>

/** @internal @This lists av's native audio formats and translates them to flow
 * definitions. */
static const struct {
    enum AVSampleFormat fmt;
    const char *flow_def;
} upipe_av_sample_fmts[] = {
    { AV_SAMPLE_FMT_U8, "sound.u8." },
    { AV_SAMPLE_FMT_S16, "sound.s16." },
    { AV_SAMPLE_FMT_S32, "sound.s32." },
    { AV_SAMPLE_FMT_FLT, "sound.f32." },
    { AV_SAMPLE_FMT_DBL, "sound.f64." },
#ifdef UPIPE_WORDS_BIGENDIAN
    { AV_SAMPLE_FMT_S16, "sound.s16be." },
    { AV_SAMPLE_FMT_S32, "sound.s32be." },
    { AV_SAMPLE_FMT_FLT, "sound.f32be." },
    { AV_SAMPLE_FMT_DBL, "sound.f64be." },
#else
    { AV_SAMPLE_FMT_S16, "sound.s16le." },
    { AV_SAMPLE_FMT_S32, "sound.s32le." },
    { AV_SAMPLE_FMT_FLT, "sound.f32le." },
    { AV_SAMPLE_FMT_DBL, "sound.f64le." },
#endif
    { AV_SAMPLE_FMT_NONE, NULL }
};

/** @This is the list of channels. FIXME channel ordering */
#define UPIPE_AV_SAMPLEFMT_CHANNELS "lrcLRS12345689"

/** @This allows to convert from av sample format to flow definition.
 *
 * @param flow_def overwritten flow definition
 * @param fmt av sample format
 * @param channels number of channels
 * @return an error code
 */
static inline int
    upipe_av_samplefmt_to_flow_def(struct uref *flow_def,
                                   enum AVSampleFormat fmt, uint8_t channels)
{
    char channels_desc[sizeof(UPIPE_AV_SAMPLEFMT_CHANNELS)];
    memcpy(channels_desc, UPIPE_AV_SAMPLEFMT_CHANNELS,
           sizeof(UPIPE_AV_SAMPLEFMT_CHANNELS));
    assert(channels < strlen(channels_desc));
    UBASE_RETURN(uref_sound_flow_set_channels(flow_def, channels))
    UBASE_RETURN(uref_sound_flow_set_planes(flow_def, 0))
    if (av_sample_fmt_is_planar(fmt)) {
        for (unsigned int i = 0; i < channels; i++) {
            char channel_desc[2];
            channel_desc[0] = channels_desc[i];
            channel_desc[1] = '\0';
            uref_sound_flow_add_plane(flow_def, channel_desc);
        }
        channels = 1;
        fmt = av_get_packed_sample_fmt(fmt);
    } else {
        channels_desc[channels] = '\0';
        uref_sound_flow_add_plane(flow_def, channels_desc);
    }

    for (unsigned int i = 0; upipe_av_sample_fmts[i].fmt != AV_SAMPLE_FMT_NONE;
         i++)
        if (upipe_av_sample_fmts[i].fmt == fmt) {
            UBASE_RETURN(uref_flow_set_def(flow_def,
                        upipe_av_sample_fmts[i].flow_def))
            UBASE_RETURN(uref_sound_flow_set_sample_size(flow_def,
                        av_get_bytes_per_sample(fmt) * channels))
            return UBASE_ERR_NONE;
        }
    return UBASE_ERR_INVALID;
}

/** @This allows to convert to av sample format from flow definition.
 *
 * @param flow_def flow definition
 * @param channels_p filled in with the number of channels
 * @return av sample format, or AV_SAMPLE_FMT_NONE if not found
 */
static inline enum AVSampleFormat
    upipe_av_samplefmt_from_flow_def(struct uref *flow_def, uint8_t *channels_p)
{
    const char *def;
    uint8_t planes;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 !ubase_check(uref_sound_flow_get_channels(flow_def,
                                                           channels_p)) ||
                 !ubase_check(uref_sound_flow_get_planes(flow_def, &planes))))
        return AV_SAMPLE_FMT_NONE;
    enum AVSampleFormat fmt = AV_SAMPLE_FMT_NONE;
    for (unsigned int i = 0; upipe_av_sample_fmts[i].fmt != AV_SAMPLE_FMT_NONE;
         i++) {
        if (!ubase_ncmp(def, upipe_av_sample_fmts[i].flow_def)) {
            fmt = upipe_av_sample_fmts[i].fmt;
            break;
        }
    }
    if (planes != 1)
        return av_get_planar_sample_fmt(fmt);
    return fmt;
}

#ifdef __cplusplus
}
#endif
#endif
