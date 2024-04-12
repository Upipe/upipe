/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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
 * @short Upipe module to buffer and reorder rtp packets from multiple sources
 */

#ifndef _UPIPE_MODULES_UPIPE_RTP_REORDER_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_RTP_REORDER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_RTPR_SIGNATURE UBASE_FOURCC('r','t','p','r')
#define UPIPE_RTPR_INPUT_SIGNATURE UBASE_FOURCC('r','t','p','i')


/** @This extends upipe_command with specific commands for rtpr pipes. */
enum upipe_rtpr_command {
    UPIPE_RTPR_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current reorder delay being set into urefs (uint64_t *) */
    UPIPE_RTPR_GET_DELAY,
    /** sets the reorder delay to set into urefs (uint64_t) */
    UPIPE_RTPR_SET_DELAY
};

/** @This extends upipe_command with specific commands for rtpr subpipes. */
enum upipe_rtpr_sub_command {
    UPIPE_RTPR_SUB_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the maximum observed delay for this input (uint64_t *) */
    UPIPE_RTPR_SUB_GET_MAX_DELAY
};

/** @This returns the management structure for rtpr pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtpr_mgr_alloc(void);

/** @This returns the current delay being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param delay_p filled with the current delay
 * @return an error code
 */
static inline int upipe_rtpr_get_delay(struct upipe *upipe, uint64_t *delay_p)
{
    return upipe_control(upipe, UPIPE_RTPR_GET_DELAY,
                         UPIPE_RTPR_SIGNATURE, delay_p);
}

/** @This sets the delay to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param delay delay to set
 * @return an error code
 */
static inline int upipe_rtpr_set_delay(struct upipe *upipe, uint64_t delay)
{
    return upipe_control(upipe, UPIPE_RTPR_SET_DELAY,
                         UPIPE_RTPR_SIGNATURE, delay);
}

/** @This returns the maximum observed delay for this input subpipe
 * since the last call.
 *
 * @param upipe description structure of the pipe
 * @param delay_p filled with the maximum delay
 * @return an error code
 */
static inline int upipe_rtpr_sub_get_max_delay(struct upipe *upipe,
                                               uint64_t *delay_p)
{
    return upipe_control(upipe, UPIPE_RTPR_SUB_GET_MAX_DELAY,
                         UPIPE_RTPR_INPUT_SIGNATURE, delay_p);
}

#ifdef __cplusplus
}
#endif
#endif
