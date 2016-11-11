/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#include <upipe-hls/upipe_hls_video.h>

#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>

struct upipe_hls_video {
    struct upipe upipe;
    struct urefcount urefcount;
    struct urefcount urefcount_real;
};

static void upipe_hls_video_free(struct urefcount *urefcount);

UPIPE_HELPER_UPIPE(upipe_hls_video, upipe, UPIPE_HLS_VIDEO_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_hls_video, urefcount, upipe_hls_video_no_ref);
UPIPE_HELPER_FLOW(upipe_hls_video, "pic.");

UBASE_FROM_TO(upipe_hls_video, urefcount, urefcount_real, urefcount_real);

static struct upipe *upipe_hls_video_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature,
                                           va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe =
        upipe_hls_video_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_hls_video_init_urefcount(upipe);

    struct upipe_hls_video *upipe_hls_video = upipe_hls_video_from_upipe(upipe);
    urefcount_init(&upipe_hls_video->urefcount_real, upipe_hls_video_free);

    upipe_throw_ready(upipe);

    return upipe;
}

static void upipe_hls_video_free(struct urefcount *urefcount)
{
    struct upipe_hls_video *upipe_hls_video =
        upipe_hls_video_from_urefcount_real(urefcount);
    struct upipe *upipe = upipe_hls_video_to_upipe(upipe_hls_video);

    upipe_throw_dead(upipe);

    upipe_hls_video_clean_urefcount(upipe);
    upipe_hls_video_free_flow(upipe);
}

static void upipe_hls_video_no_ref(struct upipe *upipe)
{
    struct upipe_hls_video *upipe_hls_video = upipe_hls_video_from_upipe(upipe);

    urefcount_release(&upipe_hls_video->urefcount_real);
}

static int upipe_hls_video_control(struct upipe *upipe,
                                   int command, va_list args)
{
    return UBASE_ERR_UNHANDLED;
}

static struct upipe_mgr upipe_hls_video_mgr = {
    .refcount = NULL,
    .signature = UPIPE_HLS_VIDEO_SIGNATURE,
    .upipe_alloc = upipe_hls_video_alloc,
    .upipe_control = upipe_hls_video_control,
    //FIXME: implement video only rendition
};

struct upipe_mgr *upipe_hls_video_mgr_alloc(void)
{
    return &upipe_hls_video_mgr;
}
