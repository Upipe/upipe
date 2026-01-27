/*
 * Copyright (C) 2016-2017 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
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

#include "upipe/upipe.h"

#define UPIPE_RTCPFB_SIGNATURE UBASE_FOURCC('r','t','c','f')
#define UPIPE_RTCPFB_INPUT_SIGNATURE UBASE_FOURCC('r','t','c','i')

enum upipe_rtcpfb_command {
    UPIPE_RTCPFB_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** get counters (uint64_t *) */
    UPIPE_RTCPFB_GET_STATS,
};

static inline int upipe_rtcpfb_get_stats(struct upipe *upipe,
                                         uint64_t *retrans)
{
    return upipe_control(upipe, UPIPE_RTCPFB_GET_STATS,
                         UPIPE_RTCPFB_SIGNATURE, retrans);
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
