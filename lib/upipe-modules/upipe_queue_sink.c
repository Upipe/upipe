/*****************************************************************************
 * upipe_queue_sink.c: upipe sink module for queues
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uqueue.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uref.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_sink.h>
#include <upipe/upipe_flows.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_queue_source.h>

static void upipe_qsink_watcher(struct upump *upump);

/** super-set of the upipe_sink structure with additional local members */
struct upipe_qsink {
    /** list of input flows */
    struct ulist flows;
    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** pointer to queue source */
    struct upipe *qsrc;
    /** true if the sink currently blocks the pipe */
    bool blocked;

    /** members common to sink pipes */
    struct upipe_sink upipe_sink;
};

/** @internal @This returns the high-level upipe structure.
 *
 * @param upipe_qsink pointer to the upipe_qsink structure
 * @return pointer to the upipe structure
 */
static inline struct upipe *upipe_qsink_to_upipe(struct upipe_qsink *upipe_qsink)
{
    return upipe_sink_to_upipe(&upipe_qsink->upipe_sink);
}

/** @internal @This returns the private struct upipe_qsink structure.
 *
 * @param upipe description structure of the pipe
 * @return pointer to the upipe_qsink structure
 */
static inline struct upipe_qsink *upipe_qsink_from_upipe(struct upipe *upipe)
{
    struct upipe_sink *upipe_sink = upipe_sink_from_upipe(upipe);
    return container_of(upipe_sink, struct upipe_qsink, upipe_sink);
}

/** @internal @This checks if the queue sink pipe is ready to process data.
 *
 * @param upipe description structure of the pipe
 */
static bool upipe_qsink_ready(struct upipe *upipe)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    return upipe_sink_ready(upipe) && upipe_qsink->uref_mgr != NULL &&
           upipe_qsink->qsrc != NULL;
}

/** @internal @This allocates a queue sink pipe.
 *
 * @param mgr common management structure
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_qsink_alloc(struct upipe_mgr *mgr)
{
    struct upipe_qsink *upipe_qsink = malloc(sizeof(struct upipe_qsink));
    if (unlikely(upipe_qsink == NULL)) return NULL;
    struct upipe *upipe = upipe_qsink_to_upipe(upipe_qsink);
    upipe_sink_init(upipe, 0);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_QSINK_SIGNATURE;
    upipe_flows_init(&upipe_qsink->flows);
    UPIPE_OBJ_INIT_TEMPLATE(upipe_qsink, uref_mgr)
    upipe_qsink->qsrc = NULL;
    upipe_qsink->blocked = false;
    return upipe;
}

/** @internal @This is called when the queue can be written again.
 * Unblock the sink.
 *
 * @param upump description structure of the watcher
 */
static void upipe_qsink_watcher(struct upump *upump)
{
    struct upump_mgr *mgr = upump->mgr;
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);

    upump_mgr_sink_unblock(mgr);
    upipe_qsink->blocked = false;
    upump_stop(upump);
}

/** @internal @This outputs data to the queue.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_qsink_output(struct upipe *upipe, struct uref *uref)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (unlikely(!uqueue_push(upipe_queue(upipe_qsink->qsrc),
                              uref_to_uchain(uref)))) {
        if (!upipe_qsink->blocked) {
            struct upump_mgr *mgr = upipe_sink_upump_mgr(upipe);
            upump_mgr_sink_block(mgr);
            upipe_qsink->blocked = true;
            upump_start(upipe_sink_upump(upipe));
        }
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_qsink_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);

    if (unlikely(!upipe_flows_input(&upipe_qsink->flows, upipe->ulog,
                                    upipe_qsink->uref_mgr, uref))) {
        uref_release(uref);
        return false;
    }

    if (unlikely(upipe_qsink->qsrc == NULL)) {
        uref_release(uref);
        ulog_warning(upipe->ulog, "received a buffer before opening a queue");
        return false;
    }

    upipe_qsink_output(upipe, uref);
    return true;
}

/** @internal @This returns a pointer to the current queue source.
 *
 * @param upipe description structure of the pipe
 * @param queue_p filled in with a pointer to the queue source
 * @return false in case of error
 */
static bool _upipe_qsink_get_qsrc(struct upipe *upipe, struct upipe **qsrc_p)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    assert(qsrc_p != NULL);
    *qsrc_p = upipe_qsink->qsrc;
    return true;
}

/** @internal @This sets the pointer to the current queue source.
 *
 * @param upipe description structure of the pipe
 * @param queue pointer to the queue source
 * @return false in case of error
 */
static bool _upipe_qsink_set_qsrc(struct upipe *upipe, struct upipe *qsrc)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);

    if (unlikely(upipe_qsink->qsrc != NULL)) {
        ulog_notice(upipe->ulog, "releasing queue source %p",
                    upipe_qsink->qsrc);
        if (likely(upipe_qsink->uref_mgr != NULL)) {
            /* signal flow deletion on old queue */
            upipe_flows_foreach_delete(&upipe_qsink->flows, upipe->ulog,
                                       upipe_qsink->uref_mgr, uref,
                                       upipe_qsink_output(upipe, uref));
        }
        upipe_release(upipe_qsink->qsrc);
        upipe_qsink->qsrc = NULL;
    }
    upipe_sink_set_upump(upipe, NULL);

    if (likely(qsrc != NULL)) {
        if (unlikely(!upipe_queue_max_length(qsrc))) {
            ulog_error(upipe->ulog,
                       "unable to use queue source %p as it is uninitialized",
                       qsrc);
            return false;
        }

        upipe_qsink->qsrc = qsrc;
        upipe_use(qsrc);
        if (upipe_qsink->blocked) {
            struct upump_mgr *mgr = upipe_sink_upump_mgr(upipe);
            upump_mgr_sink_unblock(mgr);
            upipe_qsink->blocked = false;
        }
        ulog_notice(upipe->ulog, "using queue source %p", qsrc);
        if (likely(upipe_qsink->uref_mgr != NULL)) {
            /* replay flow definitions */
            upipe_flows_foreach_replay(&upipe_qsink->flows, upipe->ulog,
                                       upipe_qsink->uref_mgr, uref,
                                       upipe_qsink_output(upipe, uref));
        }
    }
    return true;
}

/** @internal @This processes control commands on a queue sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_qsink_control(struct upipe *upipe,
                                 enum upipe_control control,
                                 va_list args)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (likely(control == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        return upipe_qsink_input(upipe, uref);
    }
    switch (control) {
        UPIPE_OBJ_CONTROL_TEMPLATE(upipe_qsink, UPIPE, uref_mgr, UREF_MGR, uref_mgr)

        case UPIPE_QSINK_GET_QSRC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_QSINK_SIGNATURE);
            struct upipe **qsrc_p = va_arg(args, struct upipe **);
            return _upipe_qsink_get_qsrc(upipe, qsrc_p);
        }
        case UPIPE_QSINK_SET_QSRC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_QSINK_SIGNATURE);
            struct upipe *qsrc = va_arg(args, struct upipe *);
            return _upipe_qsink_set_qsrc(upipe, qsrc);
        }
        default:
            return upipe_sink_control(upipe, control, args);
    }
}

/** @internal @This processes control commands on a queue sink pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_qsink_control(struct upipe *upipe, enum upipe_control control,
                                va_list args)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    bool ret = _upipe_qsink_control(upipe, control, args);

    if (unlikely(upipe_qsink_ready(upipe))) {
        if (likely(upipe_sink_upump(upipe) == NULL)) {
            struct upump_mgr *mgr = upipe_sink_upump_mgr(upipe);
            struct upump *upump =
                uqueue_upump_alloc_push(upipe_queue(upipe_qsink->qsrc),
                                        mgr, upipe_qsink_watcher, upipe);
            if (unlikely(upump == NULL)) {
                ulog_error(upipe->ulog, "can't create watcher");
                upipe_throw_upump_error(upipe);
            } else
                upipe_sink_set_upump(upipe, upump);
        }
    } else
        upipe_sink_set_upump(upipe, NULL);

    return ret;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qsink_free(struct upipe *upipe)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (likely(upipe_qsink->qsrc != NULL)) {
        ulog_notice(upipe->ulog, "releasing queue source %p",
                    upipe_qsink->qsrc);
        if (likely(upipe_qsink->uref_mgr != NULL)) {
            /* signal flow deletion on old queue */
            upipe_flows_foreach_delete(&upipe_qsink->flows, upipe->ulog,
                                       upipe_qsink->uref_mgr, uref,
                                       upipe_qsink_output(upipe, uref));
        }
        upipe_release(upipe_qsink->qsrc);
    }
    upipe_flows_clean(&upipe_qsink->flows);
    upipe_sink_cleanup(upipe);
    UPIPE_OBJ_CLEAN_TEMPLATE(upipe_qsink, uref_mgr, uref_mgr)
    free(upipe_qsink);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_qsink_mgr = {
    /* no need to initialize refcount as we don't use it */

    .upipe_alloc = upipe_qsink_alloc,
    .upipe_control = upipe_qsink_control,
    .upipe_free = upipe_qsink_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all queue sinks
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_qsink_mgr_alloc(void)
{
    return &upipe_qsink_mgr;
}
