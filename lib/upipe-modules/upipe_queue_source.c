/*****************************************************************************
 * upipe_queue_source.c: upipe source module for queues
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

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uref.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_flows.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe-modules/upipe_queue_source.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a queue source pipe. */
struct upipe_qsrc {
    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;

    /** pipe acting as output */
    struct upipe *output;
    /** list of input flows */
    struct ulist flows;
    /** extra data for the queue structure */
    void *uqueue_extra;
    /** true if we have thrown the ready event */
    bool ready;

    /** structure exported to the sinks */
    struct upipe_queue upipe_queue;
};

UPIPE_HELPER_UPIPE(upipe_qsrc, upipe_queue.upipe)
UPIPE_HELPER_UREF_MGR(upipe_qsrc, uref_mgr)

UPIPE_HELPER_UPUMP_MGR(upipe_qsrc, upump_mgr, upump)

/** @internal @This allocates a queue source pipe.
 *
 * @param mgr common management structure
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_qsrc_alloc(struct upipe_mgr *mgr)
{
    struct upipe_qsrc *upipe_qsrc = malloc(sizeof(struct upipe_qsrc));
    if (unlikely(upipe_qsrc == NULL)) return NULL;
    struct upipe *upipe = upipe_qsrc_to_upipe(upipe_qsrc);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_QSRC_SIGNATURE;
    upipe_qsrc_init_uref_mgr(upipe);
    upipe_qsrc_init_upump_mgr(upipe);
    upipe_qsrc->output = NULL;
    upipe_flows_init(&upipe_qsrc->flows);
    upipe_qsrc->uqueue_extra = NULL;
    upipe_qsrc->ready = false;
    upipe_qsrc->upipe_queue.max_length = 0;
    return upipe;
}

/** @internal @This outputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_qsrc_output(struct upipe *upipe, struct uref *uref)
{
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    upipe_input(upipe_qsrc->output, uref);
}

/** @internal @This reads data from the queue and outputs it.
 *
 * @param upump description structure of the read watcher
 */
static void upipe_qsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);

    struct uchain *uchain = uqueue_pop(upipe_queue(upipe));
    if (likely(uchain != NULL)) {
        struct uref *uref = uref_from_uchain(uchain);
        if (unlikely(!upipe_flows_input(&upipe_qsrc->flows, upipe,
                                        upipe_qsrc->uref_mgr, uref))) {
            uref_release(uref);
            return;
        }

        upipe_qsrc_output(upipe, uref);
    }
}

/** @internal @This handles the get_output control command.
 *
 * @param upipe description structure of the pipe
 * @param output_p filled in with a pointer to the output pipe
 * @return false in case of error
 */
static bool upipe_qsrc_get_output(struct upipe *upipe,
                                  struct upipe **output_p)
{
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    assert(output_p != NULL);
    *output_p = upipe_qsrc->output;
    return true;
}

/** @internal @This handles the set_output control command, and properly
 * deletes and replays flows on old and new outputs. We do not use the
 * linear output helper here, because we aren't linear (we can output
 * any number of flows).
 *
 * @param upipe description structure of the pipe
 * @param output new output pipe
 * @return false in case of error
 */
static bool upipe_qsrc_set_output(struct upipe *upipe, struct upipe *output)
{
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    if (unlikely(upipe_qsrc->output != NULL)) {
        if (likely(upipe_qsrc->uref_mgr != NULL)) {
            /* signal flow deletion on old output */
            upipe_flows_foreach_delete(&upipe_qsrc->flows, upipe,
                                       upipe_qsrc->uref_mgr, uref,
                                       upipe_qsrc_output(upipe, uref));
        }
        upipe_release(upipe_qsrc->output);
    }
    upipe_qsrc->output = output;
    if (likely(upipe_qsrc->output != NULL)) {
        upipe_use(upipe_qsrc->output);
        if (likely(upipe_qsrc->uref_mgr != NULL)) {
            /* replay flow definitions */
            upipe_flows_foreach_replay(&upipe_qsrc->flows, upipe,
                                       upipe_qsrc->uref_mgr, uref,
                                       upipe_qsrc_output(upipe, uref));
        }
    }
    return true;
}

/** @internal @This returns the maximum length of the queue.
 *
 * @param upipe description structure of the pipe
 * @param length_p filled in with the maximum length of the queue
 * @return false in case of error
 */
static bool _upipe_qsrc_get_max_length(struct upipe *upipe,
                                       unsigned int *length_p)
{
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    assert(length_p != NULL);
    *length_p = upipe_qsrc->upipe_queue.max_length;
    return true;
}

/** @internal @This sets the maximum length of the queue. Note that the queue
 * won't accept sinks until it is initialized by this function with a non-zero
 * value. Also note that it may not be changed afterwards.
 *
 * @param upipe description structure of the pipe
 * @param length maximum length of the queue
 * @return false in case of error
 */
static bool _upipe_qsrc_set_max_length(struct upipe *upipe, unsigned int length)
{
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    if (unlikely(!length || length > UINT8_MAX ||
                 upipe_queue_max_length(upipe)))
        return false;

    upipe_qsrc->uqueue_extra = malloc(uqueue_sizeof(length));
    if (unlikely(upipe_qsrc->uqueue_extra == NULL))
        return false;
    if (unlikely(!uqueue_init(upipe_queue(upipe), length,
                              upipe_qsrc->uqueue_extra)))
        return false;
    upipe_qsrc->upipe_queue.max_length = length;
    ulog_notice(upipe->ulog, "queue source %p is ready with length %u",
                upipe, length);
    return true;
}

/** @internal @This returns the current length of the queue. This function,
 * like all control functions, may only be called from the thread which runs the
 * queue source pipe. The length of the queue may change at any time and the
 * value returned may no longer be valid.
 *
 * @param upipe description structure of the pipe
 * @param length_p filled in with the current length of the queue
 * @return false in case of error
 */
static bool _upipe_qsrc_get_length(struct upipe *upipe, unsigned int *length_p)
{
    assert(length_p != NULL);
    *length_p = uqueue_length(upipe_queue(upipe));
    return true;
}

/** @internal @This processes control commands on a queue source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_qsrc_control(struct upipe *upipe, enum upipe_command command,
                                va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_qsrc_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_qsrc_set_uref_mgr(upipe, uref_mgr);
        }

        case UPIPE_LINEAR_GET_OUTPUT: {
            struct upipe **output_p = va_arg(args, struct upipe **);
            return upipe_qsrc_get_output(upipe, output_p);
        }
        case UPIPE_LINEAR_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_qsrc_set_output(upipe, output);
        }

        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_qsrc_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            return upipe_qsrc_set_upump_mgr(upipe, upump_mgr);
        }

        case UPIPE_QSRC_GET_MAX_LENGTH: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_QSRC_SIGNATURE);
            unsigned int *length_p = va_arg(args, unsigned int *);
            return _upipe_qsrc_get_max_length(upipe, length_p);
        }
        case UPIPE_QSRC_SET_MAX_LENGTH: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_QSRC_SIGNATURE);
            unsigned int length = va_arg(args, unsigned int);
            return _upipe_qsrc_set_max_length(upipe, length);
        }
        case UPIPE_QSRC_GET_LENGTH: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_QSRC_SIGNATURE);
            unsigned int *length_p = va_arg(args, unsigned int *);
            return _upipe_qsrc_get_length(upipe, length_p);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on a queue source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_qsrc_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    if (unlikely(!_upipe_qsrc_control(upipe, command, args)))
        return false;

    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    if (unlikely(upipe_qsrc->uref_mgr != NULL &&
                 upipe_qsrc->upump_mgr != NULL &&
                 upipe_qsrc->output != NULL &&
                 upipe_qsrc->upipe_queue.max_length)) {
        if (likely(upipe_qsrc->upump == NULL)) {
            struct upump *upump =
                uqueue_upump_alloc_pop(upipe_queue(upipe),
                                       upipe_qsrc->upump_mgr,
                                       upipe_qsrc_worker, upipe);
            if (unlikely(upump == NULL)) {
                ulog_error(upipe->ulog, "can't create watcher");
                upipe_throw_upump_error(upipe);
                return false;
            } 
            upipe_qsrc_set_upump(upipe, upump);
            upump_start(upump);
        }
        if (likely(!upipe_qsrc->ready)) {
            upipe_qsrc->ready = true;
            upipe_throw_ready(upipe);
        }

    } else {
        upipe_qsrc_set_upump(upipe, NULL);
        upipe_qsrc->ready = false;

        if (unlikely(upipe_qsrc->uref_mgr == NULL))
            upipe_throw_need_uref_mgr(upipe);
        else if (unlikely(upipe_qsrc->upump_mgr == NULL))
            upipe_throw_need_upump_mgr(upipe);
    }

    return true;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qsrc_free(struct upipe *upipe)
{
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    ulog_notice(upipe->ulog, "freeing queue %p", upipe);
    if (likely(upipe_qsrc->output != NULL)) {
        if (likely(upipe_qsrc->uref_mgr != NULL)) {
            /* signal flow deletion on old queue */
            upipe_flows_foreach_delete(&upipe_qsrc->flows, upipe,
                                       upipe_qsrc->uref_mgr, uref,
                                       upipe_qsrc_output(upipe, uref));
        }
        upipe_release(upipe_qsrc->output);
    }
    upipe_flows_clean(&upipe_qsrc->flows);
    upipe_qsrc_clean_upump_mgr(upipe);
    upipe_qsrc_clean_uref_mgr(upipe);

    struct uqueue *uqueue = upipe_queue(upipe);
    struct uchain *uchain;
    while ((uchain = uqueue_pop(uqueue)) != NULL) {
        struct uref *uref = uref_from_uchain(uchain);
        uref_release(uref);
    }
    uqueue_clean(uqueue);
    free(upipe_qsrc->uqueue_extra);
    free(upipe_qsrc);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_qsrc_mgr = {
    /* no need to initialize refcount as we don't use it */

    .upipe_alloc = _upipe_qsrc_alloc,
    .upipe_control = upipe_qsrc_control,
    .upipe_free = upipe_qsrc_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all queue sources
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_qsrc_mgr_alloc(void)
{
    return &upipe_qsrc_mgr;
}
