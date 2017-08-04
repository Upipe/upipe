/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
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

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_scte35_generator.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-ts/uref_ts_scte35.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/scte/35.h>

/** T-STD TB octet rate for PSI tables */
#define TB_RATE_PSI 125000
/** 2^33 (max resolution of PCR, PTS and DTS) */
#define POW2_33 UINT64_C(8589934592)
/** ratio between Upipe freq and MPEG freq */
#define CLOCK_SCALE (UCLOCK_FREQ / 90000)

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
    /** SCTE-35 insert command section */
    struct ubuf *scte35_insert_section;
    /** SCTE-35 immediate insert command section */
    struct ubuf *scte35_immediate_section;
    /** SCTE-35 insert cr_sys */
    uint64_t scte35_insert_cr_sys;

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
    upipe_ts_scte35g->scte35_insert_section = NULL;
    upipe_ts_scte35g->scte35_immediate_section = NULL;
    upipe_ts_scte35g->scte35_insert_cr_sys = 0;

    upipe_throw_ready(upipe);
    upipe_ts_scte35g_demand_uref_mgr(upipe);
    if (likely(upipe_ts_scte35g->uref_mgr != NULL)) {
        struct uref *flow_format = uref_alloc_control(upipe_ts_scte35g->uref_mgr);
        uref_flow_set_def(flow_format, "block.mpegtspsi.");
        upipe_ts_scte35g_demand_ubuf_mgr(upipe, flow_format);
    }
    return upipe;
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
    struct upipe_ts_scte35g *scte35g = upipe_ts_scte35g_from_upipe(upipe);

    if (uref == NULL || uref->udict == NULL) {
        upipe_notice(upipe, "now using splice_null command due to empty event");
        uref_free(uref);
        ubuf_free(scte35g->scte35_insert_section);
        scte35g->scte35_insert_section = NULL;
        return;
    }

    uint64_t pts_prog = UINT64_MAX;
    uref_clock_get_pts_prog(uref, &pts_prog);
    uint64_t duration = UINT64_MAX;
    uref_clock_get_duration(uref, &duration);
    scte35g->scte35_insert_cr_sys = 0;
    uref_clock_get_pts_sys(uref, &scte35g->scte35_insert_cr_sys);
    uint64_t event_id = 0;
    uref_ts_scte35_get_event_id(uref, &event_id);
    bool cancel = ubase_check(uref_ts_scte35_get_cancel(uref));
    bool out_of_network = ubase_check(uref_ts_scte35_get_out_of_network(uref));
    bool auto_return = ubase_check(uref_ts_scte35_get_auto_return(uref));
    uint64_t program_id = 0;
    uref_ts_scte35_get_unique_program_id(uref, &program_id);
    uref_free(uref);

    for ( ; ; ) {
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
            ubuf_free(scte35g->scte35_immediate_section);
            scte35g->scte35_immediate_section = ubuf;
            break;
        }

        ubuf_free(scte35g->scte35_insert_section);
        scte35g->scte35_insert_section = ubuf;
        pts_prog = UINT64_MAX;
    }

    /* Force sending the table immediately */
    scte35g->scte35_cr_sys = 0;
    upipe_notice_va(upipe,
                    "now using splice_insert command for event %"PRIu32,
                    event_id);
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
    if (unlikely(scte35g->flow_def == NULL ||
                 scte35g->scte35_null_section == NULL ||
                 !scte35g->scte35_interval ||
                 scte35g->scte35_cr_sys + scte35g->scte35_interval > cr_sys))
        return UBASE_ERR_NONE;

    if (scte35g->scte35_insert_section != NULL) {
        if (scte35g->scte35_insert_cr_sys < cr_sys) {
            upipe_notice(upipe, "event expired");
            ubuf_free(scte35g->scte35_insert_section);
            scte35g->scte35_insert_section = NULL;

        } else {
            upipe_dbg(upipe, "sending a splice insert event");
            upipe_ts_scte35g_send(upipe, scte35g->scte35_insert_section,
                                  cr_sys);
            /* From now on we no longer need a splice immediate section */
            ubuf_free(scte35g->scte35_immediate_section);
            scte35g->scte35_immediate_section = NULL;
            return UBASE_ERR_NONE;
        }
    }

    if (scte35g->scte35_immediate_section != NULL) {
        upipe_notice(upipe, "sending an immediate splice insert event");
        upipe_ts_scte35g_send(upipe, scte35g->scte35_immediate_section, cr_sys);
        ubuf_free(scte35g->scte35_immediate_section);
        scte35g->scte35_immediate_section = NULL;
        return UBASE_ERR_NONE;
    }

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

    uref_free(scte35g->flow_def);
    ubuf_free(scte35g->scte35_null_section);
    ubuf_free(scte35g->scte35_insert_section);
    ubuf_free(scte35g->scte35_immediate_section);
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
