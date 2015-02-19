/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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
 * @short Bin pipe wrapping a queue and a sink
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe-modules/upipe_worker_sink.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_transfer.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a wsink manager. */
struct upipe_wsink_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pointer to queue source manager */
    struct upipe_mgr *qsrc_mgr;
    /** pointer to queue sink manager */
    struct upipe_mgr *qsink_mgr;
    /** pointer to source xfer manager */
    struct upipe_mgr *xfer_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_wsink_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_wsink_mgr, urefcount, urefcount, urefcount)

/** @internal @This is the private context of a wsink pipe. */
struct upipe_wsink {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** list of bin requests */
    struct uchain request_list;

    /** proxy probe */
    struct uprobe proxy_probe;
    /** probe for input queue source */
    struct uprobe in_qsrc_probe;

    /** input queue sink (first inner pipe of the bin) */
    struct upipe *in_qsink;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_wsink, upipe, UPIPE_WSINK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_wsink, urefcount, upipe_wsink_no_ref)
UPIPE_HELPER_BIN_INPUT(upipe_wsink, in_qsink, request_list)

UBASE_FROM_TO(upipe_wsink, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_wsink_free(struct urefcount *urefcount_real);

/** @internal @This catches events coming from an inner pipe, and
 * attaches them to the bin pipe.
 *
 * @param uprobe pointer to the probe in upipe_wsink_alloc
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_wsink_proxy_probe(struct uprobe *uprobe, struct upipe *inner,
                                   int event, va_list args)
{
    struct upipe_wsink *s = container_of(uprobe, struct upipe_wsink,
                                         proxy_probe);
    struct upipe *upipe = upipe_wsink_to_upipe(s);
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This catches events coming from an input queue source pipe.
 *
 * @param uprobe pointer to the probe in upipe_wlin_alloc
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_wsink_in_qsrc_probe(struct uprobe *uprobe, struct upipe *inner,
                                     int event, va_list args)
{
    if (event == UPROBE_SOURCE_END)
        return UBASE_ERR_NONE;
    return uprobe_throw_next(uprobe, inner, event, args);
}

/** @internal @This allocates a wsink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_wsink_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe_wsink_mgr *wsink_mgr = upipe_wsink_mgr_from_upipe_mgr(mgr);
    if (unlikely(signature != UPIPE_WSINK_SIGNATURE ||
                 wsink_mgr->xfer_mgr == NULL))
        goto upipe_wsink_alloc_err;
    struct upipe *remote = va_arg(args, struct upipe *);
    struct uprobe *uprobe_remote = va_arg(args, struct uprobe *);
    unsigned int queue_length = va_arg(args, unsigned int);
    assert(queue_length);
    if (unlikely(remote == NULL))
        goto upipe_wsink_alloc_err2;

    struct upipe_wsink *upipe_wsink = malloc(sizeof(struct upipe_wsink));
    if (unlikely(upipe_wsink == NULL))
        goto upipe_wsink_alloc_err2;

    struct upipe *upipe = upipe_wsink_to_upipe(upipe_wsink);
    upipe_init(upipe, mgr, uprobe);
    upipe_wsink_init_urefcount(upipe);
    urefcount_init(upipe_wsink_to_urefcount_real(upipe_wsink),
                   upipe_wsink_free);
    upipe_wsink_init_bin_input(upipe);

    uprobe_init(&upipe_wsink->proxy_probe, upipe_wsink_proxy_probe, NULL);
    upipe_wsink->proxy_probe.refcount =
        upipe_wsink_to_urefcount_real(upipe_wsink);
    uprobe_init(&upipe_wsink->in_qsrc_probe, upipe_wsink_in_qsrc_probe,
                uprobe_remote);
    upipe_wsink->in_qsrc_probe.refcount =
        upipe_wsink_to_urefcount_real(upipe_wsink);
    upipe_throw_ready(upipe);

    /* remote */
    upipe_use(remote);
    struct upipe *remote_xfer = upipe_xfer_alloc(wsink_mgr->xfer_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_wsink->proxy_probe),
                             UPROBE_LOG_VERBOSE, "sink_xfer"), remote);
    if (unlikely(remote_xfer == NULL)) {
        upipe_release(remote);
        upipe_release(upipe);
        return NULL;
    }
    upipe_attach_upump_mgr(remote_xfer);

    /* input queue */
    struct upipe *in_qsrc = upipe_qsrc_alloc(wsink_mgr->qsrc_mgr,
            uprobe_pfx_alloc(
                uprobe_use(&upipe_wsink->in_qsrc_probe),
                UPROBE_LOG_VERBOSE, "in_qsrc"),
            queue_length > UINT8_MAX ? UINT8_MAX : queue_length);
    if (unlikely(in_qsrc == NULL))
        goto upipe_wsink_alloc_err3;

    struct upipe *in_qsink = upipe_qsink_alloc(wsink_mgr->qsink_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_wsink->proxy_probe),
                             UPROBE_LOG_VERBOSE, "in_qsink"),
            in_qsrc);
    if (unlikely(in_qsink == NULL))
        goto upipe_wsink_alloc_err3;
    upipe_wsink_store_first_inner(upipe, in_qsink);
    if (queue_length > UINT8_MAX)
        upipe_set_max_length(upipe_wsink->in_qsink, queue_length - UINT8_MAX);

    struct upipe *in_qsrc_xfer = upipe_xfer_alloc(wsink_mgr->xfer_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_wsink->proxy_probe),
                             UPROBE_LOG_VERBOSE, "in_qsrc_xfer"),
            in_qsrc);
    if (unlikely(in_qsrc_xfer == NULL))
        goto upipe_wsink_alloc_err3;
    upipe_set_output(in_qsrc_xfer, remote);
    upipe_attach_upump_mgr(in_qsrc_xfer);
    upipe_release(remote);
    upipe_release(remote_xfer);
    upipe_release(in_qsrc_xfer);
    return upipe;

upipe_wsink_alloc_err3:
    upipe_release(remote);
    upipe_release(remote_xfer);
    upipe_release(upipe);
    return NULL;

upipe_wsink_alloc_err2:
    upipe_release(remote);
upipe_wsink_alloc_err:
    uprobe_release(uprobe);
    return NULL;
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_wsink_free(struct urefcount *urefcount_real)
{
    struct upipe_wsink *upipe_wsink =
        upipe_wsink_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_wsink_to_upipe(upipe_wsink);
    upipe_throw_dead(upipe);
    uprobe_clean(&upipe_wsink->proxy_probe);
    uprobe_clean(&upipe_wsink->in_qsrc_probe);
    urefcount_clean(urefcount_real);
    upipe_wsink_clean_urefcount(upipe);
    upipe_clean(upipe);
    free(upipe_wsink);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_wsink_no_ref(struct upipe *upipe)
{
    struct upipe_wsink *upipe_wsink = upipe_wsink_from_upipe(upipe);
    upipe_wsink_clean_bin_input(upipe);
    urefcount_release(upipe_wsink_to_urefcount_real(upipe_wsink));
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_wsink_mgr_free(struct urefcount *urefcount)
{
    struct upipe_wsink_mgr *wsink_mgr =
        upipe_wsink_mgr_from_urefcount(urefcount);
    upipe_mgr_release(wsink_mgr->qsrc_mgr);
    upipe_mgr_release(wsink_mgr->qsink_mgr);
    upipe_mgr_release(wsink_mgr->xfer_mgr);

    urefcount_clean(urefcount);
    free(wsink_mgr);
}

/** @This processes control commands on a wsink manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_wsink_mgr_control(struct upipe_mgr *mgr,
                                  int command, va_list args)
{
    struct upipe_wsink_mgr *wsink_mgr = upipe_wsink_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_WSINK_MGR_GET_##NAME##_MGR: {                            \
            UBASE_SIGNATURE_CHECK(args, UPIPE_WSINK_SIGNATURE)              \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = wsink_mgr->name##_mgr;                                     \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_WSINK_MGR_SET_##NAME##_MGR: {                            \
            UBASE_SIGNATURE_CHECK(args, UPIPE_WSINK_SIGNATURE)              \
            if (!urefcount_single(&wsink_mgr->urefcount))                   \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(wsink_mgr->name##_mgr);                       \
            wsink_mgr->name##_mgr = upipe_mgr_use(m);                       \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(qsrc, QSRC)
        GET_SET_MGR(qsink, QSINK)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all wsink pipes.
 *
 * @param xfer_mgr manager to transfer pipes to the remote thread
 * @return pointer to manager
 */
struct upipe_mgr *upipe_wsink_mgr_alloc(struct upipe_mgr *xfer_mgr)
{
    assert(xfer_mgr != NULL);
    struct upipe_wsink_mgr *wsink_mgr =
        malloc(sizeof(struct upipe_wsink_mgr));
    if (unlikely(wsink_mgr == NULL))
        return NULL;

    wsink_mgr->qsrc_mgr = upipe_qsrc_mgr_alloc();
    wsink_mgr->qsink_mgr = upipe_qsink_mgr_alloc();
    wsink_mgr->xfer_mgr = upipe_mgr_use(xfer_mgr);

    urefcount_init(upipe_wsink_mgr_to_urefcount(wsink_mgr),
                   upipe_wsink_mgr_free);
    wsink_mgr->mgr.refcount = upipe_wsink_mgr_to_urefcount(wsink_mgr);
    wsink_mgr->mgr.signature = UPIPE_WSINK_SIGNATURE;
    wsink_mgr->mgr.upipe_alloc = _upipe_wsink_alloc;
    wsink_mgr->mgr.upipe_input = upipe_wsink_bin_input;
    wsink_mgr->mgr.upipe_control = upipe_wsink_control_bin_input;
    wsink_mgr->mgr.upipe_mgr_control = upipe_wsink_mgr_control;
    return upipe_wsink_mgr_to_upipe_mgr(wsink_mgr);
}

