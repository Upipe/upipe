/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe avcodec encode wrapper module
 */

#ifndef _UPIPE_AV_UPIPE_AV_ENCODE_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AV_ENCODE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_AVCENC_SIGNATURE UBASE_FOURCC('a', 'v', 'c', 'e')

/** @This extends upipe_command with specific commands for avcodec encode. */
enum upipe_avcenc_command {
    UPIPE_AVCENC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the currently opened codec (const char **) */
    UPIPE_AVCENC_GET_CODEC,
    /** asks to open the given codec (const char *) */
    UPIPE_AVCENC_SET_CODEC,
};

/** @This returns the currently opened codec.
 *
 * @param upipe description structure of the pipe
 * @param codec_p filled in with the codec name
 * @return false in case of error
 */
static inline bool upipe_avcenc_get_codec(struct upipe *upipe, const char **codec_p)
{
    return upipe_control(upipe, UPIPE_AVCENC_GET_CODEC, UPIPE_AVCENC_SIGNATURE,
                         codec_p);
}

/** @This asks to open the given codec.
 *
 * @param upipe description structure of the pipe
 * @param codec codec to open
 * @return false in case of error
 */
static inline bool upipe_avcenc_set_codec(struct upipe *upipe, const char *codec)
{
    return upipe_control(upipe, UPIPE_AVCENC_SET_CODEC, UPIPE_AVCENC_SIGNATURE,
                         codec);
}

/** @This returns the management structure for all avformat sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avcenc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
