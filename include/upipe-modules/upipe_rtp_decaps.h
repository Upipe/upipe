/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module decapsulating RTP header from blocks
 */

#ifndef _UPIPE_MODULES_UPIPE_RTP_DECAPS_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_RTP_DECAPS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_RTPD_SIGNATURE UBASE_FOURCC('r','t','p','d')

enum upipe_rtpd_command {
    UPIPE_RTPD_SENTINAL = UPIPE_CONTROL_LOCAL,

    UPIPE_RTPD_GET_PACKETS_LOST, /* int sig, uint64_t * */
};

static inline int upipe_rtpd_get_packets_lost(struct upipe *upipe,
        uint64_t *lost)
{
    return upipe_control(upipe, UPIPE_RTPD_GET_PACKETS_LOST,
            UPIPE_RTPD_SIGNATURE, lost);
}


/** @This returns the management structure for rtpd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtpd_mgr_alloc(void);

UREF_ATTR_UNSIGNED(rtp, timestamp, "timestamp", TIMESTAMP)
UREF_ATTR_UNSIGNED(rtp, seqnum, "seqnum", SEQNUM)

#ifdef __cplusplus
}
#endif
#endif
