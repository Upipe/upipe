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
#include <upipe/ulist.h>
#include <upipe/uqueue.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upump.h>
#include <upipe/upump_blocker.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_sink.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_queue_source.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

static void upipe_qsink_watcher(struct upump *upump);
static bool upipe_qsink_output(struct upipe *upipe, struct uref *uref,
                               struct upump *upump);

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
    /** list of blockers */
    struct ulist blockers;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_qsink, upipe)
UPIPE_HELPER_FLOW(upipe_qsink, NULL)
UPIPE_HELPER_UPUMP_MGR(upipe_qsink, upump_mgr, upump)
UPIPE_HELPER_SINK(upipe_qsink, urefs, blockers, upipe_qsink_output)

/** @internal @This allocates a queue sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_qsink_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_qsink_alloc_flow(mgr, uprobe, signature, args,
                                                 &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    upipe_qsink_init_upump_mgr(upipe);
    upipe_qsink_init_sink(upipe);
    upipe_qsink->qsrc = NULL;
    upipe_qsink->flow_def = flow_def;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This marks the sink as blocked and starts the watcher.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_qsink_wait(struct upipe *upipe, struct upump *upump)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    upump_start(upipe_qsink->upump);

    upipe_qsink_block_sink(upipe, upump);
}

/** @internal @This outputs data to the queue.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 * @return true if the output could be written
 */
static bool upipe_qsink_output(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    return uqueue_push(upipe_queue(upipe_qsink->qsrc), uref_to_uchain(uref));
}

/** @internal @This is called when the queue can be written again.
 * Unblock the sink.
 *
 * @param upump description structure of the watcher
 */
static void upipe_qsink_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    if (upipe_qsink_output_sink(upipe)) {
        upipe_qsink_unblock_sink(upipe);
        upump_stop(upump);
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
    if (unlikely(upipe_qsink->qsrc == NULL)) {
        uref_free(uref);
        upipe_warn(upipe, "received a buffer before opening a queue");
        return;
    }

    if (unlikely(!upipe_qsink_check_sink(upipe)))
        upipe_qsink_hold_sink(upipe, uref);
    else if (!upipe_qsink_output(upipe, uref, upump)) {
        upipe_qsink_hold_sink(upipe, uref);
        upipe_qsink_wait(upipe, upump);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param uref flow definition
 * @return false in case of error
 */
static bool upipe_qsink_set_flow_def(struct upipe *upipe, struct uref *uref)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (unlikely(uref == NULL))
        return false;
    struct uref *flow_def_dup = NULL;
    if ((flow_def_dup = uref_dup(uref)) == NULL) {
        upipe_throw_aerror(upipe);
        return false;
    }
    if (upipe_qsink->flow_def != NULL)
        uref_free(upipe_qsink->flow_def);
    upipe_qsink->flow_def = flow_def_dup;
    if (upipe_qsink->qsrc != NULL) {
        if ((flow_def_dup = uref_dup(uref)) == NULL) {
            upipe_throw_aerror(upipe);
            return false;
        }
        upipe_qsink_input(upipe, flow_def_dup, NULL);
    }
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
        upipe_notice_va(upipe, "releasing queue source %p", upipe_qsink->qsrc);
        if (upipe_qsink->flow_def != NULL) {
            /* play flow end */
            struct uref *uref = uref_dup(upipe_qsink->flow_def);
            if (unlikely(uref == NULL))
                upipe_throw_aerror(upipe);
            else {
                uref_flow_set_end(uref);
                upipe_qsink_input(upipe, uref, NULL);
            }
        }
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

    upipe_qsink->qsrc = qsrc;
    upipe_use(qsrc);
    upipe_qsink_unblock_sink(upipe);

    upipe_notice_va(upipe, "using queue source %p", qsrc);
    if (upipe_qsink->flow_def != NULL) {
        /* replay flow definition */
        struct uref *uref = uref_dup(upipe_qsink->flow_def);
        if (unlikely(uref == NULL))
            upipe_throw_aerror(upipe);
        else
            upipe_qsink_input(upipe, uref, NULL);
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
        case UPIPE_SET_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            return upipe_qsink_set_flow_def(upipe, uref);
        }
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
    if (unlikely(!upipe_qsink_check_sink(upipe)))
        upipe_qsink_wait(upipe, NULL);

    return true;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qsink_free(struct upipe *upipe)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    _upipe_qsink_set_qsrc(upipe, NULL);
    upipe_throw_dead(upipe);

    if (upipe_qsink->flow_def != NULL)
        uref_free(upipe_qsink->flow_def);
    upipe_qsink_clean_upump_mgr(upipe);
    upipe_qsink_clean_sink(upipe);
    upipe_qsink_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_qsink_mgr = {
    .signature = UPIPE_QSINK_SIGNATURE,

    .upipe_alloc = upipe_qsink_alloc,
    .upipe_input = upipe_qsink_input,
    .upipe_control = upipe_qsink_control,
    .upipe_free = upipe_qsink_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all queue sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_qsink_mgr_alloc(void)
{
    return &upipe_qsink_mgr;
}
