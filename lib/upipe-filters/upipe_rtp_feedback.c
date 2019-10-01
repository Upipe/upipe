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
 * @short Upipe module sending retransmit requests for lost RTP packets
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/ustring.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe-filters/upipe_rtp_feedback.h>
#include <upipe-modules/upipe_udp_sink.h>

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
#include <bitstream/ietf/rtcp_sdes.h>
#include <bitstream/ietf/rtcp_rr.h>
#include <bitstream/ietf/rtcp_sr.h>
#include <bitstream/ietf/rtcp3611.h>

#define EXPECTED_FLOW_DEF "block."

/** upipe_rtpfb structure */
struct upipe_rtpfb {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    struct upipe_mgr sub_mgr;
    /** list of output subpipes */
    struct uchain outputs;

    struct upump_mgr *upump_mgr;
    struct upump *upump_timer;
    struct upump *upump_timer_lost;
    struct uclock *uclock;
    struct urequest uclock_request;
    struct uchain queue;
    struct uprobe *uprobe;

    /** expected sequence number */
    unsigned expected_seqnum;

    /** last seq output */
    unsigned last_output_seqnum;

    /* stats */
    size_t buffered;
    size_t nacks;
    size_t repaired;
    size_t loss;
    size_t dups;

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

    /** buffer latency */
    uint64_t latency;

    struct upipe *rtpfb_output;

    /** last time a NACK was sent */
    uint64_t last_nack[65536];

    uint64_t rtt;

    /** public upipe structure */
    struct upipe upipe;
};

static int upipe_rtpfb_check(struct upipe *upipe, struct uref *flow_format);


UPIPE_HELPER_UPIPE(upipe_rtpfb, upipe, UPIPE_RTPFB_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_rtpfb, urefcount, upipe_rtpfb_no_input);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_rtpfb, urefcount_real, upipe_rtpfb_free);
UPIPE_HELPER_VOID(upipe_rtpfb);
UPIPE_HELPER_OUTPUT(upipe_rtpfb, output, flow_def, output_state, request_list);
UPIPE_HELPER_UPUMP_MGR(upipe_rtpfb, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_rtpfb, upump_timer, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_rtpfb, upump_timer_lost, upump_mgr)
UPIPE_HELPER_UCLOCK(upipe_rtpfb, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

struct upipe_rtpfb_output {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** ubuf mgr structures */
    struct ubuf_mgr *ubuf_mgr;
    struct urequest ubuf_mgr_request;
    struct uref *flow_format;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** last Sender Report (SR) received */
    uint8_t sr[RTCP_SR_SIZE];
    /** timestamp of last SR */
    uint64_t sr_cr;

    /** timestamp of last XR */
    uint64_t xr_cr;

    /** cname */
    char *name;

    /** public upipe structure */
    struct upipe upipe;
};

static int upipe_rtpfb_output_check(struct upipe *upipe, struct uref *flow_format);
UPIPE_HELPER_UPIPE(upipe_rtpfb_output, upipe, UPIPE_RTPFB_OUTPUT_SIGNATURE)
UPIPE_HELPER_VOID(upipe_rtpfb_output);
UPIPE_HELPER_UREFCOUNT(upipe_rtpfb_output, urefcount, upipe_rtpfb_output_free)
UPIPE_HELPER_OUTPUT(upipe_rtpfb_output, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_rtpfb_output, uref_mgr, uref_mgr_request,
                      upipe_rtpfb_output_check,
                      upipe_rtpfb_output_register_output_request,
                      upipe_rtpfb_output_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_rtpfb_output, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_rtpfb_output_check,
                      upipe_rtpfb_output_register_output_request,
                      upipe_rtpfb_output_unregister_output_request)
UPIPE_HELPER_SUBPIPE(upipe_rtpfb, upipe_rtpfb_output, output, sub_mgr, outputs,
                     uchain)

static int upipe_rtpfb_output_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_rtpfb_output *upipe_rtpfb_output = upipe_rtpfb_output_from_upipe(upipe);
    if (flow_format)
        upipe_rtpfb_output_store_flow_def(upipe, flow_format);

    if (upipe_rtpfb_output->uref_mgr == NULL) {
        upipe_rtpfb_output_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_rtpfb_output->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_rtpfb_output->uref_mgr, NULL);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_rtpfb_output_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    return UBASE_ERR_NONE;
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtpfb_no_input(struct upipe *upipe)
{
    upipe_rtpfb_throw_sub_outputs(upipe, UPROBE_SOURCE_END);
    upipe_rtpfb_release_urefcount_real(upipe);
}

/** @internal @This allocates an output subpipe of a dup pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtpfb_output_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    if (mgr->signature != UPIPE_RTPFB_OUTPUT_SIGNATURE)
        return NULL;

    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_sub_mgr(mgr);
    if (upipe_rtpfb->rtpfb_output)
        return NULL;

    struct upipe *upipe = upipe_rtpfb_output_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_rtpfb_output *upipe_rtpfb_output = upipe_rtpfb_output_from_upipe(upipe);

    upipe_rtpfb_output->sr_cr = UINT64_MAX;
    upipe_rtpfb_output->xr_cr = UINT64_MAX;
    upipe_rtpfb_output->name = NULL;
    upipe_rtpfb->rtpfb_output = upipe;

    upipe_rtpfb_output_init_urefcount(upipe);
    upipe_rtpfb_output_init_output(upipe);
    upipe_rtpfb_output_init_sub(upipe);
    upipe_rtpfb_output_init_ubuf_mgr(upipe);
    upipe_rtpfb_output_init_uref_mgr(upipe);

    upipe_throw_ready(upipe);

    upipe_rtpfb_output_require_uref_mgr(upipe);

    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtpfb_output_free(struct upipe *upipe)
{
    struct upipe_rtpfb_output *upipe_rtpfb_output = upipe_rtpfb_output_from_upipe(upipe);
    upipe_throw_dead(upipe);

    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_sub_mgr(upipe->mgr);
    upipe_rtpfb->rtpfb_output = NULL;
    free(upipe_rtpfb_output->name);
    upipe_rtpfb_output_clean_output(upipe);
    upipe_rtpfb_output_clean_sub(upipe);
    upipe_rtpfb_output_clean_urefcount(upipe);
    upipe_rtpfb_output_clean_ubuf_mgr(upipe);
    upipe_rtpfb_output_clean_uref_mgr(upipe);
    upipe_rtpfb_output_free_void(upipe);
}

/** @internal @This sends a retransmission request for a number of seqnums.
 *
 * @param upipe description structure of the pipe
 * @param lost_seqnum First sequence number missing
 * @param seqnum First sequence number NOT missing
 * @param ssrc TODO
 */
static void upipe_rtpfb_output_lost(struct upipe *upipe, uint16_t lost_seqnum, uint16_t seqnum, uint8_t *ssrc)
{
    struct upipe_rtpfb_output *upipe_rtpfb_output = upipe_rtpfb_output_from_upipe(upipe);
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_sub_mgr(upipe->mgr);

    /* Send a single NACK packet, with a single FCI */
    int s = RTCP_FB_HEADER_SIZE + 1 * RTCP_FB_FCI_GENERIC_NACK_SIZE;

    /* Allocate NACK packet */
    struct uref *pkt = uref_block_alloc(upipe_rtpfb_output->uref_mgr,
        upipe_rtpfb_output->ubuf_mgr, s);
    if (unlikely(!pkt)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *buf;
    uref_block_write(pkt, 0, &s, &buf);
    memset(buf, 0, s);

    /* Header */
    rtcp_set_rtp_version(buf);
    rtcp_fb_set_fmt(buf, RTCP_PT_RTPFB_GENERIC_NACK);
    rtcp_set_pt(buf, RTCP_PT_RTPFB);

    // TODO : make receiver SSRC configurable
    uint8_t ssrc_sender[4] = { 0x1, 0x2, 0x3, 0x4 };
    rtcp_fb_set_ssrc_pkt_sender(buf, ssrc_sender);
    rtcp_fb_set_ssrc_media_src(buf, ssrc);

    uint8_t *fci = &buf[RTCP_FB_HEADER_SIZE];
    rtcp_fb_nack_set_packet_id(fci, lost_seqnum);

    uint16_t pkts = seqnum - 1 - lost_seqnum;
    // TODO : add several FCI if more than 17 packets are missing
    if (pkts > 16)
        pkts = 16;

    uint16_t bits = 0;
    for (size_t i = 0; i < pkts; i++)
        bits |= 1 << i;

    rtcp_fb_nack_set_bitmask_lost(fci, bits);
    upipe_rtpfb->nacks += pkts + 1;

    rtcp_set_length(buf, s / 4 - 1);

    upipe_verbose_va(upipe, "NACKing %hu (+0x%hx)", lost_seqnum, bits);

    uref_block_unmap(pkt, 0);

    // XXX : date NACK packet?
    //uref_clock_set_date_sys(pkt, /* cr */ 0, UREF_DATE_CR);

    upipe_rtpfb_output_output(upipe, pkt, NULL);
}

static uint64_t _upipe_rtpfb_get_rtt(struct upipe *upipe)
{
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);

    /* VSF TR-06 doesn't give a mean to retrieve RTT, but defaults to 7
     * retransmissions requests per packet.
     * XXX: make it configurable ? */

    uint64_t rtt = upipe_rtpfb->rtt;
    if (!rtt)
        rtt = upipe_rtpfb->latency / 7;
    return rtt;
}

/** @internal @This periodic timer checks for missing seqnums.
 */
static void upipe_rtpfb_timer_lost(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);

    uint64_t expected_seq = UINT64_MAX;

    uint64_t rtt = _upipe_rtpfb_get_rtt(upipe);

    uint64_t now = uclock_now(upipe_rtpfb->uclock);

    /* space out NACKs a bit more than RTT. XXX: tune me */
    uint64_t next_nack = now - rtt * 12 / 10;

    /* TODO: do not look at the last pkts/s * rtt
     * It it too late to send a NACK for these
     * XXX: use cr_sys, because pkts/s also accounts for
     * the retransmitted packets */

    struct uchain *uchain;
    int holes = 0;
    ulist_foreach(&upipe_rtpfb->queue, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        uint64_t seqnum = 0;
        uref_attr_get_priv(uref, &seqnum);

        if (likely(expected_seq != UINT64_MAX) && seqnum != expected_seq) {
            /* hole found */
            uint8_t ssrc[4] = {0,}; // TODO
            upipe_dbg_va(upipe, "Found hole from %"PRIu64" (incl) to %"PRIu64" (excl)",
                expected_seq, seqnum);

            for (uint16_t seq = expected_seq; seq != seqnum; seq++) {
                /* if packet was lost, we should have detected it already */
                if (upipe_rtpfb->last_nack[seq] == 0) {
                    upipe_err_va(upipe, "packet %hu missing but was not marked as lost!", seq);
                    continue;
                }

                /* if we sent a NACK not too long ago, do not repeat it */
                /* since NACKs are sent in a batch, break loop if the first packet is too early */
                if (upipe_rtpfb->last_nack[seq] > next_nack) {
                    if (0) upipe_err_va(upipe, "Cancelling NACK due to RTT (seq %hu diff %"PRId64"",
                        seq, next_nack - upipe_rtpfb->last_nack[seq]
                    );
                    goto next;
                }
            }

            /* update NACK request time */
            for (uint16_t seq = expected_seq; seq != seqnum; seq++) {
                upipe_rtpfb->last_nack[seq] = now;
            }

            /* TODO:
                - check the following packets to fill in bitmask
                - send request in a single batch (multiple FCI)
             */
            if (upipe_rtpfb->rtpfb_output)
                upipe_rtpfb_output_lost(upipe_rtpfb->rtpfb_output, expected_seq, seqnum, ssrc);
            holes++;
        }

next:
        expected_seq = (seqnum + 1) & UINT16_MAX;
    }

    if (holes) { /* debug stats */
        uint64_t now = uclock_now(upipe_rtpfb->uclock);
        static uint64_t old;
        if (likely(old != 0))
            upipe_dbg_va(upipe, "%d holes after %"PRIu64" ms",
                    holes, 1000 * (now - old) / UCLOCK_FREQ);
        old = now;
    }
}

/** @internal @This periodic timer remove seqnums from the buffer.
 */
static void upipe_rtpfb_timer(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);

    uint64_t now = uclock_now(upipe_rtpfb->uclock);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_rtpfb->queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        uint64_t seqnum = 0;
        if (!ubase_check(uref_attr_get_priv(uref, &seqnum))) {
            upipe_err_va(upipe, "Could not read seqnum from uref");
        }

        uint64_t cr_sys = 0;
        if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &cr_sys))))
            upipe_warn_va(upipe, "Couldn't read cr_sys in %s()", __func__);

        if (now - cr_sys <= upipe_rtpfb->latency)
            break;

        upipe_verbose_va(upipe, "Output seq %"PRIu64" after %"PRIu64" clocks", seqnum, now - cr_sys);
        if (likely(upipe_rtpfb->last_output_seqnum != UINT_MAX)) {
            uint16_t diff = seqnum - upipe_rtpfb->last_output_seqnum - 1;
            if (diff) {
                upipe_rtpfb->loss += diff;
                upipe_dbg_va(upipe, "PKT LOSS: %u -> %"PRIu64" DIFF %hu",
                        upipe_rtpfb->last_output_seqnum, seqnum, diff);
            }
        }

        upipe_rtpfb->last_output_seqnum = seqnum;

        ulist_delete(uchain);
        upipe_rtpfb_output(upipe, uref, NULL); // XXX: use timer upump ?
        if (--upipe_rtpfb->buffered == 0) {
            upipe_warn_va(upipe, "Exhausted buffer");
            upipe_rtpfb->expected_seqnum = UINT_MAX;
        }
    }
}

static void upipe_rtpfb_restart_timer(struct upipe *upipe)
{
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);
    uint64_t rtt = _upipe_rtpfb_get_rtt(upipe);

    upipe_rtpfb_set_upump_timer_lost(upipe, NULL);
    if (upipe_rtpfb->upump_mgr) {
        struct upump *upump=
            upump_alloc_timer(upipe_rtpfb->upump_mgr,
                              upipe_rtpfb_timer_lost,
                              upipe, upipe->refcount,
                              0, rtt / 10);
        upump_start(upump);
        upipe_rtpfb_set_upump_timer_lost(upipe, upump);
    }
}

static int upipe_rtpfb_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);

    upipe_rtpfb_check_upump_mgr(upipe);

    if (flow_format != NULL) {
        uint64_t latency;
        if (!ubase_check(uref_clock_get_latency(flow_format, &latency)))
            latency = 0;
        uref_clock_set_latency(flow_format, latency + upipe_rtpfb->latency);
        upipe_rtpfb_store_flow_def(upipe, flow_format);
    }

    if (upipe_rtpfb->flow_def == NULL)
        return UBASE_ERR_NONE;

    if (upipe_rtpfb->upump_mgr && !upipe_rtpfb->upump_timer) {
        struct upump *upump =
            upump_alloc_timer(upipe_rtpfb->upump_mgr,
                              upipe_rtpfb_timer,
                              upipe, upipe->refcount,
                              UCLOCK_FREQ/300, UCLOCK_FREQ/300);
        upump_start(upump);
        upipe_rtpfb_set_upump_timer(upipe, upump);

        /* every 10ms, check for lost packets
         * interval is reduced each time we get the current RTT from sender */
        upipe_rtpfb_restart_timer(upipe);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_rtpfb_output_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    return UBASE_ERR_NONE;
}

static int _upipe_rtpfb_output_get_name(struct upipe *upipe, const char **name_p)
{
    struct upipe_rtpfb_output *upipe_rtpfb_output = upipe_rtpfb_output_from_upipe(upipe);
    *name_p = upipe_rtpfb_output->name;
    return UBASE_ERR_NONE;
}

static int _upipe_rtpfb_output_set_name(struct upipe *upipe, const char *name)
{
    struct upipe_rtpfb_output *upipe_rtpfb_output = upipe_rtpfb_output_from_upipe(upipe);
    free(upipe_rtpfb_output->name);
    upipe_rtpfb_output->name = name ? strdup(name) : NULL;
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
static int _upipe_rtpfb_output_control(struct upipe *upipe,
                                    int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_rtpfb_output_control_super(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_rtpfb_output_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_RTPFB_OUTPUT_GET_NAME: {
            unsigned int signature = va_arg(args, unsigned int);
            if (signature != UPIPE_RTPFB_OUTPUT_SIGNATURE)
                return UBASE_ERR_INVALID;
            const char **name_p = va_arg(args, const char **);
            return _upipe_rtpfb_output_get_name(upipe, name_p);
        }
        case UPIPE_RTPFB_OUTPUT_SET_NAME: {
            unsigned int signature = va_arg(args, unsigned int);
            if (signature != UPIPE_RTPFB_OUTPUT_SIGNATURE)
                return UBASE_ERR_INVALID;
            const char *name = va_arg(args, const char *);
            return _upipe_rtpfb_output_set_name(upipe, name);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtpfb_output_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static int upipe_rtpfb_output_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_rtpfb_output_control(upipe, command, args))
    return upipe_rtpfb_output_check(upipe, NULL);
}

static void upipe_rtpfb_output_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p);

/** @internal @This initializes the output manager for a rtpfb set pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtpfb_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_rtpfb->sub_mgr;
    sub_mgr->refcount = upipe_rtpfb_to_urefcount_real(upipe_rtpfb);
    sub_mgr->signature = UPIPE_RTPFB_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_rtpfb_output_alloc;
    sub_mgr->upipe_input = upipe_rtpfb_output_input;
    sub_mgr->upipe_control = upipe_rtpfb_output_control;
    sub_mgr->upipe_mgr_control = NULL;
}

static void upipe_rtpfb_output_send_rr_xr(struct upipe *upipe)
{
    struct upipe_rtpfb_output *upipe_rtpfb_output = upipe_rtpfb_output_from_upipe(upipe);
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_sub_mgr(upipe->mgr);

    struct ustring name = ustring_from_str(upipe_rtpfb_output->name);

    int sdes_size = RTCP_SDES_SIZE + name.len + 1; /* 0 byte */
    sdes_size = (sdes_size + 3) & ~3; /* align to 32 bits */

    int s = RTCP_RR_SIZE + RTCP_XR_HEADER_SIZE + RTCP_XR_RRTP_SIZE + sdes_size;

    /* Allocate NACK packet */
    struct uref *pkt = uref_block_alloc(upipe_rtpfb_output->uref_mgr,
        upipe_rtpfb_output->ubuf_mgr, s);
    if (unlikely(!pkt)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *buf;
    uref_block_write(pkt, 0, &s, &buf);
    memset(buf, 0, s);

    /* Header */
    rtcp_set_rtp_version(buf);
    rtcp_set_rc(buf, 1);
    rtcp_rr_set_pt(buf);
    rtcp_set_length(buf, RTCP_RR_SIZE / 4 - 1);

    // XXX: useless if loss < 1/256
    rtcp_rr_set_fraction_lost(buf, 0);

    // XXX: https://tools.ietf.org/html/rfc3550#appendix-A.3
    // XXX: this is a signed number
    rtcp_rr_set_cumulative_packets_lost(buf, upipe_rtpfb->loss - upipe_rtpfb->dups);

    // XXX: top 16 bits is number of cycles
    rtcp_rr_set_highest_seqnum(buf, upipe_rtpfb->last_output_seqnum);

    // XXX: we don't need that
    rtcp_rr_set_inter_arrival_jitter(buf, 0);

    if (upipe_rtpfb_output->sr_cr == UINT64_MAX) {
        rtcp_rr_set_last_sr(buf, 0);
        rtcp_rr_set_delay_since_last_sr(buf, 0);
    } else {
        uint32_t ntp_msw = rtcp_sr_get_ntp_time_msw(upipe_rtpfb_output->sr);
        uint32_t ntp_lsw = rtcp_sr_get_ntp_time_lsw(upipe_rtpfb_output->sr);
        rtcp_rr_set_last_sr(buf, ((ntp_msw & 0xffff) << 16) | (ntp_lsw >> 16));

        rtcp_rr_set_delay_since_last_sr(buf, 0);
    }

    buf = &buf[RTCP_RR_SIZE];
    /* Header */
    rtcp_set_rtp_version(buf);
    rtcp_set_pt(buf, RTCP_PT_XR); // XR
    rtcp_set_length(buf, (RTCP_XR_HEADER_SIZE + RTCP_XR_RRTP_SIZE) / 4 - 1);

    static const uint8_t ssrc[4] = { 0, 0, 0, 0 };
    rtcp_xr_set_ssrc_sender(buf, ssrc);

    buf += RTCP_XR_HEADER_SIZE;
    rtcp_xr_set_bt(buf, RTCP_XR_RRTP_BT);
    rtcp_xr_rrtp_set_reserved(buf);
    rtcp_xr_set_length(buf, RTCP_XR_RRTP_SIZE / 4 - 1);

    uint64_t now = uclock_now(upipe_rtpfb->uclock);
    upipe_rtpfb_output->xr_cr = now;
    rtcp_xr_rrtp_set_ntp(buf, now);

    buf = &buf[RTCP_XR_RRTP_SIZE];
    rtcp_set_rtp_version(buf);
    rtp_set_cc(buf, 1);
    rtcp_set_length(buf, (sdes_size / 4) - 1);
    rtcp_sdes_set_pt(buf);
    rtcp_sdes_set_cname(buf, 1);
    rtcp_sdes_set_name_length(buf, name.len);
    if (!ustring_is_null(name))
        memcpy(&buf[RTCP_SDES_SIZE], name.at, name.len);

    uref_block_unmap(pkt, 0);

    upipe_rtpfb_output_output(upipe, pkt, NULL);
}

/** @internal @This allocates a rtpfb pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtpfb_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_rtpfb_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);
    upipe_rtpfb_init_urefcount(upipe);
    upipe_rtpfb_init_urefcount_real(upipe);
    upipe_rtpfb_init_output(upipe);
    upipe_rtpfb_init_sub_mgr(upipe);
    upipe_rtpfb_init_sub_outputs(upipe);
    upipe_rtpfb->expected_seqnum = UINT_MAX;
    upipe_rtpfb->flow_def_input = NULL;
    upipe_rtpfb_init_upump_mgr(upipe);
    upipe_rtpfb_init_upump_timer(upipe);
    upipe_rtpfb_init_upump_timer_lost(upipe);
    upipe_rtpfb_init_uclock(upipe);
    ulist_init(&upipe_rtpfb->queue);
    memset(upipe_rtpfb->last_nack, 0, sizeof(upipe_rtpfb->last_nack));
    upipe_rtpfb->rtt = 0;
    upipe_rtpfb_require_uclock(upipe);
    upipe_rtpfb->rtpfb_output = NULL;
    upipe_rtpfb->uprobe = uprobe_use(uprobe);
    upipe_rtpfb->last_output_seqnum = UINT_MAX;
    upipe_rtpfb->buffered = 0;
    upipe_rtpfb->nacks = 0;
    upipe_rtpfb->repaired = 0;
    upipe_rtpfb->loss = 0;
    upipe_rtpfb->dups = 0;

    upipe_rtpfb->latency = UCLOCK_FREQ;

    upipe_throw_ready(upipe);
    return upipe;
}

/* returns true if uref was inserted in the queue */
static bool upipe_rtpfb_insert_inner(struct upipe *upipe, struct uref *uref,
        const uint16_t seqnum, struct uref *next)
{
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);
    uint64_t next_seqnum = 0;
    uref_attr_get_priv(next, &next_seqnum);

    uint16_t diff = seqnum - next_seqnum;
    if (!diff) {
        upipe_verbose_va(upipe, "dropping duplicate %hu", seqnum);
        upipe_rtpfb->dups++;
        uref_free(uref);
        return true;
    }

    /* browse the list until we find a seqnum bigger than ours */
    if (diff < 0x8000) // seqnum > next_seqnum
        return false;

    /* if there's no previous packet we're too late */
    struct uchain *uchain = uref_to_uchain(next);
    if (unlikely(ulist_is_first(&upipe_rtpfb->queue, uchain))) {
        upipe_dbg_va(upipe,
                "LATE packet drop: Expected %u, got %hu, didn't insert after %"PRIu64,
                upipe_rtpfb->expected_seqnum, seqnum, next_seqnum);
        uref_free(uref);
        return true;
    }

    /* Read previous packet seq & cr_sys */
    uint64_t prev_seqnum = 0, cr_sys = 0;

    struct uref *prev = uref_from_uchain(uchain->prev);
    uref_attr_get_priv(prev, &prev_seqnum);

    /* overwrite this uref' cr_sys with previous one's
     * so it get scheduled at the right time */
    if (ubase_check(uref_clock_get_cr_sys(prev, &cr_sys)))
        uref_clock_set_cr_sys(uref, cr_sys);
    else
        upipe_err_va(upipe, "Couldn't read cr_sys in %s() - %zu buffered",
                __func__, upipe_rtpfb->buffered);

    upipe_rtpfb->buffered++;
    ulist_insert(uchain->prev, uchain, uref_to_uchain(uref));
    upipe_rtpfb->repaired++;
    upipe_rtpfb->last_nack[seqnum] = 0;

    upipe_dbg_va(upipe, "Repaired %"PRIu64" > %hu > %"PRIu64" -diff %d",
            prev_seqnum, seqnum, next_seqnum, -diff);

    return true;
}

static bool upipe_rtpfb_insert(struct upipe *upipe, struct uref *uref, const uint16_t seqnum)
{
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_rtpfb->queue, uchain, uchain_tmp) {
        struct uref *next = uref_from_uchain(uchain);
        if (upipe_rtpfb_insert_inner(upipe, uref, seqnum, next))
            return true;
    }

    /* Could not insert packet */
    return false;
}

/** @internal @This handles RTCP data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_rtpfb_output_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_rtpfb_output *upipe_rtpfb_output = upipe_rtpfb_output_from_upipe(upipe);
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_sub_mgr(upipe->mgr);

    if (upipe_rtpfb_output->uref_mgr == NULL || upipe_rtpfb_output->ubuf_mgr == NULL) {
        upipe_rtpfb_output_check(upipe, NULL);
        uref_free(uref);
        return;
    }

    const uint8_t *buf;
    int s = -1;
    if (!ubase_check(uref_block_read(uref, 0, &s, &buf))) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }

    if (s < RTP_HEADER_SIZE) {
        goto unmap;
    }

    /* parse RTP header */
    bool valid = rtp_check_hdr(buf);
    uint8_t pt = rtcp_get_pt(buf);

    if (unlikely(!valid)) {
        upipe_warn(upipe, "invalid RTP header");
        goto unmap;
    }

    if (pt == RTCP_PT_SR) {
        upipe_verbose(upipe, "received sender report");
        uint16_t len = rtcp_get_length(buf);
        uint64_t ntp = ((uint64_t)rtcp_sr_get_ntp_time_msw(buf) << 32) |
            rtcp_sr_get_ntp_time_lsw(buf);
        uint32_t rtp_pts = rtcp_sr_get_rtp_time(buf);
        uint32_t pkt_cnt = rtcp_sr_get_packet_count(buf);
        uint32_t byte_cnt = rtcp_sr_get_octet_count(buf);
        uint64_t cr = 0;
        if (!ubase_check(uref_clock_get_cr_sys(uref, &cr))) {
            upipe_err(upipe, "SR packet not timed");
        }
        upipe_verbose_va(upipe, "len %d NTP %"PRIx64" rtp %u pkts %u bytes %u, CR %"PRIu64"",
                len, ntp, rtp_pts, pkt_cnt, byte_cnt, cr);

        if (upipe_rtpfb_output->sr_cr != UINT64_MAX) {
            uint32_t prev_pkt = rtcp_sr_get_packet_count(upipe_rtpfb_output->sr);
            uint32_t prev_byt = rtcp_sr_get_octet_count(upipe_rtpfb_output->sr);

            upipe_verbose_va(upipe, "%.2f pkts/s %.2fMbps",
                    (float)(pkt_cnt - prev_pkt) * UCLOCK_FREQ / (cr - upipe_rtpfb_output->sr_cr),
                    (float)(byte_cnt - prev_byt) * 8 * UCLOCK_FREQ / (cr - upipe_rtpfb_output->sr_cr) / 1000 / 1000);
        }

        memcpy(upipe_rtpfb_output->sr, buf, sizeof(upipe_rtpfb_output->sr));
        upipe_rtpfb_output->sr_cr = cr;

        upipe_rtpfb_output_send_rr_xr(upipe);
    } else if (pt == RTCP_PT_XR) {
        upipe_verbose(upipe, "XR RTCP");
        if (s < RTCP_XR_HEADER_SIZE + RTCP_XR_DLRR_SIZE)
            goto unmap;
        if ((rtcp_get_length(buf) + 1) * 4 < RTCP_XR_HEADER_SIZE + RTCP_XR_DLRR_SIZE)
            goto unmap;

        buf += RTCP_XR_HEADER_SIZE;

        if (rtcp_xr_get_bt(buf) != RTCP_XR_DLRR_BT)
            goto unmap;
        if ((rtcp_xr_get_length(buf) + 1) * 4 != RTCP_XR_DLRR_SIZE)
            goto unmap;

        uint32_t last_rr = rtcp_xr_dlrr_get_lrr(buf);

        if (last_rr != ((upipe_rtpfb_output->xr_cr >> 16) & 0xffffffff)) {
            upipe_warn_va(upipe,
                    "DLRR not for last RRT : %" PRIx64 " , %x",
                    (upipe_rtpfb_output->xr_cr >> 16) & 0xffffffff, last_rr);
            goto unmap;
        }

        uint32_t delay = rtcp_xr_dlrr_get_dlrr(buf);

        uint64_t rtt = uclock_now(upipe_rtpfb->uclock) -
            upipe_rtpfb_output->xr_cr - delay * UCLOCK_FREQ / 65536;

        upipe_notice_va(upipe, "RTT %f", (float)rtt / UCLOCK_FREQ);
        upipe_rtpfb->rtt = rtt;
        upipe_rtpfb_restart_timer(upipe_rtpfb_to_upipe(upipe_rtpfb));
    }

unmap:
    uref_block_unmap(uref, 0);
    uref_free(uref);
}

/** @internal @This handles RTP data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_rtpfb_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);
    uint8_t rtp_buffer[RTP_HEADER_SIZE];
    const uint8_t *rtp_header = uref_block_peek(uref, 0, RTP_HEADER_SIZE,
                                                rtp_buffer);
    if (unlikely(rtp_header == NULL)) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }

    /* parse RTP header */
    bool valid = rtp_check_hdr(rtp_header);
    uint16_t seqnum = rtp_get_seqnum(rtp_header);

    uint8_t ssrc[4]; // TODO: use me
    rtp_get_ssrc(rtp_header, ssrc);

    uref_block_peek_unmap(uref, 0, rtp_buffer, rtp_header);
    if (unlikely(!valid)) {
        upipe_warn(upipe, "invalid RTP header");
        uref_free(uref);
        return;
    }

    if (upipe_rtpfb->upump_mgr == NULL) {
        uref_free(uref);
        return;
    }

    /* store seqnum in uref */
    uref_attr_set_priv(uref, seqnum);

    /* first packet */
    if (unlikely(upipe_rtpfb->expected_seqnum == UINT_MAX))
        upipe_rtpfb->expected_seqnum = seqnum;

    uint16_t diff = seqnum - upipe_rtpfb->expected_seqnum;

    if (diff < 0x8000) { // seqnum > last seq, insert at the end
        /* packet is from the future */
        upipe_rtpfb->buffered++;
        ulist_add(&upipe_rtpfb->queue, uref_to_uchain(uref));
        upipe_rtpfb->last_nack[seqnum] = 0;

        if (diff != 0) {
            uint64_t rtt = _upipe_rtpfb_get_rtt(upipe);
            /* wait a bit to send a NACK, in case of reordering */
            uint64_t fake_last_nack = uclock_now(upipe_rtpfb->uclock) - rtt;
            for (uint16_t seq = upipe_rtpfb->expected_seqnum; seq != seqnum; seq++)
                if (upipe_rtpfb->last_nack[seq] == 0)
                    upipe_rtpfb->last_nack[seq] = fake_last_nack;
        }

        upipe_rtpfb->expected_seqnum = seqnum + 1;
        return;
    }

    /* packet is from the past, reordered or retransmitted */
    if (upipe_rtpfb_insert(upipe, uref, seqnum))
        return;

    uint64_t first_seq = 0, last_seq = 0;
    uref_attr_get_priv(uref_from_uchain(upipe_rtpfb->queue.next), &first_seq);
    uref_attr_get_priv(uref_from_uchain(upipe_rtpfb->queue.prev), &last_seq);
    // XXX : when much too late, it could mean RTP source restart
    upipe_err_va(upipe, "LATE packet %hu, dropped (buffered %"PRIu64" -> %"PRIu64")",
            seqnum, first_seq, last_seq);
    uref_free(uref);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_rtpfb_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);
    uref_free(upipe_rtpfb->flow_def_input);
    upipe_rtpfb->flow_def_input = uref_dup(flow_def);

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL))
        return UBASE_ERR_ALLOC;

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL))
        return UBASE_ERR_ALLOC;
    uint64_t latency = 0;
    if (!ubase_check(uref_clock_get_latency(flow_def, &latency)))
        latency = 0;
    uref_clock_set_latency(flow_def, latency + upipe_rtpfb->latency);
    upipe_rtpfb_store_flow_def(upipe, flow_def);

    if (upipe_rtpfb->rtpfb_output)
        upipe_rtpfb_output_store_flow_def(upipe_rtpfb->rtpfb_output, flow_def_dup);
    else
        uref_free(flow_def_dup);

    return UBASE_ERR_NONE;
}

static int upipe_rtpfb_set_option(struct upipe *upipe, const char *k, const char *v)
{
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);

    if (!k || !v) {
        return UBASE_ERR_INVALID;
    } else if (!strcmp(k, "latency")) {
        upipe_dbg_va(upipe, "Setting latency to %s msecs", v);
        upipe_rtpfb->latency = atoi(v) * UCLOCK_FREQ / 1000;
    } else
        return UBASE_ERR_INVALID;

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a rtpfb pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_rtpfb_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_rtpfb_control_output(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_rtpfb_control_outputs(upipe, command, args));
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_rtpfb_set_upump_timer(upipe, NULL);
            upipe_rtpfb_set_upump_timer_lost(upipe, NULL);
            return upipe_rtpfb_attach_upump_mgr(upipe);
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtpfb_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SET_OPTION: {
            const char *k = va_arg(args, const char *);
            const char *v = va_arg(args, const char *);
            return upipe_rtpfb_set_option(upipe, k, v);
        }
        case UPIPE_RTPFB_GET_STATS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTPFB_SIGNATURE)
            unsigned *expected_seqnum    = va_arg(args, unsigned*);
            unsigned *last_output_seqnum = va_arg(args, unsigned*);
            size_t   *buffered           = va_arg(args, size_t*);
            size_t   *nacks              = va_arg(args, size_t*);
            size_t   *repaired           = va_arg(args, size_t*);
            size_t   *loss               = va_arg(args, size_t*);
            size_t   *dups               = va_arg(args, size_t*);

            struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);
            *buffered = upipe_rtpfb->buffered;
            *expected_seqnum = upipe_rtpfb->expected_seqnum;
            *last_output_seqnum = upipe_rtpfb->last_output_seqnum;
            *nacks = upipe_rtpfb->nacks;
            *repaired = upipe_rtpfb->repaired;
            *loss = upipe_rtpfb->loss;
            *dups = upipe_rtpfb->dups;

            upipe_rtpfb->nacks = 0;
            upipe_rtpfb->repaired = 0;
            upipe_rtpfb->loss = 0;
            upipe_rtpfb->dups = 0;

            return UBASE_ERR_NONE;
        }
        case UPIPE_RTPFB_GET_RTT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTPFB_SIGNATURE)
            uint64_t *rtt = va_arg(args, uint64_t *);
            struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);
            *rtt = upipe_rtpfb->rtt;
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static int upipe_rtpfb_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_rtpfb_control(upipe, command, args))
    return upipe_rtpfb_check(upipe, NULL);
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtpfb_free(struct upipe *upipe)
{
    struct upipe_rtpfb *upipe_rtpfb = upipe_rtpfb_from_upipe(upipe);
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);

    uref_free(upipe_rtpfb->flow_def_input);
    upipe_rtpfb_clean_output(upipe);
    upipe_rtpfb_clean_urefcount(upipe);
    upipe_rtpfb_clean_urefcount_real(upipe);
    upipe_release(upipe_rtpfb->rtpfb_output);
    upipe_rtpfb_clean_upump_timer_lost(upipe);
    upipe_rtpfb_clean_upump_timer(upipe);
    upipe_rtpfb_clean_upump_mgr(upipe);
    upipe_rtpfb_clean_uclock(upipe);
    upipe_rtpfb_clean_sub_outputs(upipe);
    uprobe_release(upipe_rtpfb->uprobe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_rtpfb->queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }

    upipe_rtpfb_free_void(upipe);
}

static struct upipe_mgr upipe_rtpfb_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTPFB_SIGNATURE,

    .upipe_alloc = upipe_rtpfb_alloc,
    .upipe_input = upipe_rtpfb_input,
    .upipe_control = upipe_rtpfb_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for rtpfb pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtpfb_mgr_alloc(void)
{
    return &upipe_rtpfb_mgr;
}
