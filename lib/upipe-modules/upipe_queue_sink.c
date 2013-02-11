/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uqueue.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_queue_source.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

static void upipe_qsink_watcher(struct upump *upump);

/** @This is the private context of a queue sink pipe. */
struct upipe_qsink {
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *upump;
    /** flow definition */
    struct uref *flow_def;

    /** pointer to queue source */
    struct upipe *qsrc;
    /** temporary uref storage */
    struct ulist urefs;
    /** true if the sink currently blocks the pipe */
    bool blocked;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_qsink, upipe)
UPIPE_HELPER_UPUMP_MGR(upipe_qsink, upump_mgr, upump)

/** @internal @This allocates a queue sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_qsink_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe)
{
    struct upipe_qsink *upipe_qsink = malloc(sizeof(struct upipe_qsink));
    if (unlikely(upipe_qsink == NULL))
        return NULL;
    struct upipe *upipe = upipe_qsink_to_upipe(upipe_qsink);
    upipe_init(upipe, mgr, uprobe);
    upipe_qsink_init_upump_mgr(upipe);
    upipe_qsink->qsrc = NULL;
    upipe_qsink->flow_def = NULL;
    upipe_qsink->blocked = false;
    ulist_init(&upipe_qsink->urefs);
    urefcount_init(&upipe_qsink->refcount);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This marks the sink as blocked and starts the watcher.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qsink_wait(struct upipe *upipe)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (!upipe_qsink->blocked) {
        upump_mgr_sink_block(upipe_qsink->upump_mgr);
        upipe_qsink->blocked = true;
        upump_start(upipe_qsink->upump);
    }
}

/** @internal @This outputs data to the queue.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_qsink_output(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (unlikely(!ulist_empty(&upipe_qsink->urefs))) {
        ulist_add(&upipe_qsink->urefs, uref_to_uchain(uref));
        return;
    }

    if (unlikely(!uqueue_push(upipe_queue(upipe_qsink->qsrc),
                              uref_to_uchain(uref)))) {
        ulist_add(&upipe_qsink->urefs, uref_to_uchain(uref));
        upipe_qsink_wait(upipe);
    }
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
    struct ulist urefs = upipe_qsink->urefs;

    ulist_init(&upipe_qsink->urefs);
    upump_mgr_sink_unblock(mgr);
    upipe_qsink->blocked = false;
    upump_stop(upump);

    struct uchain *uchain;
    ulist_delete_foreach (&urefs, uchain) {
        ulist_delete(&urefs, uchain);
        upipe_qsink_output(upipe, uref_from_uchain(uchain), NULL);
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_qsink_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (upipe_qsink->flow_def != NULL)
            uref_free(upipe_qsink->flow_def);
        upipe_qsink->flow_def = uref_dup(uref);
        if (unlikely(upipe_qsink->flow_def == NULL)) {
            uref_free(uref);
            upipe_throw_aerror(upipe);
            return;
        }
        upipe_dbg_va(upipe, "flow definition %s", def);
    }

    if (unlikely(upipe_qsink->flow_def == NULL)) {
        upipe_throw_flow_def_error(upipe, uref);
        uref_free(uref);
        return;
    }

    if (unlikely(upipe_qsink->qsrc == NULL)) {
        uref_free(uref);
        upipe_warn(upipe, "received a buffer before opening a queue");
        return;
    }

    upipe_qsink_output(upipe, uref, upump);
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
        upipe_notice_va(upipe, "releasing queue source %p", upipe_qsink->qsrc);
        upipe_release(upipe_qsink->qsrc);
        upipe_qsink->qsrc = NULL;
    }
    upipe_qsink_set_upump(upipe, NULL);

    if (unlikely(qsrc == NULL))
        return true;
    if (upipe_qsink->upump_mgr == NULL) {
        upipe_throw_need_upump_mgr(upipe);
        if (unlikely(upipe_qsink->upump_mgr == NULL))
            return false;
    }

    if (unlikely(!upipe_queue_max_length(qsrc))) {
        upipe_err_va(upipe,
                     "unable to use queue source %p as it is uninitialized",
                     qsrc);
        return false;
    }

    upipe_qsink->qsrc = qsrc;
    upipe_use(qsrc);
    if (upipe_qsink->blocked) {
        upump_mgr_sink_unblock(upipe_qsink->upump_mgr);
        upipe_qsink->blocked = false;
    }
    upipe_notice_va(upipe, "using queue source %p", qsrc);
    if (upipe_qsink->flow_def != NULL) {
        /* replay flow definition */
        struct uref *uref = uref_dup(upipe_qsink->flow_def);
        if (unlikely(uref == NULL))
            upipe_throw_aerror(upipe);
        else
            upipe_qsink_output(upipe, uref, NULL);
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
                                 enum upipe_command command,
                                 va_list args)
{
    switch (command) {
        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_qsink_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
            if (upipe_qsink->upump != NULL)
                upipe_qsink_set_upump(upipe, NULL);
            return upipe_qsink_set_upump_mgr(upipe, upump_mgr);
        }

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
            return false;
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
static bool upipe_qsink_control(struct upipe *upipe, enum upipe_command command,
                                va_list args)
{
    if (unlikely(!_upipe_qsink_control(upipe, command, args)))
        return false;

    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (upipe_qsink->upump_mgr != NULL && upipe_qsink->qsrc != NULL &&
        upipe_qsink->upump == NULL) {
        struct upump *upump =
            uqueue_upump_alloc_push(upipe_queue(upipe_qsink->qsrc),
                                    upipe_qsink->upump_mgr,
                                    upipe_qsink_watcher, upipe);
        if (unlikely(upump == NULL)) {
            upipe_err_va(upipe, "can't create watcher");
            upipe_throw_upump_error(upipe);
            return false;
        }
        upipe_qsink_set_upump(upipe, upump);
    }
    if (unlikely(!ulist_empty(&upipe_qsink->urefs)))
        upipe_qsink_wait(upipe);

    return true;
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qsink_use(struct upipe *upipe)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    urefcount_use(&upipe_qsink->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qsink_release(struct upipe *upipe)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_qsink->refcount))) {
        if (likely(upipe_qsink->qsrc != NULL)) {
            upipe_notice_va(upipe, "releasing queue source %p",
                            upipe_qsink->qsrc);
            upipe_release(upipe_qsink->qsrc);
        }
        upipe_throw_dead(upipe);

        if (upipe_qsink->flow_def != NULL)
            uref_free(upipe_qsink->flow_def);
        upipe_qsink_clean_upump_mgr(upipe);

        struct uchain *uchain;
        ulist_delete_foreach (&upipe_qsink->urefs, uchain) {
            ulist_delete(&upipe_qsink->urefs, uchain);
            uref_free(uref_from_uchain(uchain));
        }

        upipe_clean(upipe);
        urefcount_clean(&upipe_qsink->refcount);
        free(upipe_qsink);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_qsink_mgr = {
    .signature = UPIPE_QSINK_SIGNATURE,

    .upipe_alloc = upipe_qsink_alloc,
    .upipe_input = upipe_qsink_input,
    .upipe_control = upipe_qsink_control,
    .upipe_use = upipe_qsink_use,
    .upipe_release = upipe_qsink_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all queue sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_qsink_mgr_alloc(void)
{
    return &upipe_qsink_mgr;
}
