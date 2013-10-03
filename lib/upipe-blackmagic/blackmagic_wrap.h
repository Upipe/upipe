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
 * @short BlackMagic Design wrapping API for C
 */

#ifndef _UPIPE_BLACKMAGIC_BLACKMAGIC_WRAP_H_
/** @hidden */
#define _UPIPE_BLACKMAGIC_BLACKMAGIC_WRAP_H_
#ifdef __cplusplus
extern "C" {
#endif

/** @This is a blackmagic raw frame */
struct bmd_frame {
    /** frame duration */
    uint64_t duration;
    /** frame timecode */
    int64_t timecode;
    /** data buffer */
    uint8_t *data;

    /** picture width */
    size_t width;
    /** picture height */
    size_t height;
    /** stride size (in bytes) */
    size_t stride;

    /** audio samples in packet */
    size_t samples;
};

/** @This is a blackmagic callback fonction pointer */
typedef void (*bmd_wrap_cb)(void *opaque, struct bmd_frame *frame);

/** @This is the opaque structure for the blackmagic wrapper API */
struct bmd_wrap;

/** @This allocates a blackmagic wrapper
 * @param opaque user-defined opaque
 * @return pointer to wrap structure
 */
struct bmd_wrap *bmd_wrap_alloc(void *opaque);

/** @This sets the audio callback to a blackmagic wrapper
 * @param wrap pointer to wrap structure
 * @param cb callback
 * @return previous callback
 */
bmd_wrap_cb bmd_wrap_set_audio_cb(struct bmd_wrap *wrap, bmd_wrap_cb cb);

/** @This sets the video callback to a blackmagic wrapper
 * @param wrap pointer to wrap structure
 * @param cb callback
 * @return previous callback
 */
bmd_wrap_cb bmd_wrap_set_video_cb(struct bmd_wrap *wrap, bmd_wrap_cb cb);

/** @This starts acquisition
 * @param wrap pointer to wrap structure
 * @return false in case of error
 */
bool bmd_wrap_start(struct bmd_wrap *wrap);

/** @This stops acquisition
 * @param wrap pointer to wrap structure
 * @return false in case of error
 */
bool bmd_wrap_stop(struct bmd_wrap *wrap);

/** @This stops acquisition
 * @param wrap pointer to wrap structure
 * @return false in case of error
 */
bool bmd_wrap_free(struct bmd_wrap *wrap);

#ifdef __cplusplus
}
#endif
#endif
