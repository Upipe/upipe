/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 * Copyright (C) 2021 EasyTools
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module generating SCTE-35 Splice Information Table
 * Normative references:
 *  - ISO/IEC 13818-1:2007(E) (MPEG-2 Systems)
 *  - SCTE 35 2013 (Digital Program Insertion Cueing Message for Cable)
 */

#include "upipe/ubase.h"
#include "upipe/ulist.h"
#include "upipe/uclock.h"
#include "upipe/uref.h"
#include "upipe/uref_block.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_clock.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_block.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_urefcount_real.h"
#include "upipe/upipe_helper_uprobe.h"
#include "upipe/upipe_helper_inner.h"
#include "upipe/upipe_helper_bin_input.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_uref_mgr.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_output.h"
#include "upipe-ts/upipe_ts_scte35_merge.h"
#include "upipe-ts/upipe_ts_scte35_generator.h"
#include "upipe-ts/upipe_ts_mux.h"
#include "upipe-ts/uref_ts_scte35.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

#include <bitstream/scte/35.h>

/** T-STD TB octet rate for PSI tables */
#define TB_RATE_PSI 125000
/** 2^33 (max resolution of PCR, PTS and DTS) */
#define POW2_33 UINT64_C(8589934592)
/** ratio between Upipe freq and MPEG freq */
#define CLOCK_SCALE (UCLOCK_FREQ / 90000)

/** @internal @This store a SCTE-35 message in a list. */
struct scte35_message {
    /** link in the list */
    struct uchain uchain;
    /** uref description of the message */
    struct uref *uref;
    /** type name of the message */
    const char *name;
    /** SCTE-35 message */
    struct ubuf *ubuf;
    /** immediate version of the SCTE-35 message */
    struct ubuf *immediate;
    /** SCTE-35 cr_sys */
    uint64_t cr_sys;
};

UBASE_FROM_TO(scte35_message, uchain, uchain, uchain);

/** @internal @This is the private context of a ts scte35g pipe. */
struct upipe_ts_scte35g {
    /** external refcount management structure */
    struct urefcount urefcount;
    /** internal refcount management structure */
    struct urefcount urefcount_real;

    /** internal clock */
    struct uclock uclock;
    /** internal clock value */
    uint64_t cr_sys;
    /** inner probe */
    struct uprobe proxy_probe;
    /** inner pipe */
    struct upipe *scte35m;
    /** request list */
    struct uchain input_request_list;

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
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** SCTE-35 interval */
    uint64_t scte35_interval;
    /** SCTE-35 last cr_sys */
    uint64_t scte35_cr_sys;
    /** SCTE-35 null command section */
    struct ubuf *scte35_null_section;
    /** list of SCTE-35 messages */
    struct uchain messages;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_ts_scte35g_catch_scte35m(struct uprobe *uprobe,
                                          struct upipe *inner,
                                          int event, va_list args);

UBASE_FROM_TO(upipe_ts_scte35g, uclock, uclock, uclock)
UPIPE_HELPER_UPIPE(upipe_ts_scte35g, upipe, UPIPE_TS_SCTE35G_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_scte35g, urefcount, upipe_ts_scte35g_no_ref)
UPIPE_HELPER_UREFCOUNT_REAL(upipe_ts_scte35g, urefcount_real,
                            upipe_ts_scte35g_free)
UPIPE_HELPER_UPROBE(upipe_ts_scte35g, urefcount_real, proxy_probe,
                    upipe_ts_scte35g_catch_scte35m)
UPIPE_HELPER_INNER(upipe_ts_scte35g, scte35m)
UPIPE_HELPER_BIN_INPUT(upipe_ts_scte35g, scte35m, input_request_list)
UPIPE_HELPER_VOID(upipe_ts_scte35g)
UPIPE_HELPER_OUTPUT(upipe_ts_scte35g, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_UREF_MGR(upipe_ts_scte35g, uref_mgr, uref_mgr_request, NULL,
                      upipe_ts_scte35g_register_output_request,
                      upipe_ts_scte35g_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_ts_scte35g, ubuf_mgr, flow_format, ubuf_mgr_request,
                      NULL, upipe_ts_scte35g_register_output_request,
                      upipe_ts_scte35g_unregister_output_request)

/** @internal @This allocates a SCTE-35 message.
 *
 * @param cr_sys SCTE-35 cr_sys of the message
 * @return an allocated SCTE-35 message or NULL in case of error
 */
static struct scte35_message *
scte35_message_new(struct uchain *list, struct uref *uref)
{
    if (!uref)
        return NULL;

    const char *name;
    uint8_t type;
    if (!ubase_check(uref_ts_scte35_get_command_type(uref, &type)))
        return NULL;
    switch (type) {
        case SCTE35_INSERT_COMMAND:
            name = "splice insert";
            break;
        case SCTE35_TIME_SIGNAL_COMMAND:
            name = "time signal";
            break;
        default:
            return NULL;
    }

    struct scte35_message *msg = malloc(sizeof (*msg));
    if (!msg)
        return NULL;

    uint64_t cr_sys = 0;
    uref_clock_get_pts_sys(uref, &cr_sys);
    uchain_init(&msg->uchain);
    msg->uref = uref;
    msg->name = name;
    msg->cr_sys = cr_sys;
    msg->ubuf = NULL;
    msg->immediate = NULL;
    ulist_add(list, &msg->uchain);
    return msg;
}

/** @internal @This cleans a SCTE-35 message.
 *
 * @param msg SCTE-35 message to clean
 */
static void scte35_message_clean(struct scte35_message *msg)
{
    ubuf_free(msg->ubuf);
    msg->ubuf = NULL;
    ubuf_free(msg->immediate);
    msg->immediate = NULL;
}

/** @internal @This frees a SCTE-35 message.
 *
 * @param msg SCTE-35 message to free
 */
static void scte35_message_del(struct scte35_message *msg)
{
    if (ulist_is_in(&msg->uchain))
        ulist_delete(&msg->uchain);
    scte35_message_clean(msg);
    free(msg);
}

/** @internal @This finds a previous SCTE-35 message.
 *
 * @param list list of previous SCTE-35 message
 * @param uref description of the SCTE-35 message
 * @return the previous message if any
 */
static struct scte35_message *scte35_message_find(struct uchain *list,
                                                  struct uref *uref)
{
    struct uchain *uchain;
    ulist_foreach(list, uchain) {
        struct scte35_message *msg = scte35_message_from_uchain(uchain);
        if (msg->uref == uref)
            return msg;
    }
    return NULL;
}

/** @internal @This replaces the SCTE-35 message buffer.
 *
 * @param msg SCTE-35 to modify
 * @param ubuf the new message buffer to use
 */
static void scte35_message_set_ubuf(struct scte35_message *msg,
                                    struct ubuf *ubuf)
{
    if (msg) {
        ubuf_free(msg->ubuf);
        msg->ubuf = ubuf;
    }
    else
        ubuf_free(ubuf);
}

/** @internal @This replaces the SCTE-35 immediate message buffer.
 *
 * @param msg SCTE-35 to modify
 * @param ubuf the new immediate message buffer to use
 */
static void scte35_message_set_immediate(struct scte35_message *msg,
                                         struct ubuf *ubuf)
{
    if (msg) {
        ubuf_free(msg->immediate);
        msg->immediate = ubuf;
    }
    else
        ubuf_free(ubuf);
}

/** @internal @This build a splice insert message.
 *
 * @param msg SCTE-35 message to build
 * @param ubuf_mgr block allocator
 * @return an error code
 */
static int scte35_message_build_insert(struct scte35_message *msg,
                                       struct ubuf_mgr *ubuf_mgr)
{
    struct uref *uref = msg->uref;
    uint64_t pts_prog = UINT64_MAX;
    uref_clock_get_pts_prog(uref, &pts_prog);
    uint64_t duration = UINT64_MAX;
    uref_clock_get_duration(uref, &duration);
    uint64_t event_id = 0;
    uref_ts_scte35_get_event_id(uref, &event_id);
    bool cancel = ubase_check(uref_ts_scte35_get_cancel(uref));
    bool out_of_network = ubase_check(uref_ts_scte35_get_out_of_network(uref));
    bool auto_return = ubase_check(uref_ts_scte35_get_auto_return(uref));
    uint64_t program_id = 0;
    uref_ts_scte35_get_unique_program_id(uref, &program_id);

    for ( ; ; ) {
        struct ubuf *ubuf =
            ubuf_block_alloc(ubuf_mgr, PSI_MAX_SIZE + PSI_HEADER_SIZE);
        if (unlikely(ubuf == NULL))
            return UBASE_ERR_ALLOC;

        uint8_t *scte35;
        int size = -1;
        if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &scte35))) {
            ubuf_free(ubuf);
            return UBASE_ERR_ALLOC;
        }

        scte35_init(scte35);
        /* set length later */
        psi_set_length(scte35, PSI_MAX_SIZE);
        scte35_set_pts_adjustment(scte35, 0);

        uint16_t insert_size = 0;
        if (!cancel) {
            insert_size += SCTE35_INSERT_HEADER2_SIZE + SCTE35_INSERT_FOOTER_SIZE;
            if (pts_prog != UINT64_MAX)
                insert_size += SCTE35_SPLICE_TIME_HEADER_SIZE +
                               SCTE35_SPLICE_TIME_TIME_SIZE;
            if (duration != UINT64_MAX)
                insert_size += SCTE35_BREAK_DURATION_HEADER_SIZE;
        }
        scte35_insert_init(scte35, insert_size);
        scte35_insert_set_cancel(scte35, cancel);
        scte35_insert_set_event_id(scte35, event_id);
        if (!cancel) {
            scte35_insert_set_out_of_network(scte35, out_of_network);
            scte35_insert_set_program_splice(scte35, true);
            scte35_insert_set_duration(scte35, duration != UINT64_MAX);
            scte35_insert_set_splice_immediate(scte35, pts_prog == UINT64_MAX);

            if (pts_prog != UINT64_MAX) {
                uint8_t *splice_time = scte35_insert_get_splice_time(scte35);
                scte35_splice_time_init(splice_time);
                scte35_splice_time_set_time_specified(splice_time, true);
                scte35_splice_time_set_pts_time(splice_time,
                        (pts_prog / CLOCK_SCALE) % POW2_33);
            }

            if (duration != UINT64_MAX) {
                uint8_t *break_duration = scte35_insert_get_break_duration(scte35);
                scte35_break_duration_init(break_duration);
                scte35_break_duration_set_auto_return(break_duration, auto_return);
                scte35_break_duration_set_duration(break_duration,
                        (duration / CLOCK_SCALE) % POW2_33);
            }

            scte35_insert_set_unique_program_id(scte35, program_id);
            scte35_insert_set_avail_num(scte35, 0);
            scte35_insert_set_avails_expected(scte35, 0);
        }
        scte35_set_desclength(scte35, 0);
        psi_set_length(scte35,
                scte35_get_descl(scte35) + PSI_CRC_SIZE - scte35 - PSI_HEADER_SIZE);
        psi_set_crc(scte35);

        uint16_t scte35_size = psi_get_length(scte35) + PSI_HEADER_SIZE;
        ubuf_block_unmap(ubuf, 0);
        ubuf_block_resize(ubuf, 0, scte35_size);

        if (pts_prog == UINT64_MAX) {
            scte35_message_set_immediate(msg, ubuf);
            break;
        }

        scte35_message_set_ubuf(msg, ubuf);
        pts_prog = UINT64_MAX;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This creates a new PSI section for time signal.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static int scte35_message_build_time_signal(struct scte35_message *msg,
                                            struct ubuf_mgr *ubuf_mgr)
{
    struct uref *uref = msg->uref;
    uint64_t pts_prog = UINT64_MAX;
    uref_clock_get_pts_prog(uref, &pts_prog);
    uint64_t duration = UINT64_MAX;
    uref_clock_get_duration(uref, &duration);
    uint64_t pts_orig = UINT64_MAX;
    if (pts_prog != UINT64_MAX)
        pts_orig = (pts_prog / CLOCK_SCALE) % POW2_33;

    for ( ; ; ) {
        struct ubuf *ubuf =
            ubuf_block_alloc(ubuf_mgr, PSI_MAX_SIZE + PSI_HEADER_SIZE);
        if (unlikely(ubuf == NULL))
            return UBASE_ERR_ALLOC;

        uint8_t *scte35;
        int size = -1;
        if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &scte35))) {
            ubuf_free(ubuf);
            return UBASE_ERR_ALLOC;
        }

        scte35_init(scte35);
        /* set length later */
        psi_set_length(scte35, PSI_MAX_SIZE);
        scte35_set_pts_adjustment(scte35, 0);

        uint16_t i_size = 0;
        if (pts_orig != UINT64_MAX)
            i_size = SCTE35_SPLICE_TIME_TIME_SIZE;

        scte35_time_signal_init(scte35, i_size);
        uint8_t *t = scte35_time_signal_get_splice_time(scte35);
        scte35_splice_time_init(t);
        scte35_splice_time_set_time_specified(t, pts_orig != UINT64_MAX);
        if (pts_orig != UINT64_MAX)
            scte35_splice_time_set_pts_time(t, pts_orig);

        uint16_t descl_length = 0;
        uint8_t *descl = scte35_get_descl(scte35);

        uint64_t nb = 0;
        uref_ts_flow_get_descriptors(uref, &nb);
        for (uint64_t i = 0, j = 0; i < nb; i++) {
            const uint8_t *d = NULL;
            size_t l = 0;
            uref_ts_flow_get_descriptor(uref, &d, &l, i);
            if (!d || !l)
                continue;

            uint8_t *desc = descl_get_desc(
                descl, descl_length + DESC_HEADER_SIZE, j++);
            memcpy(desc, d, l);
            descl_length +=
                DESC_HEADER_SIZE + scte35_splice_desc_get_length(desc);
        }

        scte35_set_desclength(scte35, descl_length);

        psi_set_length(scte35,
                scte35_get_descl(scte35) + PSI_CRC_SIZE -
                scte35 - PSI_HEADER_SIZE +
                descl_length);
        psi_set_crc(scte35);

        uint16_t scte35_size = psi_get_length(scte35) + PSI_HEADER_SIZE;
        ubuf_block_unmap(ubuf, 0);
        ubuf_block_resize(ubuf, 0, scte35_size);

        if (pts_prog == UINT64_MAX) {
            scte35_message_set_immediate(msg, ubuf);
            break;
        }

        scte35_message_set_ubuf(msg, ubuf);
        pts_prog = UINT64_MAX;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This builds a SCTE-35 message.
 *
 * @param msg SCTE-35 message to build
 * @return an error code
 */
static int scte35_message_build(struct scte35_message *msg,
                                struct ubuf_mgr *ubuf_mgr)
{
    uint8_t type;
    if (!msg || !msg->uref || !ubuf_mgr ||
        !ubase_check(uref_ts_scte35_get_command_type(msg->uref, &type)))
        return UBASE_ERR_INVALID;

    int ret = UBASE_ERR_UNHANDLED;
    switch (type) {
        case SCTE35_INSERT_COMMAND:
            ret = scte35_message_build_insert(msg, ubuf_mgr);
            break;
        case SCTE35_TIME_SIGNAL_COMMAND:
            ret = scte35_message_build_time_signal(msg, ubuf_mgr);
            break;
    }
    return ret;
}

/** @internal @This returns the current time of the scte35 generator.
 *
 * @param uclock private clock structure
 * @return the current scte35 generator time or UINT64_MAX
 */
static uint64_t upipe_ts_scte35g_now(struct uclock *uclock)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_uclock(uclock);
    return scte35g->cr_sys;
}

/** @internal @This allocates a ts_scte35g pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_scte35g_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_scte35g_alloc_void(mgr, uprobe, signature,
                                                      args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_scte35g *upipe_ts_scte35g =
        upipe_ts_scte35g_from_upipe(upipe);
    upipe_ts_scte35g_init_urefcount(upipe);
    upipe_ts_scte35g_init_urefcount_real(upipe);
    upipe_ts_scte35g_init_proxy_probe(upipe);
    upipe_ts_scte35g_init_bin_input(upipe);
    upipe_ts_scte35g_init_uref_mgr(upipe);
    upipe_ts_scte35g_init_ubuf_mgr(upipe);
    upipe_ts_scte35g_init_output(upipe);

    upipe_ts_scte35g->cr_sys = UINT64_MAX;
    upipe_ts_scte35g->uclock.refcount = &upipe_ts_scte35g->urefcount_real;
    upipe_ts_scte35g->uclock.uclock_now = upipe_ts_scte35g_now;
    upipe_ts_scte35g->uclock.uclock_to_real = NULL;
    upipe_ts_scte35g->uclock.uclock_from_real = NULL;
    upipe_ts_scte35g->scte35_interval = 0;
    upipe_ts_scte35g->scte35_cr_sys = 0;
    upipe_ts_scte35g->scte35_null_section = NULL;
    ulist_init(&upipe_ts_scte35g->messages);

    upipe_throw_ready(upipe);

    struct upipe_mgr *upipe_ts_scte35m_mgr = upipe_ts_scte35m_mgr_alloc();
    struct upipe *scte35m = upipe_void_alloc(
        upipe_ts_scte35m_mgr,
        uprobe_pfx_alloc(
            uprobe_use(&upipe_ts_scte35g->proxy_probe),
            UPROBE_LOG_VERBOSE, "scte35m"));
    upipe_mgr_release(upipe_ts_scte35m_mgr);
    upipe_ts_scte35g_store_bin_input(upipe, scte35m);
    if (unlikely(!scte35m)) {
        upipe_warn(upipe, "fail to allocate scte35m inner pipe");
        upipe_release(upipe);
        return NULL;
    }

    return upipe;
}

/** @internal @This catches event from the scte35m pipe.
 *
 * @param uprobe structure used to raise events
 * @param inner inner pipe
 * @param event event triggered by the inner pipe
 * @param args optional arguments
 * @return an error code
 */
static int upipe_ts_scte35g_catch_scte35m(struct uprobe *uprobe,
                                          struct upipe *inner,
                                          int event, va_list args)
{
    struct upipe_ts_scte35g *scte35g =
        upipe_ts_scte35g_from_proxy_probe(uprobe);
    struct upipe *upipe = upipe_ts_scte35g_to_upipe(scte35g);

    switch (event) {
        case UPROBE_NEED_UPUMP_MGR: {
            struct upump_mgr **upump_mgr = va_arg(args, struct upump_mgr **);
            *upump_mgr = NULL;
            return UBASE_ERR_NONE;
        }
        case UPROBE_PROVIDE_REQUEST: {
            va_list args_copy;
            va_copy(args_copy, args);
            struct urequest *urequest = va_arg(args_copy, struct urequest *);
            va_end(args_copy);
            if (urequest->type == UREQUEST_UCLOCK)
                return urequest_provide_uclock(
                    urequest, uclock_use(&scte35g->uclock));
            break;
        }
    }

    if (ubase_get_signature(args) == UPIPE_TS_SCTE35M_SIGNATURE) {
        UBASE_SIGNATURE_CHECK(args, UPIPE_TS_SCTE35M_SIGNATURE);
        struct uref *prev = va_arg(args, struct uref *);
        struct uref *uref = va_arg(args, struct uref *);
        struct scte35_message *msg = NULL;
        switch (event) {
            case UPROBE_TS_SCTE35M_CHANGED:
                if (prev) {
                    msg = scte35_message_find(&scte35g->messages, prev);
                    if (msg) {
                        scte35_message_clean(msg);
                        msg->uref = uref;
                    }
                }
                else
                    msg = scte35_message_new(&scte35g->messages, uref);

                if (msg) {
                    scte35_message_build(msg, scte35g->ubuf_mgr);
                    /* Force sending the table immediately */
                    scte35g->scte35_cr_sys = 0;
                }
                break;

            case UPROBE_TS_SCTE35M_EXPIRED: {
                msg = scte35_message_find(&scte35g->messages, uref);
                if (msg)
                    msg->uref = NULL;
                break;
            }
        }
        return UBASE_ERR_NONE;
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This builds a null command SCTE-35 section.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35g_build_null(struct upipe *upipe)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    if (unlikely(scte35g->flow_def == NULL))
        return;

    struct ubuf *ubuf = ubuf_block_alloc(scte35g->ubuf_mgr,
                                         PSI_MAX_SIZE + PSI_HEADER_SIZE);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *scte35;
    int size = -1;
    if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &scte35))) {
        ubuf_free(ubuf);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    scte35_init(scte35);
    /* set length later */
    psi_set_length(scte35, PSI_MAX_SIZE);
    scte35_set_pts_adjustment(scte35, 0);
    scte35_null_init(scte35);
    scte35_set_desclength(scte35, 0);
    psi_set_length(scte35,
            scte35_get_descl(scte35) + PSI_CRC_SIZE - scte35 - PSI_HEADER_SIZE);
    psi_set_crc(scte35);

    uint16_t scte35_size = psi_get_length(scte35) + PSI_HEADER_SIZE;
    ubuf_block_unmap(ubuf, 0);
    ubuf_block_resize(ubuf, 0, scte35_size);

    ubuf_free(scte35g->scte35_null_section);
    scte35g->scte35_null_section = ubuf;
}

/** @internal @This builds a new output flow definition.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35g_build_flow_def(struct upipe *upipe)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    if (!scte35g->scte35_interval)
        return;
    struct uref *flow_def = uref_alloc_control(scte35g->uref_mgr);
    if (unlikely(flow_def == NULL ||
        !ubase_check(uref_flow_set_def(flow_def,
                                       "block.mpegtspsi.mpegtsscte35.")) ||
        !ubase_check(uref_ts_flow_set_psi_section_interval(flow_def,
                scte35g->scte35_interval)) ||
        !ubase_check(uref_block_flow_set_octetrate(flow_def,
                (TS_SIZE - TS_HEADER_SIZE - 1) * UCLOCK_FREQ /
                scte35g->scte35_interval)) ||
        !ubase_check(uref_ts_flow_set_tb_rate(flow_def, TB_RATE_PSI)))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_scte35g_store_flow_def(upipe, flow_def);
}

/** @internal @This sends an SCTE-35 section.
 *
 * @param upipe description structure of the pipe
 * @param section section to send
 * @param cr_sys cr_sys of the next muxed packet
 */
static void upipe_ts_scte35g_send(struct upipe *upipe, struct ubuf *section,
                                  uint64_t cr_sys)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);

    struct ubuf *ubuf = ubuf_dup(section);
    struct uref *uref = uref_alloc(scte35g->uref_mgr);
    if (unlikely(uref == NULL || ubuf == NULL)) {
        ubuf_free(ubuf);
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uref_attach_ubuf(uref, ubuf);
    uref_block_set_start(uref);
    uref_block_set_end(uref);
    uref_clock_set_cr_sys(uref, cr_sys);
    upipe_ts_scte35g_output(upipe, uref, NULL);
    scte35g->scte35_cr_sys = cr_sys;
}

/** @internal @This returns the current SCTE35 interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int upipe_ts_scte35g_get_scte35_interval(struct upipe *upipe,
                                                uint64_t *interval_p)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = scte35g->scte35_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the SCTE35 interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int upipe_ts_scte35g_set_scte35_interval(struct upipe *upipe,
                                                uint64_t interval)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    scte35g->scte35_interval = interval;
    upipe_ts_scte35g_build_flow_def(upipe);
    return UBASE_ERR_NONE;
}

/** @This prepares the next PSI sections for the given date.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys current muxing date
 * @param cr_program current muxing date for the program
 * @param latency latency before the packet is output
 * @return an error code
 */
static int upipe_ts_scte35g_prepare(struct upipe *upipe, uint64_t cr_sys,
                                    uint64_t latency)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    scte35g->cr_sys = cr_sys;
    if (unlikely(scte35g->flow_def == NULL ||
                 scte35g->scte35_null_section == NULL ||
                 !scte35g->scte35_interval ||
                 scte35g->scte35_cr_sys + scte35g->scte35_interval > cr_sys))
        return UBASE_ERR_NONE;

    upipe_flush(scte35g->scte35m);

    bool handled = false;
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&scte35g->messages, uchain, uchain_tmp) {
        struct scte35_message *msg = scte35_message_from_uchain(uchain);

        if (msg->cr_sys < cr_sys) {
            if (msg->immediate) {
                upipe_dbg_va(upipe, "sending an immediate %s event", msg->name);
                upipe_ts_scte35g_send(upipe, msg->immediate, cr_sys);
                handled = true;
            }
            else
                upipe_dbg_va(upipe, "%s event expired", msg->name);
            scte35_message_del(msg);
        } else {
            upipe_dbg_va(upipe, "sending an %s event", msg->name);
            scte35_message_set_immediate(msg, NULL);
            if (msg->ubuf) {
                upipe_ts_scte35g_send(upipe, msg->ubuf, cr_sys);
                handled = true;
            }
        }
    }

    if (!handled) {
        upipe_verbose(upipe, "sending null");
        upipe_ts_scte35g_send(upipe, scte35g->scte35_null_section, cr_sys);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This checks the internal state of the pipe.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_ts_scte35g_check(struct upipe *upipe)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);

    if (unlikely(!scte35g->uref_mgr)) {
        upipe_ts_scte35g_demand_uref_mgr(upipe);
        if (!scte35g->uref_mgr)
            return UBASE_ERR_NONE;
    }

    if (unlikely(!scte35g->flow_def))
        upipe_ts_scte35g_build_flow_def(upipe);

    if (unlikely(!scte35g->ubuf_mgr)) {
        struct uref *flow_format = uref_alloc_control(scte35g->uref_mgr);
        uref_flow_set_def(flow_format, "block.mpegtspsi.");
        upipe_ts_scte35g_demand_ubuf_mgr(upipe, flow_format);
        if (!scte35g->ubuf_mgr)
            return UBASE_ERR_NONE;
    }

    if (unlikely(!scte35g->scte35_null_section))
        upipe_ts_scte35g_build_null(upipe);

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_scte35g_control_real(struct upipe *upipe,
                                         int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_ts_scte35g_control_bin_input(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_ts_scte35g_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_TS_MUX_GET_SCTE35_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_scte35g_get_scte35_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_SCTE35_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_scte35g_set_scte35_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_PREPARE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t cr_sys = va_arg(args, uint64_t);
            uint64_t latency = va_arg(args, uint64_t);
            return upipe_ts_scte35g_prepare(upipe, cr_sys, latency);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands and checks internal state of the
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_scte35g_control(struct upipe *upipe,
                                    int command, va_list args)
{
    UBASE_RETURN(upipe_ts_scte35g_control_real(upipe, command, args));
    return upipe_ts_scte35g_check(upipe);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35g_free(struct upipe *upipe)
{
    upipe_ts_scte35g_clean_proxy_probe(upipe);
    upipe_ts_scte35g_clean_urefcount_real(upipe);
    upipe_ts_scte35g_clean_urefcount(upipe);
    upipe_ts_scte35g_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35g_no_ref(struct upipe *upipe)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_ts_scte35g_clean_bin_input(upipe);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&scte35g->messages)))
        scte35_message_del(scte35_message_from_uchain(uchain));
    ubuf_free(scte35g->scte35_null_section);
    upipe_ts_scte35g_clean_output(upipe);
    upipe_ts_scte35g_clean_ubuf_mgr(upipe);
    upipe_ts_scte35g_clean_uref_mgr(upipe);
    upipe_ts_scte35g_release_urefcount_real(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_scte35g_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SCTE35G_SIGNATURE,

    .upipe_alloc = upipe_ts_scte35g_alloc,
    .upipe_input = upipe_ts_scte35g_bin_input,
    .upipe_control = upipe_ts_scte35g_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_scte35g pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte35g_mgr_alloc(void)
{
    return &upipe_ts_scte35g_mgr;
}
