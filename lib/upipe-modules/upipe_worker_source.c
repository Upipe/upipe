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
 * @short Bin pipe wrapping a source and a queue
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe-modules/upipe_worker_source.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_transfer.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a wsrc manager. */
struct upipe_wsrc_mgr {
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

UBASE_FROM_TO(upipe_wsrc_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_wsrc_mgr, urefcount, urefcount, urefcount)

/** @internal @This is the private context of a wsrc pipe. */
struct upipe_wsrc {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** proxy probe */
    struct uprobe proxy_probe;
    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;
    /** probe for queue source */
    struct uprobe qsrc_probe;

    /** queue source (last inner pipe of the bin) */
    struct upipe *out_qsrc;
    /** list of output bin requests */
    struct uchain output_request_list;
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

UPIPE_HELPER_UPIPE(upipe_wsrc, upipe, UPIPE_WSRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_wsrc, urefcount, upipe_wsrc_no_ref)
UPIPE_HELPER_INNER(upipe_wsrc, out_qsrc)
UPIPE_HELPER_UPROBE(upipe_wsrc, urefcount_real, last_inner_probe, NULL)
UPIPE_HELPER_BIN_OUTPUT(upipe_wsrc, out_qsrc, output, output_request_list)

UBASE_FROM_TO(upipe_wsrc, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_wsrc_free(struct urefcount *urefcount_real);

/** @internal @This catches events coming from an inner pipe, and
 * attaches them to the bin pipe.
 *
 * @param uprobe pointer to the probe in upipe_wsrc_alloc
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_wsrc_proxy_probe(struct uprobe *uprobe, struct upipe *inner,
                                  int event, va_list args)
{
    struct upipe_wsrc *s = container_of(uprobe, struct upipe_wsrc, proxy_probe);
    struct upipe *upipe = upipe_wsrc_to_upipe(s);
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This catches events coming from a queue source pipe.
 *
 * @param uprobe pointer to the probe in upipe_wsrc_alloc
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_wsrc_qsrc_probe(struct uprobe *uprobe, struct upipe *inner,
                                 int event, va_list args)
{
    if (event == UPROBE_SOURCE_END)
        return UBASE_ERR_NONE;
    return uprobe_throw_next(uprobe, inner, event, args);
}

/** @internal @This allocates a wsrc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_wsrc_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe_wsrc_mgr *wsrc_mgr = upipe_wsrc_mgr_from_upipe_mgr(mgr);
    if (unlikely(signature != UPIPE_WSRC_SIGNATURE ||
                 wsrc_mgr->xfer_mgr == NULL))
        goto upipe_wsrc_alloc_err;
    struct upipe *remote = va_arg(args, struct upipe *);
    struct uprobe *uprobe_remote = va_arg(args, struct uprobe *);
    unsigned int queue_length = va_arg(args, unsigned int);
    assert(queue_length);
    if (unlikely(remote == NULL))
        goto upipe_wsrc_alloc_err2;

    struct upipe_wsrc *upipe_wsrc = malloc(sizeof(struct upipe_wsrc));
    if (unlikely(upipe_wsrc == NULL))
        goto upipe_wsrc_alloc_err2;

    struct upipe *upipe = upipe_wsrc_to_upipe(upipe_wsrc);
    upipe_init(upipe, mgr, uprobe);
    upipe_wsrc_init_urefcount(upipe);
    urefcount_init(upipe_wsrc_to_urefcount_real(upipe_wsrc), upipe_wsrc_free);
    upipe_wsrc_init_last_inner_probe(upipe);
    upipe_wsrc_init_bin_output(upipe);
    ulist_init(&upipe_wsrc->upump_mgr_pipes);

    uprobe_init(&upipe_wsrc->proxy_probe, upipe_wsrc_proxy_probe, NULL);
    upipe_wsrc->proxy_probe.refcount = upipe_wsrc_to_urefcount_real(upipe_wsrc);
    uprobe_init(&upipe_wsrc->qsrc_probe, upipe_wsrc_qsrc_probe,
                &upipe_wsrc->last_inner_probe);
    upipe_wsrc->qsrc_probe.refcount = upipe_wsrc_to_urefcount_real(upipe_wsrc);
    upipe_wsrc->frozen = false;
    upipe_throw_ready(upipe);

    /* output queue */
    struct upipe *out_qsrc = upipe_qsrc_alloc(wsrc_mgr->qsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_wsrc->qsrc_probe),
                             UPROBE_LOG_VERBOSE, "out_qsrc"),
            queue_length > UINT8_MAX ? UINT8_MAX : queue_length);
    if (unlikely(out_qsrc == NULL))
        goto upipe_wsrc_alloc_err3;

    struct upipe *out_qsink = upipe_qsink_alloc(wsrc_mgr->qsink_mgr,
            uprobe_pfx_alloc(uprobe_remote, UPROBE_LOG_VERBOSE,
                             "out_qsink"),
            out_qsrc);
    if (unlikely(out_qsink == NULL)) {
        upipe_release(out_qsrc);
        goto upipe_wsrc_alloc_err3;
    }
    if (queue_length > UINT8_MAX)
        upipe_set_max_length(out_qsink, queue_length - UINT8_MAX);

    upipe_attach_upump_mgr(out_qsrc);
    ulist_add(&upipe_wsrc->upump_mgr_pipes, upipe_to_uchain(out_qsrc));
    upipe_wsrc_store_bin_output(upipe, upipe_use(out_qsrc));

    /* last remote */
    struct upipe *last_remote = upipe_use(remote);
    struct upipe *tmp;

    /* upipe_get_output is a control command and may trigger a need_upump_mgr
     * event */
    uprobe_throw(upipe->uprobe, NULL, UPROBE_FREEZE_UPUMP_MGR);
    while (ubase_check(upipe_get_output(last_remote, &tmp)) && tmp != NULL) {
        upipe_use(tmp);
        upipe_release(last_remote);
        last_remote = tmp;
    }
    uprobe_throw(upipe->uprobe, NULL, UPROBE_THAW_UPUMP_MGR);

    struct upipe *last_remote_xfer = upipe_xfer_alloc(wsrc_mgr->xfer_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_wsrc->proxy_probe),
                             UPROBE_LOG_VERBOSE, "src_last_xfer"), last_remote);
    if (unlikely(last_remote_xfer == NULL)) {
        upipe_release(out_qsink);
        goto upipe_wsrc_alloc_err3;
    }
    upipe_attach_upump_mgr(last_remote_xfer);
    upipe_wsrc->last_remote_xfer = upipe_use(last_remote_xfer);
    ulist_add(&upipe_wsrc->upump_mgr_pipes, upipe_to_uchain(last_remote_xfer));
    upipe_set_output(last_remote_xfer, out_qsink);
    upipe_release(out_qsink);

    /* remote */
    if (last_remote != remote) {
        upipe_use(remote);
        struct upipe *remote_xfer = upipe_xfer_alloc(wsrc_mgr->xfer_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_wsrc->proxy_probe),
                                 UPROBE_LOG_VERBOSE, "src_xfer"), remote);
        if (unlikely(remote_xfer == NULL))
            goto upipe_wsrc_alloc_err3;
        upipe_attach_upump_mgr(remote_xfer);
        upipe_wsrc->first_remote_xfer = upipe_use(remote_xfer);
        ulist_add(&upipe_wsrc->upump_mgr_pipes, upipe_to_uchain(remote_xfer));
    } else
        upipe_wsrc->first_remote_xfer = upipe_use(upipe_wsrc->last_remote_xfer);


    upipe_release(remote);
    return upipe;

upipe_wsrc_alloc_err3:
    upipe_release(remote);
    upipe_release(upipe);
    return NULL;

upipe_wsrc_alloc_err2:
    uprobe_release(uprobe_remote);
    upipe_release(remote);
upipe_wsrc_alloc_err:
    uprobe_release(uprobe);
    return NULL;
}

/** @internal @This freezes the inner pipes.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_wsrc_freeze(struct upipe *upipe)
{
    struct upipe_wsrc *upipe_wsrc = upipe_wsrc_from_upipe(upipe);
    if (upipe_wsrc->frozen)
        return UBASE_ERR_NONE;

    struct upipe_wsrc_mgr *wsrc_mgr = upipe_wsrc_mgr_from_upipe_mgr(upipe->mgr);
    UBASE_RETURN(upipe_xfer_mgr_freeze(wsrc_mgr->xfer_mgr));
    upipe_wsrc->frozen = true;
    return UBASE_ERR_NONE;
}

/** @internal @This thaws the inner pipes.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_wsrc_thaw(struct upipe *upipe)
{
    struct upipe_wsrc *upipe_wsrc = upipe_wsrc_from_upipe(upipe);
    if (!upipe_wsrc->frozen)
        return UBASE_ERR_NONE;

    struct upipe_wsrc_mgr *wsrc_mgr = upipe_wsrc_mgr_from_upipe_mgr(upipe->mgr);
    UBASE_RETURN(upipe_xfer_mgr_thaw(wsrc_mgr->xfer_mgr));
    upipe_wsrc->frozen = false;
    return UBASE_ERR_NONE;
}

/** @internal @This gets the first inner pipe of the bin.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_wsrc_get_first_inner(struct upipe *upipe, struct upipe **p)
{
    struct upipe_wsrc *upipe_wsrc = upipe_wsrc_from_upipe(upipe);
    if (!upipe_wsrc->frozen)
        return UBASE_ERR_BUSY;

    return upipe_xfer_get_remote(upipe_wsrc->first_remote_xfer, p);
}

/** @internal @This gets the last inner pipe of the bin.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_wsrc_get_last_inner(struct upipe *upipe, struct upipe **p)
{
    struct upipe_wsrc *upipe_wsrc = upipe_wsrc_from_upipe(upipe);
    if (!upipe_wsrc->frozen)
        return UBASE_ERR_BUSY;

    return upipe_xfer_get_remote(upipe_wsrc->last_remote_xfer, p);
}

/** @internal @This processes control commands on a wsrc pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_wsrc_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            struct upipe_wsrc *upipe_wsrc = upipe_wsrc_from_upipe(upipe);
            struct uchain *uchain;
            ulist_foreach (&upipe_wsrc->upump_mgr_pipes, uchain) {
                struct upipe *upump_mgr_pipe = upipe_from_uchain(uchain);
                upipe_attach_upump_mgr(upump_mgr_pipe);
            }
            return UBASE_ERR_NONE;
        }
        case UPIPE_BIN_FREEZE:
            return upipe_wsrc_freeze(upipe);
        case UPIPE_BIN_THAW:
            return upipe_wsrc_thaw(upipe);
        case UPIPE_BIN_GET_FIRST_INNER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_wsrc_get_first_inner(upipe, p);
        }
        case UPIPE_BIN_GET_LAST_INNER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_wsrc_get_last_inner(upipe, p);
        }
        default:
            break;
    }

    return upipe_wsrc_control_bin_output(upipe, command, args);
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_wsrc_free(struct urefcount *urefcount_real)
{
    struct upipe_wsrc *upipe_wsrc =
        upipe_wsrc_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_wsrc_to_upipe(upipe_wsrc);
    upipe_throw_dead(upipe);
    uprobe_clean(&upipe_wsrc->proxy_probe);
    upipe_wsrc_clean_last_inner_probe(upipe);
    urefcount_clean(urefcount_real);
    upipe_wsrc_clean_urefcount(upipe);
    upipe_clean(upipe);
    free(upipe_wsrc);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_wsrc_no_ref(struct upipe *upipe)
{
    struct upipe_wsrc *upipe_wsrc = upipe_wsrc_from_upipe(upipe);
    upipe_wsrc_clean_bin_output(upipe);
    upipe_release(upipe_wsrc->first_remote_xfer);
    upipe_release(upipe_wsrc->last_remote_xfer);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_wsrc->upump_mgr_pipes, uchain, uchain_tmp) {
        struct upipe *upump_mgr_pipe = upipe_from_uchain(uchain);
        ulist_delete(uchain);
        upipe_release(upump_mgr_pipe);
    }

    urefcount_release(upipe_wsrc_to_urefcount_real(upipe_wsrc));
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_wsrc_mgr_free(struct urefcount *urefcount)
{
    struct upipe_wsrc_mgr *wsrc_mgr =
        upipe_wsrc_mgr_from_urefcount(urefcount);
    upipe_mgr_release(wsrc_mgr->qsrc_mgr);
    upipe_mgr_release(wsrc_mgr->qsink_mgr);
    upipe_mgr_release(wsrc_mgr->xfer_mgr);

    urefcount_clean(urefcount);
    free(wsrc_mgr);
}

/** @This processes control commands on a wsrc manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_wsrc_mgr_control(struct upipe_mgr *mgr,
                                  int command, va_list args)
{
    struct upipe_wsrc_mgr *wsrc_mgr = upipe_wsrc_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_WSRC_MGR_GET_##NAME##_MGR: {                             \
            UBASE_SIGNATURE_CHECK(args, UPIPE_WSRC_SIGNATURE)               \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = wsrc_mgr->name##_mgr;                                      \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_WSRC_MGR_SET_##NAME##_MGR: {                             \
            UBASE_SIGNATURE_CHECK(args, UPIPE_WSRC_SIGNATURE)               \
            if (!urefcount_single(&wsrc_mgr->urefcount))                    \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(wsrc_mgr->name##_mgr);                        \
            wsrc_mgr->name##_mgr = upipe_mgr_use(m);                        \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(qsrc, QSRC)
        GET_SET_MGR(qsink, QSINK)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all wsrc pipes.
 *
 * @param xfer_mgr manager to transfer pipes to the remote thread
 * @return pointer to manager
 */
struct upipe_mgr *upipe_wsrc_mgr_alloc(struct upipe_mgr *xfer_mgr)
{
    assert(xfer_mgr != NULL);
    struct upipe_wsrc_mgr *wsrc_mgr =
        malloc(sizeof(struct upipe_wsrc_mgr));
    if (unlikely(wsrc_mgr == NULL))
        return NULL;

    memset(wsrc_mgr, 0, sizeof(*wsrc_mgr));
    wsrc_mgr->qsrc_mgr = upipe_qsrc_mgr_alloc();
    wsrc_mgr->qsink_mgr = upipe_qsink_mgr_alloc();
    wsrc_mgr->xfer_mgr = upipe_mgr_use(xfer_mgr);

    urefcount_init(upipe_wsrc_mgr_to_urefcount(wsrc_mgr),
                   upipe_wsrc_mgr_free);
    wsrc_mgr->mgr.refcount = upipe_wsrc_mgr_to_urefcount(wsrc_mgr);
    wsrc_mgr->mgr.signature = UPIPE_WSRC_SIGNATURE;
    wsrc_mgr->mgr.upipe_alloc = _upipe_wsrc_alloc;
    wsrc_mgr->mgr.upipe_input = NULL;
    wsrc_mgr->mgr.upipe_control = upipe_wsrc_control;
    wsrc_mgr->mgr.upipe_mgr_control = upipe_wsrc_mgr_control;
    return upipe_wsrc_mgr_to_upipe_mgr(wsrc_mgr);
}

