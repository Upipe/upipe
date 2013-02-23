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
 * @short Upipe source module for queues
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_output.h>
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
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** extra data for the queue structure */
    void *uqueue_extra;

    /** structure exported to the sinks */
    struct upipe_queue upipe_queue;
};

UPIPE_HELPER_UPIPE(upipe_qsrc, upipe_queue.upipe)

UPIPE_HELPER_OUTPUT(upipe_qsrc, output, flow_def, flow_def_sent)

UPIPE_HELPER_UPUMP_MGR(upipe_qsrc, upump_mgr, upump)

/** @internal @This allocates a queue source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_qsrc_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe)
{
    struct upipe_qsrc *upipe_qsrc = malloc(sizeof(struct upipe_qsrc));
    if (unlikely(upipe_qsrc == NULL))
        return NULL;
    struct upipe *upipe = upipe_qsrc_to_upipe(upipe_qsrc);
    upipe_init(upipe, mgr, uprobe);
    upipe_qsrc_init_output(upipe);
    upipe_qsrc_init_upump_mgr(upipe);
    upipe_qsrc->uqueue_extra = NULL;
    upipe_qsrc->upipe_queue.max_length = 0;
    upipe_throw_ready(upipe);
    return upipe;
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
        const char *def;
        if (unlikely(uref_flow_get_def(uref, &def))) {
            upipe_qsrc_store_flow_def(upipe, uref);
            upipe_dbg_va(upipe, "flow definition %s", def);
            return;
        }

        if (unlikely(upipe_qsrc->flow_def == NULL)) {
            upipe_throw_flow_def_error(upipe, uref);
            uref_free(uref);
            return;
        }

        upipe_qsrc_output(upipe, uref, upump);
    }
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
    upipe_notice_va(upipe, "queue source %p is ready with length %u",
                    upipe, length);
    if (unlikely(upipe_qsrc->upump_mgr == NULL))
        upipe_throw_need_upump_mgr(upipe);
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
        case UPIPE_GET_OUTPUT: {
            struct upipe **output_p = va_arg(args, struct upipe **);
            return upipe_qsrc_get_output(upipe, output_p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_qsrc_set_output(upipe, output);
        }

        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_qsrc_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
            if (upipe_qsrc->upump != NULL)
                upipe_qsrc_set_upump(upipe, NULL);
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
    if (upipe_qsrc->upump_mgr != NULL && upipe_qsrc->upipe_queue.max_length &&
        upipe_qsrc->upump == NULL) {
        struct upump *upump =
            uqueue_upump_alloc_pop(upipe_queue(upipe),
                                   upipe_qsrc->upump_mgr,
                                   upipe_qsrc_worker, upipe);
        if (unlikely(upump == NULL)) {
            upipe_throw_upump_error(upipe);
            return false;
        } 
        upipe_qsrc_set_upump(upipe, upump);
        upump_start(upump);
    }

    return true;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qsrc_free(struct upipe *upipe)
{
    struct upipe_qsrc *upipe_qsrc = upipe_qsrc_from_upipe(upipe);
    upipe_notice_va(upipe, "freeing queue %p", upipe);
    upipe_throw_dead(upipe);

    upipe_qsrc_clean_upump_mgr(upipe);
    upipe_qsrc_clean_output(upipe);

    struct uqueue *uqueue = upipe_queue(upipe);
    struct uchain *uchain;
    while ((uchain = uqueue_pop(uqueue)) != NULL) {
        struct uref *uref = uref_from_uchain(uchain);
        uref_free(uref);
    }
    uqueue_clean(uqueue);
    free(upipe_qsrc->uqueue_extra);

    upipe_clean(upipe);
    free(upipe_qsrc);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_qsrc_mgr = {
    .signature = UPIPE_QSRC_SIGNATURE,

    .upipe_alloc = _upipe_qsrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_qsrc_control,
    .upipe_free = upipe_qsrc_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all queue source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_qsrc_mgr_alloc(void)
{
    return &upipe_qsrc_mgr;
}
