/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
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
 * @short Upipe source module libavformat wrapper
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_dump.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-modules/upipe_idem.h>
#include <upipe-av/uref_av_flow.h>
#include <upipe-av/upipe_avformat_source.h>

#include "upipe_av_internal.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>

#include <libavutil/dict.h>
#include <libavformat/avformat.h>

/** lowest possible timestamp (just an arbitrarily high time) */
#define AV_CLOCK_MIN UINT32_MAX
/** offset between DTS and (artificial) clock references */
#define PCR_OFFSET (UCLOCK_FREQ * 3)

/** @internal @This is the private context of an avfsrc manager. */
struct upipe_avfsrc_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pointer to autof manager */
    struct upipe_mgr *autof_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_avfsrc_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_avfsrc_mgr, urefcount, urefcount, urefcount)

/** @internal @This is the private context of an avformat source pipe. */
struct upipe_avfsrc {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** offset between libavformat timestamps and Upipe timestamps */
    int64_t timestamp_offset;
    /** highest Upipe timestamp given to a frame */
    uint64_t timestamp_highest;
    /** last random access point */
    uint64_t systime_rap;

    /** list of subs */
    struct uchain subs;

    /** URL */
    char *url;

    /** avcodec initialization watcher */
    struct upump *upump_av_deal;
    /** avformat options */
    AVDictionary *options;
    /** avformat context opened from URL */
    AVFormatContext *context;
    /** true if the URL has already been probed by avformat */
    bool probed;

    /** manager to create subs */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avfsrc, upipe, UPIPE_AVFSRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_avfsrc, urefcount, upipe_avfsrc_no_input)
UPIPE_HELPER_VOID(upipe_avfsrc)
UPIPE_HELPER_OUTPUT(upipe_avfsrc, output, flow_def, output_state, request_list)

UPIPE_HELPER_UREF_MGR(upipe_avfsrc, uref_mgr, uref_mgr_request, NULL,
                      upipe_throw_provide_request, NULL)
UPIPE_HELPER_UCLOCK(upipe_avfsrc, uclock, uclock_request, NULL,
                    upipe_throw_provide_request, NULL)

UPIPE_HELPER_UPUMP_MGR(upipe_avfsrc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_avfsrc, upump, upump_mgr)

UBASE_FROM_TO(upipe_avfsrc, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static int upipe_avfsrc_sub_check(struct upipe *upipe, struct uref *flow_format);
/** @hidden */
static int upipe_avfsrc_sub_register_request(struct upipe *upipe, struct urequest *request);
/** @hidden */
static int upipe_avfsrc_sub_unregister_request(struct upipe *upipe, struct urequest *request);
/** @hidden */
static void upipe_avfsrc_free(struct urefcount *urefcount_real);

/** @internal @This is the private context of an output of an avformat source
 * pipe. */
struct upipe_avfsrc_sub {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;
    /** libavformat stream ID */
    uint64_t id;
    /** input flow definition */
    struct uref *flow_def;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** list of output bin requests */
    struct uchain output_request_list;
    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;
    /** pointer to the last inner pipe */
    struct upipe *last_inner;
    /** pointer to the output of the last inner pipe */
    struct upipe *output;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avfsrc_sub, upipe, UPIPE_AVFSRC_OUTPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_avfsrc_sub, urefcount, upipe_avfsrc_sub_no_ref)
UPIPE_HELPER_FLOW(upipe_avfsrc_sub, NULL)
UPIPE_HELPER_INNER(upipe_avfsrc_sub, last_inner)
UPIPE_HELPER_UPROBE(upipe_avfsrc_sub, urefcount_real, last_inner_probe,
                    NULL)
UPIPE_HELPER_BIN_OUTPUT(upipe_avfsrc_sub, last_inner,
                        output, output_request_list)

UPIPE_HELPER_UBUF_MGR(upipe_avfsrc_sub, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_avfsrc_sub_check,
                      upipe_avfsrc_sub_register_request,
                      upipe_avfsrc_sub_unregister_request)
    
UPIPE_HELPER_SUBPIPE(upipe_avfsrc, upipe_avfsrc_sub, sub, sub_mgr,
                     subs, uchain)

UBASE_FROM_TO(upipe_avfsrc_sub, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_avfsrc_sub_free(struct urefcount *urefcount_real);

/** @internal @This allocates an output subpipe of an avfsrc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avfsrc_sub_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_avfsrc_sub_alloc_flow(mgr, uprobe, signature,
                                                      args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_avfsrc_sub *upipe_avfsrc_sub =
        upipe_avfsrc_sub_from_upipe(upipe);
    upipe_avfsrc_sub_init_urefcount(upipe);
    urefcount_init(upipe_avfsrc_sub_to_urefcount_real(upipe_avfsrc_sub),
                   upipe_avfsrc_sub_free);
    upipe_avfsrc_sub_init_last_inner_probe(upipe);
    upipe_avfsrc_sub_init_bin_output(upipe);
    upipe_avfsrc_sub_init_ubuf_mgr(upipe);
    upipe_avfsrc_sub_init_sub(upipe);
    upipe_avfsrc_sub->id = UINT64_MAX;
    upipe_avfsrc_sub->flow_def = flow_def;

    uint64_t id;
    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_id(flow_def, &id)) ||
                 !ubase_check(uref_flow_get_def(flow_def, &def)))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        upipe_release(upipe);
        return NULL;
    }

    /* check that the ID is not already in use */
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_sub_mgr(upipe->mgr);
    struct uchain *uchain;
    ulist_foreach (&upipe_avfsrc->subs, uchain) {
        struct upipe_avfsrc_sub *output =
            upipe_avfsrc_sub_from_uchain(uchain);
        if (output != upipe_avfsrc_sub && output->id == id) {
            upipe_warn_va(upipe, "ID %"PRIu64" is already in use", id);
            upipe_release(upipe);
            return NULL;
        }
    }

    /* select the stream */
    if (upipe_avfsrc->context == NULL ||
        id >= upipe_avfsrc->context->nb_streams) {
        upipe_warn_va(upipe, "ID %"PRIu64" doesn't exist", id);
        upipe_release(upipe);
        return NULL;
    }
    upipe_avfsrc_sub->id = id;

    struct upipe_avfsrc *avfsrc = upipe_avfsrc_from_sub_mgr(upipe->mgr);
    struct upipe_avfsrc_mgr *avfsrc_mgr =
        upipe_avfsrc_mgr_from_upipe_mgr(upipe_avfsrc_to_upipe(avfsrc)->mgr);
    struct upipe *inner = NULL;
    if (avfsrc_mgr->autof_mgr != NULL) {
        inner = upipe_void_alloc(avfsrc_mgr->autof_mgr,
            uprobe_pfx_alloc(
                uprobe_use(&upipe_avfsrc_sub->last_inner_probe),
                UPROBE_LOG_VERBOSE, "autof"));
    }
    if (unlikely(inner == NULL)) {
        upipe_warn(upipe, "unable to allocate framer");
        struct upipe_mgr *idem_mgr = upipe_idem_mgr_alloc();
        inner = upipe_void_alloc(idem_mgr,
            uprobe_pfx_alloc(
                uprobe_use(&upipe_avfsrc_sub->last_inner_probe),
                UPROBE_LOG_VERBOSE, "idem"));
        upipe_mgr_release(idem_mgr);
    }
    upipe_avfsrc_sub_store_last_inner(upipe, inner);

    upipe_avfsrc->context->streams[id]->discard = AVDISCARD_DEFAULT;
    upipe_throw_ready(upipe);

    upipe_avfsrc_sub_require_ubuf_mgr(upipe, uref_dup(flow_def));
    return upipe;
}

/** @internal @This provides a ubuf_mgr request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_avfsrc_sub_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL) {
        struct upipe_avfsrc_sub *sub = upipe_avfsrc_sub_from_upipe(upipe);
        upipe_dbg(upipe, "avformat flow def is ready");
        uref_dump(flow_format, upipe->uprobe);
        upipe_set_flow_def(sub->last_inner, flow_format);
        uref_free(sub->flow_def);
        sub->flow_def = flow_format;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This registers a ubuf_mgr request.
 *
 * @param upipe description structure of the pipe
 * @param request urequest description structure
 * @return an error code
 */
static int upipe_avfsrc_sub_register_request(struct upipe *upipe,
                                             struct urequest *request)
{
    struct upipe_avfsrc_sub *sub = upipe_avfsrc_sub_from_upipe(upipe);
    return upipe_register_request(sub->last_inner, request);
}

/** @internal @This unregisters a ubuf_mgr request.
 *
 * @param upipe description structure of the pipe
 * @param request urequest description structure
 * @return an error code
 */
static int upipe_avfsrc_sub_unregister_request(struct upipe *upipe,
                                               struct urequest *request)
{
    struct upipe_avfsrc_sub *sub = upipe_avfsrc_sub_from_upipe(upipe);
    return upipe_unregister_request(sub->last_inner, request);
}

/** @internal @This processes control commands on an output subpipe of an avfsrc
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_avfsrc_sub_control(struct upipe *upipe,
                                    int command, va_list args)
{
    switch (command) {
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_avfsrc_sub_get_super(upipe, p);
        }
        case UPIPE_BIN_GET_FIRST_INNER: {
            struct upipe_avfsrc_sub *upipe_avfsrc_sub =
                upipe_avfsrc_sub_from_upipe(upipe);
            struct upipe **p = va_arg(args, struct upipe **);
            *p = upipe_avfsrc_sub->last_inner;
            return (*p != NULL) ? UBASE_ERR_NONE : UBASE_ERR_UNHANDLED;
        }

        default:
            return upipe_avfsrc_sub_control_bin_output(upipe, command, args);
    }
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_avfsrc_sub_free(struct urefcount *urefcount_real)
{
    struct upipe_avfsrc_sub *sub =
        upipe_avfsrc_sub_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_avfsrc_sub_to_upipe(sub);
    struct upipe_avfsrc *avfsrc = upipe_avfsrc_from_sub_mgr(upipe->mgr);
    upipe_throw_dead(upipe);

    uref_free(sub->flow_def);
    avfsrc->context->streams[sub->id]->discard = AVDISCARD_ALL;
    upipe_avfsrc_sub_clean_ubuf_mgr(upipe);
    upipe_avfsrc_sub_clean_last_inner_probe(upipe);
    urefcount_clean(urefcount_real);
    upipe_avfsrc_sub_clean_urefcount(upipe);
    upipe_avfsrc_sub_free_flow(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_sub_no_ref(struct upipe *upipe)
{
    struct upipe_avfsrc_sub *sub = upipe_avfsrc_sub_from_upipe(upipe);

    upipe_avfsrc_sub_clean_bin_output(upipe);
    upipe_avfsrc_sub_clean_sub(upipe);
    urefcount_release(upipe_avfsrc_sub_to_urefcount_real(sub));
}

/** @internal @This initializes the output manager for an avfsrc pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_avfsrc->sub_mgr;
    sub_mgr->refcount = upipe_avfsrc_to_urefcount_real(upipe_avfsrc);
    sub_mgr->signature = UPIPE_AVFSRC_OUTPUT_SIGNATURE;
    sub_mgr->upipe_err_str = NULL;
    sub_mgr->upipe_command_str = NULL;
    sub_mgr->upipe_event_str = NULL;
    sub_mgr->upipe_alloc = upipe_avfsrc_sub_alloc;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_avfsrc_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates an avfsrc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avfsrc_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_avfsrc_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    upipe_avfsrc_init_urefcount(upipe);
    urefcount_init(upipe_avfsrc_to_urefcount_real(upipe_avfsrc),
                   upipe_avfsrc_free);
    upipe_avfsrc_init_output(upipe);
    upipe_avfsrc_init_sub_mgr(upipe);
    upipe_avfsrc_init_sub_subs(upipe);
    upipe_avfsrc_init_uref_mgr(upipe);
    upipe_avfsrc_init_upump_mgr(upipe);
    upipe_avfsrc_init_upump(upipe);
    upipe_avfsrc_init_uclock(upipe);
    upipe_avfsrc->timestamp_offset = 0;
    upipe_avfsrc->timestamp_highest = AV_CLOCK_MIN;
    upipe_avfsrc->systime_rap = UINT64_MAX;

    upipe_avfsrc->url = NULL;

    upipe_avfsrc->upump_av_deal = NULL;
    upipe_avfsrc->options = NULL;
    upipe_avfsrc->context = NULL;
    upipe_avfsrc->probed = false;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This aborts and frees an existing upump watching for exclusive access to
 * avcodec_open().
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_abort_av_deal(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    if (unlikely(upipe_avfsrc->upump_av_deal != NULL)) {
        upipe_av_deal_abort(upipe_avfsrc->upump_av_deal);
        upump_free(upipe_avfsrc->upump_av_deal);
        upipe_avfsrc->upump_av_deal = NULL;
    }
}

/** @internal @This finds the given id in the list of output subpipes.
 *
 * @param upipe description structure of the pipe
 * @param id ID of the stream
 * @return a pointer to the sub pipe, or NULL if not found
 */
static struct upipe_avfsrc_sub *upipe_avfsrc_find_output(struct upipe *upipe,
                                                         uint64_t id)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    struct uchain *uchain;
    ulist_foreach (&upipe_avfsrc->subs, uchain) {
        struct upipe_avfsrc_sub *output = upipe_avfsrc_sub_from_uchain(uchain);
        if (output->id == id)
            return output;
    }
    return NULL;
}

/** @internal @This reads data from the source and outputs it.
 * It is called either when the idler triggers (permanent storage mode) or
 * when data is available on the file descriptor (live stream mode).
 *
 * @param upump description structure of the read watcher
 */
static void upipe_avfsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    AVPacket pkt;

    int error = av_read_frame(upipe_avfsrc->context, &pkt);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "read error from %s (%s)", upipe_avfsrc->url, buf);
        upipe_avfsrc_set_upump(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }

    struct upipe_avfsrc_sub *output =
        upipe_avfsrc_find_output(upipe, pkt.stream_index);
    if (output == NULL) {
        av_packet_unref(&pkt);
        return;
    }
    if (unlikely(output->ubuf_mgr == NULL)) {
        if (unlikely(!upipe_avfsrc_sub_demand_ubuf_mgr(upipe_avfsrc_sub_to_upipe(output), uref_dup(output->flow_def)))) {
            av_packet_unref(&pkt);
            return;
        }
    }

    struct uref *uref = uref_block_alloc(upipe_avfsrc->uref_mgr,
                                         output->ubuf_mgr, pkt.size);
    if (unlikely(uref == NULL)) {
        av_packet_unref(&pkt);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    AVStream *stream = upipe_avfsrc->context->streams[pkt.stream_index];
    uint64_t systime = upipe_avfsrc->uclock != NULL ?
                       uclock_now(upipe_avfsrc->uclock) : UINT64_MAX;
    uint8_t *buffer;
    int read_size = -1;
    if (unlikely(!ubase_check(uref_block_write(uref, 0, &read_size, &buffer)))) {
        uref_free(uref);
        av_packet_unref(&pkt);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    assert(read_size == pkt.size);
    memcpy(buffer, pkt.data, pkt.size);
    uref_block_unmap(uref, 0);

    bool ts = false;
    if (upipe_avfsrc->uclock != NULL)
        uref_clock_set_cr_sys(uref, systime);
    if (pkt.flags & AV_PKT_FLAG_KEY) {
        UBASE_FATAL(upipe, uref_pic_set_key(uref))
        upipe_avfsrc->systime_rap = systime;
    }

    uint64_t dts_orig = UINT64_MAX, dts_pts_delay = 0;
    if (pkt.dts != (int64_t)AV_NOPTS_VALUE) {
        dts_orig = pkt.dts * stream->time_base.num * (int64_t)UCLOCK_FREQ /
                   stream->time_base.den - INT64_MIN;
        if (pkt.pts != (int64_t)AV_NOPTS_VALUE)
            dts_pts_delay = (pkt.pts - pkt.dts) * stream->time_base.num *
                            UCLOCK_FREQ / stream->time_base.den;
    } else if (pkt.pts != (int64_t)AV_NOPTS_VALUE) {
        dts_orig = pkt.pts * stream->time_base.num * (int64_t)UCLOCK_FREQ /
                   stream->time_base.den - INT64_MIN;
    }

    if (dts_orig != UINT64_MAX) {
        uref_clock_set_dts_orig(uref, dts_orig);
        uref_clock_set_dts_pts_delay(uref, dts_pts_delay);

        if (!upipe_avfsrc->timestamp_offset)
            upipe_avfsrc->timestamp_offset = upipe_avfsrc->timestamp_highest -
                                             dts_orig + PCR_OFFSET;
        uint64_t dts = dts_orig + upipe_avfsrc->timestamp_offset;
        uref_clock_set_dts_prog(uref, dts);
        if (upipe_avfsrc->timestamp_highest < dts + dts_pts_delay)
            upipe_avfsrc->timestamp_highest = dts + dts_pts_delay;
        ts = true;

        /* this is subtly wrong, but whatever */
        upipe_throw_clock_ref(upipe, uref, dts - PCR_OFFSET, 0);
    }
    if (pkt.duration > 0) {
        uint64_t duration = pkt.duration * stream->time_base.num * UCLOCK_FREQ /
                            stream->time_base.den;
        UBASE_FATAL(upipe, uref_clock_set_duration(uref, duration))
    }
    if (upipe_avfsrc->systime_rap != UINT64_MAX)
        uref_clock_set_rap_sys(uref, upipe_avfsrc->systime_rap);

    if (ts)
        upipe_throw_clock_ts(upipe, uref);
    av_packet_unref(&pkt);

    upipe_input(output->last_inner, uref, &upipe_avfsrc->upump);
}

/** @internal @This starts the worker.
 *
 * @param upipe description structure of the pipe
 */
static bool upipe_avfsrc_start(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    struct upump *upump = upump_alloc_idler(upipe_avfsrc->upump_mgr,
                                            upipe_avfsrc_worker, upipe,
                                            upipe->refcount);
    if (unlikely(upump == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return false;
    }
    upipe_avfsrc_set_upump(upipe, upump);
    upump_start(upump);
    return true;
}

/** @internal @This returns a flow definition for a raw audio media type.
 *
 * @param upipe description structure of the pipe
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_raw_audio_def(struct upipe *upipe,
                                        struct uref_mgr *uref_mgr,
                                        AVCodecContext *codec)
{
    if (unlikely(codec->bits_per_coded_sample % 8))
        return NULL;

    const char *def = upipe_av_to_flow_def(codec->codec_id);
    if (unlikely(def == NULL))
        return NULL;

    struct uref *flow_def =
        uref_sound_flow_alloc_def(uref_mgr, def, codec->channels,
                                  codec->bits_per_coded_sample / 8);
    if (unlikely(flow_def == NULL))
        return NULL;

    UBASE_FATAL(upipe, uref_sound_flow_set_rate(flow_def, codec->sample_rate))
    if (codec->block_align)
        UBASE_FATAL(upipe, uref_sound_flow_set_samples(flow_def,
                    codec->block_align / (codec->bits_per_coded_sample / 8) /
                    codec->channels))
    return flow_def;
}

/** @internal @This returns a flow definition for a coded audio media type.
 *
 * @param upipe description structure of the pipe
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_audio_def(struct upipe *upipe,
                                    struct uref_mgr *uref_mgr,
                                    AVCodecContext *codec)
{
    const char *def = upipe_av_to_flow_def(codec->codec_id);
    if (unlikely(def == NULL))
        return NULL;

    struct uref *flow_def = uref_block_flow_alloc_def_va(uref_mgr, "%s", def);
    if (unlikely(flow_def == NULL))
        return NULL;
    UBASE_FATAL(upipe, uref_flow_set_complete(flow_def))

    if (codec->bit_rate)
        UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def,
                    (codec->bit_rate + 7) / 8))

    UBASE_FATAL(upipe, uref_sound_flow_set_channels(flow_def, codec->channels))
    UBASE_FATAL(upipe, uref_sound_flow_set_rate(flow_def, codec->sample_rate))
    if (codec->frame_size) {
        UBASE_FATAL(upipe, uref_sound_flow_set_samples(flow_def,
                    codec->frame_size))
    } else {
        UBASE_FATAL(upipe, uref_sound_flow_set_samples(flow_def,
                    av_get_audio_frame_duration(codec, 0)))
    }
    return flow_def;
}

/** @internal @This returns a flow definition for a raw video media type.
 *
 * @param upipe description structure of the pipe
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_raw_video_def(struct upipe *upipe,
                                        struct uref_mgr *uref_mgr,
                                        AVCodecContext *codec)
{
    /* TODO */
    return NULL;
}

/** @internal @This returns a flow definition for a coded video media type.
 *
 * @param upipe description structure of the pipe
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_video_def(struct upipe *upipe,
                                    struct uref_mgr *uref_mgr,
                                    AVFormatContext *format,
                                    AVCodecContext *codec,
                                    AVStream *stream)
{
    const char *def = upipe_av_to_flow_def(codec->codec_id);
    if (unlikely(def == NULL))
        return NULL;

    struct uref *flow_def = uref_block_flow_alloc_def_va(uref_mgr, "%s", def);
    if (unlikely(flow_def == NULL))
        return NULL;
    UBASE_FATAL(upipe, uref_flow_set_complete(flow_def))

    if (codec->bit_rate)
        UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def,
                    (codec->bit_rate + 7) / 8))

    UBASE_FATAL(upipe, uref_pic_flow_set_hsize(flow_def, codec->width))
    UBASE_FATAL(upipe, uref_pic_flow_set_vsize(flow_def, codec->height))
    int ticks = codec->ticks_per_frame ? codec->ticks_per_frame : 1;
    if (stream->time_base.num) {
        struct urational fps = { .num = stream->time_base.den,
                                 .den = stream->time_base.num * ticks };
        urational_simplify(&fps);
        UBASE_FATAL(upipe, uref_pic_flow_set_fps(flow_def, fps))
    }
    AVRational sample_ar = av_guess_sample_aspect_ratio(format, stream, NULL);
    if (sample_ar.num) {
        struct urational sar = { .num = sample_ar.num,
                                 .den = sample_ar.den };
        urational_simplify(&sar);
        UBASE_FATAL(upipe, uref_pic_flow_set_sar(flow_def, sar));
    }
    return flow_def;
}

/** @internal @This returns a flow definition for a subtitles media type.
 *
 * @param upipe description structure of the pipe
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_subtitles_def(struct upipe *upipe,
                                        struct uref_mgr *uref_mgr,
                                        AVCodecContext *codec)
{
    /* TODO */
    /* FIXME extradata */
    return NULL;
}

/** @internal @This returns a flow definition for a data media type.
 *
 * @param upipe description structure of the pipe
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_data_def(struct upipe *upipe,
                                   struct uref_mgr *uref_mgr,
                                   AVCodecContext *codec)
{
    /* TODO */
    return NULL;
}

/** @internal @This probes all flows from the source.
 *
 * @param upump description structure of the dealer
 */
static void upipe_avfsrc_probe(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    AVFormatContext *context = upipe_avfsrc->context;

    if (unlikely(!upipe_av_deal_grab()))
        return;

    AVDictionary *options[context->nb_streams];
    for (unsigned i = 0; i < context->nb_streams; i++) {
        options[i] = NULL;
        av_dict_copy(&options[i], upipe_avfsrc->options, 0);
    }
    int error = avformat_find_stream_info(context, options);

    upipe_av_deal_yield(upump);
    upump_free(upipe_avfsrc->upump_av_deal);
    upipe_avfsrc->upump_av_deal = NULL;
    upipe_avfsrc->probed = true;

    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't probe URL %s (%s)", upipe_avfsrc->url, buf);
        if (likely(upipe_avfsrc->url != NULL))
            upipe_notice_va(upipe, "closing URL %s", upipe_avfsrc->url);
        avformat_close_input(&upipe_avfsrc->context);
        upipe_avfsrc->context = NULL;
        ubase_clean_str(&upipe_avfsrc->url);
        return;
    }

    for (int i = 0; i < context->nb_streams; i++) {
        AVStream *stream = context->streams[i];
        AVCodecContext *codec = stream->codec;
        struct uref *flow_def;

        // discard all packets from this stream
        stream->discard = AVDISCARD_ALL; 

        switch (codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (codec->codec_id >= AV_CODEC_ID_FIRST_AUDIO &&
                    codec->codec_id < AV_CODEC_ID_ADPCM_IMA_QT)
                    flow_def = alloc_raw_audio_def(upipe,
                            upipe_avfsrc->uref_mgr, codec);
                else
                    flow_def = alloc_audio_def(upipe, upipe_avfsrc->uref_mgr,
                                               codec);
                break;
            case AVMEDIA_TYPE_VIDEO:
                if (codec->codec_id == AV_CODEC_ID_RAWVIDEO)
                    flow_def = alloc_raw_video_def(upipe,
                            upipe_avfsrc->uref_mgr, codec);
                else
                    flow_def = alloc_video_def(upipe,
                            upipe_avfsrc->uref_mgr, upipe_avfsrc->context,
                            codec, stream);
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                flow_def = alloc_subtitles_def(upipe, upipe_avfsrc->uref_mgr,
                                               codec);
                break;
            default:
                flow_def = alloc_data_def(upipe, upipe_avfsrc->uref_mgr, codec);
                break;
        }

        if (unlikely(flow_def == NULL)) {
            upipe_warn_va(upipe, "unsupported track type (%u:%u)",
                          codec->codec_type, codec->codec_id);
            continue;
        }
        UBASE_FATAL(upipe, uref_flow_set_id(flow_def, i))

        AVDictionaryEntry *lang = av_dict_get(stream->metadata, "language",
                                              NULL, 0);
        if (lang != NULL && lang->value != NULL) {
            UBASE_FATAL(upipe, uref_flow_set_languages(flow_def, 1))
            UBASE_FATAL(upipe, uref_flow_set_language(flow_def, lang->value, 0))
        }
        if (codec->extradata_size) {
            UBASE_FATAL(upipe, uref_flow_set_global(flow_def))
            UBASE_FATAL(upipe, uref_flow_set_headers(flow_def, codec->extradata,
                                                     codec->extradata_size))
        }

        codec->opaque = flow_def;
    }

    upipe_split_throw_update(upipe);
    upipe_avfsrc_start(upipe);
}


/** @internal @This iterates over output flow definitions.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the next flow definition, initialize with NULL
 * @return an error code
 */
static int upipe_avfsrc_iterate(struct upipe *upipe, struct uref **p)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    if (urefcount_dead(upipe->refcount)) {
        *p = NULL;
        return UBASE_ERR_NONE;
    }

    AVFormatContext *context = upipe_avfsrc->context;
    if (context == NULL)
        return UBASE_ERR_UNHANDLED;
    assert(p != NULL);
    uint64_t id = 0;
    if (*p != NULL) {
        uref_flow_get_id(*p, &id);
        id++;
    }

    while (id < context->nb_streams) {
        if (context->streams[id]->codec->opaque != NULL) {
            *p = (struct uref *)context->streams[id]->codec->opaque;
            return UBASE_ERR_NONE;
        }
        id++;
    }

    *p = NULL;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the content of an avformat option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content_p filled in with the content of the option
 * @return an error code
 */
static int _upipe_avfsrc_get_option(struct upipe *upipe, const char *option,
                                    const char **content_p)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(option != NULL);
    assert(content_p != NULL);
    AVDictionaryEntry *entry = av_dict_get(upipe_avfsrc->options, option,
                                           NULL, 0);
    if (unlikely(entry == NULL))
        return UBASE_ERR_EXTERNAL;
    *content_p = entry->value;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the content of an avformat option. It only take effect
 * after the next call to @ref upipe_set_uri.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return an error code
 */
static int _upipe_avfsrc_set_option(struct upipe *upipe, const char *option,
                                    const char *content)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(option != NULL);
    int error = av_dict_set(&upipe_avfsrc->options, option, content, 0);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't set option %s:%s (%s)", option, content,
                     buf);
        return UBASE_ERR_EXTERNAL;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the currently opened URL.
 *
 * @param upipe description structure of the pipe
 * @param url_p filled in with the URL
 * @return an error code
 */
static int upipe_avfsrc_get_uri(struct upipe *upipe, const char **url_p)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(url_p != NULL);
    *url_p = upipe_avfsrc->url;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given URL.
 *
 * @param upipe description structure of the pipe
 * @param url URL to open
 * @return an error code
 */
static int upipe_avfsrc_set_uri(struct upipe *upipe, const char *url)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);

    if (unlikely(upipe_avfsrc->context != NULL)) {
        if (likely(upipe_avfsrc->url != NULL))
            upipe_notice_va(upipe, "closing URL %s", upipe_avfsrc->url);
        avformat_close_input(&upipe_avfsrc->context);
        upipe_avfsrc->context = NULL;
        upipe_avfsrc_set_upump(upipe, NULL);
        upipe_avfsrc_abort_av_deal(upipe);
        upipe_avfsrc_throw_sub_subs(upipe, UPROBE_SOURCE_END);
    }
    ubase_clean_str(&upipe_avfsrc->url);

    if (unlikely(url == NULL))
        return UBASE_ERR_NONE;

    if (unlikely(!upipe_avfsrc_demand_uref_mgr(upipe)))
        return UBASE_ERR_ALLOC;
    upipe_avfsrc_check_upump_mgr(upipe);

    struct uref *flow_def = uref_alloc_control(upipe_avfsrc->uref_mgr);
    uref_flow_set_def(flow_def, "void.");
    upipe_avfsrc_store_flow_def(upipe, flow_def);
    /* Force sending flow def */
    struct uref *uref = uref_alloc(upipe_avfsrc->uref_mgr);
    upipe_avfsrc_output(upipe, uref, NULL);

    AVDictionary *options = NULL;
    av_dict_copy(&options, upipe_avfsrc->options, 0);
    int error = avformat_open_input(&upipe_avfsrc->context, url, NULL,
                                    &options);
    av_dict_free(&options);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't open URL %s (%s)", url, buf);
        return UBASE_ERR_EXTERNAL;
    }

    /* http://stackoverflow.com/questions/40991412/ffmpeg-producing-strange-nal-suffixes-for-mpeg-ts-with-h264 */
    upipe_avfsrc->context->flags |= AVFMT_FLAG_KEEP_SIDE_DATA;
    upipe_avfsrc->timestamp_offset = 0;
    upipe_avfsrc->url = strdup(url);
    upipe_avfsrc->probed = false;
    upipe_notice_va(upipe, "opening URL %s", upipe_avfsrc->url);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the time of the currently opened URL.
 *
 * @param upipe description structure of the pipe
 * @param time_p filled in with the reading time, in clock units
 * @return an error code
 */
static int _upipe_avfsrc_get_time(struct upipe *upipe, uint64_t *time_p)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(time_p != NULL);
    if (upipe_avfsrc->context != NULL && upipe_avfsrc->context->pb != NULL &&
        upipe_avfsrc->context->pb->seekable & AVIO_SEEKABLE_NORMAL) {
        *time_p = 0; /* TODO */
        return UBASE_ERR_NONE;
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This asks to read at the given time.
 *
 * @param upipe description structure of the pipe
 * @param time new reading time, in clock units
 * @return an error code
 */
static int _upipe_avfsrc_set_time(struct upipe *upipe, uint64_t time)
{
    //struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This processes control commands on an avformat source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_avfsrc_control(struct upipe *upipe,
                                 int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_avfsrc_control_subs(upipe, command, args));

    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_avfsrc_set_upump(upipe, NULL);
            upipe_avfsrc_abort_av_deal(upipe);
            return upipe_avfsrc_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_avfsrc_set_upump(upipe, NULL);
            upipe_avfsrc_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_avfsrc_control_output(upipe, command, args);

        case UPIPE_SPLIT_ITERATE: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_avfsrc_iterate(upipe, p);
        }

        case UPIPE_GET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char **content_p = va_arg(args, const char **);
            return _upipe_avfsrc_get_option(upipe, option, content_p);
        }
        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return _upipe_avfsrc_set_option(upipe, option, content);
        }
        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_avfsrc_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_avfsrc_set_uri(upipe, uri);
        }
        case UPIPE_AVFSRC_GET_TIME: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFSRC_SIGNATURE)
            uint64_t *time_p = va_arg(args, uint64_t *);
            return _upipe_avfsrc_get_time(upipe, time_p);
        }
        case UPIPE_AVFSRC_SET_TIME: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFSRC_SIGNATURE)
            uint64_t time = va_arg(args, uint64_t);
            return _upipe_avfsrc_set_time(upipe, time);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on an avformat source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_avfsrc_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_avfsrc_control(upipe, command, args));

    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    if (upipe_avfsrc->upump_mgr != NULL && upipe_avfsrc->url != NULL &&
        upipe_avfsrc->upump == NULL) {
        if (unlikely(upipe_avfsrc->probed))
            return upipe_avfsrc_start(upipe) ?
                   UBASE_ERR_NONE : UBASE_ERR_EXTERNAL;

        if (unlikely(upipe_avfsrc->upump_av_deal != NULL))
            return UBASE_ERR_NONE;

        struct upump *upump_av_deal =
            upipe_av_deal_upump_alloc(upipe_avfsrc->upump_mgr,
                    upipe_avfsrc_probe, upipe, upipe->refcount);
        if (unlikely(upump_av_deal == NULL)) {
            upipe_err(upipe, "can't create dealer");
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_avfsrc->upump_av_deal = upump_av_deal;
        upipe_av_deal_start(upump_av_deal);
    }

    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_avfsrc_free(struct urefcount *urefcount_real)
{
    struct upipe_avfsrc *upipe_avfsrc =
        upipe_avfsrc_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_avfsrc_to_upipe(upipe_avfsrc);
    upipe_avfsrc_clean_sub_subs(upipe);

    upipe_avfsrc_abort_av_deal(upipe);
    if (likely(upipe_avfsrc->context != NULL)) {
        if (likely(upipe_avfsrc->url != NULL))
            upipe_notice_va(upipe, "closing URL %s", upipe_avfsrc->url);
        
        for (int i = 0; i < upipe_avfsrc->context->nb_streams; i++)
            if (upipe_avfsrc->context->streams[i]->codec->opaque != NULL)
                uref_free((struct uref *)upipe_avfsrc->context->streams[i]->codec->opaque);

        avformat_close_input(&upipe_avfsrc->context);
    }
    upipe_throw_dead(upipe);

    av_dict_free(&upipe_avfsrc->options);
    free(upipe_avfsrc->url);

    upipe_avfsrc_clean_uclock(upipe);
    upipe_avfsrc_clean_upump(upipe);
    upipe_avfsrc_clean_upump_mgr(upipe);
    upipe_avfsrc_clean_uref_mgr(upipe);
    upipe_avfsrc_clean_output(upipe);
    urefcount_clean(urefcount_real);
    upipe_avfsrc_clean_urefcount(upipe);
    upipe_avfsrc_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_no_input(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    upipe_avfsrc_throw_sub_subs(upipe, UPROBE_SOURCE_END);
    upipe_split_throw_update(upipe);
    urefcount_release(upipe_avfsrc_to_urefcount_real(upipe_avfsrc));
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_avfsrc_mgr_free(struct urefcount *urefcount)
{
    struct upipe_avfsrc_mgr *avfsrc_mgr =
        upipe_avfsrc_mgr_from_urefcount(urefcount);
    upipe_mgr_release(avfsrc_mgr->autof_mgr);

    urefcount_clean(urefcount);
    free(avfsrc_mgr);
}

/** @This processes control commands on a avfsrc manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_avfsrc_mgr_control(struct upipe_mgr *mgr,
                                    int command, va_list args)
{
    struct upipe_avfsrc_mgr *avfsrc_mgr =
        upipe_avfsrc_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_AVFSRC_MGR_GET_##NAME##_MGR: {                           \
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFSRC_SIGNATURE)             \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = avfsrc_mgr->name##_mgr;                                    \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_AVFSRC_MGR_SET_##NAME##_MGR: {                           \
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFSRC_SIGNATURE)             \
            if (!urefcount_single(&avfsrc_mgr->urefcount))                  \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(avfsrc_mgr->name##_mgr);                      \
            avfsrc_mgr->name##_mgr = upipe_mgr_use(m);                      \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(autof, AUTOF)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all avformat sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avfsrc_mgr_alloc(void)
{
    struct upipe_avfsrc_mgr *avfsrc_mgr =
        malloc(sizeof(struct upipe_avfsrc_mgr));
    if (unlikely(avfsrc_mgr == NULL))
        return NULL;

    memset(avfsrc_mgr, 0, sizeof(*avfsrc_mgr));

    avfsrc_mgr->autof_mgr = NULL;

    urefcount_init(upipe_avfsrc_mgr_to_urefcount(avfsrc_mgr),
                   upipe_avfsrc_mgr_free);
    avfsrc_mgr->mgr.refcount = upipe_avfsrc_mgr_to_urefcount(avfsrc_mgr);
    avfsrc_mgr->mgr.signature = UPIPE_AVFSRC_SIGNATURE;
    avfsrc_mgr->mgr.upipe_alloc = upipe_avfsrc_alloc;
    avfsrc_mgr->mgr.upipe_input = NULL;
    avfsrc_mgr->mgr.upipe_control = upipe_avfsrc_control;
    avfsrc_mgr->mgr.upipe_mgr_control = upipe_avfsrc_mgr_control;
    return upipe_avfsrc_mgr_to_upipe_mgr(avfsrc_mgr);
}
