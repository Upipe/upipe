/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This event is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This event is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this event; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module decoding the splice information table of SCTE streams
 * Normative references:
 *  - SCTE 35 2013 (Digital Program Insertion Cueing Message for Cable)
 */

#include <upipe/ubase.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe-ts/upipe_ts_scte35_decoder.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/uref_ts_scte35.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/scte/35.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtsscte35."
/** we output SCTE35 metadata */
#define OUTPUT_FLOW_DEF "void.scte35."
/** 2^33 (max resolution of PCR, PTS and DTS) */
#define POW2_33 UINT64_C(8589934592)

/** @hidden */
static int upipe_ts_scte35d_check(struct upipe *upipe,
                                  struct uref *flow_format);

/** @internal @This is the private context of a ts_scte35d pipe. */
struct upipe_ts_scte35d {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_scte35d, upipe, UPIPE_TS_SCTE35D_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_scte35d, urefcount, upipe_ts_scte35d_free)
UPIPE_HELPER_VOID(upipe_ts_scte35d)
UPIPE_HELPER_OUTPUT(upipe_ts_scte35d, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_scte35d, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_scte35d_check,
                      upipe_ts_scte35d_register_output_request,
                      upipe_ts_scte35d_unregister_output_request)

/** @internal @This allocates a ts_scte35d pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_scte35d_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_scte35d_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_ts_scte35d_init_urefcount(upipe);
    upipe_ts_scte35d_init_output(upipe);
    upipe_ts_scte35d_init_ubuf_mgr(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This is a helper function to parse descriptors and import
 * the relevant ones into uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref to fill in
 * @param descl pointer to descriptor list
 * @param desclength length of the descriptor list
 * @return an error code
 */
static void upipe_ts_scte35d_parse_descs(struct upipe *upipe, struct uref *uref,
                                         const uint8_t *descl,
                                         uint16_t desclength)
{
    const uint8_t *desc;
    int j = 0;

    /* cast needed because biTStream expects an uint8_t * (but doesn't write
     * to it */
    while ((desc = descl_get_desc((uint8_t *)descl, desclength, j++)) != NULL) {
        UBASE_FATAL(upipe, uref_ts_flow_add_descriptor(uref,
                    desc, desc_get_length(desc) + DESC_HEADER_SIZE))
    }
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte35d_input(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p)
{
    struct upipe_ts_scte35d *upipe_ts_scte35d =
        upipe_ts_scte35d_from_upipe(upipe);
    assert(upipe_ts_scte35d->flow_def != NULL);

    uint8_t buffer[SCTE35_HEADER_SIZE];
    const uint8_t *header = uref_block_peek(uref, 0, SCTE35_HEADER_SIZE,
                                            buffer);
    if (unlikely(header == NULL)) {
        uref_free(uref);
        return;
    }

    uint8_t type = scte35_get_command_type(header);
    uref_block_peek_unmap(uref, 0, buffer, header);

    if (type != SCTE35_INSERT_COMMAND) {
        uref_free(uref);
        return;
    }

    const uint8_t *scte35;
    int size = -1;
    if (unlikely(!ubase_check(uref_block_merge(uref, upipe_ts_scte35d->ubuf_mgr,
                                               0, -1)) ||
                 !ubase_check(uref_block_read(uref, 0, &size, &scte35)))) {
        upipe_warn(upipe, "invalid SCTE35 section received");
        uref_free(uref);
        return;
    }

    if (!scte35_validate(scte35)) {
        upipe_warn(upipe, "invalid SCTE35 section received");
        uref_block_unmap(uref, 0);
        uref_free(uref);
        return;
    }

    uint32_t event_id = scte35_insert_get_event_id(scte35);
    bool cancel = scte35_insert_has_cancel(scte35);
    bool out_of_network = false;
    uint64_t pts = UINT64_MAX;
    uint64_t duration = UINT64_MAX;
    bool auto_return = false;
    uint16_t unique_program_id = 0;
    if (!cancel) {
        out_of_network = scte35_insert_has_out_of_network(scte35);
        bool program_splice = scte35_insert_has_program_splice(scte35);
        bool splice_immediate = scte35_insert_has_splice_immediate(scte35);
        unique_program_id = scte35_insert_get_unique_program_id(scte35);

        if (!splice_immediate) {
            const uint8_t *splice_time = NULL;
            if (program_splice)
                splice_time = scte35_insert_get_splice_time(scte35);
            else if (scte35_insert_get_component_count(scte35))
                splice_time = scte35_insert_component_get_splice_time(
                        scte35_insert_get_component(scte35, 0));

            if (splice_time != NULL &&
                scte35_splice_time_has_time_specified(splice_time)) {
                pts = scte35_splice_time_get_pts_time(splice_time);
                pts += scte35_get_pts_adjustment(scte35);
                pts %= POW2_33;
            }
        }

        if (scte35_insert_has_duration(scte35)) {
            const uint8_t *break_duration =
                scte35_insert_get_break_duration(scte35);
            auto_return = scte35_break_duration_has_auto_return(break_duration);
            duration = scte35_break_duration_get_duration(break_duration);
        }
    }

    if (scte35_get_command_length(scte35) != 0xfff &&
        scte35_get_desclength(scte35))
        upipe_ts_scte35d_parse_descs(upipe, uref, scte35_get_descl(scte35),
                                     scte35_get_desclength(scte35));

    uref_block_unmap(uref, 0);
    ubuf_free(uref_detach_ubuf(uref));

    uref_ts_scte35_set_event_id(uref, event_id);
    if (cancel)
        uref_ts_scte35_set_cancel(uref);
    else {
        if (out_of_network)
            uref_ts_scte35_set_out_of_network(uref);
        uref_ts_scte35_set_unique_program_id(uref, unique_program_id);
        if (pts != UINT64_MAX) {
            uref_clock_set_pts_orig(uref, pts * UCLOCK_FREQ / 90000);
            uref_clock_set_dts_pts_delay(uref, 0);
        }
        if (duration != UINT64_MAX) {
            uref_clock_set_duration(uref, duration * UCLOCK_FREQ / 90000);
            if (auto_return)
                uref_ts_scte35_set_auto_return(uref);
        }
        if (pts != UINT64_MAX || duration != UINT64_MAX)
            upipe_throw_clock_ts(upipe, uref);
    }

    upipe_ts_scte35d_output(upipe, uref, upump_p);
}

/** @internal @This receives an ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_scte35d_check(struct upipe *upipe, struct uref *flow_format)
{
    uref_free(flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_scte35d_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_ts_scte35d_demand_ubuf_mgr(upipe, flow_def_dup);
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL) ||
        !ubase_check(uref_flow_set_def(flow_def_dup, OUTPUT_FLOW_DEF))) {
        uref_free(flow_def_dup);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_ts_scte35d_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_scte35d_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_scte35d_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_scte35d_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_scte35d_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_scte35d_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_scte35d_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_scte35d_set_output(upipe, output);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35d_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_scte35d_clean_output(upipe);
    upipe_ts_scte35d_clean_ubuf_mgr(upipe);
    upipe_ts_scte35d_clean_urefcount(upipe);
    upipe_ts_scte35d_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_scte35d_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SCTE35D_SIGNATURE,

    .upipe_alloc = upipe_ts_scte35d_alloc,
    .upipe_input = upipe_ts_scte35d_input,
    .upipe_control = upipe_ts_scte35d_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_scte35d pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte35d_mgr_alloc(void)
{
    return &upipe_ts_scte35d_mgr;
}
