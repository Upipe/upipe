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

#ifndef _UPIPE_HLS_UPIPE_HLS_VARIANT_H_
# define _UPIPE_HLS_UPIPE_HLS_VARIANT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uprobe.h>
#include <upipe/upipe.h>

#define UPIPE_HLS_VARIANT_SIGNATURE     UBASE_FOURCC('h','l','s','V')
#define UPIPE_HLS_VARIANT_SUB_SIGNATURE UBASE_FOURCC('h','l','s','v')

enum upipe_hls_variant_command {
    UPIPE_HLS_VARIANT_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** play a variant (struct uref *) */
    UPIPE_HLS_VARIANT_PLAY,
};

static inline const char *upipe_hls_variant_command_str(int cmd)
{
    switch ((enum upipe_hls_variant_command)cmd) {
    UBASE_CASE_TO_STR(UPIPE_HLS_VARIANT_PLAY);
    case UPIPE_HLS_VARIANT_SENTINEL: break;
    }
    return NULL;
}

static inline int upipe_hls_variant_play(struct upipe *upipe,
                                         struct uref *variant)
{
    return upipe_control(upipe, UPIPE_HLS_VARIANT_PLAY,
                         UPIPE_HLS_VARIANT_SIGNATURE, variant);
}

struct upipe_mgr *upipe_hls_variant_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
