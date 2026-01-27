/*
 * Copyright (C) 2014-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Bin pipe wrapping a source subpipeline and a queue
 *
 * It allows to transfer an existing source subpipeline (the given pipe, its
 * output, the output of its output, etc.) to a remote upump_mgr,
 * while setting up a queue to retrieve the packets in the main upump_mgr.
 *
 * Please note that the remote subpipeline is not "used" so its refcount is not
 * incremented. For that reason it shouldn't be "released" afterwards. Only
 * release the wsrc pipe.
 *
 * Note that the allocator requires three additional parameters:
 * @table 2
 * @item upipe_remote @item subpipeline to transfer to remote upump_mgr
 * (belongs to the callee)
 * @item uprobe_remote @item probe hierarchy to use on the remote thread
 * (belongs to the callee)
 * @item output_queue_length @item number of packets in the queue between remote
 * and main thread
 * @end table
 */

#ifndef _UPIPE_MODULES_UPIPE_WORKER_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_WORKER_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-modules/upipe_worker.h"

#define UPIPE_WSRC_SIGNATURE UBASE_FOURCC('w','s','r','c')

/** @This returns the management structure for all wsrc pipes.
 *
 * @param xfer_mgr manager to transfer pipes to the remote thread
 * @return pointer to manager
 */
static inline struct upipe_mgr *
upipe_wsrc_mgr_alloc(struct upipe_mgr *xfer_mgr)
{
    return upipe_work_mgr_alloc(xfer_mgr);
}

/** @hidden */
#define UPIPE_WSRC_MGR_GET_SET_MGR(name)                                    \
/** @This returns the current manager for name inner pipes.                 \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param p filled in with the name manager                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_wsrc_mgr_get_##name##_mgr(struct upipe_mgr *mgr,                  \
                                    struct upipe_mgr **p)                   \
{                                                                           \
    return upipe_work_mgr_get_##name##_mgr(mgr, p);                         \
}                                                                           \
/** @This sets the manager for name inner pipes. This may only be called    \
 * before any pipe has been allocated.                                      \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param m pointer to name manager                                         \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_wsrc_mgr_set_##name##_mgr(struct upipe_mgr *mgr,                  \
                                    struct upipe_mgr *m)                    \
{                                                                           \
    return upipe_work_mgr_set_##name##_mgr(mgr, m);                         \
}

UPIPE_WSRC_MGR_GET_SET_MGR(qsrc)
UPIPE_WSRC_MGR_GET_SET_MGR(qsink)
UPIPE_WSRC_MGR_GET_SET_MGR(xfer)
#undef UPIPE_WSRC_MGR_GET_SET_MGR

/** @hidden */
#define ARGS_DECL , struct upipe *upipe_remote, struct uprobe *uprobe_remote, unsigned int output_queue_length
/** @hidden */
#define ARGS , upipe_remote, uprobe_remote, output_queue_length
UPIPE_HELPER_ALLOC(wsrc, UPIPE_WSRC_SIGNATURE)
#undef ARGS
#undef ARGS_DECL

#ifdef __cplusplus
}
#endif
#endif
