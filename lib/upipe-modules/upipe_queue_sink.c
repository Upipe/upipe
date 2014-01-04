/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_sink.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_queue_source.h>

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
                               struct upump *upump);

/** @This is the private context of a queue sink pipe. */
struct upipe_qsink {
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *upump;

    /** pseudo-output */
    struct upipe *output;
    /** flow definition */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

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
UPIPE_HELPER_UREFCOUNT(upipe_qsink, urefcount, upipe_qsink_no_input)
UPIPE_HELPER_VOID(upipe_qsink)
UPIPE_HELPER_UPUMP_MGR(upipe_qsink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_qsink, upump, upump_mgr)
UPIPE_HELPER_SINK(upipe_qsink, urefs, nb_urefs, max_urefs, blockers, upipe_qsink_output)

/** @hidden */
static void upipe_qsink_free(struct upipe *upipe);

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
    struct upipe *upipe = upipe_qsink_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    upipe_qsink_init_urefcount(upipe);
    upipe_qsink_init_upump_mgr(upipe);
    upipe_qsink_init_upump(upipe);
    upipe_qsink_init_sink(upipe);
    upipe_qsink->qsrc = NULL;
    upipe_qsink->flow_def = NULL;
    upipe_qsink->flow_def_sent = false;
    upipe_qsink->output = NULL;
    upipe_throw_ready(upipe);
    return upipe;
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
        upump_stop(upump);
    }
    upipe_qsink_unblock_sink(upipe);
    if (upipe_dead(upipe) && upipe_qsink_check_sink(upipe))
        upipe_qsink_free(upipe);
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

    if (upipe_qsink->qsrc == NULL)
        return false;

    upipe_qsink_check_upump_mgr(upipe);
    if (upipe_qsink->upump_mgr == NULL)
        return false;

    struct upump *upump =
        uqueue_upump_alloc_push(upipe_queue(upipe_qsink->qsrc),
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
    if (!upipe_qsink->flow_def_sent && upipe_qsink->flow_def != NULL) {
        struct uref *flow_def;
        if ((flow_def = uref_dup(upipe_qsink->flow_def)) == NULL)
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        else {
            upipe_qsink->flow_def_sent = true;
            upipe_qsink_input(upipe, flow_def, upump);
        }
    }
    if (!upipe_qsink_check_sink(upipe)) {
        upipe_qsink_hold_sink(upipe, uref);
        upipe_qsink_block_sink(upipe, upump);
    } else if (!upipe_qsink_output(upipe, uref, upump)) {
        if (!upipe_qsink_check_watcher(upipe)) {
            upipe_warn(upipe, "unable to spool uref");
            uref_free(uref);
            return;
        }
        upipe_qsink_hold_sink(upipe, uref);
        upipe_qsink_block_sink(upipe, upump);
        upump_start(upipe_qsink->upump);
    }
}

/** @internal @This returns a pointer to the current pseudo-output.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with a pointer to the pseudo-output
 * @return an error code
 */
static enum ubase_err upipe_qsink_get_output(struct upipe *upipe, struct upipe **p)
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
static enum ubase_err upipe_qsink_set_output(struct upipe *upipe, struct upipe *output)
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
static enum ubase_err upipe_qsink_set_flow_def(struct upipe *upipe,
                                               struct uref *uref)
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

/** @internal @This returns a pointer to the current queue source.
 *
 * @param upipe description structure of the pipe
 * @param queue_p filled in with a pointer to the queue source
 * @return an error code
 */
static enum ubase_err _upipe_qsink_get_qsrc(struct upipe *upipe,
                                            struct upipe **qsrc_p)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    assert(qsrc_p != NULL);
    *qsrc_p = upipe_qsink->qsrc;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the pointer to the current queue source.
 *
 * @param upipe description structure of the pipe
 * @param queue pointer to the queue source
 * @return an error code
 */
static enum ubase_err _upipe_qsink_set_qsrc(struct upipe *upipe,
                                            struct upipe *qsrc)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);

    if (unlikely(upipe_qsink->qsrc != NULL))
        return UBASE_ERR_UNHANDLED;
    if (unlikely(qsrc == NULL))
        return UBASE_ERR_NONE;

    upipe_qsink->qsrc = qsrc;
    upipe_use(qsrc);

    upipe_notice_va(upipe, "using queue source %p", qsrc);
    upipe_qsink->flow_def_sent = false;
    return UBASE_ERR_NONE;
}

/** @internal @This flushes all currently held buffers, and unblocks the
 * sources.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static enum ubase_err upipe_qsink_flush(struct upipe *upipe)
{
    if (upipe_qsink_flush_sink(upipe)) {
        struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
        upump_stop(upipe_qsink->upump);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a queue sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static enum ubase_err _upipe_qsink_control(struct upipe *upipe,
                                 enum upipe_command command,
                                 va_list args)
{
    switch (command) {
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

        case UPIPE_SINK_GET_MAX_LENGTH: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_qsink_get_max_length(upipe, p);
        }
        case UPIPE_SINK_SET_MAX_LENGTH: {
            unsigned int max_length = va_arg(args, unsigned int);
            return upipe_qsink_set_max_length(upipe, max_length);
        }

        case UPIPE_QSINK_GET_QSRC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_QSINK_SIGNATURE)
            struct upipe **qsrc_p = va_arg(args, struct upipe **);
            return _upipe_qsink_get_qsrc(upipe, qsrc_p);
        }
        case UPIPE_QSINK_SET_QSRC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_QSINK_SIGNATURE)
            struct upipe *qsrc = va_arg(args, struct upipe *);
            return _upipe_qsink_set_qsrc(upipe, qsrc);
        }
        case UPIPE_SINK_FLUSH:
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
static enum ubase_err upipe_qsink_control(struct upipe *upipe,
                                          enum upipe_command command,
                                          va_list args)
{
    UBASE_RETURN(_upipe_qsink_control(upipe, command, args));

    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    upipe_qsink_check_watcher(upipe);
    if (unlikely(!upipe_qsink_check_sink(upipe)))
        upump_start(upipe_qsink->upump);

    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qsink_free(struct upipe *upipe)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (likely(upipe_qsink->qsrc != NULL)) {
        upipe_notice_va(upipe, "releasing queue source %p", upipe_qsink->qsrc);
        upipe_release(upipe_qsink->qsrc);
    }
    upipe_throw_dead(upipe);

    if (upipe_qsink->output != NULL)
        upipe_release(upipe_qsink->output);
    if (upipe_qsink->flow_def != NULL)
        uref_free(upipe_qsink->flow_def);
    upipe_qsink_clean_upump(upipe);
    upipe_qsink_clean_upump_mgr(upipe);
    upipe_qsink_clean_sink(upipe);
    upipe_qsink_clean_urefcount(upipe);
    upipe_qsink_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qsink_no_input(struct upipe *upipe)
{
    struct upipe_qsink *upipe_qsink = upipe_qsink_from_upipe(upipe);
    if (upipe_qsink->qsrc == NULL || upipe_qsink->flow_def == NULL) {
        upipe_qsink_free(upipe);
        return;
    }

    /* play flow end */
    struct uref *uref = uref_dup(upipe_qsink->flow_def);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        upipe_qsink_free(upipe);
        return;
    }
    uref_flow_set_end(uref);
    upipe_qsink_input(upipe, uref, NULL);
    if (upipe_qsink_check_sink(upipe))
        upipe_qsink_free(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_qsink_mgr = {
    .signature = UPIPE_QSINK_SIGNATURE,

    .upipe_alloc = upipe_qsink_alloc,
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
