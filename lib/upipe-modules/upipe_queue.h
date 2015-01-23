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
 * @short common functions for queues
 */

#include <upipe/ubase.h>
#include <upipe/uqueue.h>
#include <upipe/upipe.h>

#include <assert.h>

/** @internal @This is the structure exported from source to sinks. */
struct upipe_queue {
    /** max length of the queue */
    unsigned int max_length;
    /** uref queue */
    struct uqueue uqueue;
    /** out of band downstream queue */
    struct uqueue downstream_oob;
    /** out of band upstream queue */
    struct uqueue upstream_oob;

    /** public upipe structure */
    struct upipe upipe;
};

/** @internal @This returns a pointer the upipe_queue structure.
 *
 * @param upipe pointer to upipe structure of type queue source
 * @return pointer to upipe_queue
 */
static inline struct upipe_queue *upipe_queue(struct upipe *upipe)
{
    assert(upipe->mgr->signature == UPIPE_QSRC_SIGNATURE);
    return container_of(upipe, struct upipe_queue, upipe);
}

/** @internal @This is a super-set of @ref urequest. */
struct upipe_queue_request {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked list, for use by the sink only */
    struct uchain uchain_sink;
    /** pointer to upstream request */
    struct urequest *upstream;

    /** urequest */
    struct urequest urequest;
};

UBASE_FROM_TO(upipe_queue_request, urequest, urequest, urequest)
UBASE_FROM_TO(upipe_queue_request, urefcount, urefcount, urefcount)
UBASE_FROM_TO(upipe_queue_request, uchain, uchain_sink, uchain_sink)

/** @internal @This allocates a request.
 *
 * @param upstream upstream request
 * @return allocated request, or NULL in case of error
 */
struct upipe_queue_request *upipe_queue_request_alloc(struct urequest *upstream);

/** @This increments the reference count of a upipe_queue_request.
 *
 * @param upipe_queue_request pointer to upipe_queue_request
 * @return same pointer to upipe_queue_request
 */
static inline struct upipe_queue_request *
    upipe_queue_request_use(struct upipe_queue_request *upipe_queue_request)
{
    if (upipe_queue_request == NULL)
        return NULL;
    urefcount_use(&upipe_queue_request->urefcount);
    return upipe_queue_request;
}

/** @This decrements the reference count of a upipe_queue_request or frees it.
 *
 * @param upipe_queue_request pointer to upipe_queue_request
 */
static inline void
    upipe_queue_request_release(struct upipe_queue_request *upipe_queue_request)
{
    if (upipe_queue_request != NULL)
        urefcount_release(&upipe_queue_request->urefcount);
}

/** @internal @This represents of type of downstream message */
enum upipe_queue_downstream_type {
    /** register a request */
    UPIPE_QUEUE_DOWNSTREAM_REGISTER,
    /** unregister a request */
    UPIPE_QUEUE_DOWNSTREAM_UNREGISTER,
    /** end of source */
    UPIPE_QUEUE_DOWNSTREAM_SOURCE_END,
    /** no references anymore */
    UPIPE_QUEUE_DOWNSTREAM_REF_END
};

/** @internal @This carries downstream out of band messages */
struct upipe_queue_downstream {
    /** type of downstream message */
    enum upipe_queue_downstream_type type;
    /** optional request */
    struct upipe_queue_request *request;
};

/** @internal @This allocates a downstream message.
 *
 * @param type message type
 * @param request optional request
 * @return allocated message, or NULL in case of error
 */
struct upipe_queue_downstream *
    upipe_queue_downstream_alloc(enum upipe_queue_downstream_type type,
                                 struct upipe_queue_request *request);

/** @internal @This frees a downstream message.
 *
 * @param downstream downstream message to free
 */
void upipe_queue_downstream_free(struct upipe_queue_downstream *downstream);

/** @internal @This represents of type of downstream message */
enum upipe_queue_upstream_type {
    /** provide a request */
    UPIPE_QUEUE_UPSTREAM_PROVIDE
};

/** @internal @This carries upstream out of band messages */
struct upipe_queue_upstream {
    /** type of upstream message */
    enum upipe_queue_upstream_type type;
    /** optional request */
    struct upipe_queue_request *request;
    /** optional uref */
    struct uref *uref;
    /** optional uref_mgr */
    struct uref_mgr *uref_mgr;
    /** optional ubuf_mgr */
    struct ubuf_mgr *ubuf_mgr;
    /** optional uclock */
    struct uclock *uclock;
    /** optional uint64_t */
    uint64_t uint64;
};

/** @internal @This allocates an upstream message.
 *
 * @param type message type
 * @param request optional request
 * @return allocated message, or NULL in case of error
 */
struct upipe_queue_upstream *
    upipe_queue_upstream_alloc(enum upipe_queue_upstream_type type,
                               struct upipe_queue_request *request);

/** @internal @This frees an upstream message.
 *
 * @param upstream upstream message to free
 */
void upipe_queue_upstream_free(struct upipe_queue_upstream *upstream);
