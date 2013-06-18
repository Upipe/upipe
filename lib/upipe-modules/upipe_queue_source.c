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

    /** structure exported to the sinks */
    struct upipe_queue upipe_queue;

    /** extra data for the queue structure */
    uint8_t uqueue_extra[];
};

UPIPE_HELPER_UPIPE(upipe_qsrc, upipe_queue.upipe)

UPIPE_HELPER_OUTPUT(upipe_qsrc, output, flow_def, flow_def_sent)

UPIPE_HELPER_UPUMP_MGR(upipe_qsrc, upump_mgr, upump)

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
        return NULL;
    unsigned int length = va_arg(args, unsigned int);
    if (!length || length > UINT8_MAX)
        return NULL;

    struct upipe_qsrc *upipe_qsrc = malloc(sizeof(struct upipe_qsrc) +
                                           uqueue_sizeof(length));
    if (unlikely(upipe_qsrc == NULL))
        return NULL;

    struct upipe *upipe = upipe_qsrc_to_upipe(upipe_qsrc);
    upipe_init(upipe, mgr, uprobe);
    if (unlikely(!uqueue_init(upipe_queue(upipe), length,
                              upipe_qsrc->uqueue_extra))) {
        free(upipe_qsrc);
        return NULL;
    }

    upipe_qsrc_init_output(upipe);
    upipe_qsrc_init_upump_mgr(upipe);
    upipe_qsrc->flow_def = NULL;
    upipe_qsrc->upipe_queue.max_length = length;
    upipe_throw_ready(upipe);

    upipe_throw_need_upump_mgr(upipe);
    return upipe;
}

/** @internal @This reads data from the queue and outputs it.
 *
 * @param upump description structure of the read watcher
 */
static void upipe_qsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);

    struct uchain *uchain = uqueue_pop(upipe_queue(upipe));
    if (likely(uchain != NULL)) {
        struct uref *uref = uref_from_uchain(uchain);
        if (unlikely(uref_flow_get_end(uref))) {
            uref_free(uref);
            upipe_throw_source_end(upipe);
            return;
        }

        const char *def;
        if (unlikely(uref_flow_get_def(uref, &def))) {
            upipe_qsrc_store_flow_def(upipe, uref);
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
