/*
 * Copyright (C) 2023 Open Broadcast Systems Ltd
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
 * @short Upipe module for SRT senders
 */

#include "upipe/config.h"
#include "upipe/ubase.h"
#include "upipe/uprobe.h"
#include "upipe/uref.h"
#include "upipe/uclock.h"
#include "upipe/uref_clock.h"
#include "upipe/upipe.h"
#include "upipe/uref_block.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_pic.h" // XXX
#include "upipe/uref_flow.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_subpipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_urefcount_real.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_uref_mgr.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_upump.h"
#include "upipe/upipe_helper_uclock.h"
#include "upipe-srt/upipe_srt_sender.h"

#include <limits.h>

#include <bitstream/haivision/srt.h>

#include <gcrypt.h>

#define EXPECTED_FLOW_DEF "block."

/** upipe_srt_sender structure */
struct upipe_srt_sender {
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

    /** list of input subpipes */
    struct uchain inputs;

    /** output pipe */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    uint32_t syn_cookie;
    uint32_t socket_id;
    uint32_t seqnum;

    uint64_t establish_time;

    /** buffer latency */
    uint64_t latency;

    uint8_t salt[16];
    uint8_t sek[2][32];
    uint8_t sek_len;

    /** public upipe structure */
    struct upipe upipe;
};

static int upipe_srt_sender_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_srt_sender, upipe, UPIPE_SRT_SENDER_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_srt_sender, urefcount, upipe_srt_sender_no_input);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_srt_sender, urefcount_real, upipe_srt_sender_free);
UPIPE_HELPER_VOID(upipe_srt_sender);
UPIPE_HELPER_OUTPUT(upipe_srt_sender, output, flow_def, output_state, request_list);
UPIPE_HELPER_UREF_MGR(upipe_srt_sender, uref_mgr, uref_mgr_request,
                      upipe_srt_sender_check,
                      upipe_srt_sender_register_output_request,
                      upipe_srt_sender_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_srt_sender, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_srt_sender_check,
                      upipe_srt_sender_register_output_request,
                      upipe_srt_sender_unregister_output_request)
UPIPE_HELPER_UPUMP_MGR(upipe_srt_sender, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_srt_sender, upump_timer, upump_mgr)
UPIPE_HELPER_UCLOCK(upipe_srt_sender, uclock, uclock_request,
                    upipe_srt_sender_check, upipe_throw_provide_request, NULL)

struct upipe_srt_sender_input {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;
    /** public upipe structure */
    struct upipe upipe;
};

static void upipe_srt_sender_lost_sub_n(struct upipe *upipe, uint32_t seq, uint32_t pkts, struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_srt_sender_input, upipe, UPIPE_SRT_SENDER_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_srt_sender_input, urefcount, upipe_srt_sender_input_free)
UPIPE_HELPER_VOID(upipe_srt_sender_input);
UPIPE_HELPER_SUBPIPE(upipe_srt_sender, upipe_srt_sender_input, output, sub_mgr, inputs,
                     uchain)

/** @internal @This handles SRT messages.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_srt_sender_input_sub(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    size_t total_size;
    ubase_assert(uref_block_size(uref, &total_size));

    const uint8_t *buf;
    int size = total_size;

    ubase_assert(uref_block_read(uref, 0, &size, &buf));
    assert(size == total_size);

    if (size < SRT_HEADER_SIZE || !srt_get_packet_control(buf)) {
        upipe_err_va(upipe, "Invalid SRT control packet (%d)", size);
        ubase_assert(uref_block_unmap(uref, 0));
        uref_free(uref);
        return;
    }

    uint16_t type = srt_get_control_packet_type(buf);

    if (type == SRT_CONTROL_TYPE_NAK) {
        buf += SRT_HEADER_SIZE;
        size -= SRT_HEADER_SIZE;
        size_t s = size;
        uint32_t seq;
        uint32_t packets;
        while (srt_get_nak_range(&buf, &s, &seq, &packets)) {
            upipe_srt_sender_lost_sub_n(upipe, seq, packets, upump_p);
        }
        uref_block_unmap(uref, 0);
        uref_free(uref);
    } else {
        struct upipe *upipe_super = NULL;
        upipe_srt_sender_input_get_super(upipe, &upipe_super);
        uref_block_unmap(uref, 0);
        upipe_srt_sender_output(upipe_super, uref, NULL);
    }
}

/** @internal @This retransmits a number of packets */
static void upipe_srt_sender_lost_sub_n(struct upipe *upipe, uint32_t seq, uint32_t pkts, struct upump **upump_p)
{
    struct upipe *upipe_super = NULL;
    upipe_srt_sender_input_get_super(upipe, &upipe_super);
    struct upipe_srt_sender *upipe_srt_sender = upipe_srt_sender_from_upipe(upipe_super);

    struct uchain *uchain;
    ulist_foreach(&upipe_srt_sender->queue, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        uint64_t uref_seqnum = 0;
        uref_attr_get_priv(uref, &uref_seqnum);

        uint32_t diff = uref_seqnum - seq;
        if (diff >= pkts) {
            /* packet not in range */
            if (diff < 0x80000000) {
                /* packet after range */
                break;
            }
            continue;
        }

        upipe_verbose_va(upipe, "Retransmit %" PRIu64, uref_seqnum);

        uint8_t *buf;
        int s = 0;
        if (ubase_check(uref_block_write(uref, 0, &s, &buf))) {
            srt_set_data_packet_retransmit(buf, true);
            uref_block_unmap(uref, 0);
        }

        upipe_srt_sender_output(upipe_super, uref_dup(uref), NULL);
        if (--pkts == 0)
            return;
        seq++;
    }

    /* XXX: Is it needed? */

    int s = SRT_HEADER_SIZE + SRT_DROPREQ_CIF_SIZE;
    struct uref *uref = uref_block_alloc(upipe_srt_sender->uref_mgr,
            upipe_srt_sender->ubuf_mgr, s);
    if (!uref) {
        upipe_throw_fatal(upipe, UBASE_ERR_UNKNOWN);
        return;
    }

    uint8_t *buf;
    s = -1;
    if (unlikely(!ubase_check(uref_block_write(uref, 0, &s, &buf)))) {
        upipe_throw_fatal(upipe, UBASE_ERR_UNKNOWN);
        uref_free(uref);
        return;
    }

    uint64_t now = uclock_now(upipe_srt_sender->uclock);

    memset(buf, 0, s);
    srt_set_packet_control(buf, true);
    srt_set_control_packet_type(buf, SRT_CONTROL_TYPE_DROPREQ);
    srt_set_control_packet_subtype(buf, 0); // message number

    srt_set_packet_timestamp(buf, (now - upipe_srt_sender->establish_time) / 27);
    srt_set_packet_dst_socket_id(buf, upipe_srt_sender->socket_id);

    uint8_t *cif = (uint8_t*)srt_get_control_packet_cif(buf);
    srt_set_dropreq_first_seq(cif, seq);
    srt_set_dropreq_last_seq(cif, seq + pkts - 1);

    uref_block_unmap(uref, 0);
    upipe_srt_sender_output(&upipe_srt_sender->upipe, uref, upump_p);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_srt_sender_no_input(struct upipe *upipe)
{
    upipe_srt_sender_throw_sub_outputs(upipe, UPROBE_SOURCE_END);
    upipe_srt_sender_release_urefcount_real(upipe);
}

/** @internal @This allocates an output subpipe of a dup pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_srt_sender_input_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct upipe *upipe =
        upipe_srt_sender_input_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_srt_sender_input_init_urefcount(upipe);
    upipe_srt_sender_input_init_sub(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_srt_sender_input_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_srt_sender_input_clean_sub(upipe);
    upipe_srt_sender_input_clean_urefcount(upipe);
    upipe_srt_sender_input_free_void(upipe);
}

/** @internal this timer removes from the queue packets that are too
 * early to be recovered by receiver.
 */
static void upipe_srt_sender_timer(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_srt_sender *upipe_srt_sender = upipe_srt_sender_from_upipe(upipe);

    uint64_t now = uclock_now(upipe_srt_sender->uclock);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_srt_sender->queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);

        uint64_t seqnum = 0;
        uref_attr_get_priv(uref, &seqnum);

        uint64_t cr_sys = 0;
        if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &cr_sys))))
            upipe_warn(upipe, "Couldn't read cr_sys");

        if (now - cr_sys < upipe_srt_sender->latency)
            return;

        upipe_verbose_va(upipe, "Delete seq %" PRIu64 " after %"PRIu64" clocks",
                seqnum, now - cr_sys);

        ulist_delete(uchain);
        uref_free(uref);
    }
}

static int upipe_srt_sender_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_srt_sender *upipe_srt_sender = upipe_srt_sender_from_upipe(upipe);

    if (flow_format != NULL)
        upipe_srt_sender_store_flow_def(upipe, flow_format);

    if (upipe_srt_sender->flow_def == NULL)
        return UBASE_ERR_NONE;

    if (upipe_srt_sender->uref_mgr == NULL) {
        upipe_srt_sender_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_srt_sender->uclock == NULL) {
        upipe_srt_sender_require_uclock(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_srt_sender->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_srt_sender->uref_mgr, NULL);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_srt_sender_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    upipe_srt_sender_check_upump_mgr(upipe);
    if (upipe_srt_sender->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_srt_sender->upump_timer == NULL) {
        upipe_srt_sender->establish_time = uclock_now(upipe_srt_sender->uclock); // FIXME
        struct upump *upump =
            upump_alloc_timer(upipe_srt_sender->upump_mgr,
                              upipe_srt_sender_timer, upipe, upipe->refcount,
                              UCLOCK_FREQ, UCLOCK_FREQ);
        upipe_srt_sender_set_upump_timer(upipe, upump);
        upump_start(upump);

    }

    return UBASE_ERR_NONE;
}

/** @internal */
static int upipe_srt_sender_input_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe *upipe_super = NULL;
    upipe_srt_sender_input_get_super(upipe, &upipe_super);
    struct upipe_srt_sender *upipe_srt_sender = upipe_srt_sender_from_upipe(upipe_super);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    uint64_t id;
    if (ubase_check(uref_flow_get_id(flow_def, &id)))
        upipe_srt_sender->socket_id = id;

    uint64_t isn;
    if (ubase_check(uref_pic_get_number(flow_def, &isn)))
            upipe_srt_sender->seqnum = isn;

    struct udict_opaque opaque;
    if (ubase_check(uref_attr_get_opaque(flow_def, &opaque, UDICT_TYPE_OPAQUE, "enc.salt"))) {
        if (opaque.size > 16)
            opaque.size = 16;
        memcpy(upipe_srt_sender->salt, opaque.v, opaque.size);
    }

#ifdef UPIPE_HAVE_GCRYPT_H
    if (ubase_check(uref_attr_get_opaque(flow_def, &opaque, UDICT_TYPE_OPAQUE, "enc.even_key"))) {
        if (opaque.size > sizeof(upipe_srt_sender->sek[0]))
            opaque.size = sizeof(upipe_srt_sender->sek[0]);
        upipe_srt_sender->sek_len = opaque.size;
        memcpy(upipe_srt_sender->sek[0], opaque.v, opaque.size);
    }
#endif

    return uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF);
}

/** @internal @This processes control commands on an output subpipe of a dup
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_srt_sender_input_control(struct upipe *upipe,
                                    int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_srt_sender_input_control_super(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_srt_sender_input_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This initializes the output manager for a srt_sender set pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_srt_sender_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_srt_sender *upipe_srt_sender = upipe_srt_sender_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_srt_sender->sub_mgr;
    sub_mgr->refcount = upipe_srt_sender_to_urefcount_real(upipe_srt_sender);
    sub_mgr->signature = UPIPE_SRT_SENDER_INPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_srt_sender_input_alloc;
    sub_mgr->upipe_input = upipe_srt_sender_input_sub;
    sub_mgr->upipe_control = upipe_srt_sender_input_control;
}

/** @internal @This allocates a srt_sender pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_srt_sender_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_srt_sender_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

#ifdef UPIPE_HAVE_GCRYPT_H
    if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) {
        uprobe_err(uprobe, upipe, "Application did not initialize libgcrypt, see "
        "https://www.gnupg.org/documentation/manuals/gcrypt/Initializing-the-library.html");
        upipe_srt_sender_free_void(upipe);
        return NULL;
    }
#endif

    struct upipe_srt_sender *upipe_srt_sender = upipe_srt_sender_from_upipe(upipe);
    upipe_srt_sender_init_urefcount(upipe);
    upipe_srt_sender_init_urefcount_real(upipe);
    upipe_srt_sender_init_upump_mgr(upipe);
    upipe_srt_sender_init_upump_timer(upipe);
    upipe_srt_sender_init_uclock(upipe);
    upipe_srt_sender_init_output(upipe);
    upipe_srt_sender_init_sub_outputs(upipe);
    upipe_srt_sender_init_sub_mgr(upipe);
    upipe_srt_sender_init_ubuf_mgr(upipe);
    upipe_srt_sender_init_uref_mgr(upipe);
    ulist_init(&upipe_srt_sender->queue);
    upipe_srt_sender->latency = UCLOCK_FREQ; /* 1 sec */
    upipe_srt_sender->socket_id = 0;
    upipe_srt_sender->seqnum = 0;
    upipe_srt_sender->syn_cookie = 1;

    upipe_srt_sender->sek_len = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static inline void upipe_srt_sender_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_srt_sender *upipe_srt_sender = upipe_srt_sender_from_upipe(upipe);

    upipe_srt_sender_check(upipe, NULL);

    if (!upipe_srt_sender->ubuf_mgr) {
        uref_free(uref);
        return;
    }

    if (upipe_srt_sender->socket_id == 0) {
        uref_free(uref);
        return;
    }

    struct ubuf *insert = ubuf_block_alloc(upipe_srt_sender->ubuf_mgr, SRT_HEADER_SIZE);
    if (!insert) {
        upipe_throw_fatal(upipe, UBASE_ERR_UNKNOWN);
        uref_free(uref);
        return;
    }

    uint8_t *buf;
    int s = -1;
    if (unlikely(!ubase_check(ubuf_block_write(insert, 0, &s, &buf)))) {
        upipe_throw_fatal(upipe, UBASE_ERR_UNKNOWN);
        ubuf_free(insert);
        uref_free(uref);
        return;
    }

    uint64_t now = uclock_now(upipe_srt_sender->uclock);

    uint32_t seqnum = upipe_srt_sender->seqnum++;
    memset(buf, 0, SRT_HEADER_SIZE);
    srt_set_packet_control(buf, false);
    srt_set_packet_timestamp(buf, (now - upipe_srt_sender->establish_time) / 27);
    srt_set_packet_dst_socket_id(buf, upipe_srt_sender->socket_id);
    srt_set_data_packet_message_number(buf, seqnum);
    srt_set_data_packet_seq(buf, seqnum);
    srt_set_data_packet_position(buf, SRT_DATA_POSITION_ONLY);
    srt_set_data_packet_order(buf, false);
    srt_set_data_packet_retransmit(buf, false);

#ifdef UPIPE_HAVE_GCRYPT_H
    if (upipe_srt_sender->sek_len) {
        //
        uint8_t *data;
        int s = -1;
        if (ubase_check(uref_block_write(uref, 0, &s, &data))) {
            const uint8_t *salt = upipe_srt_sender->salt;
            const uint8_t *sek = upipe_srt_sender->sek[0];
            int key_len = upipe_srt_sender->sek_len;

            uint8_t iv[16];
            memset(&iv, 0, 16);
            iv[10] = (seqnum >> 24) & 0xff;
            iv[11] = (seqnum >> 16) & 0xff;
            iv[12] = (seqnum >>  8) & 0xff;
            iv[13] =  seqnum & 0xff;
            for (int i = 0; i < 112/8; i++)
                iv[i] ^= salt[i];

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
                upipe_err_va(upipe, "Couldn't set encryption ctr (0x%x)", err);
                goto error_close;
            }

            err = gcry_cipher_encrypt(aes, data, s, NULL, 0);
            if (err) {
                upipe_err_va(upipe, "Couldn't encrypt packet (0x%x)", err);
                goto error_close;
            }

error_close:
            gcry_cipher_close(aes);
error:
            uref_block_unmap(uref, 0);

            if (err) {
                upipe_err(upipe, "Dropping packet");
                ubuf_block_unmap(insert, 0);
                ubuf_free(insert);
                uref_free(uref);
                return;
            }
        }

        //
        srt_set_data_packet_encryption(buf, SRT_DATA_ENCRYPTION_EVEN);
    } else
#endif
        srt_set_data_packet_encryption(buf, SRT_DATA_ENCRYPTION_CLEAR);

    ubuf_block_unmap(insert, 0);
    if (!ubase_check(uref_block_insert(uref, 0, insert))) {
        upipe_throw_fatal(upipe, UBASE_ERR_UNKNOWN);
        ubuf_free(insert);
        uref_free(uref);
        return;
    }

    uref_attr_set_priv(uref, seqnum);

    /* Output packet immediately */
    upipe_srt_sender_output(upipe, uref_dup(uref), upump_p);

    upipe_verbose_va(upipe, "Output & buffer %u", seqnum);

    /* Buffer packet in case retransmission is needed */
    ulist_add(&upipe_srt_sender->queue, uref_to_uchain(uref));
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_srt_sender_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;
    upipe_srt_sender_store_flow_def(upipe, flow_def_dup);

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a srt_sender pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_srt_sender_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_srt_sender_control_output(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_srt_sender_control_outputs(upipe, command, args));
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_srt_sender_set_upump_timer(upipe, NULL);
            return upipe_srt_sender_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_srt_sender_set_upump_timer(upipe, NULL);
            upipe_srt_sender_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_srt_sender_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SET_OPTION: {
            const char *k = va_arg(args, const char *);
            const char *v = va_arg(args, const char *);
            if (strcmp(k, "latency"))
                return UBASE_ERR_INVALID;

            struct upipe_srt_sender *upipe_srt_sender = upipe_srt_sender_from_upipe(upipe);
            upipe_srt_sender->latency = atoi(v) * UCLOCK_FREQ / 1000;
            upipe_dbg_va(upipe, "Set latency to %s msecs", v);
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static int upipe_srt_sender_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_srt_sender_control(upipe, command, args))
    return upipe_srt_sender_check(upipe, NULL);
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe the public structure of the pipe
 */
static void upipe_srt_sender_free(struct upipe *upipe)
{
    struct upipe_srt_sender *upipe_srt_sender = upipe_srt_sender_from_upipe(upipe);

    upipe_throw_dead(upipe);

    upipe_srt_sender_clean_output(upipe);
    upipe_srt_sender_clean_sub_outputs(upipe);
    upipe_srt_sender_clean_urefcount_real(upipe);
    upipe_srt_sender_clean_urefcount(upipe);
    upipe_srt_sender_clean_ubuf_mgr(upipe);
    upipe_srt_sender_clean_uref_mgr(upipe);
    upipe_srt_sender_clean_upump_timer(upipe);
    upipe_srt_sender_clean_upump_mgr(upipe);
    upipe_srt_sender_clean_uclock(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_srt_sender->queue, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }

    upipe_srt_sender_free_void(upipe);
}

static struct upipe_mgr upipe_srt_sender_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SRT_SENDER_SIGNATURE,

    .upipe_alloc = upipe_srt_sender_alloc,
    .upipe_input = upipe_srt_sender_input,
    .upipe_control = upipe_srt_sender_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for srt_sender pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_srt_sender_mgr_alloc(void)
{
    return &upipe_srt_sender_mgr;
}
