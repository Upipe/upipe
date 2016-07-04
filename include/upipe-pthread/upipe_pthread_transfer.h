/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
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
 * @short Upipe module allowing to transfer other pipes to a new POSIX thread
 * This is particularly helpful for multithreaded applications.
 */

#ifndef _UPIPE_MODULES_UPIPE_PTHREAD_TRANSFER_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_PTHREAD_TRANSFER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#include <pthread.h>

/** @This represents the application call-back that is supposed to create the
 * event loop in the new thread. */
typedef struct upump_mgr *
    (*upipe_pthread_upump_mgr_alloc)(void *);

/** @This represents the application call-back that is supposed to run the
 * event loop in the new thread. */
typedef void (*upipe_pthread_upump_mgr_work)(struct upump_mgr *);

/** @This represents the application call-back that is supposed to free the
 * event loop in the new thread. */
typedef void (*upipe_pthread_upump_mgr_free)(struct upump_mgr *);

/** @This returns a management structure for transfer pipes, using a new
 * pthread. You would need one management structure per target thread.
 *
 * @param queue_length maximum length of the internal queue of commands
 * @param msg_pool_depth maximum number of messages in the pool
 * @param uprobe_pthread_upump_mgr pointer to optional probe, that will be set
 * with the created upump_mgr
 * @param upump_mgr_alloc callback creating the event loop in the new thread
 * @param upump_mgr_work callback running the event loop in the new thread
 * @param upump_mgr_free callback freeing the event loop in the new thread
 * @param upump_mgr_opaque opaque for upump_mgr_alloc
 * @param pthread_id_p reference to created thread ID (may be NULL)
 * @param attr pthread attributes
 * @return pointer to xfer manager
 */
struct upipe_mgr *upipe_pthread_xfer_mgr_alloc(uint8_t queue_length,
        uint16_t msg_pool_depth, struct uprobe *uprobe_pthread_upump_mgr,
        upipe_pthread_upump_mgr_alloc upump_mgr_alloc,
        upipe_pthread_upump_mgr_work upump_mgr_work,
        upipe_pthread_upump_mgr_free upump_mgr_free, void *opaque,
        pthread_t *pthread_id_p, const pthread_attr_t *restrict attr);

#ifdef __cplusplus
}
#endif
#endif
