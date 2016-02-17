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

#ifndef _UPIPE_HLS_UPIPE_SEGMENT_SOURCE_H_
# define _UPIPE_HLS_UPIPE_SEGMENT_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_SEG_SRC_SIGNATURE UBASE_FOURCC('s','e','g','s')

enum upipe_seg_src_mgr_command {
    UPIPE_SEG_SRC_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

    UPIPE_SEG_SRC_MGR_SET_SOURCE_MGR,
    UPIPE_SEG_SRC_MGR_GET_SOURCE_MGR,
};

static inline int upipe_seg_src_mgr_set_source_mgr(struct upipe_mgr *mgr,
                                                   struct upipe_mgr *src_mgr)
{
    return upipe_mgr_control(mgr, UPIPE_SEG_SRC_MGR_SET_SOURCE_MGR,
                             UPIPE_SEG_SRC_SIGNATURE, src_mgr);
}

static inline int upipe_seg_src_mgr_get_source_mgr(struct upipe_mgr *mgr,
                                                   struct upipe_mgr **mgr_p)
{
    return upipe_mgr_control(mgr, UPIPE_SEG_SRC_MGR_GET_SOURCE_MGR,
                             UPIPE_SEG_SRC_SIGNATURE, mgr_p);
}

enum uprobe_seg_src_event {
    UPROBE_SEG_SRC_SENTINEL = UPROBE_LOCAL,

    UPROBE_SEG_SRC_UPDATE,
};

static inline const char *upipe_seg_src_event_str(int event)
{
    switch ((enum uprobe_seg_src_event)event) {
    UBASE_CASE_TO_STR(UPROBE_SEG_SRC_UPDATE);
    case UPROBE_SEG_SRC_SENTINEL: break;
    }
    return NULL;
}

struct upipe_mgr *upipe_seg_src_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_HLS_UPIPE_SEGMENT_SOURCE_H_ */
