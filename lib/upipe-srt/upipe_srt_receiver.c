/*
 * Copyright (C) 2023 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
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
 * @short Upipe module for SRT receivers
 */

#include "upipe/config.h"
#include "upipe/ubase.h"
#include "upipe/uclock.h"
#include "upipe/uref.h"
#include "upipe/uref_block.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_clock.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_subpipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_urefcount_real.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_uref_mgr.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_upump.h"
#include "upipe/upipe_helper_uclock.h"

#include "upipe-srt/upipe_srt_receiver.h"

#include <bitstream/haivision/srt.h>

#include <arpa/inet.h>
#include <limits.h>

#include <gcrypt.h>

/** @hidden */
static int upipe_srt_receiver_check(struct upipe *upipe, struct uref *flow_format);

struct ack_entry {
    uint32_t ack_num;
    uint64_t timestamp;
};

/** @internal @This is the private context of a SRT receiver pipe. */
struct upipe_srt_receiver {
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


    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    uint32_t socket_id;

    struct upipe *control;

    struct uchain queue;
    /** expected sequence number */
    uint64_t expected_seqnum;

    /** last seq output */
    uint64_t last_output_seqnum;

    /* stats */
    size_t buffered;
    size_t nacks;
    size_t repaired;
    size_t loss;
    size_t dups;

    /** buffer latency */
    uint64_t latency;
    /** last time a NACK was sent */
    uint64_t last_nack[65536];

    /** last time any SRT packet was sent */
    uint64_t last_sent;

    /** number of packets in the buffer */
    uint64_t packets;

    /** number of bytes in the buffer*/
    uint64_t bytes;

    uint32_t ack_num;
    uint64_t last_ack;
    struct ack_entry *acks;
    size_t n_acks;
    size_t ack_ridx;
    size_t ack_widx;

    uint64_t rtt;
    uint64_t rtt_variance;

    uint8_t salt[16];
    uint8_t sek[2][32];
    uint8_t sek_len;

    uint64_t establish_time;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_srt_receiver, upipe, UPIPE_SRT_RECEIVER_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_srt_receiver, urefcount, upipe_srt_receiver_no_input)
UPIPE_HELPER_UREFCOUNT_REAL(upipe_srt_receiver, urefcount_real, upipe_srt_receiver_free);

UPIPE_HELPER_VOID(upipe_srt_receiver)

UPIPE_HELPER_OUTPUT(upipe_srt_receiver, output, flow_def, output_state, request_list)
UPIPE_HELPER_UPUMP_MGR(upipe_srt_receiver, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_srt_receiver, upump_timer, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_srt_receiver, upump_timer_lost, upump_mgr)
UPIPE_HELPER_UCLOCK(upipe_srt_receiver, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

UPIPE_HELPER_UREF_MGR(upipe_srt_receiver, uref_mgr, uref_mgr_request,
                      upipe_srt_receiver_check,
                      upipe_srt_receiver_register_output_request,
                      upipe_srt_receiver_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_srt_receiver, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_srt_receiver_check,
                      upipe_srt_receiver_register_output_request,
                      upipe_srt_receiver_unregister_output_request)

/** @internal @This is the private context of a SRT receiver output pipe. */
struct upipe_srt_receiver_output {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

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

static int upipe_srt_receiver_output_check(struct upipe *upipe, struct uref *flow_format);
UPIPE_HELPER_UPIPE(upipe_srt_receiver_output, upipe, UPIPE_SRT_RECEIVER_OUTPUT_SIGNATURE)
UPIPE_HELPER_VOID(upipe_srt_receiver_output);
UPIPE_HELPER_UREFCOUNT(upipe_srt_receiver_output, urefcount, upipe_srt_receiver_output_free)
UPIPE_HELPER_OUTPUT(upipe_srt_receiver_output, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_srt_receiver_output, uref_mgr, uref_mgr_request,
                      upipe_srt_receiver_output_check,
                      upipe_srt_receiver_output_register_output_request,
                      upipe_srt_receiver_output_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_srt_receiver_output, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_srt_receiver_output_check,
                      upipe_srt_receiver_output_register_output_request,
                      upipe_srt_receiver_output_unregister_output_request)
UPIPE_HELPER_SUBPIPE(upipe_srt_receiver, upipe_srt_receiver_output, output, sub_mgr, outputs,
                     uchain)

static int upipe_srt_receiver_output_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_srt_receiver_output *upipe_srt_receiver_output = upipe_srt_receiver_output_from_upipe(upipe);
    if (flow_format)
        upipe_srt_receiver_output_store_flow_def(upipe, flow_format);

    if (upipe_srt_receiver_output->uref_mgr == NULL) {
        upipe_srt_receiver_output_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_srt_receiver_output->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_srt_receiver_output->uref_mgr, NULL);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_srt_receiver_output_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    return UBASE_ERR_NONE;
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_srt_receiver_no_input(struct upipe *upipe)
{
    upipe_srt_receiver_throw_sub_outputs(upipe, UPROBE_SOURCE_END);
    upipe_srt_receiver_release_urefcount_real(upipe);
}
/** @internal @This allocates an output subpipe of a dup pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_srt_receiver_output_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    if (mgr->signature != UPIPE_SRT_RECEIVER_OUTPUT_SIGNATURE)
        return NULL;

    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_sub_mgr(mgr);
    if (upipe_srt_receiver->control)
        return NULL;

    struct upipe *upipe = upipe_srt_receiver_output_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_srt_receiver->control = upipe;

    upipe_srt_receiver_output_init_urefcount(upipe);
    upipe_srt_receiver_output_init_output(upipe);
    upipe_srt_receiver_output_init_sub(upipe);
    upipe_srt_receiver_output_init_ubuf_mgr(upipe);
    upipe_srt_receiver_output_init_uref_mgr(upipe);

    upipe_throw_ready(upipe);

    upipe_srt_receiver_output_require_uref_mgr(upipe);

    return upipe;
}


/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_srt_receiver_output_free(struct upipe *upipe)
{
    //struct upipe_srt_receiver_output *upipe_srt_receiver_output = upipe_srt_receiver_output_from_upipe(upipe);
    upipe_throw_dead(upipe);

    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_sub_mgr(upipe->mgr);
    upipe_srt_receiver_set_upump_timer_lost(&upipe_srt_receiver->upipe, NULL);
    upipe_srt_receiver->control = NULL;
    upipe_srt_receiver_output_clean_output(upipe);
    upipe_srt_receiver_output_clean_sub(upipe);
    upipe_srt_receiver_output_clean_urefcount(upipe);
    upipe_srt_receiver_output_clean_ubuf_mgr(upipe);
    upipe_srt_receiver_output_clean_uref_mgr(upipe);
    upipe_srt_receiver_output_free_void(upipe);
}

static uint64_t upipe_srt_receiver_get_rtt(struct upipe *upipe)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);

    /* VSF TR-06 doesn't give a mean to retrieve RTT, but defaults to 7
     * retransmissions requests per packet.
     * XXX: make it configurable ? */

    uint64_t rtt = upipe_srt_receiver->rtt;
    if (!rtt)
        rtt = upipe_srt_receiver->latency / 7;
    return rtt;
}


/** @internal @This periodic timer checks for missing seqnums.
 */
static void upipe_srt_receiver_timer_lost(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);

    uint64_t expected_seq = UINT64_MAX;

    uint64_t rtt = upipe_srt_receiver_get_rtt(upipe);

    uint64_t now = uclock_now(upipe_srt_receiver->uclock);

    if (now - upipe_srt_receiver->last_sent > UCLOCK_FREQ) {
        struct uref *uref = uref_block_alloc(upipe_srt_receiver->uref_mgr,
                upipe_srt_receiver->ubuf_mgr, SRT_HEADER_SIZE + 4 /* WTF */);
        if (uref) {
            uint8_t *out;
            int output_size = -1;
            if (unlikely(!ubase_check(uref_block_write(uref, 0, &output_size, &out)))) {
                uref_free(uref);
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            }

            srt_set_packet_control(out, true);
            srt_set_packet_timestamp(out, (now - upipe_srt_receiver->establish_time) / 27);
            srt_set_packet_dst_socket_id(out, upipe_srt_receiver->socket_id);
            srt_set_control_packet_type(out, SRT_CONTROL_TYPE_KEEPALIVE);
            srt_set_control_packet_subtype(out, 0);
            srt_set_control_packet_type_specific(out, 0);
            uint8_t *extra = (uint8_t*)srt_get_control_packet_cif(out);
            memset(extra, 0, 4);

            uref_block_unmap(uref, 0);

            upipe_srt_receiver->last_sent = now;
            upipe_srt_receiver_output_output(upipe_srt_receiver->control, uref,
                    &upipe_srt_receiver->upump_timer_lost);
        }
    }

    if (upipe_srt_receiver->buffered == 0)
        return;

    /* space out NACKs a bit more than RTT. XXX: tune me */
    uint64_t next_nack = now - rtt * 12 / 10;

    /* TODO: do not look at the last pkts/s * rtt
     * It it too late to send a NACK for these
     * XXX: use cr_sys, because pkts/s also accounts for
     * the retransmitted packets */

    struct uchain *uchain;
    int holes = 0;

    uint64_t last_received = UINT64_MAX;


    int s = 1472; /* 1500 - IP - UDP */
    struct uref *pkt = NULL;
    uint8_t *cif = NULL;

    ulist_foreach(&upipe_srt_receiver->queue, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        uint64_t seqnum = 0;
        uref_attr_get_priv(uref, &seqnum);

        if (likely(expected_seq != UINT64_MAX) && seqnum != expected_seq) {
            /* hole found */
            upipe_verbose_va(upipe, "Found hole from %"PRIu64" (incl) to %"PRIu64" (excl)",
                    expected_seq, seqnum);
            if (last_received == UINT64_MAX)
                last_received = expected_seq - 1;

            for (uint32_t seq = expected_seq; seq != seqnum; seq = (seq + 1) & ~(1 << 31)) {
                /* if packet was lost, we should have detected it already */
                if (upipe_srt_receiver->last_nack[seq & 0xffff] == 0) {
                    upipe_err_va(upipe, "packet %u missing but was not marked as lost!", seq);
                    continue;
                }

                /* if we sent a NACK not too long ago, do not repeat it */
                /* since NACKs are sent in a batch, break loop if the first packet is too early */
                if (upipe_srt_receiver->last_nack[seq & 0xffff] > next_nack) {
                    if (0) upipe_err_va(upipe, "Cancelling NACK due to RTT (seq %u diff %"PRId64"",
                        seq, next_nack - upipe_srt_receiver->last_nack[seq & 0xffff]
                    );
                    goto next;
                }
            }

            /* update NACK request time */
            for (uint32_t seq = expected_seq; seq != seqnum; seq = (seq + 1) & ~(1 << 31)) {
                upipe_srt_receiver->last_nack[seq & 0xffff] = now;
            }

            if (!pkt) {
                pkt = uref_block_alloc(upipe_srt_receiver->uref_mgr, upipe_srt_receiver->ubuf_mgr, s);
                if (unlikely(!pkt)) {
                    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                    return;
                }

                uint8_t *buf;
                uref_block_write(pkt, 0, &s, &buf);
                memset(buf, 0, s);

                s -= SRT_HEADER_SIZE;

                srt_set_packet_control(buf, true);
                srt_set_packet_timestamp(buf, (now - upipe_srt_receiver->establish_time) / 27);
                srt_set_packet_dst_socket_id(buf, upipe_srt_receiver->socket_id);
                srt_set_control_packet_type(buf, SRT_CONTROL_TYPE_NAK);
                srt_set_control_packet_subtype(buf, 0);
                cif = (uint8_t*)srt_get_control_packet_cif(buf);
            }

            // TODO : if full, transmit and realloc next one
            if (seqnum - expected_seq > 1) {
                if (s < 8) {
                    upipe_warn_va(upipe, "NAK FULL");
                    break;
                }
                cif[0] = ((expected_seq >> 24) & 0x7f) | 0x80;
                cif[1] = (expected_seq >> 16) & 0xff;
                cif[2] = (expected_seq >>  8) & 0xff;
                cif[3] = (expected_seq      ) & 0xff;
                cif[4] = ((seqnum - 1) >> 24) & 0x7f;
                cif[5] = ((seqnum - 1) >> 16) & 0xff;
                cif[6] = ((seqnum - 1) >>  8) & 0xff;
                cif[7] = ((seqnum - 1)      ) & 0xff;
                s -= 8;
                cif += 8;
            } else {
                cif[0] = (expected_seq >> 24) & 0x7f;
                cif[1] = (expected_seq >> 16) & 0xff;
                cif[2] = (expected_seq >>  8) & 0xff;
                cif[3] = (expected_seq      ) & 0xff;
                s -= 4;
                cif += 4;
                if (s < 4) {
                    upipe_warn_va(upipe, "NAK FULL");
                    break;
                }
            }

            holes++;
        }

next:
        expected_seq = (seqnum + 1) & ~(1 << 31);
    }

    // A Full ACK control packet is sent every 10 ms and has all the fields of Figure 13.
    if (upipe_srt_receiver->last_ack == UINT64_MAX || (now - upipe_srt_receiver->last_ack > UCLOCK_FREQ / 100)) {
        struct uref *uref = uref_block_alloc(upipe_srt_receiver->uref_mgr,
                upipe_srt_receiver->ubuf_mgr, SRT_HEADER_SIZE + SRT_ACK_CIF_SIZE_3);
                //
        if (uref) {
            uint8_t *out;
            int output_size = -1;
            if (unlikely(!ubase_check(uref_block_write(uref, 0, &output_size, &out)))) {
                uref_free(uref);
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            } else {
                uint32_t ack_num = upipe_srt_receiver->ack_num++;
                srt_set_packet_control(out, true);
                srt_set_packet_timestamp(out, (now - upipe_srt_receiver->establish_time) / 27);
                srt_set_packet_dst_socket_id(out, upipe_srt_receiver->socket_id);
                srt_set_control_packet_type(out, SRT_CONTROL_TYPE_ACK);
                srt_set_control_packet_subtype(out, 0);
                srt_set_control_packet_type_specific(out, ack_num);
                uint8_t *out_cif = (uint8_t*)srt_get_control_packet_cif(out);

                uint64_t last_seq = 0;
                uref_attr_get_priv(uref_from_uchain(upipe_srt_receiver->queue.prev), &last_seq);
                if (last_received != UINT64_MAX)
                    last_seq = last_received;
                srt_set_ack_last_ack_seq(out_cif, last_seq);
                srt_set_ack_rtt(out_cif, upipe_srt_receiver->rtt * 1000000 / UCLOCK_FREQ);
                srt_set_ack_rtt_variance(out_cif, upipe_srt_receiver->rtt_variance * 1000000 / UCLOCK_FREQ);
                uint64_t t = upipe_srt_receiver->latency;

                uint64_t packets_per_sec = upipe_srt_receiver->packets * UCLOCK_FREQ / t;
                srt_set_ack_packets_receiving_rate(out_cif, packets_per_sec);
                /* If we sent value 0, libsrt will stop sending */
                srt_set_ack_avail_bufsize(out_cif, 8192);
                srt_set_ack_estimated_link_capacity(out_cif, 10 * packets_per_sec); /* ? */
                srt_set_ack_receiving_rate(out_cif, upipe_srt_receiver->bytes * UCLOCK_FREQ / t);

                uref_block_unmap(uref, 0);
                upipe_srt_receiver->last_ack = now;
                if (!upipe_srt_receiver->control) {
                    uref_free(uref);
                    return;
                }
                upipe_srt_receiver->last_sent = now;
                upipe_srt_receiver_output_output(upipe_srt_receiver->control, uref, NULL);

                upipe_srt_receiver->acks[upipe_srt_receiver->ack_widx].ack_num = ack_num;
                upipe_srt_receiver->acks[upipe_srt_receiver->ack_widx].timestamp = now;
                upipe_srt_receiver->ack_widx++;
                upipe_srt_receiver->ack_widx %= upipe_srt_receiver->n_acks;
            }
        }
    }

    if (holes) { /* debug stats */
        static uint64_t old;
        if (likely(old != 0))
            upipe_verbose_va(upipe, "%d holes after %"PRIu64" ms",
                    holes, 1000 * (now - old) / UCLOCK_FREQ);
        old = now;

        upipe_srt_receiver->nacks += holes; // XXX

        uref_block_unmap(pkt, 0);

        // XXX : date NACK packet?
        //uref_clock_set_date_sys(pkt, /* cr */ 0, UREF_DATE_CR);

        uref_block_resize(pkt, 0, 1472 - s);

        upipe_srt_receiver->last_sent = now;
        upipe_srt_receiver_output_output(upipe_srt_receiver->control, pkt, NULL);
    }
}

/** @internal @This periodic timer remove seqnums from the buffer.
 */
static void upipe_srt_receiver_timer(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);

    uint64_t now = uclock_now(upipe_srt_receiver->uclock);
    uint64_t rtt = upipe_srt_receiver_get_rtt(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_srt_receiver->queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        uint64_t seqnum = 0;
        if (!ubase_check(uref_attr_get_priv(uref, &seqnum))) {
            upipe_err_va(upipe, "Could not read seqnum from uref");
        }

        uint64_t cr_sys = 0;
        if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &cr_sys))))
            upipe_warn_va(upipe, "Couldn't read cr_sys in %s()", __func__);

        if (now - cr_sys <= upipe_srt_receiver->latency - rtt)
            break;

        upipe_verbose_va(upipe, "Output seq %"PRIu64" after %"PRIu64" clocks", seqnum, now - cr_sys);
        if (likely(upipe_srt_receiver->last_output_seqnum != UINT64_MAX)) {
            uint32_t diff = seqnum - upipe_srt_receiver->last_output_seqnum - 1;
            diff &= ~(1 << 31); // seqnums are 31 bits
            if (diff) {
                upipe_srt_receiver->loss += diff;
                upipe_dbg_va(upipe, "PKT LOSS: %" PRIu64 " -> %"PRIu64" DIFF %u",
                        upipe_srt_receiver->last_output_seqnum, seqnum, diff);
            }
        }

        upipe_srt_receiver->last_output_seqnum = seqnum;

        ulist_delete(uchain);
        upipe_srt_receiver->packets--;
        size_t size;
        if (unlikely(!ubase_check(uref_block_size(uref, &size))))
            size = 0;
        upipe_srt_receiver->bytes -= size;

        uref_clock_set_cr_sys(uref, cr_sys + upipe_srt_receiver->latency - rtt);
        upipe_srt_receiver_output(upipe, uref, NULL); // XXX: use timer upump ?

        static uint64_t old;
        if (now - old > UCLOCK_FREQ) {
            upipe_dbg_va(upipe, "level %zu , %zu nacks (repaired %zu, lost %zu, dups %zu)",
                    upipe_srt_receiver->buffered, upipe_srt_receiver->nacks,
                    upipe_srt_receiver->repaired, upipe_srt_receiver->loss,
                    upipe_srt_receiver->dups);
            old = now;
        }

        if (--upipe_srt_receiver->buffered == 0) {
            upipe_warn_va(upipe, "Exhausted buffer");
            upipe_srt_receiver->expected_seqnum = UINT64_MAX;
            upipe_srt_receiver->last_output_seqnum = UINT64_MAX;
        }
    }
}

static void upipe_srt_receiver_restart_timer(struct upipe *upipe)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);
    uint64_t rtt = upipe_srt_receiver_get_rtt(upipe);

    upipe_srt_receiver_set_upump_timer_lost(upipe, NULL);
    if (upipe_srt_receiver->upump_mgr) {
        struct upump *upump=
            upump_alloc_timer(upipe_srt_receiver->upump_mgr,
                              upipe_srt_receiver_timer_lost,
                              upipe, upipe->refcount,
                              0, rtt / 10);
        upump_start(upump);
        upipe_srt_receiver_set_upump_timer_lost(upipe, upump);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_srt_receiver_output_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "block."))

    if (upipe_srt_receiver->control) {
        struct uref *flow_def_dup = uref_dup(flow_def);
        if (unlikely(flow_def_dup == NULL))
            return UBASE_ERR_ALLOC;
        upipe_srt_receiver_output_store_flow_def(upipe_srt_receiver->control, flow_def_dup);
    }

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
static int _upipe_srt_receiver_output_control(struct upipe *upipe,
                                    int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_srt_receiver_output_control_super(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_srt_receiver_output_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_srt_receiver_output_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}
static int upipe_srt_receiver_output_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_srt_receiver_output_control(upipe, command, args))
    return upipe_srt_receiver_output_check(upipe, NULL);
}

/** @internal @This initializes the output manager for a srt set pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_srt_receiver_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_srt_receiver->sub_mgr;
    sub_mgr->refcount = upipe_srt_receiver_to_urefcount_real(upipe_srt_receiver);
    sub_mgr->signature = UPIPE_SRT_RECEIVER_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_srt_receiver_output_alloc;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_srt_receiver_output_control;
}


/** @internal @This allocates a SRT receiver pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_srt_receiver_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_srt_receiver_alloc_void(mgr, uprobe, signature, args);
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);

#ifdef UPIPE_HAVE_GCRYPT_H
    if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) {
        uprobe_err(uprobe, upipe, "Application did not initialize libgcrypt, see "
        "https://www.gnupg.org/documentation/manuals/gcrypt/Initializing-the-library.html");
        upipe_srt_receiver_free_void(upipe);
        return NULL;
    }
#endif

    upipe_srt_receiver_init_urefcount(upipe);
    upipe_srt_receiver_init_urefcount_real(upipe);
    upipe_srt_receiver_init_sub_outputs(upipe);
    upipe_srt_receiver_init_sub_mgr(upipe);

    upipe_srt_receiver_init_uref_mgr(upipe);
    upipe_srt_receiver_init_ubuf_mgr(upipe);
    upipe_srt_receiver_init_output(upipe);

    upipe_srt_receiver_init_upump_mgr(upipe);
    upipe_srt_receiver_init_upump_timer(upipe);
    upipe_srt_receiver_init_upump_timer_lost(upipe);
    upipe_srt_receiver_init_uclock(upipe);
    upipe_srt_receiver_require_uclock(upipe);

    // FIXME
    upipe_srt_receiver->socket_id = 0;
    upipe_srt_receiver->control = NULL;

    ulist_init(&upipe_srt_receiver->queue);
    memset(upipe_srt_receiver->last_nack, 0, sizeof(upipe_srt_receiver->last_nack));
    upipe_srt_receiver->rtt = 100 * UCLOCK_FREQ / 1000;
    upipe_srt_receiver->rtt_variance = 50 * UCLOCK_FREQ / 1000;
    upipe_srt_receiver->expected_seqnum = UINT64_MAX;

    upipe_srt_receiver->last_output_seqnum = UINT64_MAX;
    upipe_srt_receiver->last_ack = UINT64_MAX;
    upipe_srt_receiver->ack_num = 0;

    upipe_srt_receiver->acks = NULL;
    upipe_srt_receiver->n_acks = 0;
    upipe_srt_receiver->ack_ridx = 0;
    upipe_srt_receiver->ack_widx = 0;

    upipe_srt_receiver->buffered = 0;
    upipe_srt_receiver->nacks = 0;
    upipe_srt_receiver->repaired = 0;
    upipe_srt_receiver->loss = 0;
    upipe_srt_receiver->dups = 0;

    upipe_srt_receiver->latency = 0;
    upipe_srt_receiver->last_sent = 0;

    upipe_srt_receiver->packets = 0;
    upipe_srt_receiver->bytes = 0;

    upipe_srt_receiver->sek_len = 0;

    upipe_srt_receiver->establish_time = 0;

    upipe_throw_ready(upipe);
    return upipe;
}


/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_srt_receiver_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);

    upipe_srt_receiver_check_upump_mgr(upipe);

    if (upipe_srt_receiver->latency == 0) {
        upipe_err(upipe, "Latency unset");
        return UBASE_ERR_UNKNOWN;
    }

    if (flow_format != NULL) {
        uint64_t latency;
        if (!ubase_check(uref_clock_get_latency(flow_format, &latency)))
            latency = 0;
        uref_clock_set_latency(flow_format, latency + upipe_srt_receiver->latency);

        upipe_srt_receiver_store_flow_def(upipe, flow_format);
    }

    if (upipe_srt_receiver->flow_def == NULL)
        return UBASE_ERR_NONE;

    if (upipe_srt_receiver->uref_mgr == NULL) {
        upipe_srt_receiver_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_srt_receiver->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_srt_receiver->uref_mgr, NULL);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_srt_receiver_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_srt_receiver->upump_mgr && !upipe_srt_receiver->upump_timer && upipe_srt_receiver->control) {
        struct upump *upump =
            upump_alloc_timer(upipe_srt_receiver->upump_mgr,
                              upipe_srt_receiver_timer,
                              upipe, upipe->refcount,
                              UCLOCK_FREQ/300, UCLOCK_FREQ/300);
        upump_start(upump);
        upipe_srt_receiver_set_upump_timer(upipe, upump);

        /* every 10ms, check for lost packets
         * interval is reduced each time we get the current RTT from sender */
        upipe_srt_receiver_restart_timer(upipe);
    }

    return UBASE_ERR_NONE;
}

static void upipe_srt_receiver_empty_buffer(struct upipe *upipe)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);

    /* empty buffer */
    upipe_warn(upipe, "Emptying buffer");
    upipe_srt_receiver->expected_seqnum = UINT64_MAX;
    upipe_srt_receiver->last_output_seqnum = UINT64_MAX;
    upipe_srt_receiver->buffered = 0;
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_srt_receiver->queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_srt_receiver_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))

    if (ubase_ncmp(def, "block.")) {
        upipe_err_va(upipe, "Unknown def %s", def);
        return UBASE_ERR_INVALID;
    }

    uint64_t id;
    if (ubase_check(uref_flow_get_id(flow_def, &id))) {
        if (upipe_srt_receiver->socket_id != id)
            upipe_srt_receiver_empty_buffer(upipe);

        upipe_srt_receiver->socket_id = id;
    }
    else {
        /* XXX: Is this reachable in reality? */
        upipe_srt_receiver_empty_buffer(upipe);
    }

    struct udict_opaque opaque;
    if (ubase_check(uref_attr_get_opaque(flow_def, &opaque, UDICT_TYPE_OPAQUE, "enc.salt"))) {
        if (opaque.size > 16)
            opaque.size = 16;
        memcpy(upipe_srt_receiver->salt, opaque.v, opaque.size);
    }

#ifdef UPIPE_HAVE_GCRYPT_H
    if (ubase_check(uref_attr_get_opaque(flow_def, &opaque, UDICT_TYPE_OPAQUE, "enc.even_key"))) {
        if (opaque.size > sizeof(upipe_srt_receiver->sek[0]))
            opaque.size = sizeof(upipe_srt_receiver->sek[0]);
        upipe_srt_receiver->sek_len = opaque.size;
        memcpy(upipe_srt_receiver->sek[0], opaque.v, opaque.size);
    }

    if (ubase_check(uref_attr_get_opaque(flow_def, &opaque, UDICT_TYPE_OPAQUE, "enc.odd_key"))) {
        if (opaque.size > sizeof(upipe_srt_receiver->sek[1]))
            opaque.size = sizeof(upipe_srt_receiver->sek[1]);
        upipe_srt_receiver->sek_len = opaque.size;
        memcpy(upipe_srt_receiver->sek[1], opaque.v, opaque.size);
    }
#endif

    flow_def = uref_dup(flow_def);
    if (!flow_def)
        return UBASE_ERR_ALLOC;

    upipe_srt_receiver_store_flow_def(upipe, flow_def);

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a SRT receiver pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_srt_receiver_control(struct upipe *upipe,
                                 int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_srt_receiver_control_output(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_srt_receiver_control_outputs(upipe, command, args));

    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_srt_receiver_set_upump_timer(upipe, NULL);
            upipe_srt_receiver_set_upump_timer_lost(upipe, NULL);
            return upipe_srt_receiver_attach_upump_mgr(upipe);

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_srt_receiver_set_flow_def(upipe, flow);
        }

        case UPIPE_SET_OPTION: {
            const char *k = va_arg(args, const char *);
            const char *v = va_arg(args, const char *);
            if (strcmp(k, "latency"))
                return UBASE_ERR_INVALID;

            struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);
            if (upipe_srt_receiver->latency) {
                upipe_err(upipe, "Latency already set");
                return UBASE_ERR_UNHANDLED;
            }
            unsigned latency_ms = atoi(v);
            upipe_srt_receiver->latency = latency_ms * UCLOCK_FREQ / 1000;
            upipe_dbg_va(upipe, "Set latency to %u msecs", latency_ms);

            upipe_srt_receiver->n_acks = (latency_ms + 9) / 10;
            upipe_srt_receiver->acks = malloc(sizeof(*upipe_srt_receiver->acks) * upipe_srt_receiver->n_acks);
            if (!upipe_srt_receiver->acks)
                return UBASE_ERR_ALLOC;

            return UBASE_ERR_NONE;
        }

        case UPIPE_SRTR_GET_STATS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SRT_RECEIVER_SIGNATURE)
            unsigned *expected_seqnum    = va_arg(args, unsigned*);
            unsigned *last_output_seqnum = va_arg(args, unsigned*);
            size_t   *buffered           = va_arg(args, size_t*);
            size_t   *nacks              = va_arg(args, size_t*);
            size_t   *repaired           = va_arg(args, size_t*);
            size_t   *loss               = va_arg(args, size_t*);
            size_t   *dups               = va_arg(args, size_t*);

            struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);
            *buffered = upipe_srt_receiver->buffered;
            *expected_seqnum = upipe_srt_receiver->expected_seqnum;
            *last_output_seqnum = upipe_srt_receiver->last_output_seqnum;
            *nacks = upipe_srt_receiver->nacks;
            *repaired = upipe_srt_receiver->repaired;
            *loss = upipe_srt_receiver->loss;
            *dups = upipe_srt_receiver->dups;

            upipe_srt_receiver->nacks = 0;
            upipe_srt_receiver->repaired = 0;
            upipe_srt_receiver->loss = 0;
            upipe_srt_receiver->dups = 0;

            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a SRT receiver pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_srt_receiver_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_srt_receiver_control(upipe, command, args));

    return upipe_srt_receiver_check(upipe, NULL);
}

/* returns true if uref was inserted in the queue */
static bool upipe_srt_receiver_insert_inner(struct upipe *upipe, struct uref *uref,
        const uint32_t seqnum, struct uref *next)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);
    uint64_t next_seqnum = 0;
    uref_attr_get_priv(next, &next_seqnum);

    uint32_t diff = seqnum - next_seqnum;
    if (!diff) {
        upipe_verbose_va(upipe, "dropping duplicate %u", seqnum);
        upipe_srt_receiver->dups++;
        uref_free(uref);
        return true;
    }

    diff &= ~(1<<31); /* seqnums are 31 bits */

    /* browse the list until we find a seqnum bigger than ours */

    if (diff < 1<<30) // make sure seqnum > next_seqnum
        return false;

    /* if there's no previous packet we're too late */
    struct uchain *uchain = uref_to_uchain(next);
    if (unlikely(ulist_is_first(&upipe_srt_receiver->queue, uchain))) {
        upipe_dbg_va(upipe,
                "LATE packet drop: Expected %" PRIu64 ", got %u, didn't insert after %"PRIu64,
                upipe_srt_receiver->expected_seqnum, seqnum, next_seqnum);
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
                __func__, upipe_srt_receiver->buffered);

    upipe_srt_receiver->buffered++;
    ulist_insert(uchain->prev, uchain, uref_to_uchain(uref));
    upipe_srt_receiver->repaired++;
    upipe_srt_receiver->last_nack[seqnum & 0xffff] = 0;

    upipe_verbose_va(upipe, "Repaired %"PRIu64" > %u > %"PRIu64" -diff %d",
            prev_seqnum, seqnum, next_seqnum, -diff);

    return true;
}

static bool upipe_srt_receiver_insert(struct upipe *upipe, struct uref *uref, const uint32_t seqnum)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_srt_receiver->queue, uchain, uchain_tmp) {
        struct uref *next = uref_from_uchain(uchain);
        if (upipe_srt_receiver_insert_inner(upipe, uref, seqnum, next))
            return true;
    }

    /* Could not insert packet */
    return false;
}

static uint64_t upipe_srt_receiver_ackack(struct upipe *upipe, uint32_t ack_num, uint64_t ts)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);

    const size_t n = upipe_srt_receiver->n_acks;
    const size_t ridx = upipe_srt_receiver->ack_ridx;

    //upipe_verbose_va(upipe,"%s(%u), start at %zu", __func__, ack_num, ridx);

    size_t max = (ridx > 0) ? (ridx - 1) : (n - 1); // end of loop

    for (size_t i = ridx; i != max; i = (i + 1) % n) {
        uint32_t a = upipe_srt_receiver->acks[i].ack_num;

        if (upipe_srt_receiver->acks[i].timestamp == UINT64_MAX) { // already acked
            //upipe_verbose_va(upipe, "break at %zu", i);
            break;
        }

        if (ack_num < a) { // too late
            //upipe_verbose_va(upipe, "break2 at %zu", i);
            break;
        } else if (ack_num == a) {
            uint64_t rtt = ts - upipe_srt_receiver->acks[i].timestamp;
            //upipe_verbose_va(upipe, "rtt[%d] %" PRId64, a, rtt);
            upipe_srt_receiver->acks[i].timestamp = UINT64_MAX; // do not ack twice
            upipe_srt_receiver->ack_ridx = (i+1) % n; // advance
            return rtt;
        }
    }

    //upipe_verbose_va(upipe, "%d not found", ack_num);

    return 0;
}

static void upipe_srt_receiver_input(struct upipe *upipe, struct uref *uref,
        struct upump **upump_p)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);

    size_t total_size;
    ubase_assert(uref_block_size(uref, &total_size));

    const uint8_t *buf;
    int size = total_size;

    ubase_assert(uref_block_read(uref, 0, &size, &buf));
    assert(size == total_size);

    if (size < SRT_HEADER_SIZE) {
        upipe_err_va(upipe, "Packet too small (%d)", size);
        ubase_assert(uref_block_unmap(uref, 0));
        uref_free(uref);
        return;
    }

    uint64_t now = uclock_now(upipe_srt_receiver->uclock);

    if (srt_get_packet_control(buf)) {
        uint16_t type = srt_get_control_packet_type(buf);

        if (type == SRT_CONTROL_TYPE_ACKACK) {
            uint32_t ack_num = srt_get_control_packet_type_specific(buf);
            uint64_t rtt = upipe_srt_receiver_ackack(upipe, ack_num, now);
            upipe_verbose_va(upipe, "RTT %.2f", (float) rtt / 27000.);
            if (rtt) {
                uint64_t var = 0;
                if (rtt > upipe_srt_receiver->rtt) {
                    var = rtt - upipe_srt_receiver->rtt;
                } else {
                    var = upipe_srt_receiver->rtt - rtt;
                }
                upipe_srt_receiver->rtt *= 7;
                upipe_srt_receiver->rtt += rtt;
                upipe_srt_receiver->rtt /= 8;

                upipe_srt_receiver->rtt_variance *= 3;
                upipe_srt_receiver->rtt_variance += var;
                upipe_srt_receiver->rtt_variance /= 4;
            }
            ubase_assert(uref_block_unmap(uref, 0));
            uref_free(uref);
        } else {

            if (type == SRT_CONTROL_TYPE_HANDSHAKE && upipe_srt_receiver->establish_time == 0 && now > 0) {
                uint64_t ts = srt_get_packet_timestamp(buf);
                upipe_srt_receiver->establish_time = now - ts * UCLOCK_FREQ / 1000000;
            }

            ubase_assert(uref_block_unmap(uref, 0));
            if (upipe_srt_receiver->control) {
                upipe_srt_receiver->last_sent = now;
                upipe_srt_receiver_output_output(upipe_srt_receiver->control, uref, upump_p);
            } else
                uref_free(uref);
        }
        return;
    }

    /* data */
    if (!upipe_srt_receiver->control) {
        ubase_assert(uref_block_unmap(uref, 0));
        uref_free(uref);
        return;
    }

    uint32_t seqnum = srt_get_data_packet_seq(buf);
    uint32_t position = srt_get_data_packet_position(buf);
    bool order = srt_get_data_packet_order(buf);
    uint8_t encryption = srt_get_data_packet_encryption(buf);
    bool retransmit = srt_get_data_packet_retransmit(buf);
    uint32_t num = srt_get_data_packet_message_number(buf);
    uint32_t ts = srt_get_packet_timestamp(buf);
    uint8_t kk = srt_get_data_packet_encryption(buf);

    ubase_assert(uref_block_unmap(uref, 0));
    uref_block_resize(uref, SRT_HEADER_SIZE, -1); /* skip SRT header */
    total_size -= SRT_HEADER_SIZE;

    upipe_verbose_va(upipe, "Data seq %u (retx %u)", seqnum, retransmit);

    (void)order;
    (void)num;
    (void)retransmit; // stats?
    (void)ts; // TODO (µs)

    /* store seqnum in uref */
    uref_attr_set_priv(uref, seqnum);
    if (position != 3) {
        upipe_err_va(upipe, "PP %d not handled for live streaming", position);
        uref_free(uref);
        return;
    }

    if (encryption != SRT_DATA_ENCRYPTION_CLEAR) {
        if (upipe_srt_receiver->sek_len == 0) {
            upipe_err(upipe, "Encryption not handled");
            uref_free(uref);
            return;
#ifdef UPIPE_HAVE_GCRYPT_H
        } else {
            const uint8_t *salt = upipe_srt_receiver->salt;
            const uint8_t *sek = upipe_srt_receiver->sek[(kk & (1<<0))? 0 : 1];
            int key_len = upipe_srt_receiver->sek_len;

            uint8_t iv[16];
            memset(&iv, 0, 16);
            iv[10] = (seqnum >> 24) & 0xff;
            iv[11] = (seqnum >> 16) & 0xff;
            iv[12] = (seqnum >>  8) & 0xff;
            iv[13] =  seqnum & 0xff;
            for (int i = 0; i < 112/8; i++)
                iv[i] ^= salt[i];

            uint8_t *buf;
            size = total_size;

            ubase_assert(uref_block_write(uref, 0, &size, &buf));
            assert(size == total_size);

            gcry_cipher_hd_t aes;
            gpg_error_t err;
            err = gcry_cipher_open(&aes, GCRY_CIPHER_AES, GCRY_CIPHER_MODE_CTR, 0);
            if (err) {
                upipe_err_va(upipe, "Cipher open failed (0x%x)", err);
                goto error;
            }

            err = gcry_cipher_setkey(aes, sek, key_len);
            if (err) {
                upipe_err_va(upipe, "Couldn't set session key (0x%x)", err);
                goto error_close;
            }

            err = gcry_cipher_setctr(aes, iv, 16);
            if (err) {
                upipe_err_va(upipe, "Couldn't set ctr (0x%x)", err);
                goto error_close;
            }

            err = gcry_cipher_encrypt(aes, buf, size, NULL, 0);
            if (err) {
                upipe_err_va(upipe, "Couldn't decrypt packet (0x%x)", err);
                goto error_close;
            }

error_close:
            gcry_cipher_close(aes);
error:
            uref_block_unmap(uref, 0);
#endif
        }
    }

    /* first packet */
    if (unlikely(upipe_srt_receiver->expected_seqnum == UINT64_MAX))
        upipe_srt_receiver->expected_seqnum = seqnum;

    uint32_t diff = seqnum - upipe_srt_receiver->expected_seqnum;
    diff &= ~(1 << 31); // seqnums are 31 bits

    if (diff < 1<<30) { // seqnum > last seq, insert at the end
        /* packet is from the future */
        upipe_srt_receiver->buffered++;

        upipe_srt_receiver->packets++;
        upipe_srt_receiver->bytes += total_size + SRT_HEADER_SIZE;
        ulist_add(&upipe_srt_receiver->queue, uref_to_uchain(uref));
        upipe_srt_receiver->last_nack[seqnum & 0xffff] = 0;

        if (diff != 0) {
            uint64_t rtt = upipe_srt_receiver_get_rtt(upipe);
            /* wait a bit to send a NACK, in case of reordering */
            uint64_t fake_last_nack = uclock_now(upipe_srt_receiver->uclock) - rtt;
            for (uint32_t seq = upipe_srt_receiver->expected_seqnum; seq != seqnum; seq++)
                if (upipe_srt_receiver->last_nack[seq & 0xffff] == 0)
                    upipe_srt_receiver->last_nack[seq & 0xffff] = fake_last_nack;
        }

        upipe_srt_receiver->expected_seqnum = (seqnum + 1) & ~(1 << 31);
        return;
    }

    /* packet is from the past, reordered or retransmitted */
    if (upipe_srt_receiver_insert(upipe, uref, seqnum))
        return;

    uint64_t first_seq = 0, last_seq = 0;
    uref_attr_get_priv(uref_from_uchain(upipe_srt_receiver->queue.next), &first_seq);
    uref_attr_get_priv(uref_from_uchain(upipe_srt_receiver->queue.prev), &last_seq);
    // XXX : when much too late, it could mean RTP source restart
    upipe_err_va(upipe, "LATE packet %u, dropped (buffered %"PRIu64" -> %"PRIu64")",
            seqnum, first_seq, last_seq);
    uref_free(uref);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_srt_receiver_free(struct upipe *upipe)
{
    struct upipe_srt_receiver *upipe_srt_receiver = upipe_srt_receiver_from_upipe(upipe);
    upipe_throw_dead(upipe);

    free(upipe_srt_receiver->acks);
    upipe_srt_receiver->acks = NULL;

    upipe_srt_receiver_clean_output(upipe);
    upipe_srt_receiver_clean_upump_timer(upipe);
    upipe_srt_receiver_clean_upump_timer_lost(upipe);
    upipe_srt_receiver_clean_upump_mgr(upipe);
    upipe_srt_receiver_clean_uclock(upipe);
    upipe_srt_receiver_clean_ubuf_mgr(upipe);
    upipe_srt_receiver_clean_uref_mgr(upipe);
    upipe_srt_receiver_clean_urefcount(upipe);
    upipe_srt_receiver_clean_urefcount_real(upipe);
    upipe_srt_receiver_clean_sub_outputs(upipe);
    upipe_srt_receiver_empty_buffer(upipe);
    upipe_srt_receiver_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_srt_receiver_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SRT_RECEIVER_SIGNATURE,

    .upipe_alloc = upipe_srt_receiver_alloc,
    .upipe_input = upipe_srt_receiver_input,
    .upipe_control = upipe_srt_receiver_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all SRT receiver sources
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_srt_receiver_mgr_alloc(void)
{
    return &upipe_srt_receiver_mgr;
}
