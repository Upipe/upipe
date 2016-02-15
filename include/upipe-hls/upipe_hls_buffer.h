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

#ifndef _UPIPE_HLS_UPIPE_HLS_BUFFER_H_
# define _UPIPE_HLS_UPIPE_HLS_BUFFER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <upipe/uclock.h>

#define UPIPE_HLS_BUFFER_SIGNATURE UBASE_FOURCC('h','l','s','b')

enum upipe_hls_buffer_command {
    UPIPE_HLS_BUFFER_SENTINEL = UPIPE_CONTROL_LOCAL,
    UPIPE_HLS_BUFFER_SET_MAX_SIZE,
    UPIPE_HLS_BUFFER_GET_DURATION,
};

static inline const char *upipe_hls_buffer_command_str(int cmd)
{
    switch ((enum upipe_hls_buffer_command)cmd) {
    UBASE_CASE_TO_STR(UPIPE_HLS_BUFFER_SET_MAX_SIZE);
    UBASE_CASE_TO_STR(UPIPE_HLS_BUFFER_GET_DURATION);
    case UPIPE_HLS_BUFFER_SENTINEL: break;
    }
    return NULL;
}

static inline int upipe_hls_buffer_set_max_size(struct upipe *upipe,
                                                uint64_t max_size)
{
    return upipe_control(upipe, UPIPE_HLS_BUFFER_SET_MAX_SIZE,
                         UPIPE_HLS_BUFFER_SIGNATURE, max_size);
}

static inline int upipe_hls_buffer_get_duration(struct upipe *upipe,
                                                uint64_t *duration_p)
{
    return upipe_control(upipe, UPIPE_HLS_BUFFER_GET_DURATION,
                         UPIPE_HLS_BUFFER_SIGNATURE, duration_p);
}

enum uprobe_hls_buffer_event {
    UPROBE_HLS_BUFFER_SENTINEL = UPROBE_LOCAL,

    /** the buffered duration has changed (uint64_t) */
    UPROBE_HLS_BUFFER_UPDATE,
    /** an end of block was reached */
    UPROBE_HLS_BUFFER_EOB,
};

static inline const char *upipe_hls_buffer_event_str(int event)
{
    switch ((enum uprobe_hls_buffer_event)event) {
    UBASE_CASE_TO_STR(UPROBE_HLS_BUFFER_EOB);
    UBASE_CASE_TO_STR(UPROBE_HLS_BUFFER_UPDATE);
    case UPROBE_HLS_BUFFER_SENTINEL: break;
    }
    return NULL;
}

struct upipe_mgr *upipe_hls_buffer_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_HLS_UPIPE_HLS_BUFFER_H_ */
