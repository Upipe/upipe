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
 * @short Upipe sink module for queues
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uqueue.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upump.h>
#include <upipe/upump_blocker.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_queue_source.h>

#include "upipe_queue.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @hidden */
static void upipe_qsink_watcher(struct upump *upump);
/** @hidden */
static bool upipe_qsink_output(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p);
/** @hidden */
static void upipe_qsink_oob(struct upump *upump);

/** @This is the private context of a queue sink pipe. */
struct upipe_qsink {
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *upump;
    /** oob watcher */
    struct upump *upump_oob;

    /** pseudo-output */
    struct upipe *output;
    /** flow definition */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;
    /** list of output requests */
    struct uchain request_list;

    /** pointer to queue source */
    struct upipe *qsrc;
    /** temporary uref storage */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_qsink, upipe, UPIPE_QSINK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_qsink, urefcount, upipe_qsink_free)
UPIPE_HELPER_UPUMP_MGR(upipe_qsink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_qsink, upump, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_qsink, upump_oob, upump_mgr)
UPIPE_HELPER_INPUT(upipe_qsink, urefs, nb_urefs, max_urefs, blockers, upipe_qsink_output)

/** @internal @This allocates a queue sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_qsink_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    if (signature != UPIPE_QSINK_SIGNATURE)
        goto upipe_qsink_alloc_err;
    struct upipe *qsrc = va_arg(args, struct upipe *);
    if (qsrc == NULL)
        goto upipe_qsink_alloc_err;

    struct upipe_qsink *upipe_qsink = malloc(sizeof(struct upipe_qsink));
    if (unlikely(upipe_qsink == NULL))
        goto upipe_qsink_alloc_err;

    struct upipe *upipe = upipe_qsink_to_upipe(upipe_qsink);
    upipe_init(upipe, mgr, uprobe);
    upipe_qsink_init_urefcount(upipe);
    upipe_qsink_init_upump_mgr(upipe);
    upipe_qsink_init_upump(upipe);
    upipe_qsink_init_upump_oob(upipe);
    upipe_qsink_init_input(upipe);
    upipe_qsink->qsrc = upipe_use(qsrc);
    upipe_qsink->flow_def = NULL;
    upipe_qsink->flow_def_sent = false;
    upipe_qsink->output = NULL;
    ulist_init(&upipe_qsink->request_list);

    upipe_throw_ready(upipe);
    upipe_notice_va(upipe, "using queue source %p", qsrc);
    return upipe;

upipe_qsink_alloc_err:
    uprobe_release(uprobe);
    return NULL;
}

/** @internal @This outputs data to the queue.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return true if the output could be written
 */
static bool upipe_qsink_output(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    return uqueue_push(&upipe_queue(upipe_qsink->qsrc)->uqueue,
                       uref_to_uchain(uref));
}

/** @internal @This is called when the queue can be written again.
 * Unblock the sink.
 *
 * @param upump description structure of the watcher
 */
static void upipe_qsink_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_qsink_output_input(upipe);
    upipe_qsink_unblock_input(upipe);
    if (upipe_qsink_check_input(upipe)) {
        upump_stop(upump);
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_qsink_input. */
        upipe_release(upipe);
    }
}

/** @internal @This checks and creates the upump watcher to wait for the
 * availability of the queue.
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool upipe_qsink_check_watcher(struct upipe *upipe)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (likely(upipe_qsink->upump != NULL))
        return true;

    upipe_qsink_check_upump_mgr(upipe);
    if (upipe_qsink->upump_mgr == NULL)
        return false;

    struct upump *upump =
        uqueue_upump_alloc_push(&upipe_queue(upipe_qsink->qsrc)->uqueue,
                                upipe_qsink->upump_mgr,
                                upipe_qsink_watcher, upipe);
    if (unlikely(upump == NULL)) {
        upipe_err_va(upipe, "can't create watcher");
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return false;
    }
    upipe_qsink_set_upump(upipe, upump);
    return true;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_qsink_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (!upipe_qsink->flow_def_sent && upipe_qsink->flow_def != NULL) {
        struct uref *flow_def;
        if ((flow_def = uref_dup(upipe_qsink->flow_def)) == NULL)
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        else {
            upipe_qsink->flow_def_sent = true;
            upipe_qsink_input(upipe, flow_def, upump_p);
        }
    }

    if (!upipe_qsink_check_input(upipe)) {
        upipe_qsink_hold_input(upipe, uref);
        upipe_qsink_block_input(upipe, upump_p);
    } else if (!upipe_qsink_output(upipe, uref, upump_p)) {
        if (!upipe_qsink_check_watcher(upipe)) {
            upipe_warn(upipe, "unable to spool uref");
            uref_free(uref);
            return;
        }
        upump_start(upipe_qsink->upump);
        upipe_qsink_hold_input(upipe, uref);
        upipe_qsink_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This returns a pointer to the current pseudo-output.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with a pointer to the pseudo-output
 * @return an error code
 */
static int upipe_qsink_get_output(struct upipe *upipe, struct upipe **p)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    assert(p != NULL);
    *p = upipe_qsink->output;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the pointer to the current pseudo-output.
 *
 * @param upipe description structure of the pipe
 * @param output pointer to the pseudo-output
 * @return an error code
 */
static int upipe_qsink_set_output(struct upipe *upipe, struct upipe *output)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);

    if (unlikely(upipe_qsink->output != NULL))
        upipe_release(upipe_qsink->output);
    if (unlikely(output == NULL))
        return UBASE_ERR_NONE;

    upipe_qsink->output = output;
    upipe_use(output);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param uref flow definition
 * @return an error code
 */
static int upipe_qsink_set_flow_def(struct upipe *upipe, struct uref *uref)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (unlikely(uref == NULL))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup = NULL;
    if ((flow_def_dup = uref_dup(uref)) == NULL) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    if (upipe_qsink->flow_def != NULL)
        uref_free(upipe_qsink->flow_def);
    upipe_qsink->flow_def = flow_def_dup;
    upipe_qsink->flow_def_sent = false;
    return UBASE_ERR_NONE;
}

/** @internal @This flushes all currently held buffers, and unblocks the
 * sources.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_qsink_flush(struct upipe *upipe)
{
    if (upipe_qsink_flush_input(upipe)) {
        struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
        upump_stop(upipe_qsink->upump);
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_qsink_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This pushes a downstream message.
 *
 * @param upipe description structure of the pipe
 * @param type type of message
 * @param request optional request
 * @return an error code
 */
static int upipe_qsink_push_downstream(struct upipe *upipe,
                                       enum upipe_queue_downstream_type type,
                                       struct upipe_queue_request *request)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    struct upipe_queue_downstream *downstream =
        upipe_queue_downstream_alloc(type, request);
    UBASE_ALLOC_RETURN(downstream);

    if (unlikely(!uqueue_push(&upipe_queue(upipe_qsink->qsrc)->downstream_oob,
                              downstream))) {
        upipe_warn(upipe, "unable to send downstream message");
        upipe_queue_downstream_free(downstream);
        return UBASE_ERR_BUSY;
    }

    if (upipe_qsink->upump_oob == NULL) {
        upipe_qsink_check_upump_mgr(upipe);
        if (upipe_qsink->upump_mgr != NULL) {
            struct upump *upump = uqueue_upump_alloc_pop(
                    &upipe_queue(upipe_qsink->qsrc)->upstream_oob,
                    upipe_qsink->upump_mgr, upipe_qsink_oob, upipe);
            if (unlikely(upump == NULL)) {
                upipe_err_va(upipe, "can't create watcher");
                return UBASE_ERR_UPUMP;
            }
            upipe_qsink_set_upump_oob(upipe, upump);
            upump_start(upump);
        } else
            upipe_warn(upipe, "unable to create upstream watcher");
    }
    return UBASE_ERR_NONE;
}

/** @internal @This registers a request from upstream.
 *
 * @param upipe description structure of the pipe
 * @param urequest request to register
 * @return an error code
 */
static int upipe_qsink_register_request(struct upipe *upipe,
                                        struct urequest *urequest)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    struct upipe_queue_request *upipe_queue_proxy =
        upipe_queue_request_alloc(urequest);
    UBASE_ALLOC_RETURN(upipe_queue_proxy);
    ulist_add(&upipe_qsink->request_list,
              upipe_queue_request_to_uchain_sink(upipe_queue_proxy));

    int err = upipe_qsink_push_downstream(upipe,
            UPIPE_QUEUE_DOWNSTREAM_REGISTER, upipe_queue_proxy);
    if (unlikely(!ubase_check(err))) {
        ulist_delete(upipe_queue_request_to_uchain_sink(upipe_queue_proxy));
        upipe_queue_request_release(upipe_queue_proxy);
    } else
        upipe_verbose_va(upipe, "registered request %p", upipe_queue_proxy);
    return err;
}

/** @internal @This unregisters a request from upstream.
 *
 * @param upipe description structure of the pipe
 * @param urequest request to unregister
 * @return an error code
 */
static int upipe_qsink_unregister_request(struct upipe *upipe,
                                          struct urequest *urequest)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_qsink->request_list, uchain, uchain_tmp) {
        struct upipe_queue_request *upipe_queue_proxy =
            upipe_queue_request_from_uchain_sink(uchain);
        if (upipe_queue_proxy->upstream == urequest) {
            ulist_delete(uchain);

            int err = upipe_qsink_push_downstream(upipe,
                    UPIPE_QUEUE_DOWNSTREAM_UNREGISTER, upipe_queue_proxy);
            upipe_verbose_va(upipe, "unregistered request %p",
                             upipe_queue_proxy);
            upipe_queue_request_release(upipe_queue_proxy);
            return err;
        }
    }
    return UBASE_ERR_INVALID;
}

/** @internal @This reads out of band packets from the queue and handles them.
 *
 * @param upump description structure of the oob watcher
 */
static void upipe_qsink_oob(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    struct upipe_queue_upstream *upstream =
        uqueue_pop(&upipe_queue(upipe_qsink->qsrc)->upstream_oob,
                   struct upipe_queue_upstream *);
    if (unlikely(upstream == NULL))
        return;

    if (unlikely(upstream->type != UPIPE_QUEUE_UPSTREAM_PROVIDE)) {
        upipe_queue_upstream_free(upstream);
        return;
    }

    struct upipe_queue_request *request = upstream->request;
    if (unlikely(!ulist_is_in(upipe_queue_request_to_uchain_sink(request)))) {
        /* The request was unregistered in the meantime. */
        upipe_verbose_va(upipe, "provided unregistered request %p", request);
        upipe_queue_upstream_free(upstream);
        return;
    }

    switch (request->upstream->type) {
        case UREQUEST_UREF_MGR:
            urequest_provide_uref_mgr(request->upstream, upstream->uref_mgr);
            upstream->uref_mgr = NULL;
            break;
        case UREQUEST_FLOW_FORMAT:
            urequest_provide_flow_format(request->upstream, upstream->uref);
            upstream->uref = NULL;
            break;
        case UREQUEST_UBUF_MGR:
            urequest_provide_ubuf_mgr(request->upstream, upstream->ubuf_mgr,
                                      upstream->uref);
            upstream->ubuf_mgr = NULL;
            upstream->uref = NULL;
            break;
        case UREQUEST_UCLOCK:
            urequest_provide_uclock(request->upstream, upstream->uclock);
            upstream->uclock = NULL;
            break;
        case UREQUEST_SINK_LATENCY:
            urequest_provide_sink_latency(request->upstream, upstream->uint64);
            break;
    }

    upipe_verbose_va(upipe, "provided request %p", request);
    upipe_queue_upstream_free(upstream);
}

/** @internal @This processes control commands on a queue sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_qsink_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_qsink_register_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_qsink_unregister_request(upipe, request);
        }
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_qsink_set_upump(upipe, NULL);
            return upipe_qsink_attach_upump_mgr(upipe);
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_qsink_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_qsink_set_output(upipe, output);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            return upipe_qsink_set_flow_def(upipe, uref);
        }

        case UPIPE_GET_MAX_LENGTH: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_qsink_get_max_length(upipe, p);
        }
        case UPIPE_SET_MAX_LENGTH: {
            unsigned int max_length = va_arg(args, unsigned int);
            return upipe_qsink_set_max_length(upipe, max_length);
        }

        case UPIPE_FLUSH:
            return upipe_qsink_flush(upipe);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a queue sink pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_qsink_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_qsink_control(upipe, command, args));

    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (unlikely(!upipe_qsink_check_input(upipe))) {
        upipe_qsink_check_watcher(upipe);
        upump_start(upipe_qsink->upump);
    }

    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qsink_free(struct upipe *upipe)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);

    /* play source end */
    upipe_notice_va(upipe, "ending queue source %p", upipe_qsink->qsrc);
    upipe_qsink_push_downstream(upipe, UPIPE_QUEUE_DOWNSTREAM_SOURCE_END, NULL);
    upipe_release(upipe_qsink->qsrc);

    upipe_throw_dead(upipe);

    upipe_release(upipe_qsink->output);
    uref_free(upipe_qsink->flow_def);
    upipe_qsink_clean_upump(upipe);
    upipe_qsink_clean_upump_oob(upipe);
    upipe_qsink_clean_upump_mgr(upipe);
    upipe_qsink_clean_input(upipe);
    upipe_qsink_clean_urefcount(upipe);
    upipe_clean(upipe);
    free(upipe_qsink);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_qsink_mgr = {
    .signature = UPIPE_QSINK_SIGNATURE,

    .upipe_alloc = _upipe_qsink_alloc,
    .upipe_input = upipe_qsink_input,
    .upipe_control = upipe_qsink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all queue sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_qsink_mgr_alloc(void)
{
    return &upipe_qsink_mgr;
}
