/*
 * Copyright (C) 2016-2017 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
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
 * @short Upipe module receiving rfc4585 feedback
 */

#ifndef _UPIPE_FILTERS_UPIPE_RTCP_FB_RECEIVER_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_RTCP_FB_RECEIVER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_RTCPFB_SIGNATURE UBASE_FOURCC('r','t','c','f')
#define UPIPE_RTCPFB_INPUT_SIGNATURE UBASE_FOURCC('r','t','c','i')

/** @This extends upipe_command with specific commands for upipe_rtcpfb pipes.
 */
enum upipe_rtcpfb_command {
     UPIPE_RTCPFB_SENTINEL = UPIPE_CONTROL_LOCAL,

     /** sets the payload type of the retransmit stream (unsigned) */
     UPIPE_RTCPFB_SET_RTX_PT,
};

/** @This sets the value of the rtx_pt channel.
 *
 * @param upipe description structure of the pipe
 * @param rtx_pt value of the rtx_pt channel
 * @return an error code
 */
static inline int upipe_rtcpfb_set_rtx_pt(struct upipe *upipe,
        uint8_t rtx_pt)
{
    return upipe_control(upipe, UPIPE_RTCPFB_SET_RTX_PT,
                         UPIPE_RTCPFB_SIGNATURE, (unsigned)rtx_pt);
}

/** @This returns the management structure for rtcpfb pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtcpfb_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
