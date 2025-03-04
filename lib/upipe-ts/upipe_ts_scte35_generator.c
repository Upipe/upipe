/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 * Copyright (C) 2021-2025 EasyTools
 *
 * Authors: Christophe Massiot
 *          Arnaud de Turckheim
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

#include "upipe-ts/uref_ts_flow.h"
#include "upipe/ubase.h"
#include "upipe/ulist.h"
#include "upipe/uclock.h"
#include "upipe/uref.h"
#include "upipe/uref_block.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_clock.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_block.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_uref_mgr.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_output.h"
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
    /** SCTE-35 cr_sys */
    uint64_t cr_sys;
    /** uref describing the SCTE-35 message */
    struct uref *uref;
    /** message type */
    uint8_t type;
    /** message id */
    uint64_t id;
    /** message was already sent? */
    bool sent;
};

UBASE_FROM_TO(scte35_message, uchain, uchain, uchain);

/** @internal @This is the private context of a ts scte35g pipe. */
struct upipe_ts_scte35g {
    /** refcount management structure */
    struct urefcount urefcount;
    /** input flow definition */
    struct uref *flow_def;

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
    struct uref *output_flow_def;
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
    struct uchain scte35_sections;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_scte35g, upipe, UPIPE_TS_SCTE35G_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_scte35g, urefcount, upipe_ts_scte35g_free)
UPIPE_HELPER_VOID(upipe_ts_scte35g)
UPIPE_HELPER_OUTPUT(upipe_ts_scte35g, output, output_flow_def, output_state,
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
scte35_message_new(struct uref *uref, uint8_t type,  uint64_t id)
{
    struct scte35_message *msg = malloc(sizeof (*msg));
    if (!msg)
        return NULL;
    uchain_init(&msg->uchain);
    msg->cr_sys = 0;
    msg->type = type;
    msg->id = id;
    msg->uref = uref_dup(uref);
    msg->sent = false;
    return msg;
}

/** @internal @This frees a SCTE-35 message.
 *
 * @param msg SCTE-35 message to free
 */
static void scte35_message_del(struct scte35_message *msg)
{
    uref_free(msg->uref);
    free(msg);
}

/** @internal @This returns the matching event in the list if any.
 *
 * @param list list to look into
 * @param type message type (splice insert, time signal, ...)
 * @param id message id
 * @return the matching event or NULL
 */
static struct scte35_message *scte35_message_find(struct uchain *list,
                                                  uint8_t type,
                                                  uint64_t id)
{
    struct uchain *uchain;
    if (!list)
        return NULL;

    ulist_foreach(list, uchain) {
        struct scte35_message *msg = scte35_message_from_uchain(uchain);
        if (msg->type == type && msg->id == id)
            return msg;
    }
    return NULL;
}

/** @internal @This returns the message type as a string (for debug messages).
 *
 * @param msg message to get the type name from
 * @return the type name
 */
static const char *scte35_message_type_str(struct scte35_message *msg)
{
    if (!msg) return "(invalid)";
    switch (msg->type) {
        case SCTE35_INSERT_COMMAND:         return "splice insert";
        case SCTE35_TIME_SIGNAL_COMMAND:    return "time signal";
    }
    return "(unknown)";
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
    upipe_ts_scte35g_init_uref_mgr(upipe);
    upipe_ts_scte35g_init_ubuf_mgr(upipe);
    upipe_ts_scte35g_init_output(upipe);
    upipe_ts_scte35g->flow_def = NULL;

    upipe_ts_scte35g->scte35_interval = 0;
    upipe_ts_scte35g->scte35_cr_sys = 0;
    upipe_ts_scte35g->scte35_null_section = NULL;
    ulist_init(&upipe_ts_scte35g->scte35_sections);

    upipe_throw_ready(upipe);
    upipe_ts_scte35g_demand_uref_mgr(upipe);
    if (likely(upipe_ts_scte35g->uref_mgr != NULL)) {
        struct uref *flow_format = uref_alloc_control(upipe_ts_scte35g->uref_mgr);
        uref_flow_set_def(flow_format, "block.mpegtspsi.");
        upipe_ts_scte35g_demand_ubuf_mgr(upipe, flow_format);
    }
    return upipe;
}

/** @internal @This builds a splice insert message.
 *
 * @param upipe description structure of the pipe
 * @param uref uref describing the message
 * @return a buffer with the SCTE-35 message
 */
static struct ubuf *upipe_ts_scte35g_build_splice_insert(struct upipe *upipe,
                                                         struct uref *uref)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    struct ubuf *ubuf = ubuf_block_alloc(scte35g->ubuf_mgr,
                                         PSI_MAX_SIZE + PSI_HEADER_SIZE);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    uint8_t *scte35;
    int size = -1;
    if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &scte35))) {
        ubuf_free(ubuf);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    bool cancel = ubase_check(uref_ts_scte35_get_cancel(uref));
    bool out_of_network = ubase_check(uref_ts_scte35_get_out_of_network(uref));
    bool auto_return = ubase_check(uref_ts_scte35_get_auto_return(uref));
    uint64_t program_id = 0;
    uref_ts_scte35_get_unique_program_id(uref, &program_id);
    uint64_t pts_prog = UINT64_MAX;
    uref_clock_get_pts_prog(uref, &pts_prog);
    uint64_t duration = UINT64_MAX;
    uref_clock_get_duration(uref, &duration);
    uint64_t event_id = 0;
    uref_ts_scte35_get_event_id(uref, &event_id);

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

        uint8_t *desc =
            descl_get_desc(descl, descl_length + DESC_HEADER_SIZE, j++);
        memcpy(desc, d, l);
        descl_length += DESC_HEADER_SIZE + scte35_splice_desc_get_length(desc);
    }

    scte35_set_desclength(scte35, descl_length);

    psi_set_length(scte35, scte35_get_descl(scte35) + PSI_CRC_SIZE - scte35 -
                               PSI_HEADER_SIZE + descl_length);
    psi_set_crc(scte35);

    uint16_t scte35_size = psi_get_length(scte35) + PSI_HEADER_SIZE;
    ubuf_block_unmap(ubuf, 0);
    ubuf_block_resize(ubuf, 0, scte35_size);

    return ubuf;
}

/** @internal @This builds a time signal message.
 *
 * @param upipe description structure of the pipe
 * @param uref uref describing the message
 * @return a buffer with the SCTE-35 message
 */
static struct ubuf *upipe_ts_scte35g_build_time_signal(struct upipe *upipe,
                                                       struct uref *uref)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    struct ubuf *ubuf = ubuf_block_alloc(scte35g->ubuf_mgr,
                                         PSI_MAX_SIZE + PSI_HEADER_SIZE);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    uint8_t *scte35;
    int size = -1;
    if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &scte35))) {
        ubuf_free(ubuf);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    uint64_t pts_prog = UINT64_MAX;
    uref_clock_get_pts_prog(uref, &pts_prog);

    scte35_init(scte35);
    /* set length later */
    psi_set_length(scte35, PSI_MAX_SIZE);
    scte35_set_pts_adjustment(scte35, 0);

    uint16_t i_size = 0;
    if (pts_prog != UINT64_MAX)
        i_size = SCTE35_SPLICE_TIME_TIME_SIZE;

    scte35_time_signal_init(scte35, i_size);
    uint8_t *t = scte35_time_signal_get_splice_time(scte35);
    scte35_splice_time_init(t);
    scte35_splice_time_set_time_specified(t, pts_prog != UINT64_MAX);
    if (pts_prog != UINT64_MAX)
        scte35_splice_time_set_pts_time(t, (pts_prog / CLOCK_SCALE) % POW2_33);

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

        uint8_t *desc =
            descl_get_desc(descl, descl_length + DESC_HEADER_SIZE, j++);
        memcpy(desc, d, l);
        descl_length += DESC_HEADER_SIZE + scte35_splice_desc_get_length(desc);
    }

    scte35_set_desclength(scte35, descl_length);

    psi_set_length(scte35,
                   scte35_get_descl(scte35) + PSI_CRC_SIZE -
                   scte35 - PSI_HEADER_SIZE + descl_length);
    psi_set_crc(scte35);

    uint16_t scte35_size = psi_get_length(scte35) + PSI_HEADER_SIZE;
    ubuf_block_unmap(ubuf, 0);
    ubuf_block_resize(ubuf, 0, scte35_size);

    return ubuf;
}

/** @internal @This builds a SCTE-35 message.
 *
 * @param upipe description structure of the pipe
 * @param uref uref describing the message
 * @return a buffer with the SCTE-35 message
 */
static struct ubuf *upipe_ts_scte35g_build(struct upipe *upipe,
                                           struct uref *uref)
{
    uint8_t type;
    if (unlikely(!ubase_check(uref_ts_scte35_get_command_type(uref, &type))))
        return NULL;

    switch (type) {
        case SCTE35_INSERT_COMMAND:
            return upipe_ts_scte35g_build_splice_insert(upipe, uref);
        case SCTE35_TIME_SIGNAL_COMMAND:
            return upipe_ts_scte35g_build_time_signal(upipe, uref);
    }
    return NULL;
}

/** @internal @This creates a new PSI section for splice insert.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35g_input_splice_insert(struct upipe *upipe,
                                                 struct uref *uref)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    uint64_t duration = UINT64_MAX;
    uref_clock_get_duration(uref, &duration);
    uint64_t cr_sys = 0;
    uref_clock_get_pts_sys(uref, &cr_sys);
    uint64_t event_id = 0;
    uref_ts_scte35_get_event_id(uref, &event_id);

    struct scte35_message *msg =
        scte35_message_find(&scte35g->scte35_sections,
                            SCTE35_INSERT_COMMAND, event_id);
    if (!msg) {
        msg = scte35_message_new(uref, SCTE35_INSERT_COMMAND, event_id);
        if (unlikely(!msg)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        ulist_add(&scte35g->scte35_sections, scte35_message_to_uchain(msg));

        /* Force sending the table immediately */
        scte35g->scte35_cr_sys = 0;
        upipe_notice_va(upipe,
                        "now using splice insert event %"PRIu64, event_id);
    } else {
        if (uref_ts_scte35_cmp_cancel(msg->uref, uref) ||
            uref_ts_scte35_cmp_out_of_network(msg->uref, uref) ||
            uref_ts_scte35_cmp_auto_return(msg->uref, uref) ||
            uref_ts_scte35_cmp_unique_program_id(msg->uref, uref) ||
            uref_clock_cmp_duration(msg->uref, uref) ||
            uref_clock_cmp_pts_prog(msg->uref, uref)) {
            uref_free(msg->uref);
            msg->uref = uref_dup(uref);
            msg->sent = false;

            /* Force sending the table immediately */
            scte35g->scte35_cr_sys = 0;
            upipe_dbg_va(upipe,
                         "updating splice insert event %"PRIu64, event_id);
        } else
            upipe_verbose_va(upipe, "ignore duplicate splice insert event %"
                             PRIu64, event_id);
    }
    msg->cr_sys = cr_sys;
}

/** @internal @This creates a new PSI section for time signal.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_ts_scte35g_input_time_signal(struct upipe *upipe,
                                               struct uref *uref)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    uint64_t pts_prog = UINT64_MAX;
    uref_clock_get_pts_prog(uref, &pts_prog);
    uint64_t cr_sys = 0;
    uref_clock_get_pts_sys(uref, &cr_sys);

    uint64_t nb = 0;
    uref_ts_flow_get_descriptors(uref, &nb);
    for (uint64_t i = 0; i < nb; i++) {
        const uint8_t *d = NULL;
        size_t l = 0;
        uref_ts_scte35_desc_get_seg(uref, &d, &l, i);
        uref_ts_flow_get_descriptor(uref, &d, &l, i);
        if (!d || !l)
            continue;

        uint64_t event_id = scte35_seg_desc_get_event_id(d);
        struct uref *tmp = uref_dup(uref);
        uref_ts_flow_set_descriptors(tmp, 0);
        uref_ts_flow_add_descriptor(tmp, d, l);
        struct scte35_message *msg =
            scte35_message_find(&scte35g->scte35_sections,
                                SCTE35_TIME_SIGNAL_COMMAND, event_id);
        if (!msg) {
            msg = scte35_message_new(tmp, SCTE35_TIME_SIGNAL_COMMAND, event_id);
            if (unlikely(!msg)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return;
            }
            ulist_add(&scte35g->scte35_sections, scte35_message_to_uchain(msg));

            /* Force sending the table immediately */
            scte35g->scte35_cr_sys = 0;
            upipe_notice_va(upipe, "now using time signal event %"PRIu64,
                            event_id);
        } else {
            if (uref_ts_flow_compare_descriptors(msg->uref, tmp) ||
                uref_clock_cmp_pts_prog(msg->uref, tmp)) {
                uref_free(msg->uref);
                msg->uref = uref_dup(tmp);
                msg->sent = false;

                /* Force sending the table immediately */
                scte35g->scte35_cr_sys = 0;
                upipe_dbg_va(upipe, "updating time signal event %"PRIu64,
                             event_id);
            } else {
                upipe_verbose_va(upipe, "ignore duplicate time signal event %"
                                 PRIu64, event_id);
            }
        }
        uref_free(tmp);
        msg->cr_sys = cr_sys;
    }
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

/** @internal @This creates a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte35g_input(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p)
{
    uint8_t cmd_type;

    if (uref == NULL || uref->udict == NULL)
        upipe_notice(upipe, "now using splice_null command due to empty event");
    else if (!ubase_check(uref_ts_scte35_get_command_type(uref, &cmd_type)))
        upipe_warn(upipe, "no command type in packet");
    else {
        switch (cmd_type) {
            case SCTE35_INSERT_COMMAND:
                upipe_ts_scte35g_input_splice_insert(upipe, uref);
                break;
            case SCTE35_NULL_COMMAND:
                upipe_ts_scte35g_build_null(upipe);
                break;
            case SCTE35_TIME_SIGNAL_COMMAND:
                upipe_ts_scte35g_input_time_signal(upipe, uref);
                break;
            default:
                upipe_warn_va(upipe, "unimplemented command type %u", cmd_type);
                break;
        }
    }
    uref_free(uref);
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

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_scte35g_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "void.scte35."))

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    bool scte35_change = scte35g->flow_def == NULL;

    uref_free(scte35g->flow_def);
    scte35g->flow_def = flow_def_dup;

    if (scte35_change) {
        upipe_ts_scte35g_build_null(upipe);
        upipe_ts_scte35g_build_flow_def(upipe);
    }
    return UBASE_ERR_NONE;
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
    struct uchain *uchain, *next;

    if (unlikely(!scte35g->flow_def || !scte35g->scte35_null_section))
        return UBASE_ERR_NONE;

    if (!scte35g->scte35_interval) {
        ulist_delete_foreach(&scte35g->scte35_sections, uchain, next) {
            struct scte35_message *msg = scte35_message_from_uchain(uchain);
            if (msg->cr_sys < cr_sys) {
                upipe_dbg_va(upipe, "event %" PRIu64 " %s", msg->id,
                             msg->sent ? "expired" : "ignored");
                ulist_delete(uchain);
                scte35_message_del(msg);
            }
        }
        return UBASE_ERR_NONE;
    }


    if (scte35g->scte35_cr_sys + scte35g->scte35_interval > cr_sys)
        return UBASE_ERR_NONE;

    bool handled = false;
    struct uchain list;
    ulist_init(&list);
    while ((uchain = ulist_pop(&scte35g->scte35_sections))) {
        struct scte35_message *msg = scte35_message_from_uchain(uchain);
        const char *name = scte35_message_type_str(msg);

        ulist_add(&list, uchain);

        if (msg->cr_sys < cr_sys && msg->sent)
            continue;

        struct uref *uref = uref_dup(msg->uref);
        if (unlikely(uref == NULL)) {
            upipe_warn_va(upipe, "fail to duplicate %s event %" PRIu64,
                          name, msg->id);
            continue;
        }

        if (msg->cr_sys < cr_sys) {
            upipe_notice_va(upipe, "sending a %s immediate event %" PRIu64,
                            name, msg->id);
            uref_clock_delete_date_prog(uref);
        } else if (msg->sent) {
            upipe_dbg_va(upipe, "resending a %s event %" PRIu64,
                         name, msg->id);
        } else {
            upipe_notice_va(upipe, "sending a %s event %" PRIu64,
                            name, msg->id);
        }
        msg->sent = true;

        if (msg->type == SCTE35_TIME_SIGNAL_COMMAND) {
            ulist_delete_foreach(&scte35g->scte35_sections, uchain, next) {
                struct scte35_message *add = scte35_message_from_uchain(uchain);

                if (add->type == msg->type &&
                    !uref_clock_cmp_pts_prog(msg->uref, add->uref)) {
                    const uint8_t *d = NULL;
                    size_t l = 0;
                    uref_ts_flow_get_descriptor(add->uref, &d, &l, 0);

                    ulist_delete(uchain);
                    ulist_add(&list, uchain);

                    if (add->cr_sys < cr_sys && add->sent)
                        continue;

                    if (d && l) {
                        if (add->sent)
                            upipe_dbg_va(upipe, "reaggregating %s "
                                         "event %" PRIu64 " "
                                         "to event %" PRIu64,
                                         name, add->id,
                                         msg->id);
                        else
                            upipe_notice_va(upipe, "aggregating %s "
                                            "event %" PRIu64 " "
                                            "to event %" PRIu64,
                                            name, add->id,
                                            msg->id);
                        uref_ts_flow_add_descriptor(uref, d, l);
                        add->sent = true;
                    }
                }
            }
        }

        struct ubuf *ubuf = upipe_ts_scte35g_build(upipe, uref);
        if (ubuf) {
            upipe_ts_scte35g_send(upipe, ubuf, cr_sys);
            ubuf_free(ubuf);
            handled = true;
        } else {
            upipe_warn_va(upipe, "fail to build %s event", name);
        }
        uref_free(uref);
    }

    while ((uchain = ulist_pop(&list))) {
        struct scte35_message *msg = scte35_message_from_uchain(uchain);
        if (msg->cr_sys < cr_sys) {
            upipe_notice_va(upipe, "event %" PRIu64 " expired", msg->id);
            scte35_message_del(msg);
        } else {
            ulist_add(&scte35g->scte35_sections, uchain);
        }
    }

    if (!handled)
        upipe_ts_scte35g_send(upipe, scte35g->scte35_null_section, cr_sys);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_scte35g_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_scte35g_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_scte35g_set_flow_def(upipe, flow_def);
        }

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

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35g_free(struct upipe *upipe)
{
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);
    upipe_throw_dead(upipe);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&scte35g->scte35_sections)))
        scte35_message_del(scte35_message_from_uchain(uchain));
    uref_free(scte35g->flow_def);
    ubuf_free(scte35g->scte35_null_section);
    upipe_ts_scte35g_clean_output(upipe);
    upipe_ts_scte35g_clean_ubuf_mgr(upipe);
    upipe_ts_scte35g_clean_uref_mgr(upipe);
    upipe_ts_scte35g_clean_urefcount(upipe);
    upipe_ts_scte35g_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_scte35g_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SCTE35G_SIGNATURE,

    .upipe_alloc = upipe_ts_scte35g_alloc,
    .upipe_input = upipe_ts_scte35g_input,
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
