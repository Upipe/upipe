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
 * @short Upipe avcodec decode wrapper module
 */

#ifndef _UPIPE_AV_UPIPE_AVCODEC_DECODE_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AVCODEC_DECODE_H_

#include <upipe/upipe.h>

#define UPIPE_AVCDV_SIGNATURE UBASE_FOURCC('a', 'v', 'c', 'd')

/** @This extends upipe_command with specific commands for avcodec decode. */
enum upipe_avcdv_command {
    UPIPE_AVCDV_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the currently opened codec (const char **) */
    UPIPE_AVCDV_GET_CODEC,
    /** asks to open the given codec (const char *) */
    UPIPE_AVCDV_SET_CODEC,

    /** set extradata (const char *, int) */
    UPIPE_AVCDV_SET_EXTRADATA
};

/** @This returns the currently opened codec.
 *
 * @param upipe description structure of the pipe
 * @param codec_p filled in with the codec name
 * @return false in case of error
 */
static inline bool upipe_avcdv_get_codec(struct upipe *upipe, const char **codec_p)
{
    return upipe_control(upipe, UPIPE_AVCDV_GET_CODEC, UPIPE_AVCDV_SIGNATURE,
                         codec_p);
}

/** @This asks to open the given codec.
 *
 * @param upipe description structure of the pipe
 * @param codec codec to open
 * @return false in case of error
 */
static inline bool upipe_avcdv_set_codec(struct upipe *upipe, const char *codec)
{
    return upipe_control(upipe, UPIPE_AVCDV_SET_CODEC, UPIPE_AVCDV_SIGNATURE,
                         codec);
}

/** @This sends extradata to avcodec.
 *
 * @param upipe description structure of the pipe
 * @param extradata extradata buffer to copy from
 * @param size extradata size
 * @return false in case of error
 */
static inline bool upipe_avcdv_set_extradata(struct upipe *upipe, const char *extradata, int size)
{
    return upipe_control(upipe, UPIPE_AVCDV_SET_EXTRADATA, UPIPE_AVCDV_SIGNATURE,
                         extradata, size);
}

/** @This returns the management structure for all avformat sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avcdv_mgr_alloc(void);

#endif
