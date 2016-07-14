/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
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
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe-modules/upipe_worker_linear.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_transfer.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a wlin manager. */
struct upipe_wlin_mgr {
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

UBASE_FROM_TO(upipe_wlin_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_wlin_mgr, urefcount, urefcount, urefcount)

/** @internal @This is the private context of a wlin pipe. */
struct upipe_wlin {
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
    /** output */
    struct upipe *output;

    /** list of inner pipes that may require @ref upipe_attach_upump_mgr */
    struct uchain upump_mgr_pipes;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_wlin, upipe, UPIPE_WLIN_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_wlin, urefcount, upipe_wlin_no_ref)
UPIPE_HELPER_INNER(upipe_wlin, in_qsink)
UPIPE_HELPER_BIN_INPUT(upipe_wlin, in_qsink, input_request_list)
UPIPE_HELPER_INNER(upipe_wlin, out_qsrc)
UPIPE_HELPER_UPROBE(upipe_wlin, urefcount_real, last_inner_probe, NULL)
UPIPE_HELPER_BIN_OUTPUT(upipe_wlin, out_qsrc, output, output_request_list)

UBASE_FROM_TO(upipe_wlin, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_wlin_free(struct urefcount *urefcount_real);

/** @internal @This catches events coming from an inner pipe, and
 * attaches them to the bin pipe.
 *
 * @param uprobe pointer to the probe in upipe_wlin_alloc
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_wlin_proxy_probe(struct uprobe *uprobe, struct upipe *inner,
                                  int event, va_list args)
{
    struct upipe_wlin *s = container_of(uprobe, struct upipe_wlin, proxy_probe);
    struct upipe *upipe = upipe_wlin_to_upipe(s);
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
static int upipe_wlin_in_qsrc_probe(struct uprobe *uprobe, struct upipe *inner,
                                    int event, va_list args)
{
    if (event == UPROBE_SOURCE_END)
        return UBASE_ERR_NONE;
    return uprobe_throw_next(uprobe, inner, event, args);
}

/** @internal @This catches events coming from an output queue source pipe.
 *
 * @param uprobe pointer to the probe in upipe_wlin_alloc
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_wlin_out_qsrc_probe(struct uprobe *uprobe, struct upipe *inner,
                                     int event, va_list args)
{
    if (event == UPROBE_SOURCE_END)
        return UBASE_ERR_NONE;
    return uprobe_throw_next(uprobe, inner, event, args);
}

/** @internal @This allocates a wlin pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_wlin_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe_wlin_mgr *wlin_mgr = upipe_wlin_mgr_from_upipe_mgr(mgr);
    if (unlikely(signature != UPIPE_WLIN_SIGNATURE ||
                 wlin_mgr->xfer_mgr == NULL))
        goto upipe_wlin_alloc_err;
    struct upipe *remote = va_arg(args, struct upipe *);
    struct uprobe *uprobe_remote = va_arg(args, struct uprobe *);
    unsigned int in_queue_length = va_arg(args, unsigned int);
    unsigned int out_queue_length = va_arg(args, unsigned int);
    assert(in_queue_length);
    assert(out_queue_length);
    if (unlikely(remote == NULL))
        goto upipe_wlin_alloc_err2;

    struct upipe_wlin *upipe_wlin = malloc(sizeof(struct upipe_wlin));
    if (unlikely(upipe_wlin == NULL))
        goto upipe_wlin_alloc_err2;

    struct upipe *upipe = upipe_wlin_to_upipe(upipe_wlin);
    upipe_init(upipe, mgr, uprobe);
    upipe_wlin_init_urefcount(upipe);
    urefcount_init(upipe_wlin_to_urefcount_real(upipe_wlin), upipe_wlin_free);
    upipe_wlin_init_last_inner_probe(upipe);
    upipe_wlin_init_bin_input(upipe);
    upipe_wlin_init_bin_output(upipe);
    ulist_init(&upipe_wlin->upump_mgr_pipes);

    uprobe_init(&upipe_wlin->proxy_probe, upipe_wlin_proxy_probe, NULL);
    upipe_wlin->proxy_probe.refcount = upipe_wlin_to_urefcount_real(upipe_wlin);
    uprobe_init(&upipe_wlin->in_qsrc_probe, upipe_wlin_in_qsrc_probe,
                uprobe_use(uprobe_remote));
    upipe_wlin->in_qsrc_probe.refcount =
        upipe_wlin_to_urefcount_real(upipe_wlin);
    uprobe_init(&upipe_wlin->out_qsrc_probe, upipe_wlin_out_qsrc_probe,
                &upipe_wlin->last_inner_probe);
    upipe_wlin->out_qsrc_probe.refcount =
        upipe_wlin_to_urefcount_real(upipe_wlin);
    upipe_throw_ready(upipe);

    /* output queue */
    struct upipe *out_qsrc = upipe_qsrc_alloc(wlin_mgr->qsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_wlin->out_qsrc_probe),
                             UPROBE_LOG_VERBOSE, "out_qsrc"),
            out_queue_length > UINT8_MAX ? UINT8_MAX : out_queue_length);
    if (unlikely(out_qsrc == NULL))
        goto upipe_wlin_alloc_err3;

    struct upipe *out_qsink = upipe_qsink_alloc(wlin_mgr->qsink_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_remote), UPROBE_LOG_VERBOSE,
                             "out_qsink"),
            out_qsrc);
    if (unlikely(out_qsink == NULL)) {
        upipe_release(out_qsrc);
        goto upipe_wlin_alloc_err3;
    }
    if (out_queue_length > UINT8_MAX)
        upipe_set_max_length(out_qsink, out_queue_length - UINT8_MAX);

    upipe_attach_upump_mgr(out_qsrc);
    ulist_add(&upipe_wlin->upump_mgr_pipes, upipe_to_uchain(out_qsrc));
    upipe_wlin_store_bin_output(upipe, upipe_use(out_qsrc));

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

    struct upipe *last_remote_xfer = upipe_xfer_alloc(wlin_mgr->xfer_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_wlin->proxy_probe),
                             UPROBE_LOG_VERBOSE, "lin_last_xfer"), last_remote);
    if (unlikely(last_remote_xfer == NULL)) {
        upipe_release(out_qsink);
        goto upipe_wlin_alloc_err3;
    }
    upipe_attach_upump_mgr(last_remote_xfer);
    ulist_add(&upipe_wlin->upump_mgr_pipes, upipe_to_uchain(last_remote_xfer));
    upipe_set_output(last_remote_xfer, out_qsink);
    upipe_release(out_qsink);

    /* remote */
    struct upipe *remote_xfer;
    if (last_remote != remote) {
        upipe_use(remote);
        remote_xfer = upipe_xfer_alloc(wlin_mgr->xfer_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_wlin->proxy_probe),
                                 UPROBE_LOG_VERBOSE, "lin_xfer"), remote);
        if (unlikely(remote_xfer == NULL)) {
            upipe_release(out_qsink);
            goto upipe_wlin_alloc_err3;
        }
        upipe_attach_upump_mgr(remote_xfer);
        ulist_add(&upipe_wlin->upump_mgr_pipes, upipe_to_uchain(remote_xfer));
    } else {
        remote_xfer = last_remote_xfer;
    }

    /* input queue */
    struct upipe *in_qsrc = upipe_qsrc_alloc(wlin_mgr->qsrc_mgr,
            uprobe_pfx_alloc(
                uprobe_use(&upipe_wlin->in_qsrc_probe),
                UPROBE_LOG_VERBOSE, "in_qsrc"),
            in_queue_length > UINT8_MAX ? UINT8_MAX : in_queue_length);
    if (unlikely(in_qsrc == NULL))
        goto upipe_wlin_alloc_err4;
    uprobe_release(uprobe_remote);

    struct upipe *in_qsink = upipe_qsink_alloc(wlin_mgr->qsink_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_wlin->proxy_probe),
                             UPROBE_LOG_VERBOSE, "in_qsink"),
            in_qsrc);
    if (unlikely(in_qsink == NULL)) {
        upipe_release(in_qsrc);
        goto upipe_wlin_alloc_err4;
    }
    upipe_wlin_store_bin_input(upipe, in_qsink);
    if (in_queue_length > UINT8_MAX)
        upipe_set_max_length(upipe_wlin->in_qsink,
                                  in_queue_length - UINT8_MAX);

    struct upipe *in_qsrc_xfer = upipe_xfer_alloc(wlin_mgr->xfer_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_wlin->proxy_probe),
                             UPROBE_LOG_VERBOSE, "in_qsrc_xfer"),
            in_qsrc);
    if (unlikely(in_qsrc_xfer == NULL))
        goto upipe_wlin_alloc_err4;
    upipe_set_output(in_qsrc_xfer, remote);
    upipe_attach_upump_mgr(in_qsrc_xfer);
    ulist_add(&upipe_wlin->upump_mgr_pipes, upipe_to_uchain(in_qsrc_xfer));
    upipe_release(remote);
    return upipe;

upipe_wlin_alloc_err4:
    upipe_release(remote);
    upipe_release(upipe);
    return NULL;

upipe_wlin_alloc_err3:
    upipe_release(remote);
    upipe_release(upipe);
    return NULL;

upipe_wlin_alloc_err2:
    upipe_release(remote);
upipe_wlin_alloc_err:
    uprobe_release(uprobe);
    return NULL;
}

/** @internal @This processes control commands on a wlin pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_wlin_control(struct upipe *upipe, int command, va_list args)
{
    if (command == UPIPE_ATTACH_UPUMP_MGR) {
        struct upipe_wlin *upipe_wlin = upipe_wlin_from_upipe(upipe);
        struct uchain *uchain;
        ulist_foreach (&upipe_wlin->upump_mgr_pipes, uchain) {
            struct upipe *upump_mgr_pipe = upipe_from_uchain(uchain);
            upipe_attach_upump_mgr(upump_mgr_pipe);
        }
    }

    int err = upipe_wlin_control_bin_input(upipe, command, args);
    if (err == UBASE_ERR_UNHANDLED)
        return upipe_wlin_control_bin_output(upipe, command, args);
    return err;
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_wlin_free(struct urefcount *urefcount_real)
{
    struct upipe_wlin *upipe_wlin =
        upipe_wlin_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_wlin_to_upipe(upipe_wlin);
    upipe_throw_dead(upipe);
    uprobe_clean(&upipe_wlin->proxy_probe);
    upipe_wlin_clean_last_inner_probe(upipe);
    uprobe_clean(&upipe_wlin->in_qsrc_probe);
    uprobe_clean(&upipe_wlin->out_qsrc_probe);
    urefcount_clean(urefcount_real);
    upipe_wlin_clean_urefcount(upipe);
    upipe_clean(upipe);
    free(upipe_wlin);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_wlin_no_ref(struct upipe *upipe)
{
    struct upipe_wlin *upipe_wlin = upipe_wlin_from_upipe(upipe);
    upipe_wlin_clean_bin_input(upipe);
    upipe_wlin_clean_bin_output(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_wlin->upump_mgr_pipes, uchain, uchain_tmp) {
        struct upipe *upump_mgr_pipe = upipe_from_uchain(uchain);
        ulist_delete(uchain);
        upipe_release(upump_mgr_pipe);
    }

    urefcount_release(upipe_wlin_to_urefcount_real(upipe_wlin));
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_wlin_mgr_free(struct urefcount *urefcount)
{
    struct upipe_wlin_mgr *wlin_mgr =
        upipe_wlin_mgr_from_urefcount(urefcount);
    upipe_mgr_release(wlin_mgr->qsrc_mgr);
    upipe_mgr_release(wlin_mgr->qsink_mgr);
    upipe_mgr_release(wlin_mgr->xfer_mgr);

    urefcount_clean(urefcount);
    free(wlin_mgr);
}

/** @This processes control commands on a wlin manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_wlin_mgr_control(struct upipe_mgr *mgr,
                                  int command, va_list args)
{
    struct upipe_wlin_mgr *wlin_mgr = upipe_wlin_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_WLIN_MGR_GET_##NAME##_MGR: {                             \
            UBASE_SIGNATURE_CHECK(args, UPIPE_WLIN_SIGNATURE)               \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = wlin_mgr->name##_mgr;                                      \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_WLIN_MGR_SET_##NAME##_MGR: {                             \
            UBASE_SIGNATURE_CHECK(args, UPIPE_WLIN_SIGNATURE)               \
            if (!urefcount_single(&wlin_mgr->urefcount))                    \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(wlin_mgr->name##_mgr);                        \
            wlin_mgr->name##_mgr = upipe_mgr_use(m);                        \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(qsrc, QSRC)
        GET_SET_MGR(qsink, QSINK)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all wlin pipes.
 *
 * @param xfer_mgr manager to transfer pipes to the remote thread
 * @return pointer to manager
 */
struct upipe_mgr *upipe_wlin_mgr_alloc(struct upipe_mgr *xfer_mgr)
{
    assert(xfer_mgr != NULL);
    struct upipe_wlin_mgr *wlin_mgr =
        malloc(sizeof(struct upipe_wlin_mgr));
    if (unlikely(wlin_mgr == NULL))
        return NULL;

    wlin_mgr->qsrc_mgr = upipe_qsrc_mgr_alloc();
    wlin_mgr->qsink_mgr = upipe_qsink_mgr_alloc();
    wlin_mgr->xfer_mgr = upipe_mgr_use(xfer_mgr);

    urefcount_init(upipe_wlin_mgr_to_urefcount(wlin_mgr),
                   upipe_wlin_mgr_free);
    wlin_mgr->mgr.refcount = upipe_wlin_mgr_to_urefcount(wlin_mgr);
    wlin_mgr->mgr.signature = UPIPE_WLIN_SIGNATURE;
    wlin_mgr->mgr.upipe_alloc = _upipe_wlin_alloc;
    wlin_mgr->mgr.upipe_input = upipe_wlin_bin_input;
    wlin_mgr->mgr.upipe_control = upipe_wlin_control;
    wlin_mgr->mgr.upipe_mgr_control = upipe_wlin_mgr_control;
    return upipe_wlin_mgr_to_upipe_mgr(wlin_mgr);
}

