/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Bin pipe wrapping a queue, a linear subpipeline and a queue
 *
 * It allows to transfer an existing linear subpipeline (the given pipe, its
 * output, the output of its output, etc.) to a remote upump_mgr,
 * while setting up a queue to send the packets to the linear subpipeline,
 * and a queue to retrieve the processed packets in the main upump_mgr.
 *
 * Please note that the remote subpipeline is not "used" so its refcount is not
 * incremented. For that reason it shouldn't be "released" afterwards. Only
 * release the worker pipe.
 *
 * Note that the allocator requires four additional parameters:
 * @table 2
 * @item upipe_remote @item subpipeline to transfer to remote upump_mgr
 * (belongs to the callee)
 * @item uprobe_remote @item probe hierarchy to use on the remote thread
 * (belongs to the callee)
 * @item input_queue_length @item number of packets in the queue between main
 * and remote thread
 * @item output_queue_length @item number of packets in the queue between remote
 * and main thread
 * @end table
 */

#ifndef _UPIPE_MODULES_UPIPE_WORKER_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_WORKER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_WORK_SIGNATURE UBASE_FOURCC('w','o','r','k')

/** @This returns the management structure for all worker pipes.
 *
 * @param xfer_mgr manager to transfer pipes to the remote thread
 * @return pointer to manager
 */
struct upipe_mgr *upipe_work_mgr_alloc(struct upipe_mgr *xfer_mgr);

/** @This extends upipe_mgr_command with specific commands for worker. */
enum upipe_work_mgr_command {
    UPIPE_WORK_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

/** @hidden */
#define UPIPE_WORK_MGR_GET_SET_MGR(name, NAME)                              \
    /** returns the current manager for name inner pipes                    \
     * (struct upipe_mgr **) */                                             \
    UPIPE_WORK_MGR_GET_##NAME##_MGR,                                        \
    /** sets the manager for name inner pipes (struct upipe_mgr *) */       \
    UPIPE_WORK_MGR_SET_##NAME##_MGR,

    UPIPE_WORK_MGR_GET_SET_MGR(qsrc, QSRC)
    UPIPE_WORK_MGR_GET_SET_MGR(qsink, QSINK)
    UPIPE_WORK_MGR_GET_SET_MGR(xfer, XFER)
#undef UPIPE_WORK_MGR_GET_SET_MGR
};

/** @hidden */
#define UPIPE_WORK_MGR_GET_SET_MGR2(name, NAME)                             \
/** @This returns the current manager for name inner pipes.                 \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param p filled in with the name manager                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_work_mgr_get_##name##_mgr(struct upipe_mgr *mgr,                  \
                                    struct upipe_mgr **p)                   \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_WORK_MGR_GET_##NAME##_MGR,          \
                             UPIPE_WORK_SIGNATURE, p);                      \
}                                                                           \
/** @This sets the manager for name inner pipes. This may only be called    \
 * before any pipe has been allocated.                                      \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param m pointer to name manager                                         \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_work_mgr_set_##name##_mgr(struct upipe_mgr *mgr,                  \
                                    struct upipe_mgr *m)                    \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_WORK_MGR_SET_##NAME##_MGR,          \
                             UPIPE_WORK_SIGNATURE, m);                      \
}

UPIPE_WORK_MGR_GET_SET_MGR2(qsrc, QSRC)
UPIPE_WORK_MGR_GET_SET_MGR2(qsink, QSINK)
UPIPE_WORK_MGR_GET_SET_MGR2(xfer, XFER)
#undef UPIPE_WORK_MGR_GET_SET_MGR2

/** @hidden */
#define ARGS_DECL , struct upipe *upipe_remote, struct uprobe *uprobe_remote, unsigned int input_queue_length, unsigned int output_queue_length
/** @hidden */
#define ARGS , upipe_remote, uprobe_remote, input_queue_length, output_queue_length
UPIPE_HELPER_ALLOC(work, UPIPE_WORK_SIGNATURE)
#undef ARGS
#undef ARGS_DECL

#ifdef __cplusplus
}
#endif
#endif
