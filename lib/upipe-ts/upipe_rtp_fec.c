/*
 * Copyright (C) 2015-2017 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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
 * @short Upipe RTP FEC module

    The code does not handle one special case:
    X - lost
    O - received

    3x3 Matrix example:
        XXOR
        OOOR
        OOOR
        CXC

    This would require two passes of row FEC, adding significant complexity for an unlikely case.
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/upipe.h>
#include <upipe/ulist.h>
#include <upipe/uref_flow.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>

#include <upipe-ts/upipe_rtp_fec.h>

#include <bitstream/ietf/rtp.h>
#include <bitstream/mpeg/ts.h>
#include <bitstream/smpte/2022_1_fec.h>

#define UPIPE_FEC_JITTER UCLOCK_FREQ/25
#define FEC_MAX 255
#define LATENCY_MAX (UCLOCK_FREQ*2)

/** upipe_rtp_fec structure with rtp-fec parameters */
struct upipe_rtp_fec {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** source manager */
    struct upipe_mgr sub_mgr;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** watcher */
    struct upump *upump;

    uint64_t pkts_since_last_fec;

    int cols;
    int rows;

    uint64_t prev_sys;

    uint32_t first_seqnum;
    uint32_t last_seqnum;
    uint32_t last_send_seqnum;

    /* Lowest (base) sequence number of current FEC matrix */
    uint32_t cur_matrix_snbase;
    /* Lowest (base) sequence number of current FEC row */
    uint32_t cur_row_fec_snbase;

    struct {
        uint64_t seqnum;
        uint64_t date_sys;
    } recent[2 * FEC_MAX * FEC_MAX];
    uint64_t latency;

    /** detected payload type */
    uint8_t pt;

    /** main subpipe **/
    struct upipe main_subpipe;
    /** col subpipe */
    struct upipe col_subpipe;
    /** row subpipe */
    struct upipe row_subpipe;

    struct uchain main_queue;
    struct uchain col_queue;
    struct uchain row_queue;

    /* number of packets not recovered */
    uint64_t lost;

    /* number of packets recovered */
    uint64_t recovered;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** date_sys of previous packet */
    uint64_t prev_date_sys;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtp_fec, upipe, UPIPE_RTP_FEC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_rtp_fec, urefcount, upipe_rtp_fec_free);

UPIPE_HELPER_OUTPUT(upipe_rtp_fec, output, flow_def, output_state, request_list)

UPIPE_HELPER_UCLOCK(upipe_rtp_fec, uclock, uclock_request, NULL,
                    upipe_rtp_fec_register_output_request,
                    upipe_rtp_fec_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_rtp_fec, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_rtp_fec, upump, upump_mgr);

UBASE_FROM_TO(upipe_rtp_fec, upipe_mgr, sub_mgr, sub_mgr)
UBASE_FROM_TO(upipe_rtp_fec, upipe, main_subpipe, main_subpipe)
UBASE_FROM_TO(upipe_rtp_fec, upipe, col_subpipe, col_subpipe)
UBASE_FROM_TO(upipe_rtp_fec, upipe, row_subpipe, row_subpipe)

/** @internal @This initializes an subpipe of a rtp fec pipe.
 *
 * @param upipe pointer to subpipe
 * @param sub_mgr manager of the subpipe
 * @param uprobe structure used to raise events by the subpipe
 */
static void upipe_rtp_fec_sub_init(struct upipe *upipe,
        struct upipe_mgr *sub_mgr, struct uprobe *uprobe)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_sub_mgr(sub_mgr);
    upipe_init(upipe, sub_mgr, uprobe);
    upipe->refcount = &upipe_rtp_fec->urefcount;

    upipe_throw_ready(upipe);
}

static inline bool seq_num_lt(uint16_t s1, uint16_t s2)
{
    uint16_t diff = s2 - s1;
    if (s1 == s2)
        return false;

    return diff < 0x8000;
}

static void upipe_rtp_fec_extract_parameters(struct uref *fec_uref,
                                             uint32_t *ts_rec,
                                             uint16_t *length_rec)
{
    uint8_t fec_header[SMPTE_2022_FEC_HEADER_SIZE];

    const uint8_t *peek = uref_block_peek(fec_uref, RTP_HEADER_SIZE,
            sizeof(fec_header), fec_header);
    if (ts_rec)
        *ts_rec = smpte_fec_get_ts_recovery(peek);
    if (length_rec)
        *length_rec = smpte_fec_get_length_rec(peek);
    uref_block_peek_unmap(fec_uref, RTP_HEADER_SIZE, fec_header, peek);
}

/* Delete main packets older than the reference point */
static void clear_main_list(struct uchain *main_list, uint16_t snbase)
{
    struct uchain *uchain, *uchain_tmp;

    ulist_delete_foreach (main_list, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        if (!seq_num_lt(uref->priv, snbase))
            break;

        ulist_delete(uchain);
        uref_free(uref);
    }
}

// TODO : merge with above, cache seqnum
/* Delete FEC packets older than the reference point */
static void clear_fec_list(struct uchain *fec_list, uint16_t last_fec_snbase)
{
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (fec_list, uchain, uchain_tmp) {
        struct uref *fec_uref = uref_from_uchain(uchain);
        uint16_t snbase_low = fec_uref->priv >> 32;

        if (!seq_num_lt(snbase_low, last_fec_snbase))
            break;

        ulist_delete(uchain);
        uref_free(fec_uref);
    }
}

static void insert_ordered_uref(struct uchain *queue, struct uref *uref)
{
    uint16_t new_seqnum = uref->priv;

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach_reverse(queue, uchain, uchain_tmp) {
        struct uref *cur_uref = uref_from_uchain(uchain);
        uint16_t seqnum = cur_uref->priv;

        /* Duplicate packet */
        if (new_seqnum == seqnum) {
            uref_free(uref);
            return;
        }

        if (!seq_num_lt(new_seqnum, seqnum))
            break;

        /* Check previous packet if any */
        struct uref *prev_uref = uref_from_uchain(uchain->prev);
        if (prev_uref) {
            uint16_t prev_seqnum = prev_uref->priv;
            if (prev_seqnum == new_seqnum)
                continue;

            if (seq_num_lt(new_seqnum, prev_uref->priv))
                continue;
        }

        uref_clock_delete_date_sys(uref);
        ulist_insert(uchain->prev, uchain, uref_to_uchain(uref));
        return;
    }

    /* Add to end of queue */
    ulist_add(queue, uref_to_uchain(uref));
}

/* apply the correction from that fec packet */
static void upipe_rtp_fec_correct_packets(struct upipe *upipe,
        struct uref *fec_uref, uint16_t *seqnum_list, int items)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_upipe(upipe);

    bool found_seqnum[FEC_MAX] = {0};

    /* Search to see if any packets are lost */
    int processed = 0;
    struct uchain *uchain, *uchain_tmp;
    ulist_foreach (&upipe_rtp_fec->main_queue, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        uint16_t seqnum = uref->priv;

        for (int i = 0; i < items; i++) {
            if (seqnum_list[i] == seqnum) {
                processed++;
                found_seqnum[i] = true;
                break;
            }
        }

        if (processed == items) {
            upipe_verbose_va(upipe, "no packets lost");
            uref_free(fec_uref);
            return;
        }
    }

    if (processed != items - 1) {
        upipe_dbg_va(upipe, "Too much packet loss: found only %d out of %d",
                processed, items);
        uref_free(fec_uref);
        return;
    }

    /* Extract parameters from FEC packet */
    uint16_t length_rec;
    uint32_t ts_rec;
    upipe_rtp_fec_extract_parameters(fec_uref, &ts_rec, &length_rec);

    /* Recoverable packet */
    ulist_foreach (&upipe_rtp_fec->main_queue, uchain) {
        struct uref *uref = uref_from_uchain(uchain);

        // TODO: only if in list?

        uint8_t rtp_buffer[RTP_HEADER_SIZE];
        const uint8_t *rtp_header = uref_block_peek(uref, 0, RTP_HEADER_SIZE,
                rtp_buffer);
        if (unlikely(rtp_header == NULL)) {
            upipe_warn(upipe, "invalid buffer");
            continue;
        }

        uint32_t timestamp = rtp_get_timestamp(rtp_header);
        uref_block_peek_unmap(uref, 0, rtp_buffer, rtp_header);

        /* Recover length and timestamp of missing packet */
        for (int i = 0; i < items; i++) {
            if (seqnum_list[i] == uref->priv) {
                size_t uref_len = 0;
                uref_block_size(uref, &uref_len);
                uref_len -= RTP_HEADER_SIZE;

                length_rec ^= uref_len;
                ts_rec ^= timestamp;
            }
        }
    }

    if (length_rec != 7 * TS_SIZE)
        upipe_warn_va(upipe_rtp_fec_to_upipe(upipe_rtp_fec),
                "DUBIOUS REC LEN %i timestamp %u", length_rec, ts_rec);

    uref_block_resize(fec_uref, SMPTE_2022_FEC_HEADER_SIZE, -1);
    uint8_t *dst;
    int size = length_rec + RTP_HEADER_SIZE;
    uref_block_write(fec_uref, 0, &size, &dst);

    bool copy_header = true;

    processed = 0;
    ulist_foreach (&upipe_rtp_fec->main_queue, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        for (int i = 0; i < items; i++) {
            if (seqnum_list[i] == uref->priv) {
                size_t size = 0;
                uref_block_size(uref, &size);
                uint8_t payload_buf[TS_SIZE * 7 + RTP_HEADER_SIZE];

                if(size < sizeof(payload_buf))
                    continue;

                // TODO: uref_block_read in a loop
                const uint8_t *peek = uref_block_peek(uref, 0, size,
                        payload_buf);
                if (copy_header) {
                    memcpy(dst, peek, RTP_HEADER_SIZE);
                    copy_header = false;
                }
                for (int i = 0; i < size - RTP_HEADER_SIZE; i++)
                    dst[RTP_HEADER_SIZE + i] ^= peek[RTP_HEADER_SIZE + i];
                uref_block_peek_unmap(uref, RTP_HEADER_SIZE, payload_buf, peek);
                processed++;
                break;
            }
        }

        if (processed == items-1)
            break;
    }

    /* Maybe possible to merge with above */
    uint16_t missing_seqnum = 0;
    for (int i = 0; i < items; i++)
        if (!found_seqnum[i]) {
            missing_seqnum = seqnum_list[i];
            break;
        }

    upipe_dbg_va(&upipe_rtp_fec->upipe, "Corrected packet. Sequence number: %u", missing_seqnum);
    upipe_rtp_fec->recovered++;
    fec_uref->priv = missing_seqnum;
    rtp_set_seqnum(dst, missing_seqnum);
    rtp_set_timestamp(dst, ts_rec);
    uref_block_unmap(fec_uref, 0);
    uref_block_resize(fec_uref, 0, size);

    /* Don't insert an FEC corrected packet from the past */
    if (upipe_rtp_fec->last_send_seqnum != UINT32_MAX &&
       (seq_num_lt(missing_seqnum, upipe_rtp_fec->last_send_seqnum) || upipe_rtp_fec->last_send_seqnum == missing_seqnum))
        uref_free(fec_uref);
    else
        insert_ordered_uref(&upipe_rtp_fec->main_queue, fec_uref);
}

static void upipe_rtp_fec_apply_col_fec(struct upipe *upipe)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_upipe(upipe);
    uint16_t seqnum_list[FEC_MAX];

    for (;;) {
        struct uchain *fec_uchain = ulist_peek(&upipe_rtp_fec->col_queue);
        if (!fec_uchain)
            break;

        struct uref *fec_uref = uref_from_uchain(fec_uchain);
        uint16_t snbase_low = fec_uref->priv >> 32;
        uint16_t col_delta = upipe_rtp_fec->last_seqnum - snbase_low - 1;

        /* Account for late column FEC packets by making sure at least one extra row exists */
        if (col_delta <= (upipe_rtp_fec->cols + 1) * upipe_rtp_fec->rows)
            break;

        ulist_pop(&upipe_rtp_fec->col_queue);

        /* If no current matrix is being processed and we have enough packets
         * set existing matrix to the snbase value */
        if (upipe_rtp_fec->cur_matrix_snbase == UINT32_MAX &&
            seq_num_lt(upipe_rtp_fec->first_seqnum, snbase_low)) {
            upipe_rtp_fec->cur_matrix_snbase = snbase_low;
        }

        /* Build a list of the expected sequence numbers in matrix column */
        seqnum_list[0] = snbase_low;
        for (int i = 1; i < upipe_rtp_fec->rows; i++)
            seqnum_list[i] = seqnum_list[i-1] + upipe_rtp_fec->cols;

        upipe_rtp_fec_correct_packets(upipe, fec_uref, seqnum_list,
                upipe_rtp_fec->rows);
    }
}

static void upipe_rtp_fec_apply_row_fec(struct upipe *upipe,
                                       uint16_t cur_row_fec_snbase)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_upipe(upipe);

    uint16_t seqnum_list[FEC_MAX];

    /* get rid of old row FEC packets */
    clear_fec_list(&upipe_rtp_fec->row_queue, cur_row_fec_snbase);

    /* Row FEC packets are optional so may not actually exist */
    struct uchain *fec_uchain = ulist_pop(&upipe_rtp_fec->row_queue);
    if (!fec_uchain)
        return;

    struct uref *fec_uref = uref_from_uchain(fec_uchain);
    uint16_t snbase_low = fec_uref->priv >> 32;

    upipe_rtp_fec->cur_row_fec_snbase = snbase_low;

    /* Build a list of the expected sequence numbers */
    for (int i = 0; i < upipe_rtp_fec->cols; i++)
        seqnum_list[i] = snbase_low++;

    upipe_rtp_fec_correct_packets(upipe, fec_uref, seqnum_list,
            upipe_rtp_fec->cols);
}

static void upipe_rtp_fec_clear_queue(struct uchain *queue)
{
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }
}

static void upipe_rtp_fec_clear(struct upipe_rtp_fec *upipe_rtp_fec)
{
    upipe_rtp_fec_clear_queue(&upipe_rtp_fec->main_queue);
    upipe_rtp_fec_clear_queue(&upipe_rtp_fec->col_queue);
    upipe_rtp_fec_clear_queue(&upipe_rtp_fec->row_queue);
}

// TODO: wait_upump?
static void upipe_rtp_fec_timer(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_upipe(upipe);
    uint64_t now = uclock_now(upipe_rtp_fec->uclock);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_rtp_fec->main_queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        uint64_t date_sys = UINT64_MAX;
        int type;
        uref_clock_get_date_sys(uref, &date_sys, &type);
        uint64_t seqnum = uref->priv & UINT32_MAX;

        if (date_sys != UINT64_MAX) {
            // TODO: replace by output latency
            date_sys += upipe_rtp_fec->latency;

            if (now < date_sys)
                break;

            uref_clock_set_date_sys(uref, date_sys, type);
        }

        ulist_delete(uchain);
        upipe_rtp_fec_output(upipe, uref, NULL);

        if (upipe_rtp_fec->last_send_seqnum != UINT32_MAX) {
            uint16_t expected = upipe_rtp_fec->last_send_seqnum + 1;
            if (expected != seqnum) {
                upipe_dbg_va(upipe, "FEC output LOST, expected seqnum %hu got %" PRIu64,
                        expected, seqnum);
                upipe_rtp_fec->lost +=
                    (seqnum + UINT16_MAX + 1 - expected) & UINT16_MAX;

            }
        }

        upipe_rtp_fec->last_send_seqnum = seqnum;
    }
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 */
static int upipe_rtp_fec_build_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uref_flow_set_def(flow_def, "block.mpegtsaligned.");
    upipe_rtp_fec_store_flow_def(upipe, flow_def);

    return UBASE_ERR_NONE;
}

/* Clear matrices if change of FEC */
static void clear_fec(struct upipe *upipe)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_upipe(upipe);

    upipe_rtp_fec_clear(upipe_rtp_fec);

    upipe_rtp_fec->prev_sys = UINT64_MAX;

    upipe_rtp_fec->first_seqnum = UINT32_MAX;
    upipe_rtp_fec->last_seqnum = UINT32_MAX;
    upipe_rtp_fec->latency = 0;

    memset(upipe_rtp_fec->recent, 0xff, 2 * upipe_rtp_fec->rows *
            upipe_rtp_fec->cols * sizeof(*upipe_rtp_fec->recent));
}

static void upipe_rtp_fec_start_timer(struct upipe *upipe, uint16_t seqnum)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_sub_mgr(upipe->mgr);

    /* Clear any old non-FEC packets */
    clear_main_list(&upipe_rtp_fec->main_queue, upipe_rtp_fec->cur_matrix_snbase);

    struct uchain *first_uchain = ulist_peek(&upipe_rtp_fec->main_queue);
    if (!first_uchain)
        return;

    struct uref *first_uref = uref_from_uchain(first_uchain);
    upipe_rtp_fec->first_seqnum = first_uref->priv;

    /* Make sure we have at least two matrices of data as per the spec */
    uint16_t seq_delta = seqnum - upipe_rtp_fec->first_seqnum - 1;
    int two_matrix_size = 2 * upipe_rtp_fec->cols * upipe_rtp_fec->rows;
    if (seq_delta < two_matrix_size || seq_delta == UINT16_MAX) {
        return;
    }

    /* Calculate delay from first packet of matrix arriving to pump start time */
    int type;
    uint64_t date_sys = UINT64_MAX;
    uref_clock_get_date_sys(first_uref, &date_sys, &type);

    if (date_sys == UINT64_MAX) {
        /* First packet having an unusable date_sys is not useful */
        ulist_delete(first_uchain);
        uref_free(first_uref);
        first_uchain = ulist_peek(&upipe_rtp_fec->main_queue);
        if (first_uchain) {
            struct uref *uref = uref_from_uchain(first_uchain);
            upipe_rtp_fec->first_seqnum = uref->priv;
        }
        return;
    }

    /* First packet of matrix can be recovered packet and have no date_sys */
    uint64_t now = uclock_now(upipe_rtp_fec->uclock);
    upipe_rtp_fec->latency = now - date_sys + UPIPE_FEC_JITTER;

    /* Start pump that clears the buffer */
    struct upump *upump = upump_alloc_timer(upipe_rtp_fec->upump_mgr,
            upipe_rtp_fec_timer, &upipe_rtp_fec->upipe,
            upipe_rtp_fec->upipe.refcount,
            0, UCLOCK_FREQ/90000); // 300Hz ??!
    upipe_rtp_fec_set_upump(&upipe_rtp_fec->upipe, upump);
    upump_start(upump);
}

/* main */
static void upipe_rtp_fec_main_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_sub_mgr(upipe->mgr);
    struct upipe *super_pipe = upipe_rtp_fec_to_upipe(upipe_rtp_fec);

    uint64_t date_sys = UINT64_MAX;
    int type;
    uref_clock_get_date_sys(uref, &date_sys, &type);

    if (upipe_rtp_fec->prev_date_sys == UINT64_MAX ||
            upipe_rtp_fec->prev_date_sys == date_sys) {
        upipe_verbose_va(upipe, "date_sys == %" PRIu64 ", waiting for increase",
            date_sys);
        upipe_rtp_fec->prev_date_sys = date_sys;
        uref_free(uref);
        return;
    }

    uint16_t seqnum = uref->priv;

    if (upipe_rtp_fec->first_seqnum == UINT32_MAX)
        upipe_rtp_fec->first_seqnum = seqnum;

    /* Output packets immediately if no FEC packets found as per spec */
    if (!upipe_rtp_fec->cols && !upipe_rtp_fec->rows) {
        upipe_rtp_fec->last_seqnum = seqnum;
        upipe_verbose_va(upipe, "no FEC detected");
        upipe_rtp_fec_output(&upipe_rtp_fec->upipe, uref, NULL);
        return;
    }

    /* We use timestamp difference to measure the duration of 2 matrices.
     * When we start receiving packets, the Linux buffer is emptied at once,
     * and all the packets have the same timestamp.
     * Discard these until we can make a good measurement */
    if (upipe_rtp_fec->prev_sys != UINT64_MAX) {
        if (upipe_rtp_fec->prev_sys == date_sys) {
            clear_fec(super_pipe);
            uref_free(uref);
            return;
        }
    }

    upipe_rtp_fec->prev_sys = date_sys;

    /* Difference between last received sequence number and current sequence number */
    uint16_t seq_delta = upipe_rtp_fec->last_seqnum - seqnum;
    if (seq_delta > 0x8000)
        seq_delta = -seq_delta; // XXX ?

    unsigned int two_matrix_size = 2 * upipe_rtp_fec->cols * upipe_rtp_fec->rows;
    bool fec_change = false;
    if (upipe_rtp_fec->last_seqnum != UINT32_MAX && seq_delta > two_matrix_size) {
        /* Resync if packet is too old or too new */
        upipe_warn_va(upipe, "resync");
        fec_change = true;
        uref_free(uref);
    } else if (upipe_rtp_fec->last_seqnum != UINT32_MAX &&
               upipe_rtp_fec->last_send_seqnum != UINT32_MAX &&
               seq_num_lt(seqnum, upipe_rtp_fec->last_send_seqnum)) {
        /* Packet is older than the last sent packet but within the two-matrix window so don't insert
           But don't resync either. Packet is late but not late enough to resync */
        uref_free(uref);
    } else {
        upipe_rtp_fec->last_seqnum = seqnum;

        /* Get the date before inserting uref.
           Inserting uref could delete the date if packet is reordered */
        int type;
        uint64_t date_sys = 0;
        uref_clock_get_date_sys(uref, &date_sys, &type);

        insert_ordered_uref(&upipe_rtp_fec->main_queue, uref);

        /* Owing to clock drift the latency of 2x the FEC matrix may increase
         * Build a continually updating duration and correct the latency if necessary.
         * Also helps with undershoot of latency calculation from initial packets */
        uint8_t idx = seqnum % two_matrix_size;
        uint64_t prev_date_sys = upipe_rtp_fec->recent[idx].date_sys;
        uint64_t prev_seqnum = upipe_rtp_fec->recent[idx].seqnum;
        uint16_t expected_seqnum = prev_seqnum + two_matrix_size;
        uint16_t later_seqnum = seqnum + two_matrix_size;
        uint8_t new_idx = later_seqnum % two_matrix_size;

        /* Make sure the sequence number is exactly two matrices behind and not more,
         * otherwise the latency calculation will be too large.
         * date_sys or prev_date_sys could be reordered */
        if (date_sys != UINT64_MAX && prev_date_sys != UINT64_MAX && prev_seqnum != UINT64_MAX && seqnum == expected_seqnum) {
            uint64_t latency = date_sys - prev_date_sys;
            if (latency > LATENCY_MAX) {
                upipe_warn_va(upipe,"resync. Latency too high. date_sys %"PRIu64" prev_date_sys %"PRIu64", seqnum %u, prev_seqnum %"PRIu64"", date_sys, prev_date_sys, seqnum, prev_seqnum);
                fec_change = true;
            } else if (upipe_rtp_fec->latency < latency) {
                upipe_rtp_fec->latency = latency + UPIPE_FEC_JITTER;
                upipe_warn_va(upipe, "Late packets increasing buffer-size/latency to %f seconds", (double)upipe_rtp_fec->latency / UCLOCK_FREQ);
            }
        }

        upipe_rtp_fec->recent[new_idx].date_sys = date_sys;
        upipe_rtp_fec->recent[new_idx].seqnum = seqnum;
    }

    if (!upipe_rtp_fec->cols)
        goto out;

    if (upipe_rtp_fec->rows) {
        upipe_rtp_fec_apply_col_fec(super_pipe);

        uint32_t cur_row_fec_snbase = upipe_rtp_fec->cur_row_fec_snbase;
        if (cur_row_fec_snbase == UINT32_MAX)
            cur_row_fec_snbase = upipe_rtp_fec->first_seqnum;

        /* Wait for two rows to arrive to allow for late row FEC packets */
        uint16_t row_delta = seqnum - cur_row_fec_snbase - 1;
        if (!seq_num_lt(seqnum, cur_row_fec_snbase) && row_delta > 2 * upipe_rtp_fec->cols)
            upipe_rtp_fec_apply_row_fec(super_pipe, cur_row_fec_snbase);
    }

    if (upipe_rtp_fec->cur_matrix_snbase == UINT32_MAX)
        goto out;

    if (!upipe_rtp_fec->upump)
        upipe_rtp_fec_start_timer(upipe, seqnum);

out:
    if (fec_change)
        clear_fec(super_pipe);
}

static void upipe_rtp_fec_colrow_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_rtp_fec *upipe_rtp_fec =
        upipe_rtp_fec_from_sub_mgr(upipe->mgr);

    uint8_t fec_buffer[SMPTE_2022_FEC_HEADER_SIZE];

    const uint8_t *fec_header = uref_block_peek(uref, RTP_HEADER_SIZE,
            sizeof(fec_buffer), fec_buffer);
    uint8_t d = smpte_fec_check_d(fec_header);
    uint8_t offset = smpte_fec_get_offset(fec_header);
    uint8_t na = smpte_fec_get_na(fec_header);
    uint64_t snbase_low = smpte_fec_get_snbase_low(fec_header);
    uref->priv |= (snbase_low << 32);
    uref_block_peek_unmap(uref, RTP_HEADER_SIZE, fec_buffer, fec_header);

    bool col = (upipe == upipe_rtp_fec_to_col_subpipe(upipe_rtp_fec));
    struct uchain *queue = col ? &upipe_rtp_fec->col_queue :
        &upipe_rtp_fec->row_queue;

    if (col) {
        if (d) {
            upipe_warn(upipe, "Invalid column FEC packet found, ignoring");
            goto invalid;
        }

        if (!offset || !na) {
            upipe_warn(upipe, "Invalid row/column in FEC packet, ignoring");
            goto invalid;
        }

        if (upipe_rtp_fec->cols != offset) {
            upipe_rtp_fec->cols = offset;
            upipe_rtp_fec->rows = na;

            upipe_warn_va(upipe, "FEC detected %u rows and %u columns", upipe_rtp_fec->rows,
                    upipe_rtp_fec->cols);
            clear_fec(upipe_rtp_fec_to_upipe(upipe_rtp_fec));
        }
    } else {
        assert(upipe == upipe_rtp_fec_to_row_subpipe(upipe_rtp_fec));
        if (!d) {
            upipe_warn(upipe, "Invalid row FEC packet found, ignoring");
            goto invalid;
        }
    }

    insert_ordered_uref(queue, uref);
    upipe_rtp_fec->pkts_since_last_fec = 0;
    return;

invalid:
    uref_free(uref);
}

/** @internal @This handles input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_rtp_fec_sub_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_rtp_fec *upipe_rtp_fec =
        upipe_rtp_fec_from_sub_mgr(upipe->mgr);

    uint8_t rtp_buffer[RTP_HEADER_SIZE];
    const uint8_t *rtp_header = uref_block_peek(uref, 0, sizeof(rtp_buffer),
            rtp_buffer);
    if (unlikely(rtp_header == NULL)) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }

    uref->priv = rtp_get_seqnum(rtp_header);
    uref_block_peek_unmap(uref, 0, rtp_buffer, rtp_header);

    if (upipe != upipe_rtp_fec_to_main_subpipe(upipe_rtp_fec)) {
        upipe_rtp_fec_colrow_input(upipe, uref);
        return;
    }

    uint8_t pt = rtp_get_type(rtp_header);
    if (upipe_rtp_fec->pt != pt) {
        upipe_dbg_va(upipe, "Forwarding payload type %u", pt);
        upipe_rtp_fec_output(upipe_rtp_fec_to_upipe(upipe_rtp_fec), uref, NULL);
        return;
    }

    upipe_rtp_fec_main_input(upipe, uref);

    /* Disable FEC if no FEC packets arrive for a while */
    if (++upipe_rtp_fec->pkts_since_last_fec > 200 && // FIXME : hardcoded value
            (upipe_rtp_fec->rows || upipe_rtp_fec->cols)) {
        upipe_rtp_fec->rows = 0;
        upipe_rtp_fec->cols = 0;
        clear_fec(upipe_rtp_fec_to_upipe(upipe_rtp_fec));
        upipe_warn(upipe, "No FEC Packets received for a while, disabling FEC");
    }
}

/** @internal @This processes control commands on an output subpipe of an
 * upipe_rtp_fec pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtp_fec_sub_control(struct upipe *upipe,
                                     int command, va_list args)
{
    struct upipe_rtp_fec *upipe_rtp_fec =
        upipe_rtp_fec_from_sub_mgr(upipe->mgr);

    switch (command) {
    case UPIPE_REGISTER_REQUEST: {
        struct urequest *request = va_arg(args, struct urequest *);
        return upipe_throw_provide_request(upipe, request);
    }
    case UPIPE_UNREGISTER_REQUEST:
        return UBASE_ERR_NONE;
    case UPIPE_SET_FLOW_DEF: {
        if (upipe != upipe_rtp_fec_to_main_subpipe(upipe_rtp_fec))
            return UBASE_ERR_NONE;

        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_rtp_fec_build_flow_def(upipe_rtp_fec_to_upipe(
                    upipe_rtp_fec), flow_def);
    }
    case UPIPE_SUB_GET_SUPER: {
        struct upipe **p = va_arg(args, struct upipe **);
        *p = upipe_rtp_fec_to_upipe(upipe_rtp_fec);
        return UBASE_ERR_NONE;
    }

    default:
        return UBASE_ERR_UNHANDLED;
    }
}

/** @This cleans a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtp_fec_sub_clean(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
}

/** @internal @This initializes the output manager for an upipe_rtp_fec pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtp_fec_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_rtp_fec->sub_mgr;

    sub_mgr->refcount = upipe_rtp_fec_to_urefcount(upipe_rtp_fec);
    sub_mgr->signature = UPIPE_RTP_FEC_INPUT_SIGNATURE;
    sub_mgr->upipe_alloc = NULL;
    sub_mgr->upipe_input = upipe_rtp_fec_sub_input;
    sub_mgr->upipe_control = upipe_rtp_fec_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a rtp-fec pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_rtp_fec_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    if (signature != UPIPE_RTP_FEC_SIGNATURE)
        return NULL;
    struct uprobe *uprobe_main = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_col  = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_row  = va_arg(args, struct uprobe *);

    struct upipe_rtp_fec *upipe_rtp_fec =
        (struct upipe_rtp_fec *)calloc(1, sizeof(struct upipe_rtp_fec));
    if (unlikely(upipe_rtp_fec == NULL)) {
        uprobe_release(uprobe_main);
        uprobe_release(uprobe_col);
        uprobe_release(uprobe_row);
        return NULL;
    }

    upipe_rtp_fec->first_seqnum = UINT32_MAX;
    upipe_rtp_fec->last_seqnum = UINT32_MAX;
    upipe_rtp_fec->last_send_seqnum = UINT32_MAX;
    upipe_rtp_fec->cur_matrix_snbase = UINT32_MAX;
    upipe_rtp_fec->cur_row_fec_snbase = UINT32_MAX;
    upipe_rtp_fec->pt = UINT8_MAX;

    upipe_rtp_fec->lost = 0;
    upipe_rtp_fec->prev_date_sys = UINT64_MAX;
    upipe_rtp_fec->recovered = 0;
    upipe_rtp_fec->prev_sys = UINT64_MAX;

    struct upipe *upipe = upipe_rtp_fec_to_upipe(upipe_rtp_fec);
    upipe_init(upipe, mgr, uprobe);

    upipe_rtp_fec_init_upump_mgr(upipe);
    upipe_rtp_fec_init_upump(upipe);
    upipe_rtp_fec_init_uclock(upipe);
    upipe_rtp_fec_init_urefcount(upipe);
    upipe_rtp_fec_init_sub_mgr(upipe);
    upipe_rtp_fec_init_output(upipe);

    /* Initialize subpipes */
    upipe_rtp_fec_sub_init(upipe_rtp_fec_to_main_subpipe(upipe_rtp_fec),
                            &upipe_rtp_fec->sub_mgr, uprobe_main);
    upipe_rtp_fec_sub_init(upipe_rtp_fec_to_col_subpipe(upipe_rtp_fec),
                            &upipe_rtp_fec->sub_mgr, uprobe_col);
    upipe_rtp_fec_sub_init(upipe_rtp_fec_to_row_subpipe(upipe_rtp_fec),
                            &upipe_rtp_fec->sub_mgr, uprobe_row);

    ulist_init(&upipe_rtp_fec->main_queue);
    ulist_init(&upipe_rtp_fec->col_queue);
    ulist_init(&upipe_rtp_fec->row_queue);

    upipe_rtp_fec_check_upump_mgr(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtp_fec_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_upipe(upipe);

    switch (command) {
    case UPIPE_ATTACH_UPUMP_MGR:
        upipe_rtp_fec_set_upump(upipe, NULL);
        return upipe_rtp_fec_attach_upump_mgr(upipe);
    case UPIPE_ATTACH_UCLOCK:
        upipe_rtp_fec_set_upump(upipe, NULL);
        upipe_rtp_fec_require_uclock(upipe);
        return UBASE_ERR_NONE;
    case UPIPE_REGISTER_REQUEST: {
        struct urequest *request = va_arg(args, struct urequest *);
        return upipe_throw_provide_request(upipe, request);
    }
    case UPIPE_UNREGISTER_REQUEST:
        return UBASE_ERR_NONE;
    case UPIPE_GET_FLOW_DEF: {
        struct uref **p = va_arg(args, struct uref **);
        return upipe_rtp_fec_get_flow_def(upipe, p);
    }
    case UPIPE_GET_OUTPUT: {
        struct upipe **p = va_arg(args, struct upipe **);
        return upipe_rtp_fec_get_output(upipe, p);
    }
    case UPIPE_SET_OUTPUT: {
        struct upipe *output = va_arg(args, struct upipe *);
        return upipe_rtp_fec_set_output(upipe, output);
    }

    /* specific commands */
    case UPIPE_RTP_FEC_GET_MAIN_SUB: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_FEC_SIGNATURE)
        struct upipe **upipe_p = va_arg(args, struct upipe **);
        *upipe_p = upipe_rtp_fec_to_main_subpipe(upipe_rtp_fec);
        return UBASE_ERR_NONE;
    }
    case UPIPE_RTP_FEC_GET_COL_SUB: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_FEC_SIGNATURE)
        struct upipe **upipe_p = va_arg(args, struct upipe **);
        *upipe_p = upipe_rtp_fec_to_col_subpipe(upipe_rtp_fec);
        return UBASE_ERR_NONE;
    }
    case UPIPE_RTP_FEC_GET_ROW_SUB: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_FEC_SIGNATURE)
        struct upipe **upipe_p = va_arg(args, struct upipe **);
        *upipe_p = upipe_rtp_fec_to_row_subpipe(upipe_rtp_fec);
        return UBASE_ERR_NONE;
    }
    case UPIPE_RTP_FEC_GET_PACKETS_LOST: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_FEC_SIGNATURE)
        uint64_t *lost = va_arg(args, uint64_t*);
        *lost = upipe_rtp_fec->lost;
        upipe_rtp_fec->lost = 0; /* reset counter */
        return UBASE_ERR_NONE;
    }
    case UPIPE_RTP_FEC_GET_PACKETS_RECOVERED: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_FEC_SIGNATURE)
        uint64_t *recovered = va_arg(args, uint64_t*);
        *recovered = upipe_rtp_fec->recovered;
        upipe_rtp_fec->recovered = 0; /* reset counter */
        return UBASE_ERR_NONE;
    }
    case UPIPE_RTP_FEC_GET_ROWS: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_FEC_SIGNATURE)
        uint64_t *rows = va_arg(args, uint64_t*);
        *rows = upipe_rtp_fec->rows;
        return UBASE_ERR_NONE;
    }
    case UPIPE_RTP_FEC_GET_COLUMNS: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_FEC_SIGNATURE)
        uint64_t *columns = va_arg(args, uint64_t*);
        *columns = upipe_rtp_fec->cols;
        return UBASE_ERR_NONE;
    }
    case UPIPE_RTP_FEC_SET_PT: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_FEC_SIGNATURE)
        upipe_rtp_fec->pt = va_arg(args, unsigned);
        return UBASE_ERR_NONE;
    }
    default:
        return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtp_fec_free(struct upipe *upipe)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_upipe(upipe);

    upipe_throw_dead(upipe);

    upipe_rtp_fec_clear(upipe_rtp_fec);

    upipe_rtp_fec_sub_clean(upipe_rtp_fec_to_main_subpipe(upipe_rtp_fec));
    upipe_rtp_fec_sub_clean(upipe_rtp_fec_to_col_subpipe(upipe_rtp_fec));
    upipe_rtp_fec_sub_clean(upipe_rtp_fec_to_row_subpipe(upipe_rtp_fec));

    upipe_rtp_fec_clean_uclock(upipe);
    upipe_rtp_fec_clean_upump(upipe);
    upipe_rtp_fec_clean_upump_mgr(upipe);
    upipe_rtp_fec_clean_urefcount(upipe);

    upipe_rtp_fec_clean_output(upipe);

    upipe_clean(upipe);
    free(upipe_rtp_fec);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_rtp_fec_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTP_FEC_SIGNATURE,

    .upipe_alloc = _upipe_rtp_fec_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_rtp_fec_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for rtp-fec pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtp_fec_mgr_alloc(void)
{
    return &upipe_rtp_fec_mgr;
}
