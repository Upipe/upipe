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
 * @short Bin pipe decoding a flow
 */

#ifndef _UPIPE_FILTERS_UPIPE_FILTER_DECODE_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_FILTER_DECODE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/uref_attr.h"

#define UPIPE_FDEC_SIGNATURE UBASE_FOURCC('f','d','e','c')

/** @This enumerates the filter decode private control commands. */
enum upipe_fdec_command {
    /** sentinel */
    UPIPE_FDEC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set watchdog timeout (uint64_t) */
    UPIPE_FDEC_SET_TIMEOUT,
    /** get watchdog timeout (uint64_t *) */
    UPIPE_FDEC_GET_TIMEOUT,
};

/** @This sets the watchdog timeout.
 *
 * @param upipe description structure of the pipe
 * @param timeout watchdog timeout in 27MHz ticks, UINT64_MAX to disable
 * @return an error code
 */
static inline int upipe_fdec_set_timeout(struct upipe *upipe, uint64_t timeout)
{
    return upipe_control(upipe, UPIPE_FDEC_SET_TIMEOUT, UPIPE_FDEC_SIGNATURE,
                         timeout);
}

/** @This gets the configured watchdog timeout.
 *
 * @param upipe description structure of the pipe
 * @param timeout filled with the configured timeout value in 27MHz ticks,
 * UINT64_MAX means disabled
 * @return an error code
 */
static inline int upipe_fdec_get_timeout(struct upipe *upipe, uint64_t *timeout)
{
    return upipe_control(upipe, UPIPE_FDEC_GET_TIMEOUT, UPIPE_FDEC_SIGNATURE,
                         timeout);
}

/** @This returns the management structure for all fdec pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_fdec_mgr_alloc(void);

/** @This extends upipe_mgr_command with specific commands for fdec. */
enum upipe_fdec_mgr_command {
    UPIPE_FDEC_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

/** @hidden */
#define UPIPE_FDEC_MGR_GET_SET_MGR(name, NAME)                              \
    /** returns the current manager for name inner pipes                    \
     * (struct upipe_mgr **) */                                             \
    UPIPE_FDEC_MGR_GET_##NAME##_MGR,                                        \
    /** sets the manager for name inner pipes (struct upipe_mgr *) */       \
    UPIPE_FDEC_MGR_SET_##NAME##_MGR,

    UPIPE_FDEC_MGR_GET_SET_MGR(avcdec, AVCDEC)
#undef UPIPE_FDEC_MGR_GET_SET_MGR
};

/** @hidden */
#define UPIPE_FDEC_MGR_GET_SET_MGR2(name, NAME)                             \
/** @This returns the current manager for name inner pipes.                 \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param p filled in with the name manager                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_fdec_mgr_get_##name##_mgr(struct upipe_mgr *mgr,                  \
                                      struct upipe_mgr *p)                  \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_FDEC_MGR_GET_##NAME##_MGR,          \
                             UPIPE_FDEC_SIGNATURE, p);                      \
}                                                                           \
/** @This sets the manager for name inner pipes. This may only be called    \
 * before any pipe has been allocated.                                      \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param m pointer to name manager                                         \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_fdec_mgr_set_##name##_mgr(struct upipe_mgr *mgr,                  \
                                      struct upipe_mgr *m)                  \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_FDEC_MGR_SET_##NAME##_MGR,          \
                             UPIPE_FDEC_SIGNATURE, m);                      \
}

UPIPE_FDEC_MGR_GET_SET_MGR2(avcdec, AVCDEC)
#undef UPIPE_FDEC_MGR_GET_SET_MGR2

#ifdef __cplusplus
}
#endif
#endif
