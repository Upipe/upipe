/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe source module for queues
 *
 * Note that the allocator requires an additional parameter:
 * @table 2
 * @item queue_length @item maximum length of the queue (<= 255)
 * @end table
 *
 * Also note that this module is exceptional in that upipe_release() may be
 * called from another thread. The release function is thread-safe.
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe-modules/upipe_queue_source.h>

#include "upipe_queue.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** maximum length of out of band queues */
#define OOB_QUEUES 255

/** @internal @This is the private context of a queue source pipe. */
struct upipe_qsrc {
    /** refcount management structure */
    struct urefcount urefcount;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** oob watcher */
    struct upump *upump_oob;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** structure exported to the sinks */
    struct upipe_queue upipe_queue;

    /** extra data for the queue structure */
    uint8_t uqueue_extra[];
};

UPIPE_HELPER_UPIPE(upipe_qsrc, upipe_queue.upipe, UPIPE_QSRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_qsrc, urefcount, upipe_qsrc_no_ref)

UPIPE_HELPER_OUTPUT(upipe_qsrc, output, flow_def, output_state, request_list)

UPIPE_HELPER_UPUMP_MGR(upipe_qsrc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_qsrc, upump, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_qsrc, upump_oob, upump_mgr)

/** @internal @This allocates a queue source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_qsrc_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    if (signature != UPIPE_QSRC_SIGNATURE)
        goto upipe_qsrc_alloc_err;
    unsigned int length = va_arg(args, unsigned int);
    if (!length || length > UINT8_MAX)
        goto upipe_qsrc_alloc_err;

    struct upipe_qsrc *upipe_qsrc = malloc(sizeof(struct upipe_qsrc) +
                                           uqueue_sizeof(length) +
                                           2 * uqueue_sizeof(OOB_QUEUES));
    if (unlikely(upipe_qsrc == NULL))
        goto upipe_qsrc_alloc_err;

    struct upipe *upipe = upipe_qsrc_to_upipe(upipe_qsrc);
    upipe_init(upipe, mgr, uprobe);
    if (unlikely(!uqueue_init(&upipe_queue(upipe)->uqueue, length,
                              upipe_qsrc->uqueue_extra) ||
                 !uqueue_init(&upipe_queue(upipe)->downstream_oob, OOB_QUEUES,
                              upipe_qsrc->uqueue_extra +
                              uqueue_sizeof(length)) ||
                 !uqueue_init(&upipe_queue(upipe)->upstream_oob, OOB_QUEUES,
                              upipe_qsrc->uqueue_extra + uqueue_sizeof(length) +
                              uqueue_sizeof(OOB_QUEUES)))) {
        free(upipe_qsrc);
        goto upipe_qsrc_alloc_err;
    }

    upipe_qsrc_init_urefcount(upipe);
    upipe_qsrc_init_output(upipe);
    upipe_qsrc_init_upump_mgr(upipe);
    upipe_qsrc_init_upump(upipe);
    upipe_qsrc_init_upump_oob(upipe);
    upipe_qsrc->upipe_queue.max_length = length;
    upipe_throw_ready(upipe);

    return upipe;

upipe_qsrc_alloc_err:
    uprobe_release(uprobe);
    return NULL;
}

/** @internal @This takes data as input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_qsrc_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_qsrc_store_flow_def(upipe, uref);
        return;
    }

    upipe_qsrc_output(upipe, uref, upump_p);
}

/** @internal @This reads data from the queue and outputs it.
 *
 * @param upump description structure of the read watcher
 */
static void upipe_qsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    struct uref *uref = uqueue_pop(&upipe_queue(upipe)->uqueue, struct uref *);
    if (likely(uref != NULL))
        upipe_qsrc_input(upipe, uref, &upipe_qsrc->upump);
}

/** @internal @This handles the result of a request.
 *
 * @param urequest request provided
 * @param args optional arguments
 * @return an error code
 */
static int upipe_qsrc_provide_request(struct urequest *urequest, va_list args)
{
    struct upipe_queue_request *request =
        upipe_queue_request_from_urequest(urequest);
    struct upipe *upipe = urequest_get_opaque(urequest, struct upipe *);
    upipe_verbose_va(upipe, "provided request %p", request);

    struct upipe_queue_upstream *upstream =
        upipe_queue_upstream_alloc(UPIPE_QUEUE_UPSTREAM_PROVIDE, request);
    UBASE_ALLOC_RETURN(upstream);
    upstream->type = UPIPE_QUEUE_UPSTREAM_PROVIDE;

    switch (urequest->type) {
        case UREQUEST_UREF_MGR:
            upstream->uref_mgr = va_arg(args, struct uref_mgr *);
            break;
        case UREQUEST_FLOW_FORMAT:
            upstream->uref = va_arg(args, struct uref *);
            break;
        case UREQUEST_UBUF_MGR:
            upstream->ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            upstream->uref = va_arg(args, struct uref *);
            break;
        case UREQUEST_UCLOCK:
            upstream->uclock = va_arg(args, struct uclock *);
            break;
        case UREQUEST_SINK_LATENCY:
            upstream->uint64 = va_arg(args, uint64_t);
            break;
        default:
            upipe_warn_va(upipe, "unknown request type %d", urequest->type);
            break;
    }

    if (unlikely(!uqueue_push(&upipe_queue(upipe)->upstream_oob, upstream))) {
        upipe_warn(upipe, "unable to send upstream message");
        upipe_queue_upstream_free(upstream);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This registers a request from upstream.
 *
 * @param upipe description structure of the pipe
 * @param request request to register
 * @return an error code
 */
static int upipe_qsrc_register_request(struct upipe *upipe,
                                       struct upipe_queue_request *request)
{
    upipe_queue_request_use(request);
    struct urequest *urequest = upipe_queue_request_to_urequest(request);
    upipe_verbose_va(upipe, "registered request %p", request);
    urequest_set_opaque(urequest, upipe);
    urequest->urequest_provide = upipe_qsrc_provide_request;
    return upipe_qsrc_register_output_request(upipe, urequest);
}

/** @internal @This unregisters a request from upstream.
 *
 * @param upipe description structure of the pipe
 * @param uequest request to unregister
 * @return an error code
 */
static int upipe_qsrc_unregister_request(struct upipe *upipe,
                                         struct upipe_queue_request *request)
{
    struct urequest *urequest = upipe_queue_request_to_urequest(request);
    upipe_verbose_va(upipe, "unregistered request %p", request);
    int err = upipe_qsrc_unregister_output_request(upipe, urequest);
    upipe_queue_request_release(request);
    return err;
}

/** @internal @This flushes the queue and emits source end.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static void upipe_qsrc_source_end(struct upipe *upipe)
{
    struct uref *uref;
    while ((uref = uqueue_pop(&upipe_queue(upipe)->uqueue,
                              struct uref *)) != NULL)
        upipe_qsrc_input(upipe, uref, NULL);

    upipe_throw_source_end(upipe);
}

/** @internal @This kills the pipe.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static void upipe_qsrc_ref_end(struct upipe *upipe)
{
    struct uref *uref;
    while ((uref = uqueue_pop(&upipe_queue(upipe)->uqueue,
                              struct uref *)) != NULL)
        upipe_qsrc_input(upipe, uref, NULL);

    upipe_notice_va(upipe, "freeing queue %p", upipe);
    upipe_throw_dead(upipe);

    struct upipe_queue_downstream *downstream;
    while ((downstream = uqueue_pop(&upipe_queue(upipe)->downstream_oob,
                                    struct upipe_queue_downstream *)) != NULL)
        upipe_queue_downstream_free(downstream);

    struct upipe_queue_upstream *upstream;
    while ((upstream = uqueue_pop(&upipe_queue(upipe)->upstream_oob,
                                  struct upipe_queue_upstream *)) != NULL)
        upipe_queue_upstream_free(upstream);

    upipe_qsrc_clean_upump(upipe);
    upipe_qsrc_clean_upump_oob(upipe);
    upipe_qsrc_clean_upump_mgr(upipe);
    upipe_qsrc_clean_output(upipe);

    uqueue_clean(&upipe_queue(upipe)->uqueue);
    uqueue_clean(&upipe_queue(upipe)->downstream_oob);
    uqueue_clean(&upipe_queue(upipe)->upstream_oob);

    upipe_qsrc_clean_urefcount(upipe);
    upipe_clean(upipe);
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    free(upipe_qsrc);
}

/** @internal @This reads out of band packets from the queue and handles them.
 *
 * @param upump description structure of the oob watcher
 */
static void upipe_qsrc_oob(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_queue_downstream *downstream =
        uqueue_pop(&upipe_queue(upipe)->downstream_oob,
                   struct upipe_queue_downstream *);
    if (unlikely(downstream == NULL))
        return;

    switch (downstream->type) {
        case UPIPE_QUEUE_DOWNSTREAM_REGISTER:
            upipe_qsrc_register_request(upipe, downstream->request);
            break;

        case UPIPE_QUEUE_DOWNSTREAM_UNREGISTER:
            upipe_qsrc_unregister_request(upipe, downstream->request);
            break;

        case UPIPE_QUEUE_DOWNSTREAM_SOURCE_END:
            upipe_qsrc_source_end(upipe);
            break;

        case UPIPE_QUEUE_DOWNSTREAM_REF_END:
            upipe_qsrc_ref_end(upipe);
            break;
    }

    upipe_queue_downstream_free(downstream);
}

/** @internal @This returns the maximum length of the queue.
 *
 * @param upipe description structure of the pipe
 * @param length_p filled in with the maximum length of the queue
 * @return an error code
 */
static int _upipe_qsrc_get_max_length(struct upipe *upipe,
                                      unsigned int *length_p)
{
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    assert(length_p != NULL);
    *length_p = upipe_qsrc->upipe_queue.max_length;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current length of the queue. This function,
 * like all control functions, may only be called from the thread which runs the
 * queue source pipe. The length of the queue may change at any time and the
 * value returned may no longer be valid.
 *
 * @param upipe description structure of the pipe
 * @param length_p filled in with the current length of the queue
 * @return an error code
 */
static int _upipe_qsrc_get_length(struct upipe *upipe, unsigned int *length_p)
{
    assert(length_p != NULL);
    *length_p = uqueue_length(&upipe_queue(upipe)->uqueue);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a queue source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_qsrc_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_qsrc_set_upump(upipe, NULL);
            return upipe_qsrc_attach_upump_mgr(upipe);
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_qsrc_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **output_p = va_arg(args, struct upipe **);
            return upipe_qsrc_get_output(upipe, output_p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_qsrc_set_output(upipe, output);
        }

        case UPIPE_QSRC_GET_MAX_LENGTH: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_QSRC_SIGNATURE)
            unsigned int *length_p = va_arg(args, unsigned int *);
            return _upipe_qsrc_get_max_length(upipe, length_p);
        }
        case UPIPE_QSRC_GET_LENGTH: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_QSRC_SIGNATURE)
            unsigned int *length_p = va_arg(args, unsigned int *);
            return _upipe_qsrc_get_length(upipe, length_p);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a queue source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_qsrc_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_qsrc_control(upipe, command, args));
    upipe_qsrc_check_upump_mgr(upipe);

    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    if (upipe_qsrc->upump_mgr != NULL && upipe_qsrc->upipe_queue.max_length &&
        upipe_qsrc->upump == NULL) {
        struct upump *upump =
            uqueue_upump_alloc_pop(&upipe_queue(upipe)->uqueue,
                                   upipe_qsrc->upump_mgr,
                                   upipe_qsrc_worker, upipe, upipe->refcount);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        } 
        upipe_qsrc_set_upump(upipe, upump);
        upump_start(upump);
        upump = uqueue_upump_alloc_pop(&upipe_queue(upipe)->downstream_oob,
                                       upipe_qsrc->upump_mgr,
                                       upipe_qsrc_oob, upipe, upipe->refcount);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            upipe_qsrc_set_upump(upipe, NULL);
            return UBASE_ERR_UPUMP;
        } 
        upipe_qsrc_set_upump_oob(upipe, upump);
        upump_start(upump);
    }

    return UBASE_ERR_NONE;
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * This may be called from any thread so we must be cautious.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qsrc_no_ref(struct upipe *upipe)
{
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    if (upipe_qsrc->upump_oob == NULL) {
        upipe_qsrc_ref_end(upipe);
        return;
    }

    struct upipe_queue_downstream *downstream =
        upipe_queue_downstream_alloc(UPIPE_QUEUE_DOWNSTREAM_REF_END, NULL);

    if (unlikely(downstream == NULL ||
                 !uqueue_push(&upipe_queue(upipe)->downstream_oob,
                              downstream))) {
        upipe_warn(upipe, "unable to send downstream message");
        upipe_queue_downstream_free(downstream);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_qsrc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_QSRC_SIGNATURE,

    .upipe_alloc = _upipe_qsrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_qsrc_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all queue source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_qsrc_mgr_alloc(void)
{
    return &upipe_qsrc_mgr;
}
