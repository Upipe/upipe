/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ueventfd.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe-modules/upipe_transfer.h>
#include <upipe-pthread/upipe_pthread_transfer.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <assert.h>

/** @internal @This is the private context for pthread. */
struct upipe_pthread_ctx {
    /** xfer manager */
    struct upipe_mgr *xfer_mgr;
    /** pointer to upump_mgr probe */
    struct uprobe *uprobe_pthread_upump_mgr;
    /** callback creating the event loop in the new thread */
    upipe_pthread_upump_mgr_alloc upump_mgr_alloc;
    /** callback running the event loop in the new thread */
    upipe_pthread_upump_mgr_work upump_mgr_work;
    /** callback freeing the event loop in the new thread */
    upipe_pthread_upump_mgr_free upump_mgr_free;
    /** thread ID */
    pthread_t pthread_id;
    /** eventfd used for thread termination */
    struct ueventfd event;
};

/** @internal @This is the main function of the new thread.
 *
 * @param mgr pointer to a upipe pthread manager
 */
static void *upipe_pthread_start(void *_pthread_ctx)
{
    struct upipe_pthread_ctx *pthread_ctx =
        (struct upipe_pthread_ctx *)_pthread_ctx;
    struct upump_mgr *upump_mgr = pthread_ctx->upump_mgr_alloc();
    if (unlikely(upump_mgr == NULL)) {
        uprobe_err(pthread_ctx->uprobe_pthread_upump_mgr, NULL,
                   "unable to create upump_mgr");
        uprobe_release(pthread_ctx->uprobe_pthread_upump_mgr);
        upipe_mgr_release(pthread_ctx->xfer_mgr);
        free(pthread_ctx);
        return NULL;
    }

    int err =
        uprobe_pthread_upump_mgr_set(pthread_ctx->uprobe_pthread_upump_mgr,
                                     upump_mgr);
    if (unlikely(!ubase_check(err)))
        uprobe_err_va(pthread_ctx->uprobe_pthread_upump_mgr, NULL,
                      "unable to attach upump_mgr (%d)", err);

    err = upipe_xfer_mgr_attach(pthread_ctx->xfer_mgr, upump_mgr);
    if (unlikely(!ubase_check(err)))
        uprobe_err_va(pthread_ctx->uprobe_pthread_upump_mgr, NULL,
                      "unable to attach xfer (%d)", err);

    uprobe_release(pthread_ctx->uprobe_pthread_upump_mgr);
    upipe_mgr_release(pthread_ctx->xfer_mgr);

    pthread_ctx->upump_mgr_work(upump_mgr);
    upump_mgr_release(upump_mgr);
    pthread_ctx->upump_mgr_free(upump_mgr);

    ueventfd_write(&pthread_ctx->event);
    return NULL;
}

/** @internal @This is called on the main thread when the thread dies.
 *
 * @param upump pointer to pump watching the eventfd
 */
static void upipe_pthread_stop(struct upump *upump)
{
    struct upipe_pthread_ctx *pthread_ctx =
        upump_get_opaque(upump, struct upipe_pthread_ctx *);
    upump_stop(upump);
    upump_free(upump);

    pthread_join(pthread_ctx->pthread_id, NULL);
    ueventfd_clean(&pthread_ctx->event);
    free(pthread_ctx);
}

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
 * @param pthread_id_p reference to created thread ID (may be NULL)
 * @param attr pthread attributes
 * @return pointer to xfer manager
 */
struct upipe_mgr *upipe_pthread_xfer_mgr_alloc(uint8_t queue_length,
        uint16_t msg_pool_depth, struct uprobe *uprobe_pthread_upump_mgr,
        upipe_pthread_upump_mgr_alloc upump_mgr_alloc,
        upipe_pthread_upump_mgr_work upump_mgr_work,
        upipe_pthread_upump_mgr_free upump_mgr_free,
        pthread_t *pthread_id_p, const pthread_attr_t *restrict attr)
{
    struct upipe_pthread_ctx *pthread_ctx =
        malloc(sizeof(struct upipe_pthread_ctx));
    if (unlikely(pthread_ctx == NULL))
        goto upipe_pthread_xfer_mgr_alloc_err1;

    if (unlikely(!ueventfd_init(&pthread_ctx->event, false)))
        goto upipe_pthread_xfer_mgr_alloc_err2;

    struct upump_mgr *upump_mgr = NULL;
    uprobe_throw(uprobe_pthread_upump_mgr, NULL, UPROBE_NEED_UPUMP_MGR, &upump_mgr);
    if (unlikely(upump_mgr == NULL))
        goto upipe_pthread_xfer_mgr_alloc_err3;

    struct upump *upump = ueventfd_upump_alloc(&pthread_ctx->event, upump_mgr,
                                               upipe_pthread_stop, pthread_ctx,
                                               NULL);
    upump_mgr_release(upump_mgr);
    if (unlikely(upump == NULL))
        goto upipe_pthread_xfer_mgr_alloc_err3;

    struct upipe_mgr *xfer_mgr = upipe_xfer_mgr_alloc(queue_length, msg_pool_depth);
    if (unlikely(xfer_mgr == NULL))
        goto upipe_pthread_xfer_mgr_alloc_err4;

    pthread_ctx->xfer_mgr = upipe_mgr_use(xfer_mgr);
    pthread_ctx->uprobe_pthread_upump_mgr = uprobe_pthread_upump_mgr;
    pthread_ctx->upump_mgr_alloc = upump_mgr_alloc;
    pthread_ctx->upump_mgr_work = upump_mgr_work;
    pthread_ctx->upump_mgr_free = upump_mgr_free;

    if (unlikely(pthread_create(&pthread_ctx->pthread_id, attr,
                                upipe_pthread_start, pthread_ctx) != 0))
        goto upipe_pthread_xfer_mgr_alloc_err5;
    if (pthread_id_p != NULL)
        *pthread_id_p = pthread_ctx->pthread_id;

    upump_start(upump);
    return xfer_mgr;

upipe_pthread_xfer_mgr_alloc_err5:
    upipe_mgr_release(pthread_ctx->xfer_mgr);
    upipe_mgr_release(xfer_mgr);
upipe_pthread_xfer_mgr_alloc_err4:
    upump_free(upump);
upipe_pthread_xfer_mgr_alloc_err3:
    ueventfd_clean(&pthread_ctx->event);
upipe_pthread_xfer_mgr_alloc_err2:
    free(pthread_ctx);
upipe_pthread_xfer_mgr_alloc_err1:
    uprobe_release(uprobe_pthread_upump_mgr);
    return NULL;
}
