/*
 * Copyright (C) 2014-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Bin pipe decapsulating RTP packets from a UDP source
 */

#ifndef _UPIPE_MODULES_UPIPE_RTP_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_RTP_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_RTPSRC_SIGNATURE UBASE_FOURCC('r','t','p','s')

/** @This returns the management structure for all rtpsrc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtpsrc_mgr_alloc(void);

/** @This extends upipe_mgr_command with specific commands for rtpsrc. */
enum upipe_rtpsrc_mgr_command {
    UPIPE_RTPSRC_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

/** @hidden */
#define UPIPE_RTPSRC_MGR_GET_SET_MGR(name, NAME)                            \
    /** returns the current manager for name inner pipes                    \
     * (struct upipe_mgr **) */                                             \
    UPIPE_RTPSRC_MGR_GET_##NAME##_MGR,                                      \
    /** sets the manager for name inner pipes (struct upipe_mgr *) */       \
    UPIPE_RTPSRC_MGR_SET_##NAME##_MGR,

    UPIPE_RTPSRC_MGR_GET_SET_MGR(udpsrc, UDPSRC)
    UPIPE_RTPSRC_MGR_GET_SET_MGR(rtpd, RTPD)
    UPIPE_RTPSRC_MGR_GET_SET_MGR(setflowdef, SETFLOWDEF)
#undef UPIPE_RTPSRC_MGR_GET_SET_MGR
};

/** @hidden */
#define UPIPE_RTPSRC_MGR_GET_SET_MGR2(name, NAME)                           \
/** @This returns the current manager for name inner pipes.                 \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param p filled in with the name manager                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_rtpsrc_mgr_get_##name##_mgr(struct upipe_mgr *mgr,                \
                                      struct upipe_mgr *p)                  \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_RTPSRC_MGR_GET_##NAME##_MGR,        \
                             UPIPE_RTPSRC_SIGNATURE, p);                    \
}                                                                           \
/** @This sets the manager for name inner pipes. This may only be called    \
 * before any pipe has been allocated.                                      \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param m pointer to name manager                                         \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_rtpsrc_mgr_set_##name##_mgr(struct upipe_mgr *mgr,                \
                                      struct upipe_mgr *m)                  \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_RTPSRC_MGR_SET_##NAME##_MGR,        \
                             UPIPE_RTPSRC_SIGNATURE, m);                    \
}

UPIPE_RTPSRC_MGR_GET_SET_MGR2(udpsrc, UDPSRC)
UPIPE_RTPSRC_MGR_GET_SET_MGR2(rtpd, RTPD)
UPIPE_RTPSRC_MGR_GET_SET_MGR2(setflowdef, SETFLOWDEF)
#undef UPIPE_RTPSRC_MGR_GET_SET_MGR2

#ifdef __cplusplus
}
#endif
#endif
