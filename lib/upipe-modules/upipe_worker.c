/*
 * Copyright (C) 2014-2017 OpenHeadend S.A.R.L.
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
 * @short Bin pipe wrapping a queue, a linear pipe and a queue
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_transfer.h>
#include <upipe-modules/upipe_worker_linear.h>
#include <upipe-modules/upipe_worker_source.h>
#include <upipe-modules/upipe_worker_sink.h>
#include <upipe-modules/upipe_worker.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a worker manager. */
struct upipe_work_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pointer to queue source manager */
    struct upipe_mgr *qsrc_mgr;
    /** pointer to queue sink manager */
    struct upipe_mgr *qsink_mgr;
    /** pointer to xfer manager */
    struct upipe_mgr *xfer_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_work_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_work_mgr, urefcount, urefcount, urefcount)

/** @internal @This is the private context of a worker pipe. */
struct upipe_work {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** list of input bin requests */
    struct uchain input_request_list;
    /** list of output bin requests */
    struct uchain output_request_list;
    /** proxy probe */
    struct uprobe proxy_probe;
    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;
    /** probe for input queue source */
    struct uprobe in_qsrc_probe;
    /** probe for output queue source */
    struct uprobe out_qsrc_probe;

    /** input queue sink (first inner pipe of the bin) */
    struct upipe *in_qsink;
    /** output queue source (last inner pipe of the bin) */
    struct upipe *out_qsrc;
    /** first remote pipe */
    struct upipe *first_remote_xfer;
    /** last remote pipe */
    struct upipe *last_remote_xfer;
    /** output */
    struct upipe *output;

    /** list of inner pipes that may require @ref upipe_attach_upump_mgr */
    struct uchain upump_mgr_pipes;

    /** true if @ref upipe_bin_freeze has been called */
    bool frozen;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_work, upipe, UPIPE_WORK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_work, urefcount, upipe_work_no_ref)
UPIPE_HELPER_UREFCOUNT_REAL(upipe_work, urefcount_real, upipe_work_free)
UPIPE_HELPER_INNER(upipe_work, in_qsink)
UPIPE_HELPER_BIN_INPUT(upipe_work, in_qsink, input_request_list)
UPIPE_HELPER_INNER(upipe_work, out_qsrc)
UPIPE_HELPER_UPROBE(upipe_work, urefcount_real, proxy_probe, NULL)
UPIPE_HELPER_UPROBE(upipe_work, urefcount_real, last_inner_probe, NULL)
UPIPE_HELPER_BIN_OUTPUT(upipe_work, out_qsrc, output, output_request_list)

/** @internal @This catches events coming from an input queue source pipe.
 *
 * @param uprobe pointer to the probe in upipe_work_alloc
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_work_in_qsrc_probe(struct uprobe *uprobe, struct upipe *inner,
                                    int event, va_list args)
{
    if (event == UPROBE_SOURCE_END)
        return UBASE_ERR_NONE;
    return uprobe_throw_next(uprobe, inner, event, args);
}

/** @internal @This catches events coming from an output queue source pipe.
 *
 * @param uprobe pointer to the probe in upipe_work_alloc
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_work_out_qsrc_probe(struct uprobe *uprobe, struct upipe *inner,
                                     int event, va_list args)
{
    if (event == UPROBE_SOURCE_END)
        return UBASE_ERR_NONE;
    return uprobe_throw_next(uprobe, inner, event, args);
}

/** @internal @This allocates a worker pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_work_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe_work_mgr *work_mgr = upipe_work_mgr_from_upipe_mgr(mgr);
    struct upipe *upipe = NULL;
    struct upipe *remote = NULL;
    struct upipe *last_remote = NULL;
    struct upipe *out_qsink = NULL;
    struct uprobe *uprobe_remote = NULL;
    unsigned int in_queue_length = 0;
    unsigned int out_queue_length = 0;

    switch (signature) {
        case UPIPE_WSRC_SIGNATURE:
            remote = va_arg(args, struct upipe *);
            uprobe_remote = va_arg(args, struct uprobe *);
            out_queue_length = va_arg(args, unsigned int);
            break;

        case UPIPE_WSINK_SIGNATURE:
            remote = va_arg(args, struct upipe *);
            uprobe_remote = va_arg(args, struct uprobe *);
            in_queue_length = va_arg(args, unsigned int);
            break;

        case UPIPE_WLIN_SIGNATURE:
        case UPIPE_WORK_SIGNATURE:
            remote = va_arg(args, struct upipe *);
            uprobe_remote = va_arg(args, struct uprobe *);
            in_queue_length = va_arg(args, unsigned int);
            out_queue_length = va_arg(args, unsigned int);
            break;
    }

    if (unlikely(!work_mgr->xfer_mgr))
        goto error;

    if (unlikely(remote == NULL))
        goto error;

    struct upipe_work *upipe_work = malloc(sizeof(struct upipe_work));
    if (unlikely(upipe_work == NULL))
        goto error;

    upipe = upipe_work_to_upipe(upipe_work);
    upipe_init(upipe, mgr, uprobe_use(uprobe));
    upipe_work_init_urefcount(upipe);
    upipe_work_init_urefcount_real(upipe);
    upipe_work_init_proxy_probe(upipe);
    upipe_work_init_last_inner_probe(upipe);
    upipe_work_init_bin_input(upipe);
    upipe_work_init_bin_output(upipe);
    ulist_init(&upipe_work->upump_mgr_pipes);

    uprobe_init(&upipe_work->in_qsrc_probe, upipe_work_in_qsrc_probe,
                uprobe_use(uprobe_remote));
    upipe_work->in_qsrc_probe.refcount =
        upipe_work_to_urefcount_real(upipe_work);
    uprobe_init(&upipe_work->out_qsrc_probe, upipe_work_out_qsrc_probe,
                &upipe_work->last_inner_probe);
    upipe_work->out_qsrc_probe.refcount =
        upipe_work_to_urefcount_real(upipe_work);
    upipe_work->frozen = false;
    upipe_throw_ready(upipe);

    /* last remote */
    struct upipe *tmp;
    int ret;
    last_remote = upipe_use(remote);

    /* upipe_get_output is a control command and may trigger a need_upump_mgr
     * event */
    uprobe_throw(upipe->uprobe, NULL, UPROBE_FREEZE_UPUMP_MGR);
    while (ubase_check((ret = upipe_get_output(last_remote, &tmp)))
           && tmp != NULL) {
        upipe_use(tmp);
        upipe_release(last_remote);
        last_remote = tmp;
    }
    uprobe_throw(upipe->uprobe, NULL, UPROBE_THAW_UPUMP_MGR);

    if (signature == UPIPE_WSRC_SIGNATURE ||
        signature == UPIPE_WLIN_SIGNATURE ||
        /* automatic detection */
        (signature == UPIPE_WORK_SIGNATURE && ubase_check(ret))) {

        /* output queue */
        assert(out_queue_length);

        struct upipe *out_qsrc = upipe_qsrc_alloc(work_mgr->qsrc_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_work->out_qsrc_probe),
                                 UPROBE_LOG_VERBOSE, "out_qsrc"),
                out_queue_length > UINT8_MAX ? UINT8_MAX : out_queue_length);
        if (unlikely(out_qsrc == NULL))
            goto error;

        out_qsink = upipe_qsink_alloc(work_mgr->qsink_mgr,
                uprobe_pfx_alloc(uprobe_use(uprobe_remote), UPROBE_LOG_VERBOSE,
                                 "out_qsink"),
                out_qsrc);
        if (unlikely(out_qsink == NULL)) {
            upipe_release(out_qsrc);
            goto error;
        }
        if (out_queue_length > UINT8_MAX)
            upipe_set_max_length(out_qsink, out_queue_length - UINT8_MAX);

        upipe_attach_upump_mgr(out_qsrc);
        ulist_add(&upipe_work->upump_mgr_pipes, upipe_to_uchain(out_qsrc));
        upipe_work_store_bin_output(upipe, upipe_use(out_qsrc));
    }

    struct upipe *last_remote_xfer = upipe_xfer_alloc(work_mgr->xfer_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_work->proxy_probe),
                             UPROBE_LOG_VERBOSE, "lin_last_xfer"),
            upipe_use(last_remote));
    if (unlikely(last_remote_xfer == NULL))
        goto error;
    upipe_attach_upump_mgr(last_remote_xfer);
    upipe_work->last_remote_xfer = upipe_use(last_remote_xfer);
    ulist_add(&upipe_work->upump_mgr_pipes, upipe_to_uchain(last_remote_xfer));
    if (out_qsink)
        upipe_set_output(last_remote_xfer, out_qsink);

    /* remote */
    if (last_remote != remote) {
        struct upipe *remote_xfer = upipe_xfer_alloc(work_mgr->xfer_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_work->proxy_probe),
                                 UPROBE_LOG_VERBOSE, "lin_xfer"),
                upipe_use(remote));
        if (unlikely(remote_xfer == NULL))
            goto error;
        upipe_attach_upump_mgr(remote_xfer);
        upipe_work->first_remote_xfer = upipe_use(remote_xfer);
        ulist_add(&upipe_work->upump_mgr_pipes, upipe_to_uchain(remote_xfer));
    } else
        upipe_work->first_remote_xfer = upipe_use(upipe_work->last_remote_xfer);

    if (signature == UPIPE_WSINK_SIGNATURE ||
        signature == UPIPE_WLIN_SIGNATURE ||
        /* automatic detection */
        (signature == UPIPE_WORK_SIGNATURE && remote->mgr->upipe_input)) {

        /* input queue */
        assert(in_queue_length);

        struct upipe *in_qsrc = upipe_qsrc_alloc(work_mgr->qsrc_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_work->in_qsrc_probe),
                    UPROBE_LOG_VERBOSE, "in_qsrc"),
                in_queue_length > UINT8_MAX ? UINT8_MAX : in_queue_length);
        if (unlikely(in_qsrc == NULL))
            goto error;

        struct upipe *in_qsink = upipe_qsink_alloc(work_mgr->qsink_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_work->proxy_probe),
                                 UPROBE_LOG_VERBOSE, "in_qsink"),
                in_qsrc);
        if (unlikely(in_qsink == NULL)) {
            upipe_release(in_qsrc);
            goto error;
        }
        upipe_work_store_bin_input(upipe, in_qsink);
        if (in_queue_length > UINT8_MAX)
            upipe_set_max_length(upipe_work->in_qsink,
                                      in_queue_length - UINT8_MAX);

        struct upipe *in_qsrc_xfer = upipe_xfer_alloc(work_mgr->xfer_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_work->proxy_probe),
                                 UPROBE_LOG_VERBOSE, "in_qsrc_xfer"),
                in_qsrc);
        if (unlikely(in_qsrc_xfer == NULL))
            goto error;
        upipe_set_output(in_qsrc_xfer, remote);
        upipe_attach_upump_mgr(in_qsrc_xfer);
        ulist_add(&upipe_work->upump_mgr_pipes, upipe_to_uchain(in_qsrc_xfer));
    }

    upipe_release(last_remote);
    upipe_release(out_qsink);
    upipe_release(remote);
    uprobe_release(uprobe_remote);
    uprobe_release(uprobe);
    return upipe;

error:
    upipe_release(last_remote);
    upipe_release(out_qsink);
    upipe_release(upipe);
    upipe_release(remote);
    uprobe_release(uprobe_remote);
    uprobe_release(uprobe);
    return NULL;
}

/** @internal @This freezes the inner pipes.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_work_freeze(struct upipe *upipe)
{
    struct upipe_work *upipe_work = upipe_work_from_upipe(upipe);
    if (upipe_work->frozen)
        return UBASE_ERR_NONE;

    struct upipe_work_mgr *work_mgr = upipe_work_mgr_from_upipe_mgr(upipe->mgr);
    UBASE_RETURN(upipe_xfer_mgr_freeze(work_mgr->xfer_mgr));
    upipe_work->frozen = true;
    return UBASE_ERR_NONE;
}

/** @internal @This thaws the inner pipes.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_work_thaw(struct upipe *upipe)
{
    struct upipe_work *upipe_work = upipe_work_from_upipe(upipe);
    if (!upipe_work->frozen)
        return UBASE_ERR_NONE;

    struct upipe_work_mgr *work_mgr = upipe_work_mgr_from_upipe_mgr(upipe->mgr);
    UBASE_RETURN(upipe_xfer_mgr_thaw(work_mgr->xfer_mgr));
    upipe_work->frozen = false;
    return UBASE_ERR_NONE;
}

/** @internal @This gets the first inner pipe of the bin.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_work_get_first_inner(struct upipe *upipe, struct upipe **p)
{
    struct upipe_work *upipe_work = upipe_work_from_upipe(upipe);
    if (!upipe_work->frozen)
        return UBASE_ERR_BUSY;

    return upipe_xfer_get_remote(upipe_work->first_remote_xfer, p);
}

/** @internal @This gets the last inner pipe of the bin.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_work_get_last_inner(struct upipe *upipe, struct upipe **p)
{
    struct upipe_work *upipe_work = upipe_work_from_upipe(upipe);
    if (!upipe_work->frozen)
        return UBASE_ERR_BUSY;

    return upipe_xfer_get_remote(upipe_work->last_remote_xfer, p);
}

/** @internal @This processes control commands on a worker pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_work_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            struct upipe_work *upipe_work = upipe_work_from_upipe(upipe);
            struct uchain *uchain;
            ulist_foreach (&upipe_work->upump_mgr_pipes, uchain) {
                struct upipe *upump_mgr_pipe = upipe_from_uchain(uchain);
                upipe_attach_upump_mgr(upump_mgr_pipe);
            }
            return UBASE_ERR_NONE;
        }
        case UPIPE_BIN_FREEZE:
            return upipe_work_freeze(upipe);
        case UPIPE_BIN_THAW:
            return upipe_work_thaw(upipe);
        case UPIPE_BIN_GET_FIRST_INNER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_work_get_first_inner(upipe, p);
        }
        case UPIPE_BIN_GET_LAST_INNER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_work_get_last_inner(upipe, p);
        }
        default:
            break;
    }

    int err = upipe_work_control_bin_input(upipe, command, args);
    if (err == UBASE_ERR_UNHANDLED)
        return upipe_work_control_bin_output(upipe, command, args);
    return err;
}

/** @This frees a upipe.
 *
 * @param upipe pipe to free
 */
static void upipe_work_free(struct upipe *upipe)
{
    struct upipe_work *upipe_work = upipe_work_from_upipe(upipe);

    upipe_throw_dead(upipe);
    upipe_work_clean_proxy_probe(upipe);
    upipe_work_clean_last_inner_probe(upipe);
    uprobe_clean(&upipe_work->in_qsrc_probe);
    uprobe_clean(&upipe_work->out_qsrc_probe);
    upipe_work_clean_urefcount_real(upipe);
    upipe_work_clean_urefcount(upipe);
    upipe_clean(upipe);
    free(upipe_work);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_work_no_ref(struct upipe *upipe)
{
    struct upipe_work *upipe_work = upipe_work_from_upipe(upipe);
    upipe_work_clean_bin_input(upipe);
    upipe_work_clean_bin_output(upipe);
    upipe_release(upipe_work->first_remote_xfer);
    upipe_release(upipe_work->last_remote_xfer);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_work->upump_mgr_pipes, uchain, uchain_tmp) {
        struct upipe *upump_mgr_pipe = upipe_from_uchain(uchain);
        ulist_delete(uchain);
        upipe_release(upump_mgr_pipe);
    }

    upipe_work_release_urefcount_real(upipe);
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_work_mgr_free(struct urefcount *urefcount)
{
    struct upipe_work_mgr *work_mgr =
        upipe_work_mgr_from_urefcount(urefcount);
    upipe_mgr_release(work_mgr->qsrc_mgr);
    upipe_mgr_release(work_mgr->qsink_mgr);
    upipe_mgr_release(work_mgr->xfer_mgr);

    urefcount_clean(urefcount);
    free(work_mgr);
}

/** @This processes control commands on a worker manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_work_mgr_control(struct upipe_mgr *mgr,
                                  int command, va_list args)
{
    struct upipe_work_mgr *work_mgr = upipe_work_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_WORK_MGR_GET_##NAME##_MGR: {                             \
            uint32_t signature = va_arg(args, uint32_t);                    \
            if (signature != UPIPE_WSRC_SIGNATURE &&                        \
                signature != UPIPE_WSINK_SIGNATURE &&                       \
                signature != UPIPE_WLIN_SIGNATURE &&                        \
                signature != UPIPE_WORK_SIGNATURE)                          \
                return UBASE_ERR_INVALID;                                   \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = work_mgr->name##_mgr;                                      \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_WORK_MGR_SET_##NAME##_MGR: {                             \
            uint32_t signature = va_arg(args, uint32_t);                    \
            if (signature != UPIPE_WSRC_SIGNATURE &&                        \
                signature != UPIPE_WSINK_SIGNATURE &&                       \
                signature != UPIPE_WLIN_SIGNATURE &&                        \
                signature != UPIPE_WORK_SIGNATURE)                          \
                return UBASE_ERR_INVALID;                                   \
            if (!urefcount_single(&work_mgr->urefcount))                    \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(work_mgr->name##_mgr);                        \
            work_mgr->name##_mgr = upipe_mgr_use(m);                        \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(qsrc, QSRC)
        GET_SET_MGR(qsink, QSINK)
        GET_SET_MGR(xfer, XFER)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all worker pipes.
 *
 * @param xfer_mgr manager to transfer pipes to the remote thread
 * @return pointer to manager
 */
struct upipe_mgr *upipe_work_mgr_alloc(struct upipe_mgr *xfer_mgr)
{
    assert(xfer_mgr != NULL);
    struct upipe_work_mgr *work_mgr =
        malloc(sizeof(struct upipe_work_mgr));
    if (unlikely(work_mgr == NULL))
        return NULL;

    memset(work_mgr, 0, sizeof(*work_mgr));
    work_mgr->qsrc_mgr = upipe_qsrc_mgr_alloc();
    work_mgr->qsink_mgr = upipe_qsink_mgr_alloc();
    work_mgr->xfer_mgr = upipe_mgr_use(xfer_mgr);

    urefcount_init(upipe_work_mgr_to_urefcount(work_mgr),
                   upipe_work_mgr_free);
    work_mgr->mgr.refcount = upipe_work_mgr_to_urefcount(work_mgr);
    work_mgr->mgr.signature = UPIPE_WORK_SIGNATURE;
    work_mgr->mgr.upipe_alloc = _upipe_work_alloc;
    work_mgr->mgr.upipe_input = upipe_work_bin_input;
    work_mgr->mgr.upipe_control = upipe_work_control;
    work_mgr->mgr.upipe_mgr_control = upipe_work_mgr_control;
    return upipe_work_mgr_to_upipe_mgr(work_mgr);
}
