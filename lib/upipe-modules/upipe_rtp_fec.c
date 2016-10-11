/** @file
 * @short Upipe RTP FEC module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/upipe.h>
#include <upipe/ulist.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>

#include <upipe-modules/upipe_rtp_decaps.h>
#include <upipe-modules/upipe_rtp_fec.h>
#include <bitstream/ietf/rtp.h>
#include <bitstream/smpte/2022_1_fec.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#define UPIPE_FEC_JITTER UCLOCK_FREQ/25

/** @hidden */
static int upipe_rtp_fec_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of an output of an upipe_rtp_fec sub
 * pipe. */
struct upipe_rtp_fec_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** buffered urefs */
    struct uchain urefs;

    /** public upipe structure */
    struct upipe upipe;
};

struct seqnum_history {
    uint64_t seqnum;
    uint64_t date_sys;
};

/** upipe_rtp_fec structure with rtp-fec parameters */
struct upipe_rtp_fec {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

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

    int64_t pkts_since_last_fec;

    int cols;
    int rows;

    uint64_t first_seqnum;
    uint64_t last_seqnum;
    uint64_t last_send_seqnum;

    /* Lowest (base) sequence number of current FEC matrix */
    uint64_t cur_matrix_snbase;
    /* Lowest (base) sequence number of current FEC row */
    uint64_t cur_row_fec_snbase;

    struct seqnum_history *recent;
    int recent_len;
    uint64_t latency;

    /** main subpipe **/
    struct upipe_rtp_fec_sub main_subpipe;
    /** col subpipe */
    struct upipe_rtp_fec_sub col_subpipe;
    /** row subpipe */
    struct upipe_rtp_fec_sub row_subpipe;

    struct uchain main_queue;
    struct uchain col_queue;
    struct uchain row_queue;

    int pump_start;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtp_fec, upipe, UPIPE_RTP_FEC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_rtp_fec, urefcount, upipe_rtp_fec_free);

UPIPE_HELPER_OUTPUT(upipe_rtp_fec, output, flow_def, output_state, request_list)

UPIPE_HELPER_UREF_MGR(upipe_rtp_fec, uref_mgr, uref_mgr_request,
                      upipe_rtp_fec_check,
                      upipe_rtp_fec_register_output_request,
                      upipe_rtp_fec_unregister_output_request)

UPIPE_HELPER_UCLOCK(upipe_rtp_fec, uclock, uclock_request, upipe_rtp_fec_check,
                    upipe_rtp_fec_register_output_request,
                    upipe_rtp_fec_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_rtp_fec, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_rtp_fec, upump, upump_mgr);

UPIPE_HELPER_UPIPE(upipe_rtp_fec_sub, upipe, UPIPE_RTP_FEC_INPUT_SIGNATURE)

UBASE_FROM_TO(upipe_rtp_fec, upipe_mgr, sub_mgr, sub_mgr)
UBASE_FROM_TO(upipe_rtp_fec, upipe_rtp_fec_sub, main_subpipe, main_subpipe)
UBASE_FROM_TO(upipe_rtp_fec, upipe_rtp_fec_sub, col_subpipe, col_subpipe)
UBASE_FROM_TO(upipe_rtp_fec, upipe_rtp_fec_sub, row_subpipe, row_subpipe)

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

static void upipe_rtp_fec_xor_c(uint8_t *dst, const uint8_t *src, size_t len)
{
    for (int i = 0; i < len; i++)
        dst[i] ^= src[i];
}

/* based off Live555 code */
static inline int seq_num_lt(uint16_t s1, uint16_t s2)
 {
    /* a 'less-than' on 16-bit sequence numbers */
    int diff = s2-s1;
    if (diff > 0)
        return (diff < 0x8000);
    else if (diff < 0)
        return (diff < -0x8000);
    else
        return 0;
}

/* Clears the main queue of packets with seqnum < snbase */
static void clear_main_list(struct uchain *main_list, uint64_t snbase)
{
    struct uchain *uchain, *uchain_tmp;

    /* Delete main packets older than the reference point */
    ulist_delete_foreach (main_list, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        uint64_t seqnum = 0;
        uref_rtp_get_seqnum(uref, &seqnum);

        if (seq_num_lt(seqnum, snbase)){
            ulist_delete(uchain);
            uref_free(uref);
        }
    }
}

static void clear_fec_list(struct uchain *fec_list, uint64_t last_fec_snbase)
{
    struct uchain *uchain, *uchain_tmp;
    const uint8_t *peek;
    uint8_t fec_header[SMPTE_2022_FEC_HEADER_SIZE];
    int size = SMPTE_2022_FEC_HEADER_SIZE;

    /* Delete FEC packets older than the reference point */
    ulist_delete_foreach (fec_list, uchain, uchain_tmp) {
        struct uref *fec_uref = uref_from_uchain(uchain);
        peek = uref_block_peek(fec_uref, 0, size, fec_header);
        uint64_t snbase_low = smpte_fec_get_snbase_low(peek);
        uref_block_peek_unmap(fec_uref, 0, fec_header, peek);

        if (seq_num_lt(snbase_low, last_fec_snbase)) {
            ulist_delete(uchain);
            uref_free(fec_uref);
        }
    }
}

static void insert_ordered_uref(struct uchain *queue, struct uref *uref)
{
    int dup = 0, ooo = 0;
    struct uchain *uchain, *uchain_tmp;
    uint64_t new_seqnum = 0;
    uref_rtp_get_seqnum(uref, &new_seqnum);

    //printf("\n INSERT \n");

    ulist_delete_foreach_reverse(queue, uchain, uchain_tmp) {
        struct uref *cur_uref = uref_from_uchain(uchain);
        uint64_t seqnum = 0;
        uref_rtp_get_seqnum(cur_uref, &seqnum);

        //printf("\n checking seqnum %u new_seqnum %u \n", seqnum, new_seqnum);
        if (seq_num_lt(new_seqnum, seqnum)) {
            /* Inserting first needs a special case */
            if (ulist_is_first(queue, uchain)){
                //printf("\n FIRST insert %u before %u \n", new_seqnum, seqnum );
                uref_clock_delete_date_sys(uref);
                ulist_insert(uchain->prev, uchain, uref_to_uchain(uref));
                ooo = 1;
                break;
            }
            else {
                struct uref *prev_uref = uref_from_uchain(uchain->prev);
                uint64_t prev_seqnum = 0;
                uref_rtp_get_seqnum(prev_uref, &prev_seqnum);
                if (!seq_num_lt(new_seqnum, prev_seqnum) && !(new_seqnum == prev_seqnum)) {
                    //printf("\n insert %u before %u \n", new_seqnum, seqnum );
                    uref_clock_delete_date_sys(uref);
                    ulist_insert(uchain->prev, uchain, uref_to_uchain(uref));
                    ooo = 1;
                    break;
                }
            }
        }
        /* Duplicate packet */
        else if (new_seqnum == seqnum) {
            //printf("\n duplicate \n");
            dup = 1;
            uref_free(uref);
            break;
        }
        else
            break;
    }

    /* Add to end if normal packet */
    if ((!dup && !ooo)) {
        ulist_add(queue, uref_to_uchain(uref));
        //printf("\n adding %u \n", new_seqnum);
    }
}

static void upipe_rtp_fec_extract_parameters(struct uref *fec_uref,
                                             uint64_t *snbase_low,
                                             uint64_t *ts_rec,
                                             uint16_t *length_rec)
{
    const uint8_t *peek;
    uint8_t fec_header[SMPTE_2022_FEC_HEADER_SIZE];
    size_t size = SMPTE_2022_FEC_HEADER_SIZE;
    
    peek = uref_block_peek(fec_uref, 0, size, fec_header);
    *snbase_low = smpte_fec_get_snbase_low(peek);
    *ts_rec = smpte_fec_get_ts_recovery(peek);
    *length_rec = smpte_fec_get_length_rec(peek);
    uref_block_peek_unmap(fec_uref, 0, fec_header, peek);
}

static void upipe_rtp_fec_correct_packets(struct upipe_rtp_fec *upipe_rtp_fec,
                                          struct uref *fec_uref,
                                          uint64_t ts_rec,
                                          uint16_t length_rec,
                                          uint16_t *seqnum_list,
                                          int items)
{
    struct uchain *uchain, *uchain_tmp;
    const uint8_t *peek;
    uint64_t seqnum = 0;
    bool found_seqnum[50] = {0};
    uint8_t payload_buf[188*7];

    /* Build a list of expected packets */
    int processed = 0;
    ulist_foreach (&upipe_rtp_fec->main_queue, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        uref_rtp_get_seqnum(uref, &seqnum);

        for (int i = 0; i < items; i++) {
            if (seqnum_list[i] == seqnum) {
                processed++;
                found_seqnum[i] = 1;
            }

            if (processed == items)
                break;
        }
    }

    //printf("\n items %i processed %i snbase %u \n", items, processed, seqnum_list[0] );
    /* Recoverable packet */
    if (processed == items-1) {
        uint64_t missing_seqnum = 0;
        uint8_t *dst;

        ulist_foreach (&upipe_rtp_fec->main_queue, uchain) {
            struct uref *uref = uref_from_uchain(uchain);
            size_t uref_len = 0;
            uref_rtp_get_seqnum(uref, &seqnum);

            /* Recover length and timestamp of missing packet */
            for (int i = 0; i < items; i++) {
                if(seqnum_list[i] == seqnum) {
                    uint64_t timestamp = 0;
                    uref_rtp_get_timestamp(uref, &timestamp);
                    uref_block_size(uref, &uref_len);
                    length_rec ^= uref_len;
                    ts_rec ^= timestamp;
                    //printf("\n applying items %u length rec %u seqnum %u \n", items, uref_len, seqnum );
                }
            }
        }

        if( length_rec != 1316 )
        {
            //printf("\n DUBIOUS REC LEN %i \n", length_rec );
        }

        for (int i = 0; i < items; i++) {
            if (found_seqnum[i] == 0) {
                missing_seqnum = seqnum_list[i];
                break;
            }
        }

        uref_block_resize(fec_uref, SMPTE_2022_FEC_HEADER_SIZE, -1);
        uref_block_write(fec_uref, 0, (int *)&length_rec, &dst);

        processed = 0;
        ulist_foreach (&upipe_rtp_fec->main_queue, uchain) {
            struct uref *uref = uref_from_uchain(uchain);
            size_t size = 0;
            uref_rtp_get_seqnum(uref, &seqnum);

            for (int i = 0; i < items; i++) {
                if (seqnum_list[i] == seqnum) {
                    uref_block_size(uref, &size);
                    peek = uref_block_peek(uref, 0, -1, payload_buf);
                    upipe_rtp_fec_xor_c(dst, peek, size);
                    uref_block_peek_unmap(uref, 0, payload_buf, peek);
                    break;
                }
            }

            if (processed == items-1)
                break;
        }

        upipe_warn_va(&upipe_rtp_fec->upipe, "Corrected packet. Sequence number: %u", missing_seqnum);
        //printf("\n items %i correctheader %x not-lost seqnum %u timestamp %u \n", items, dst[0], missing_seqnum, ts_rec );
        uref_block_unmap(fec_uref, 0);
        uref_rtp_set_seqnum(fec_uref, missing_seqnum);
        uref_rtp_set_timestamp(fec_uref, ts_rec);

        insert_ordered_uref(&upipe_rtp_fec->main_queue, fec_uref);
    }
    else {
        uref_free(fec_uref);
    }
}

static int upipe_rtp_fec_apply_col_fec(struct upipe_rtp_fec *upipe_rtp_fec)
{
    uint64_t snbase_low, ts_rec;
    uint16_t length_rec;
    uint16_t seqnum_list[50];

    while (1) {
        struct uchain *fec_uchain = ulist_peek(&upipe_rtp_fec->col_queue);
        if (!fec_uchain)
            break;
        struct uref *fec_uref = uref_from_uchain(fec_uchain);

        /* Extract parameters from FEC packet */
        upipe_rtp_fec_extract_parameters(fec_uref, &snbase_low, &ts_rec, &length_rec); 
        uint64_t col_delta = (upipe_rtp_fec->last_seqnum + UINT16_MAX - snbase_low) & UINT16_MAX;

        /* Account for late column FEC packets by making sure at least one extra row exists */
        if (col_delta > (upipe_rtp_fec->cols+1)*upipe_rtp_fec->rows) {
            ulist_pop(&upipe_rtp_fec->col_queue);
            /* If no current matrix is being processed and we have enough packets
             * set existing matrix to the snbase value */
            if (upipe_rtp_fec->cur_matrix_snbase == UINT64_MAX &&
                seq_num_lt(upipe_rtp_fec->first_seqnum, snbase_low))
                upipe_rtp_fec->cur_matrix_snbase = snbase_low;

            uint16_t expected_seqnum = snbase_low;

            //printf("\n col queuelen %i snbaselow %u \n", ulist_depth(&upipe_rtp_fec->main_queue), expected_seqnum ); 

            /* Build a list of the expected sequence numbers in matrix column */
            for (int i = 0; i < upipe_rtp_fec->rows; i++) {
                seqnum_list[i] = expected_seqnum;
                expected_seqnum += upipe_rtp_fec->cols;
            }

            upipe_rtp_fec_correct_packets(upipe_rtp_fec, fec_uref, ts_rec,
                                          length_rec, seqnum_list, upipe_rtp_fec->rows);
        }
        else {
            break;
        }
    }

    return 0;
}

static int upipe_rtp_fec_apply_row_fec(struct upipe_rtp_fec *upipe_rtp_fec,
                                       uint64_t cur_row_fec_snbase)
{
    uint64_t snbase_low, ts_rec;
    uint16_t length_rec;
    uint16_t seqnum_list[50];

    /* get rid of old row FEC packets */
    clear_fec_list(&upipe_rtp_fec->row_queue, cur_row_fec_snbase);

    //printf("\n try row depth %i \n", ulist_depth(&upipe_rtp_fec->row_queue));

    /* Row FEC packets are optional so may not actually exist */
    struct uchain *fec_uchain = ulist_pop(&upipe_rtp_fec->row_queue);
    if (!fec_uchain)
        return 0;
    struct uref *fec_uref = uref_from_uchain(fec_uchain);

    /* Extract FEC parameters */
    upipe_rtp_fec_extract_parameters(fec_uref, &snbase_low, &ts_rec, &length_rec);

    uint16_t expected_seqnum = snbase_low;

    upipe_rtp_fec->cur_row_fec_snbase = snbase_low;

    //printf("\n row processing %"PRIu64" \n", snbase_low);
    //printf("\n row queuelen %i snbaselow %u length-rec %u \n", ulist_depth(&upipe_rtp_fec->main_queue), expected_seqnum, length_rec );

    /* Build a list of the expected sequence numbers */
    for (int i = 0; i < upipe_rtp_fec->cols; i++)
        seqnum_list[i] = expected_seqnum++;

    upipe_rtp_fec_correct_packets(upipe_rtp_fec, fec_uref, ts_rec,
                                  length_rec, seqnum_list, upipe_rtp_fec->cols);

    return 0;
}



static void upipe_rtp_fec_clear(struct upipe_rtp_fec *upipe_rtp_fec)
{
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_rtp_fec->main_queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }

    ulist_delete_foreach (&upipe_rtp_fec->col_queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }

    ulist_delete_foreach (&upipe_rtp_fec->row_queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }
}

static void upipe_rtp_fec_timer(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_upipe(upipe);
    uint64_t now = uclock_now(upipe_rtp_fec->uclock);
    uint64_t date_sys, seqnum = 0;
    int type;
    struct uchain *uchain, *uchain_tmp;
    struct uref *uref;

    //printf("\n iter \n");

    ulist_delete_foreach (&upipe_rtp_fec->main_queue, uchain, uchain_tmp) {
        uref = uref_from_uchain(uchain);
        uref_clock_get_date_sys(uref, &date_sys, &type);
        uref_rtp_get_seqnum(uref, &seqnum);

        //printf("\n check now %"PRIu64" date_sys %"PRIu64" seqnum %"PRIu64" latency %"PRIu64"  \n", now, date_sys, seqnum, upipe_rtp_fec->latency );
        if (now >= date_sys + upipe_rtp_fec->latency || date_sys == UINT64_MAX) {
            //printf("\n send now %"PRIu64" date_sys %"PRIu64" seqnum %"PRIu64" \n", now, date_sys, seqnum );

            /* Don't overflow date_sys on error corrected frames */
            if (date_sys != UINT64_MAX) {
                date_sys += upipe_rtp_fec->latency;
                uref_clock_set_date_sys(uref, date_sys, type);
            }
            ulist_delete(uchain);
            upipe_rtp_fec_output(upipe, uref, NULL);

            if (upipe_rtp_fec->last_send_seqnum != UINT64_MAX &&
                ((upipe_rtp_fec->last_send_seqnum+1) & UINT16_MAX) != seqnum) {
                //printf("\n FEC LOST expected seqnum %u got %u \n", ((upipe_rtp_fec->last_send_seqnum+1) & UINT16_MAX), seqnum);
            }

            upipe_rtp_fec->last_send_seqnum = seqnum;
        }
        else {
            break;
        }
    }
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 */
static int upipe_rtp_fec_build_flow_def(struct upipe *upipe)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_upipe(upipe);
    if (upipe_rtp_fec->uref_mgr == NULL)
        return UBASE_ERR_ALLOC;

    struct uref *flow_def =
        uref_block_flow_alloc_def(upipe_rtp_fec->uref_mgr, "block.mpegtsaligned.");
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_rtp_fec_store_flow_def(upipe, flow_def);
    //printf("\n store flow def \n");

    return UBASE_ERR_NONE;
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_rtp_fec_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_rtp_fec *upipe_rtp_fec = upipe_rtp_fec_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_rtp_fec_store_flow_def(upipe, flow_format);

    upipe_rtp_fec_check_upump_mgr(upipe);
    if (upipe_rtp_fec->uref_mgr == NULL) {
        upipe_rtp_fec_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    upipe_rtp_fec_build_flow_def(upipe);

    // FIXME this is broke

    return UBASE_ERR_NONE;
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
    struct upipe_rtp_fec_sub *upipe_rtp_fec_sub =
        upipe_rtp_fec_sub_from_upipe(upipe);
    uint8_t fec_buffer[SMPTE_2022_FEC_HEADER_SIZE];
    int fec_change = 0;

    if (upipe_rtp_fec_sub == &upipe_rtp_fec->main_subpipe) {
        int type;
        uint64_t seqnum = 0, date_sys = 0;
        uref_rtp_get_seqnum(uref, &seqnum);
        uref_clock_get_date_sys(uref, &date_sys, &type);

        if (upipe_rtp_fec->first_seqnum == UINT64_MAX) {
            upipe_rtp_fec->first_seqnum = seqnum;
            //printf("\n FIRST seqnum %"PRIu64" \n", seqnum );
        }

        //printf("\n arrived %"PRIu64" ts %"PRIu64" \n", seqnum, date_sys);

        /* Output packets immediately if no FEC packets found as per spec */
        if (!upipe_rtp_fec->cols && !upipe_rtp_fec->rows) {
            upipe_rtp_fec->last_seqnum = seqnum;
            upipe_rtp_fec_output(&upipe_rtp_fec->upipe, uref, NULL);
        }
        else {
            int two_matrix_size = 2*(upipe_rtp_fec->cols*upipe_rtp_fec->rows);

            uint64_t seq_delta;
            if (seq_num_lt(seqnum, upipe_rtp_fec->last_seqnum))
                seq_delta = (upipe_rtp_fec->last_seqnum + UINT16_MAX + 1 - seqnum) & UINT16_MAX;
            else
                seq_delta = (seqnum + UINT16_MAX + 1 - upipe_rtp_fec->last_seqnum) & UINT16_MAX;
                      
            /* Resync if packet is too old or too new */
            if (upipe_rtp_fec->last_seqnum != UINT64_MAX && seq_delta > two_matrix_size) {
                fec_change = 1;
                uref_free(uref);
            }
            else {
                upipe_rtp_fec->last_seqnum = seqnum;
                insert_ordered_uref(&upipe_rtp_fec->main_queue, uref);

                /* Owing to clock drift the latency of 2x the FEC matrix may increase
                 * Build a continually updating duration and correct the latency if necessary.
                 * Also helps with undershoot of latency calculation from initial packets */
                uint8_t idx = seqnum % two_matrix_size;
                uint64_t prev_date_sys = upipe_rtp_fec->recent[idx].date_sys;
                uint64_t prev_seqnum = upipe_rtp_fec->recent[idx].seqnum;
                uint64_t expected_seqnum = (prev_seqnum + two_matrix_size) & UINT16_MAX;
                uint16_t later_seqnum = (seqnum + two_matrix_size) & UINT16_MAX;
                uint8_t new_idx = later_seqnum % two_matrix_size;

                /* Make sure the sequence number is exactly two matrices behind and not more,
                 * otherwise the latency calculation will be too large */
                if (prev_date_sys != UINT64_MAX && prev_seqnum != UINT64_MAX && seqnum == expected_seqnum) {
                    uint64_t latency = date_sys - prev_date_sys;
                    if (upipe_rtp_fec->latency < latency) {
                        upipe_rtp_fec->latency = latency + UPIPE_FEC_JITTER;
                        upipe_warn_va(upipe, "Late packets increasing buffer-size/latency to %f seconds", (double)upipe_rtp_fec->latency / UCLOCK_FREQ);
                    }
                }
                
                upipe_rtp_fec->recent[new_idx].date_sys = date_sys;
                upipe_rtp_fec->recent[new_idx].seqnum = seqnum;
            }
            
        }

        if (upipe_rtp_fec->cols && upipe_rtp_fec->rows) {
            upipe_rtp_fec_apply_col_fec(upipe_rtp_fec);
            
            uint64_t cur_row_fec_snbase = upipe_rtp_fec->cur_row_fec_snbase == UINT64_MAX ?
                                          upipe_rtp_fec->first_seqnum :
                                          upipe_rtp_fec->cur_row_fec_snbase;

            /* Wait for two rows to arrive to allow for late row FEC packets */
            uint64_t row_delta = (seqnum + UINT16_MAX - cur_row_fec_snbase) & UINT16_MAX;
            if (row_delta > 2*upipe_rtp_fec->cols) {
                ////printf("APPLY ROW delta %u queuelen %u \n", delta, ulist_depth(&upipe_rtp_fec->main_queue) );
                upipe_rtp_fec_apply_row_fec(upipe_rtp_fec, cur_row_fec_snbase);
            }
        }

        if (upipe_rtp_fec->cols && !upipe_rtp_fec->pump_start &&
            upipe_rtp_fec->cur_matrix_snbase != UINT64_MAX) {
            struct uchain *first_uchain;
            struct uref *first_uref;

            int matrix_size = upipe_rtp_fec->cols*upipe_rtp_fec->rows;

            /* Clear any old non-FEC packets */
            clear_main_list(&upipe_rtp_fec->main_queue, upipe_rtp_fec->cur_matrix_snbase);

            first_uchain = ulist_peek(&upipe_rtp_fec->main_queue);
            if (first_uchain) {
                //printf("\n depth %i \n", ulist_depth(&upipe_rtp_fec->main_queue));
                first_uref = uref_from_uchain(first_uchain);
                uref_rtp_get_seqnum(first_uref, &upipe_rtp_fec->first_seqnum);
                
                /* Make sure we have at least two matrices of data as per the spec */
                uint64_t mat_delta = (seqnum + UINT16_MAX - upipe_rtp_fec->cur_matrix_snbase) & UINT16_MAX;
                uint64_t seq_delta = (seqnum + UINT16_MAX - upipe_rtp_fec->first_seqnum) & UINT16_MAX;
                //printf("\n mat_delta %u seq_delta %u cur_matrix_snbase %u first_seqnum %u \n", mat_delta, seq_delta,
                       //upipe_rtp_fec->cur_matrix_snbase, upipe_rtp_fec->first_seqnum);
                if (seq_delta >= 2*matrix_size && seq_delta != UINT16_MAX) {

                    uint64_t date_sys, now;
                    int type;
                    
                    /* Calculate delay from first packet of matrix arriving to pump start time */
                    uref_clock_get_date_sys(first_uref, &date_sys, &type);

                    //printf("\n date_sys %"PRIu64" seqnum %u \n", date_sys, seqnum);

                    /* First packet of matrix can be recovered packet and have no date_sys */
                    if (date_sys != UINT64_MAX) {
                        now = uclock_now(upipe_rtp_fec->uclock);
                        upipe_rtp_fec->latency = now - date_sys + UPIPE_FEC_JITTER;

                        //printf("\n pump start depth %u delta %u seqdelta %u first_date_sys %"PRIu64" latency %"PRIu64" \n", ulist_depth(&upipe_rtp_fec->main_queue), mat_delta, seq_delta, date_sys, upipe_rtp_fec->latency );
                        /* Start pump that clears the buffer */
                        struct upump *upump = upump_alloc_timer(upipe_rtp_fec->upump_mgr,
                                                                upipe_rtp_fec_timer, &upipe_rtp_fec->upipe,
                                                                upipe_rtp_fec->upipe.refcount,
                                                                0, UCLOCK_FREQ/90000);
                        upipe_rtp_fec_set_upump(&upipe_rtp_fec->upipe, upump);
                        upump_start(upump);
                        upipe_rtp_fec->pump_start = 1;
                    }
                    else {
                        /* First packet having an unusable date_sys is not useful */
                        ulist_delete(first_uchain);
                        uref_free(first_uref);
                        first_uchain = ulist_peek(&upipe_rtp_fec->main_queue);
                        if (first_uchain) {
                            first_uref = uref_from_uchain(first_uchain);
                            uref_rtp_get_seqnum(uref, &upipe_rtp_fec->first_seqnum);
                        }
                    }
                }
            }
        }

        upipe_rtp_fec->pkts_since_last_fec++;
    }
    else {
        const uint8_t *fec_header = uref_block_peek(uref, 0, SMPTE_2022_FEC_HEADER_SIZE, fec_buffer);
        uint8_t d = smpte_fec_check_d(fec_header);
        uint8_t offset = smpte_fec_get_offset(fec_header);
        uint8_t na = smpte_fec_get_na(fec_header);
        uref_block_peek_unmap(uref, 0, fec_buffer, fec_header);

        if (upipe_rtp_fec_sub == &upipe_rtp_fec->col_subpipe) {
            if (d) {
                upipe_warn(upipe, "Invalid column FEC packet found, ignoring");
                uref_free(uref);
                return;
            }

            if(!offset || !na) {
                upipe_warn(upipe, "Invalid row/column in FEC packet, ignoring");
                uref_free(uref);
                return;
            }
            
            if (upipe_rtp_fec->cols != offset && upipe_rtp_fec->cols <= 50 &&
                upipe_rtp_fec->rows <= 50) {
                upipe_rtp_fec->cols = offset;
                upipe_rtp_fec->rows = na;

                fec_change = 1;
                upipe_warn_va(upipe, "FEC detected %u rows and %u columns", upipe_rtp_fec->rows,
                              upipe_rtp_fec->cols);
            }
            insert_ordered_uref(&upipe_rtp_fec->col_queue, uref);
        }
        else if (upipe_rtp_fec_sub == &upipe_rtp_fec->row_subpipe) {
            if (!d) {
                upipe_warn(upipe, "Invalid row FEC packet found, ignoring");
                uref_free(uref);
                return;
            }
            insert_ordered_uref(&upipe_rtp_fec->row_queue, uref);
        }

        upipe_rtp_fec->pkts_since_last_fec = 0;
    }

    /* Disable FEC if no FEC packets arrive for a while */
    if (upipe_rtp_fec->pkts_since_last_fec > 200 && 
       (upipe_rtp_fec->rows || upipe_rtp_fec->cols)) {
        upipe_rtp_fec->rows = upipe_rtp_fec->cols = 0;
        fec_change = 1;
        upipe_warn(upipe, "No FEC Packets received for a while, disabling FEC");
    }

    /* Clear matrices if change of FEC */
    if (fec_change) {
        upipe_rtp_fec_clear(upipe_rtp_fec);
        fec_change = 0;

        upipe_rtp_fec->first_seqnum = UINT64_MAX;
        upipe_rtp_fec->last_seqnum = UINT64_MAX;

        free(upipe_rtp_fec->recent);
        upipe_rtp_fec->recent = NULL;
        if (upipe_rtp_fec->rows && upipe_rtp_fec->cols) {
            upipe_rtp_fec->recent_len = 2 * upipe_rtp_fec->rows * upipe_rtp_fec->cols;
            upipe_rtp_fec->recent = malloc(upipe_rtp_fec->recent_len * sizeof(*upipe_rtp_fec->recent));
            if (!upipe_rtp_fec->recent) {
                upipe_err(upipe, "Malloc failed");
                return;
            }

            memset(upipe_rtp_fec->recent, 0xff, upipe_rtp_fec->recent_len * sizeof(*upipe_rtp_fec->recent));
        }
        upipe_rtp_fec->latency = 0;

        //printf("\n FEC change \n");
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_rtp_fec_sub_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_rtp_fec *upipe_rtp_fec =
        upipe_rtp_fec_from_sub_mgr(upipe->mgr);
    struct upipe_rtp_fec_sub *upipe_rtp_fec_sub =
        upipe_rtp_fec_sub_from_upipe(upipe);

    return UBASE_ERR_NONE;
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
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtp_fec_sub_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            *p = upipe_rtp_fec_to_upipe(upipe_rtp_fec_from_sub_mgr(upipe->mgr));
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
    struct upipe_rtp_fec *upipe_rtp_fec =
        upipe_rtp_fec_from_sub_mgr(upipe->mgr);
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

    upipe_rtp_fec->first_seqnum = UINT64_MAX;
    upipe_rtp_fec->last_seqnum = UINT64_MAX;
    upipe_rtp_fec->last_send_seqnum = UINT64_MAX;
    upipe_rtp_fec->cur_matrix_snbase = UINT64_MAX;
    upipe_rtp_fec->cur_row_fec_snbase = UINT64_MAX;

    upipe_rtp_fec->pump_start = 0;

    struct upipe *upipe = upipe_rtp_fec_to_upipe(upipe_rtp_fec);
    upipe_init(upipe, mgr, uprobe);

    upipe_rtp_fec_init_upump_mgr(upipe);
    upipe_rtp_fec_init_upump(upipe);
    upipe_rtp_fec_init_uclock(upipe);
    upipe_rtp_fec_init_urefcount(upipe);
    upipe_rtp_fec_init_uref_mgr(upipe);
    upipe_rtp_fec_init_sub_mgr(upipe);
    upipe_rtp_fec_init_output(upipe);

    /* Initalise subpipes */
    upipe_rtp_fec_sub_init(upipe_rtp_fec_sub_to_upipe(upipe_rtp_fec_to_main_subpipe(upipe_rtp_fec)),
                            &upipe_rtp_fec->sub_mgr, uprobe_main);
    upipe_rtp_fec_sub_init(upipe_rtp_fec_sub_to_upipe(upipe_rtp_fec_to_col_subpipe(upipe_rtp_fec)),
                            &upipe_rtp_fec->sub_mgr, uprobe_col);
    upipe_rtp_fec_sub_init(upipe_rtp_fec_sub_to_upipe(upipe_rtp_fec_to_row_subpipe(upipe_rtp_fec)),
                            &upipe_rtp_fec->sub_mgr, uprobe_row);

    ulist_init(&upipe_rtp_fec->main_queue);
    ulist_init(&upipe_rtp_fec->col_queue);
    ulist_init(&upipe_rtp_fec->row_queue);

    upipe_rtp_fec_require_uref_mgr(upipe);
    upipe_rtp_fec_check_upump_mgr(upipe);
    upipe_rtp_fec_build_flow_def(upipe);

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
            *upipe_p =  upipe_rtp_fec_sub_to_upipe(
                            upipe_rtp_fec_to_main_subpipe(
                                upipe_rtp_fec_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_RTP_FEC_GET_COL_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_FEC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p =  upipe_rtp_fec_sub_to_upipe(
                            upipe_rtp_fec_to_col_subpipe(
                                upipe_rtp_fec_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_RTP_FEC_GET_ROW_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_FEC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p =  upipe_rtp_fec_sub_to_upipe(
                            upipe_rtp_fec_to_row_subpipe(
                                upipe_rtp_fec_from_upipe(upipe)));
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

    if (upipe_rtp_fec->pump_start)
        upump_stop(upipe_rtp_fec->upump);

    upipe_rtp_fec_clear(upipe_rtp_fec);

    upipe_rtp_fec_sub_clean(upipe_rtp_fec_sub_to_upipe(upipe_rtp_fec_to_main_subpipe(upipe_rtp_fec)));
    upipe_rtp_fec_sub_clean(upipe_rtp_fec_sub_to_upipe(upipe_rtp_fec_to_col_subpipe(upipe_rtp_fec)));
    upipe_rtp_fec_sub_clean(upipe_rtp_fec_sub_to_upipe(upipe_rtp_fec_to_row_subpipe(upipe_rtp_fec)));

    upipe_throw_dead(upipe);

    upipe_rtp_fec_clean_uclock(upipe);
    upipe_rtp_fec_clean_upump(upipe);
    upipe_rtp_fec_clean_upump_mgr(upipe);
    upipe_rtp_fec_clean_urefcount(upipe);
    upipe_rtp_fec_clean_uref_mgr(upipe);

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

