/*
 * Copyright (C) 2016-2017 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module receiving rfc4585 feedback
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-filters/upipe_rtcp_fb_receiver.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <assert.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <bitstream/ietf/rtp.h>
#include <bitstream/ietf/rtcp.h>
#include <bitstream/ietf/rtcp_fb.h>

#define EXPECTED_FLOW_DEF "block."

/** upipe_rtcpfb structure */
struct upipe_rtcpfb {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** ubuf mgr structures */
    struct ubuf_mgr *ubuf_mgr;
    struct urequest ubuf_mgr_request;
    struct uref *flow_format;

    struct upipe_mgr sub_mgr;

    struct upump_mgr *upump_mgr;
    struct upump *upump_timer;
    struct uclock *uclock;
    struct urequest uclock_request;
    struct uchain queue;
    unsigned last_seq;

    /** list of input subpipes */
    struct uchain inputs;

    /** expected sequence number */
    int expected_seqnum;

    /** output pipe */
    struct upipe *output;
    /** input flow definition packet */
    struct uref *flow_def_input;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** retransmit seqnum */
    uint16_t retransmit_seq;

    /** retransmit payload type */
    uint8_t type;

    /** buffer latency */
    uint64_t latency;

    /** public upipe structure */
    struct upipe upipe;
};

static int upipe_rtcpfb_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_rtcpfb, upipe, UPIPE_RTCPFB_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_rtcpfb, urefcount, upipe_rtcpfb_no_input);
UPIPE_HELPER_VOID(upipe_rtcpfb);
UPIPE_HELPER_OUTPUT(upipe_rtcpfb, output, flow_def, output_state, request_list);
UPIPE_HELPER_UREF_MGR(upipe_rtcpfb, uref_mgr, uref_mgr_request,
                      upipe_rtcpfb_check,
                      upipe_rtcpfb_register_output_request,
                      upipe_rtcpfb_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_rtcpfb, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_rtcpfb_check,
                      upipe_rtcpfb_register_output_request,
                      upipe_rtcpfb_unregister_output_request)
UBASE_FROM_TO(upipe_rtcpfb, urefcount, urefcount_real, urefcount_real)
UPIPE_HELPER_UPUMP_MGR(upipe_rtcpfb, upump_mgr)
UPIPE_HELPER_UCLOCK(upipe_rtcpfb, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

struct upipe_rtcpfb_input {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

static void upipe_rtcpfb_lost_sub(struct upipe *upipe, uint16_t seq, uint16_t mask);

/** @internal @This handles NACK RTCP messages.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */

static inline void upipe_rtcpfb_input_sub(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    size_t s = 0;
    uref_block_size(uref, &s);
    const uint8_t *rtp = uref_block_peek(uref, 0, s, NULL);
    if (!rtp) {
        upipe_err(upipe, "Can't peek rtcp message");
        uref_free(uref);
        return;
    }

    if (s < RTP_HEADER_SIZE || !rtp_check_hdr(rtp)) {
        upipe_warn_va(upipe, "Received invalid RTP packet");
        goto end;
    }

    if (rtcp_get_pt(rtp) != RTCP_PT_RTPFB) {
        upipe_warn_va(upipe, "Received non Transport Layer specific message");
        goto end;
    }

    if (rtcp_fb_get_fmt(rtp) != RTCP_PT_RTPFB_GENERIC_NACK) {
        upipe_warn_va(upipe, "Received non generic NACK message");
        goto end;
    }

    uint16_t len = rtpx_get_length(rtp);
    if ((len + 1) * 4 != s) {
        upipe_warn_va(upipe, "Invalid RTP length");
        goto end;
    }

    // TODO: ssrc

    s -= RTCP_FB_HEADER_SIZE;
    const uint8_t *fci = &rtp[RTCP_FB_HEADER_SIZE];

    for (size_t i = 0; i < s; i += RTCP_FB_FCI_GENERIC_NACK_SIZE) {
        uint16_t id = rtcp_fb_nack_get_packet_id(&fci[i]);
        uint16_t mask = rtcp_fb_nack_get_bitmask_lost(&fci[i]);
        upipe_verbose_va(upipe, "Received NACK: %hu (0x%hx)", id, mask);
        upipe_rtcpfb_lost_sub(upipe, id, mask);
    }

end:
    uref_block_peek_unmap(uref, 0, NULL, rtp);
    uref_free(uref);
}

UPIPE_HELPER_UPIPE(upipe_rtcpfb_input, upipe, UPIPE_RTCPFB_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_rtcpfb_input, urefcount, upipe_rtcpfb_input_free)
UPIPE_HELPER_SUBPIPE(upipe_rtcpfb, upipe_rtcpfb_input, output, sub_mgr, inputs,
                     uchain)

static int ctz(unsigned int x)
{
#ifdef __GNUC__
    return __builtin_ctz(x);
#else
    unsigned tz = 32;
    for (; x; x <<= 1)
        tz--;
    return tz;
#endif
}

/** @internal @This retransmits a list of packets described by a single FCI.
 * TODO: handle a list of FCIs.
 */
static void upipe_rtcpfb_lost_sub(struct upipe *upipe, uint16_t seq, uint16_t mask)
{
    struct upipe *upipe_super = NULL;
    upipe_rtcpfb_input_get_super(upipe, &upipe_super);
    struct upipe_rtcpfb *upipe_rtcpfb = upipe_rtcpfb_from_upipe(upipe_super);

    struct uchain *uchain;
    ulist_foreach(&upipe_rtcpfb->queue, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        uint64_t uref_seqnum = 0;
        uref_attr_get_priv(uref, &uref_seqnum);

        if (seq != uref_seqnum)
            continue;

        upipe_warn_va(upipe, "Retransmit %hu", seq);
        size_t size;
        UBASE_FATAL_RETURN(upipe, uref_block_size(uref, &size));

        struct ubuf *retransmit = ubuf_block_alloc(upipe_rtcpfb->ubuf_mgr,
                size + 2 /* OSN */);

        if (!retransmit) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        int s = -1;
        const uint8_t *buf;
        uint8_t *buf_retransmit;

        ubuf_block_write(retransmit, 0, &s, &buf_retransmit);
        uref_block_read(uref, 0, &s, &buf);

        uint32_t ts = rtp_get_timestamp(buf);
        memcpy(buf_retransmit, buf, RTP_HEADER_SIZE);

        uint8_t ssrc[4];
        rtp_get_ssrc(buf, ssrc);

        rtp_set_type(buf_retransmit, upipe_rtcpfb->type);
        rtp_set_seqnum(buf_retransmit,
                upipe_rtcpfb->retransmit_seq++);
        rtp_set_timestamp(buf_retransmit, ts);
        ssrc[3]++; /* XXX */
        rtp_set_ssrc(buf_retransmit, ssrc);

        uint16_t osn = rtp_get_seqnum(buf);

        buf_retransmit[RTP_HEADER_SIZE] = osn >> 8;
        buf_retransmit[RTP_HEADER_SIZE + 1] = osn & 0xff;

        memcpy(&buf_retransmit[RTP_HEADER_SIZE+2],
                &buf[RTP_HEADER_SIZE], s - RTP_HEADER_SIZE);

        ubuf_block_unmap(retransmit, 0);
        uref_block_unmap(uref, 0);

        upipe_rtcpfb_output(upipe_super,
                uref_fork(uref, retransmit), NULL);

        if (!mask)
            return;

        int zeros = ctz(mask);
        mask >>= zeros + 1;
        seq += zeros + 1;
    }

    upipe_warn_va(upipe, "Couldn't find seq %hu", seq);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtcpfb_no_input(struct upipe *upipe)
{
    struct upipe_rtcpfb *upipe_rtcpfb = upipe_rtcpfb_from_upipe(upipe);
    upipe_rtcpfb_throw_sub_outputs(upipe, UPROBE_SOURCE_END);
    urefcount_release(upipe_rtcpfb_to_urefcount_real(upipe_rtcpfb));
}

/** @internal @This allocates an output subpipe of a dup pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtcpfb_input_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    if (signature != UPIPE_VOID_SIGNATURE ||
        mgr->signature != UPIPE_RTCPFB_INPUT_SIGNATURE)
        return NULL;

    struct upipe_rtcpfb_input *upipe_rtcpfb_input =
        malloc(sizeof(struct upipe_rtcpfb_input));
    if (unlikely(upipe_rtcpfb_input == NULL))
        return NULL;

    upipe_rtcpfb_input->flow_def = NULL;

    struct upipe *upipe = upipe_rtcpfb_input_to_upipe(upipe_rtcpfb_input);
    upipe_init(upipe, mgr, uprobe);
    upipe_rtcpfb_input_init_urefcount(upipe);
    upipe_rtcpfb_input_init_sub(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtcpfb_input_free(struct upipe *upipe)
{
    struct upipe_rtcpfb_input *upipe_rtcpfb_input =
        upipe_rtcpfb_input_from_upipe(upipe);
    upipe_throw_dead(upipe);

    uref_free(upipe_rtcpfb_input->flow_def);

    upipe_rtcpfb_input_clean_sub(upipe);
    upipe_rtcpfb_input_clean_urefcount(upipe);
    upipe_clean(upipe);
    free(upipe_rtcpfb_input);
}

static int upipe_rtcpfb_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_rtcpfb *upipe_rtcpfb = upipe_rtcpfb_from_upipe(upipe);

    if (flow_format != NULL)
        upipe_rtcpfb_store_flow_def(upipe, flow_format);

    if (upipe_rtcpfb->flow_def == NULL)
        return UBASE_ERR_NONE;

    if (upipe_rtcpfb->uref_mgr == NULL) {
        upipe_rtcpfb_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_rtcpfb->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_rtcpfb->uref_mgr, NULL);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_rtcpfb_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    return UBASE_ERR_NONE;
}

/** @internal */
static int upipe_rtcpfb_input_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    struct uref *flow_def_dup = uref_dup(flow_def) ;
    if (unlikely(flow_def_dup == NULL))
        return UBASE_ERR_ALLOC;

    struct upipe_rtcpfb_input *upipe_rtcpfb_input = upipe_rtcpfb_input_from_upipe(upipe);
    uref_free(upipe_rtcpfb_input->flow_def);
    upipe_rtcpfb_input->flow_def = flow_def_dup;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an output subpipe of a dup
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtcpfb_input_control(struct upipe *upipe,
                                    int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtcpfb_input_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_rtcpfb_input_get_super(upipe, p);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This initializes the output manager for a rtcpfb set pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtcpfb_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_rtcpfb *upipe_rtcpfb = upipe_rtcpfb_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_rtcpfb->sub_mgr;
    sub_mgr->refcount = upipe_rtcpfb_to_urefcount_real(upipe_rtcpfb);
    sub_mgr->signature = UPIPE_RTCPFB_INPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_rtcpfb_input_alloc;
    sub_mgr->upipe_input = upipe_rtcpfb_input_sub;
    sub_mgr->upipe_control = upipe_rtcpfb_input_control;
    sub_mgr->upipe_mgr_control = NULL;
}

static void upipe_rtcpfb_free(struct urefcount *urefcount_real);

/** @internal this timer removes from the queue packets that are too
 * early to be recovered by receiver.
 */
static void upipe_rtcpfb_timer(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_rtcpfb *upipe_rtcpfb = upipe_rtcpfb_from_upipe(upipe);

    uint64_t now = uclock_now(upipe_rtcpfb->uclock);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_rtcpfb->queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);

        uint64_t seqnum = 0;
        uref_attr_get_priv(uref, &seqnum);

        uint64_t cr_sys = 0;
        if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &cr_sys))))
            upipe_warn(upipe, "Couldn't read cr_sys");

        if (now - cr_sys < upipe_rtcpfb->latency * UCLOCK_FREQ / 1000)
            return;

        upipe_verbose_va(upipe, "Delete seq %" PRIu64 " after %"PRIu64" clocks",
                seqnum, now - cr_sys);

        ulist_delete(uchain);
        uref_free(uref);
    }
}


/** @internal @This allocates a rtcpfb pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtcpfb_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_rtcpfb_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_rtcpfb *upipe_rtcpfb = upipe_rtcpfb_from_upipe(upipe);
    upipe_rtcpfb_init_urefcount(upipe);
    urefcount_init(upipe_rtcpfb_to_urefcount_real(upipe_rtcpfb), upipe_rtcpfb_free);
    upipe_rtcpfb_init_upump_mgr(upipe);
    upipe_rtcpfb_check_upump_mgr(upipe);
    upipe_rtcpfb_init_uclock(upipe);
    ulist_init(&upipe_rtcpfb->queue);
    upipe_rtcpfb_init_output(upipe);
    upipe_rtcpfb_init_sub_mgr(upipe);
    upipe_rtcpfb_init_sub_outputs(upipe);
    upipe_rtcpfb->expected_seqnum = -1;
    upipe_rtcpfb->flow_def_input = NULL;
    upipe_rtcpfb_init_ubuf_mgr(upipe);
    upipe_rtcpfb_init_uref_mgr(upipe);
    upipe_rtcpfb->last_seq = UINT_MAX;
    upipe_rtcpfb_require_uclock(upipe);
    upipe_rtcpfb->latency = 1000; /* 1 sec */
    upipe_rtcpfb->retransmit_seq = 0;
    upipe_rtcpfb->type = 1; /* reserved */

    /* This timer does not need to run frequently */
    upipe_rtcpfb->upump_timer = upump_alloc_timer(upipe_rtcpfb->upump_mgr,
            upipe_rtcpfb_timer, upipe, upipe->refcount,
            UCLOCK_FREQ, UCLOCK_FREQ);
    upump_start(upipe_rtcpfb->upump_timer);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static inline void upipe_rtcpfb_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_rtcpfb *upipe_rtcpfb = upipe_rtcpfb_from_upipe(upipe);
    uint8_t rtp_buffer[RTP_HEADER_SIZE];
    const uint8_t *rtp_header = uref_block_peek(uref, 0, RTP_HEADER_SIZE,
                                                rtp_buffer);
    if (unlikely(rtp_header == NULL)) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }

    uint16_t seqnum = rtp_get_seqnum(rtp_header);
#if 0 // TODO : ssrc
    uint8_t ssrc[4];
    rtp_get_ssrc(rtp_header, ssrc);
#endif
    uref_block_peek_unmap(uref, 0, rtp_buffer, rtp_header);

    uref_attr_set_priv(uref, seqnum);

    /* Output packet immediately */
    upipe_rtcpfb_output(upipe, uref_dup(uref), NULL); // FIXME : upump?

    upipe_verbose_va(upipe, "Output & buffer %hu", seqnum);

    /* Buffer packet in case retransmission is needed */
    ulist_add(&upipe_rtcpfb->queue, uref_to_uchain(uref));

    upipe_rtcpfb->last_seq = seqnum;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_rtcpfb_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;
    struct upipe_rtcpfb *upipe_rtcpfb = upipe_rtcpfb_from_upipe(upipe);
    uref_free(upipe_rtcpfb->flow_def_input);
    upipe_rtcpfb->flow_def_input = flow_def_dup;
    upipe_rtcpfb_store_flow_def(upipe, uref_dup(flow_def));

    return UBASE_ERR_NONE;
}

static int _upipe_rtcpfb_set_pt(struct upipe *upipe, uint8_t pt)
{
    struct upipe_rtcpfb *upipe_rtcpfb = upipe_rtcpfb_from_upipe(upipe);
    upipe_rtcpfb->type = pt;

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a rtcpfb pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_rtcpfb_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_rtcpfb_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            return upipe_rtcpfb_attach_upump_mgr(upipe);
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtcpfb_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_rtcpfb_get_sub_mgr(upipe, p);
        }
        case UPIPE_SET_OPTION: {
            const char *k = va_arg(args, const char *);
            const char *v = va_arg(args, const char *);
            if (strcmp(k, "latency"))
                return UBASE_ERR_INVALID;

            struct upipe_rtcpfb *upipe_rtcpfb = upipe_rtcpfb_from_upipe(upipe);
            upipe_rtcpfb->latency = atoi(v);
            upipe_dbg_va(upipe, "Set latency to %"PRIu64" msecs",
                    upipe_rtcpfb->latency);
            return UBASE_ERR_NONE;
        }
        case UPIPE_RTCPFB_SET_RTX_PT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTCPFB_SIGNATURE);
            uint8_t pt = va_arg(args, unsigned);
            return _upipe_rtcpfb_set_pt(upipe, pt);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static int upipe_rtcpfb_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_rtcpfb_control(upipe, command, args))
    return upipe_rtcpfb_check(upipe, NULL);
}

/** @internal @This frees all resources allocated.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_rtcpfb_free(struct urefcount *urefcount_real)
{
    struct upipe_rtcpfb *upipe_rtcpfb = upipe_rtcpfb_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_rtcpfb_to_upipe(upipe_rtcpfb);
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);

    uref_free(upipe_rtcpfb->flow_def_input);
    upipe_rtcpfb_clean_output(upipe);
    upipe_rtcpfb_clean_sub_outputs(upipe);
    upipe_rtcpfb_clean_urefcount(upipe);
    upipe_rtcpfb_clean_ubuf_mgr(upipe);
    upipe_rtcpfb_clean_uref_mgr(upipe);
    upump_stop(upipe_rtcpfb->upump_timer);
    upump_free(upipe_rtcpfb->upump_timer);
    upipe_rtcpfb_clean_upump_mgr(upipe);
    upipe_rtcpfb_clean_uclock(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_rtcpfb->queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }

    upipe_rtcpfb_free_void(upipe);
}

static struct upipe_mgr upipe_rtcpfb_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTCPFB_SIGNATURE,

    .upipe_alloc = upipe_rtcpfb_alloc,
    .upipe_input = upipe_rtcpfb_input,
    .upipe_control = upipe_rtcpfb_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for rtcpfb pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtcpfb_mgr_alloc(void)
{
    return &upipe_rtcpfb_mgr;
}
