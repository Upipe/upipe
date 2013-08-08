/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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

#include <libavutil/samplefmt.h>

/** @internal @This lists av's native audio formats and translates them to flow
 * definitions. */
static const struct {
    enum AVSampleFormat fmt;
    const char *flow_def;
} upipe_av_sample_fmts[] = {
    { AV_SAMPLE_FMT_U8, "block.pcm_u8.sound." },
#ifdef UPIPE_WORDS_BIGENDIAN
    { AV_SAMPLE_FMT_S16, "block.pcm_s16be.sound." },
    { AV_SAMPLE_FMT_S32, "block.pcm_s32be.sound." },
    { AV_SAMPLE_FMT_FLT, "block.pcm_f32be.sound." },
    { AV_SAMPLE_FMT_DBL, "block.pcm_f64be.sound." },
#else
    { AV_SAMPLE_FMT_S16, "block.pcm_s16le.sound." },
    { AV_SAMPLE_FMT_S32, "block.pcm_s32le.sound." },
    { AV_SAMPLE_FMT_FLT, "block.pcm_f32le.sound." },
    { AV_SAMPLE_FMT_DBL, "block.pcm_f64le.sound." },
#endif
    /* TODO: planar types when they are available in Upipe */
    { AV_SAMPLE_FMT_NONE, NULL }
};

/** @This allows to convert from av sample format to flow definition.
 *
 * @param fmt av sample format
 * @return flow definition, or NULL if not found
 */
const char *upipe_av_samplefmt_to_flow_def(enum AVSampleFormat fmt)
{
    for (unsigned int i = 0; upipe_av_sample_fmts[i].fmt != AV_SAMPLE_FMT_NONE;
         i++)
        if (upipe_av_sample_fmts[i].fmt == fmt)
            return upipe_av_sample_fmts[i].flow_def;
    return NULL;
}

/** @This allows to convert to av sample format from flow definition.
 *
 * @param flow_def flow definition
 * @return av sample format, or AV_SAMPLE_FMT_NONE if not found
 */
enum AVSampleFormat upipe_av_samplefmt_from_flow_def(const char *flow_def)
{
    for (unsigned int i = 0; upipe_av_sample_fmts[i].fmt != AV_SAMPLE_FMT_NONE;
         i++)
        if (!ubase_ncmp(flow_def, upipe_av_sample_fmts[i].flow_def))
            return upipe_av_sample_fmts[i].fmt;
    return 0;
}

#ifdef __cplusplus
}
#endif
#endif
