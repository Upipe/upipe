/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe ubuf manager for AVFrame
 */

#ifndef _UPIPE_AV_UBUF_AV_H_
#define _UPIPE_AV_UBUF_AV_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubuf.h>
#include <libavutil/frame.h>

#define UBUF_AV_SIGNATURE UBASE_FOURCC('A','V','F','b')
#define UBUF_AV_ALLOC_PICTURE UBASE_FOURCC('A','V','F','p')
#define UBUF_AV_ALLOC_SOUND UBASE_FOURCC('A','V','F','s')

/** @This extends ubuf_command with specific commands for AVFrame buffers. */
enum ubuf_av_command {
    UBUF_AV_SENTINEL = UBUF_CONTROL_LOCAL,

    /** returns the underlying AVFrame (AVFrame *) */
    UBUF_AV_GET_AVFRAME
};

/** @This allocates an ubuf for a picture AVFrame.
 *
 * @param ubuf_mgr pointer to AVFrame ubuf manager
 * @param frame pointer to AVFrame
 * @return a pointer to an ubuf or NULL in case of error
 */
static inline struct ubuf *ubuf_pic_av_alloc(struct ubuf_mgr *mgr,
                                             AVFrame *frame)
{
    return ubuf_alloc(mgr, UBUF_AV_ALLOC_PICTURE, frame);
}

/** @This allocates an ubuf for a sound AVFrame.
 *
 * @param ubuf_mgr pointer to AVFrame ubuf manager
 * @param frame pointer to AVFrame
 * @return a pointer to an ubuf or NULL in case of error
 */
static inline struct ubuf *ubuf_sound_av_alloc(struct ubuf_mgr *mgr,
                                               AVFrame *frame)
{
    return ubuf_alloc(mgr, UBUF_AV_ALLOC_SOUND, frame);
}

/** @This returns a reference to the underlying AVFrame.
 * The frame should be freed when it is no longer needed.
 *
 * @param ubuf pointer to ubuf
 * @param frame unreferenced or newly allocated AVFrame
 * @return an error code
 */
static inline int ubuf_av_get_avframe(struct ubuf *ubuf, AVFrame *frame)
{
    return ubuf_control(ubuf, UBUF_AV_GET_AVFRAME, UBUF_AV_SIGNATURE, frame);
}

/** @This allocates and initializes an AVFrame buffer manager.
 *
 * @return a pointer to an ubuf manager
 */
struct ubuf_mgr *ubuf_av_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
