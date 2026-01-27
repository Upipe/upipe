/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Bin pipe transforming the input to the given format
 */

#ifndef _UPIPE_FILTERS_UPIPE_FILTER_FORMAT_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_FILTER_FORMAT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/uref_attr.h"

#define UPIPE_FFMT_SIGNATURE UBASE_FOURCC('f','f','m','t')

/** @This returns the management structure for all ffmt pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ffmt_mgr_alloc(void);

/** @This extends upipe_mgr_command with specific commands for ffmt. */
enum upipe_ffmt_mgr_command {
    UPIPE_FFMT_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

/** @hidden */
#define UPIPE_FFMT_MGR_GET_SET_MGR(name, NAME)                              \
    /** returns the current manager for name inner pipes                    \
     * (struct upipe_mgr **) */                                             \
    UPIPE_FFMT_MGR_GET_##NAME##_MGR,                                        \
    /** sets the manager for name inner pipes (struct upipe_mgr *) */       \
    UPIPE_FFMT_MGR_SET_##NAME##_MGR,

    UPIPE_FFMT_MGR_GET_SET_MGR(sws, SWS)
    UPIPE_FFMT_MGR_GET_SET_MGR(swr, SWR)
    UPIPE_FFMT_MGR_GET_SET_MGR(deint, DEINT)
    UPIPE_FFMT_MGR_GET_SET_MGR(avfilter, AVFILTER)
#undef UPIPE_FFMT_MGR_GET_SET_MGR
};

/** @hidden */
#define UPIPE_FFMT_MGR_GET_SET_MGR2(name, NAME)                             \
/** @This returns the current manager for name inner pipes.                 \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param p filled in with the name manager                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_ffmt_mgr_get_##name##_mgr(struct upipe_mgr *mgr,                  \
                                      struct upipe_mgr *p)                  \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_FFMT_MGR_GET_##NAME##_MGR,          \
                             UPIPE_FFMT_SIGNATURE, p);                      \
}                                                                           \
/** @This sets the manager for name inner pipes. This may only be called    \
 * before any pipe has been allocated.                                      \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param m pointer to name manager                                         \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_ffmt_mgr_set_##name##_mgr(struct upipe_mgr *mgr,                  \
                                      struct upipe_mgr *m)                  \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_FFMT_MGR_SET_##NAME##_MGR,          \
                             UPIPE_FFMT_SIGNATURE, m);                      \
}

UPIPE_FFMT_MGR_GET_SET_MGR2(sws, SWS)
UPIPE_FFMT_MGR_GET_SET_MGR2(swr, SWR)
UPIPE_FFMT_MGR_GET_SET_MGR2(deint, DEINT)
UPIPE_FFMT_MGR_GET_SET_MGR2(avfilter, AVFILTER)
#undef UPIPE_FFMT_MGR_GET_SET_MGR2

#ifdef __cplusplus
}
#endif
#endif
