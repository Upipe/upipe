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
#include <upipe/urefcount.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uclock.h>
#include <upipe-modules/upipe_queue_source.h>

#include "upipe_queue.h"

#include <stdlib.h>

/** @hidden */
struct urequest;

/** @internal @This frees a request.
 *
 * @param request request to free
 */
static void upipe_queue_request_free(struct urefcount *urefcount)
{
    struct upipe_queue_request *request =
        upipe_queue_request_from_urefcount(urefcount);
    urequest_clean(upipe_queue_request_to_urequest(request));
    urefcount_clean(&request->urefcount);
    free(request);
}

/** @internal @This allocates a request.
 *
 * @param upstream upstream request
 * @return allocated request, or NULL in case of error
 */
struct upipe_queue_request *upipe_queue_request_alloc(struct urequest *upstream)
{
    struct upipe_queue_request *request =
        malloc(sizeof(struct upipe_queue_request));
    if (request == NULL)
        return NULL;

    struct urequest *urequest = upipe_queue_request_to_urequest(request);
    struct uref *uref = NULL;
    if (upstream->uref != NULL &&
        (uref = uref_dup(upstream->uref)) == NULL) {
        free(request);
        return NULL;
    }

    urefcount_init(upipe_queue_request_to_urefcount(request),
                   upipe_queue_request_free);
    uchain_init(&request->uchain_sink);
    request->upstream = upstream;

    urequest_init(urequest, upstream->type, uref, NULL, NULL);
    return request;
}

/** @internal @This allocates a downstream message.
 *
 * @param type message type
 * @param request optional request
 * @return allocated message, or NULL in case of error
 */
struct upipe_queue_downstream *
    upipe_queue_downstream_alloc(enum upipe_queue_downstream_type type,
                                 struct upipe_queue_request *request)
{
    struct upipe_queue_downstream *downstream =
        malloc(sizeof(struct upipe_queue_downstream));
    if (downstream == NULL)
        return NULL;
    downstream->request = upipe_queue_request_use(request);
    downstream->type = type;
    return downstream;
}

/** @internal @This frees a downstream message.
 *
 * @param downstream downstream message to free
 */
void upipe_queue_downstream_free(struct upipe_queue_downstream *downstream)
{
    upipe_queue_request_release(downstream->request);
    free(downstream);
}

/** @internal @This allocates an upstream message.
 *
 * @param type message type
 * @param request optional request
 * @return allocated message, or NULL in case of error
 */
struct upipe_queue_upstream *
    upipe_queue_upstream_alloc(enum upipe_queue_upstream_type type,
                               struct upipe_queue_request *request)
{
    struct upipe_queue_upstream *upstream =
        malloc(sizeof(struct upipe_queue_upstream));
    if (upstream == NULL)
        return NULL;
    upstream->request = upipe_queue_request_use(request);
    upstream->type = type;
    upstream->uref = NULL;
    upstream->uref_mgr = NULL;
    upstream->ubuf_mgr = NULL;
    upstream->uclock = NULL;
    upstream->uint64 = 0;
    return upstream;
}

/** @internal @This frees an upstream message.
 *
 * @param upstream upstream message to free
 */
void upipe_queue_upstream_free(struct upipe_queue_upstream *upstream)
{
    uref_free(upstream->uref);
    uref_mgr_release(upstream->uref_mgr);
    ubuf_mgr_release(upstream->ubuf_mgr);
    uclock_release(upstream->uclock);
    upipe_queue_request_release(upstream->request);
    free(upstream);
}
