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
 * @short Upipe module sending retransmit requests for lost RTP packets
 */

#ifndef _UPIPE_MODULES_UPIPE_RTP_FEEDBACK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_RTP_FEEDBACK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_RTPFB_SIGNATURE UBASE_FOURCC('r','t','p','f')
#define UPIPE_RTPFB_OUTPUT_SIGNATURE UBASE_FOURCC('r','t','f','b')


enum upipe_rtpd_command {
    UPIPE_RTPFB_SENTINAL = UPIPE_CONTROL_LOCAL,

    UPIPE_RTPFB_GET_STATS, /* int sig, unsigned *, unsigned *, size_t *, size_t *, size_t *, size_t *, size_t * */
};

static inline int upipe_rtpfb_get_stats(struct upipe *upipe,
		unsigned *expected_seqnum, unsigned *last_output_seqnum,
		size_t *buffered, size_t *nacks, size_t *repaired,
		size_t *lost, size_t *duplicates)
{
    return upipe_control(upipe, UPIPE_RTPFB_GET_STATS,
            UPIPE_RTPFB_SIGNATURE, expected_seqnum, last_output_seqnum,
			buffered, nacks, repaired, lost, duplicates);
}

/** @This returns the management structure for rtpfb pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtpfb_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
