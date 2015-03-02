/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short Bin pipe decapsulating RTP packets from a UDP source
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe-modules/upipe_rtp_source.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_rtp_decaps.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a rtpsrc manager. */
struct upipe_rtpsrc_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pointer to udp source manager */
    struct upipe_mgr *udpsrc_mgr;
    /** pointer to rtp decaps manager */
    struct upipe_mgr *rtpd_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_rtpsrc_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_rtpsrc_mgr, urefcount, urefcount, urefcount)

/** @internal @This is the private context of a rtpsrc pipe. */
struct upipe_rtpsrc {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** proxy probe */
    struct uprobe proxy_probe;
    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;

    /** source inner pipe */
    struct upipe *source;
    /** last inner pipe of the bin (rtpd) */
    struct upipe *last_inner;
    /** list of output bin requests */
    struct uchain output_request_list;
    /** output */
    struct upipe *output;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtpsrc, upipe, UPIPE_RTPSRC_SIGNATURE)
UPIPE_HELPER_VOID(upipe_rtpsrc)
UPIPE_HELPER_UREFCOUNT(upipe_rtpsrc, urefcount, upipe_rtpsrc_no_ref)
UPIPE_HELPER_BIN_OUTPUT(upipe_rtpsrc, last_inner_probe, last_inner, output,
                        output_request_list)

UBASE_FROM_TO(upipe_rtpsrc, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_rtpsrc_free(struct urefcount *urefcount_real);

/** @internal @This catches events coming from an inner pipe, and
 * attaches them to the bin pipe.
 *
 * @param uprobe pointer to the probe in upipe_rtpsrc_alloc
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_rtpsrc_proxy_probe(struct uprobe *uprobe, struct upipe *inner,
                                    int event, va_list args)
{
    struct upipe_rtpsrc *s = container_of(uprobe, struct upipe_rtpsrc,
                                          proxy_probe);
    struct upipe *upipe = upipe_rtpsrc_to_upipe(s);
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This allocates a rtpsrc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtpsrc_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_rtpsrc_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_rtpsrc *upipe_rtpsrc = upipe_rtpsrc_from_upipe(upipe);
    upipe_rtpsrc_init_urefcount(upipe);
    urefcount_init(upipe_rtpsrc_to_urefcount_real(upipe_rtpsrc),
                   upipe_rtpsrc_free);
    upipe_rtpsrc_init_bin_output(upipe,
            upipe_rtpsrc_to_urefcount_real(upipe_rtpsrc));
    upipe_rtpsrc->source = NULL;

    uprobe_init(&upipe_rtpsrc->proxy_probe, upipe_rtpsrc_proxy_probe, NULL);
    upipe_rtpsrc->proxy_probe.refcount =
        upipe_rtpsrc_to_urefcount_real(upipe_rtpsrc);
    upipe_throw_ready(upipe);

    struct upipe_rtpsrc_mgr *rtpsrc_mgr =
        upipe_rtpsrc_mgr_from_upipe_mgr(upipe->mgr);
    upipe_rtpsrc->source = upipe_void_alloc(rtpsrc_mgr->udpsrc_mgr,
            uprobe_pfx_alloc(
                uprobe_use(&upipe_rtpsrc->proxy_probe),
                UPROBE_LOG_VERBOSE, "udpsrc"));
    if (unlikely(upipe_rtpsrc->source == NULL))
        goto upipe_rtpsrc_alloc_err;

    struct upipe *rtpd = upipe_void_alloc_output(upipe_rtpsrc->source,
            rtpsrc_mgr->rtpd_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_rtpsrc->last_inner_probe),
                             UPROBE_LOG_VERBOSE, "rtpd"));
    if (unlikely(rtpd == NULL))
        goto upipe_rtpsrc_alloc_err;
    upipe_rtpsrc_store_last_inner(upipe, rtpd);
    return upipe;

upipe_rtpsrc_alloc_err:
    upipe_release(upipe);
    return NULL;
}

/** @internal @This processes control commands on a rtpsrc pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtpsrc_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_rtpsrc *upipe_rtpsrc = upipe_rtpsrc_from_upipe(upipe);

    switch (command) {
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
        case UPIPE_ATTACH_UREF_MGR:
        case UPIPE_ATTACH_UPUMP_MGR:
        case UPIPE_ATTACH_UBUF_MGR:
        case UPIPE_ATTACH_UCLOCK:
        case UPIPE_GET_OUTPUT_SIZE:
        case UPIPE_SET_OUTPUT_SIZE:
        case UPIPE_GET_URI:
        case UPIPE_SET_URI:
            return upipe_control_va(upipe_rtpsrc->source, command, args);

        default:
            return upipe_rtpsrc_control_bin_output(upipe, command, args);
    }
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_rtpsrc_free(struct urefcount *urefcount_real)
{
    struct upipe_rtpsrc *upipe_rtpsrc =
        upipe_rtpsrc_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_rtpsrc_to_upipe(upipe_rtpsrc);
    upipe_throw_dead(upipe);
    uprobe_clean(&upipe_rtpsrc->proxy_probe);
    uprobe_clean(&upipe_rtpsrc->last_inner_probe);
    urefcount_clean(urefcount_real);
    upipe_rtpsrc_clean_urefcount(upipe);
    upipe_rtpsrc_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtpsrc_no_ref(struct upipe *upipe)
{
    struct upipe_rtpsrc *upipe_rtpsrc = upipe_rtpsrc_from_upipe(upipe);
    upipe_release(upipe_rtpsrc->source);
    upipe_rtpsrc->source = NULL;
    upipe_rtpsrc_clean_bin_output(upipe);
    urefcount_release(upipe_rtpsrc_to_urefcount_real(upipe_rtpsrc));
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_rtpsrc_mgr_free(struct urefcount *urefcount)
{
    struct upipe_rtpsrc_mgr *rtpsrc_mgr =
        upipe_rtpsrc_mgr_from_urefcount(urefcount);
    upipe_mgr_release(rtpsrc_mgr->udpsrc_mgr);
    upipe_mgr_release(rtpsrc_mgr->rtpd_mgr);

    urefcount_clean(urefcount);
    free(rtpsrc_mgr);
}

/** @This processes control commands on a rtpsrc manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtpsrc_mgr_control(struct upipe_mgr *mgr,
                                 int command, va_list args)
{
    struct upipe_rtpsrc_mgr *rtpsrc_mgr = upipe_rtpsrc_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_RTPSRC_MGR_GET_##NAME##_MGR: {                           \
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTPSRC_SIGNATURE)             \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = rtpsrc_mgr->name##_mgr;                                    \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_RTPSRC_MGR_SET_##NAME##_MGR: {                           \
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTPSRC_SIGNATURE)             \
            if (!urefcount_single(&rtpsrc_mgr->urefcount))                  \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(rtpsrc_mgr->name##_mgr);                      \
            rtpsrc_mgr->name##_mgr = upipe_mgr_use(m);                      \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(udpsrc, UDPSRC)
        GET_SET_MGR(rtpd, RTPD)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all rtpsrc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtpsrc_mgr_alloc(void)
{
    struct upipe_rtpsrc_mgr *rtpsrc_mgr = malloc(sizeof(struct upipe_rtpsrc_mgr));
    if (unlikely(rtpsrc_mgr == NULL))
        return NULL;

    rtpsrc_mgr->udpsrc_mgr = upipe_udpsrc_mgr_alloc();
    rtpsrc_mgr->rtpd_mgr = upipe_rtpd_mgr_alloc();

    urefcount_init(upipe_rtpsrc_mgr_to_urefcount(rtpsrc_mgr),
                   upipe_rtpsrc_mgr_free);
    rtpsrc_mgr->mgr.refcount = upipe_rtpsrc_mgr_to_urefcount(rtpsrc_mgr);
    rtpsrc_mgr->mgr.signature = UPIPE_RTPSRC_SIGNATURE;
    rtpsrc_mgr->mgr.upipe_alloc = upipe_rtpsrc_alloc;
    rtpsrc_mgr->mgr.upipe_input = NULL;
    rtpsrc_mgr->mgr.upipe_control = upipe_rtpsrc_control;
    rtpsrc_mgr->mgr.upipe_mgr_control = upipe_rtpsrc_mgr_control;
    return upipe_rtpsrc_mgr_to_upipe_mgr(rtpsrc_mgr);
}

