/*
 * Copyright (C) 2019 Open Broadcast Systems Ltd
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
 * @short Upipe module performing SCTE-35 to SCTE-104 conversion
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/ubuf.h>
#include <upipe/uclock.h>
#include <upipe-ts/uref_ts_scte35.h>

#include <upipe-ts/upipe_ts_scte104_generator.h>
#include <bitstream/scte/104.h>

#define EXPECTED_FLOW_DEF "void.scte35."

/** upipe_ts_scte104_generator structure */
struct upipe_ts_scte104_generator {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_ts_scte104_generator_check(struct upipe *upipe, struct uref *flow_format);

/** @hidden */
static bool upipe_ts_scte104_generator_handle(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_ts_scte104_generator, upipe, UPIPE_TS_SCTE104_GENERATOR_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_ts_scte104_generator, urefcount, upipe_ts_scte104_generator_free)
UPIPE_HELPER_VOID(upipe_ts_scte104_generator);
UPIPE_HELPER_OUTPUT(upipe_ts_scte104_generator, output, flow_def, output_state, request_list);
UPIPE_HELPER_INPUT(upipe_ts_scte104_generator, urefs, nb_urefs, max_urefs, blockers, upipe_ts_scte104_generator_handle)
UPIPE_HELPER_UBUF_MGR(upipe_ts_scte104_generator, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_scte104_generator_check,
                      upipe_ts_scte104_generator_register_output_request,
                      upipe_ts_scte104_generator_unregister_output_request)

static int upipe_ts_scte104_generator_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL)
        upipe_ts_scte104_generator_store_flow_def(upipe, flow_format);

    bool was_buffered = !upipe_ts_scte104_generator_check_input(upipe);
    upipe_ts_scte104_generator_output_input(upipe);
    upipe_ts_scte104_generator_unblock_input(upipe);
    if (was_buffered && upipe_ts_scte104_generator_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_ts_scte104_generator_input. */
        upipe_release(upipe);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte104_generator_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    if (!upipe_ts_scte104_generator_check_input(upipe)) {
        upipe_ts_scte104_generator_hold_input(upipe, uref);
        upipe_ts_scte104_generator_block_input(upipe, upump_p);
    } else if (!upipe_ts_scte104_generator_handle(upipe, uref, upump_p)) {
        upipe_ts_scte104_generator_hold_input(upipe, uref);
        upipe_ts_scte104_generator_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static bool upipe_ts_scte104_generator_handle(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_ts_scte104_generator *upipe_ts_scte104_generator = upipe_ts_scte104_generator_from_upipe(upipe);

    if (!upipe_ts_scte104_generator->ubuf_mgr)
        return false;

    uint8_t splice_insert_type;
    uint8_t len = SCTE104M_HEADER_SIZE + SCTE104T_HEADER_SIZE + 1 /* num_opts */ + SCTE104O_HEADER_SIZE + SCTE104SRD_HEADER_SIZE;
    uint64_t pts_orig = UINT64_MAX, event_id = 0, unique_program_id = 0, cr_dts_delay = 0, duration = UINT64_MAX, pts_prog = 0, pts_sys = 0;
    uref_clock_get_pts_orig(uref, &pts_orig);

    if (uref_ts_scte35_get_cancel(uref))
        splice_insert_type = SCTE104SRD_CANCEL;
    else if (uref_ts_scte35_get_out_of_network(uref))
        splice_insert_type = pts_orig == UINT64_MAX ? SCTE104SRD_START_IMMEDIATE : SCTE104SRD_START_NORMAL;
    else
        splice_insert_type = pts_orig == UINT64_MAX ? SCTE104SRD_END_IMMEDIATE : SCTE104SRD_END_NORMAL;

    struct ubuf *ubuf = ubuf_block_alloc(upipe_ts_scte104_generator->ubuf_mgr, len);
    int size = -1;
    uint8_t *buf;
    ubase_assert(ubuf_block_write(ubuf, 0, &size, &buf));

    uint8_t *ts = scte104m_get_timestamp(buf);
    uint8_t *op;

    scte104_set_opid(buf, SCTE104_OPID_MULTIPLE);
    scte104_set_size(buf, len);

    scte104m_set_protocol(buf, 0);
    scte104m_set_as_index(buf, 0);
    scte104m_set_message_number(buf, 1); /* arbitrary */
    scte104m_set_dpi_pid_index(buf, 0);
    scte104m_set_scte35_protocol(buf, 0);

    scte104t_set_type(ts, SCTE104T_TYPE_NONE);
    scte104m_set_num_ops(buf, 1);

    op = scte104m_get_op(buf, 0);

    scte104o_set_opid(op, SCTE104_OPID_SPLICE);
    scte104o_set_data_length(op, SCTE104SRD_HEADER_SIZE);
    op = scte104o_get_data(op);

    scte104srd_set_insert_type(op, splice_insert_type);
    uref_ts_scte35_get_event_id(uref, &event_id);
    scte104srd_set_event_id(op, event_id);
    uref_ts_scte35_get_unique_program_id(uref, &unique_program_id);
    scte104srd_set_unique_program_id(op, unique_program_id);
    uref_clock_get_cr_dts_delay(uref, &cr_dts_delay);
    scte104srd_set_pre_roll_time(op, cr_dts_delay / (UCLOCK_FREQ/10));
    uref_clock_get_duration(uref, &duration);
    if (duration != UINT64_MAX)
        scte104srd_set_break_duration(op, duration / (UCLOCK_FREQ/10));
    scte104srd_set_avail_num(op, 0);
    scte104srd_set_avails_expected(op, 0);
    scte104srd_set_auto_return(op, uref_ts_scte35_get_auto_return(uref));

    ubuf_block_unmap(ubuf, 0);
    uref_attach_ubuf(uref, ubuf);

    /* SCTE-35 "PTS" is actually the splice time. Make SCTE-104 urefs use the real PTS */
    pts_orig -= cr_dts_delay;
    uref_clock_set_pts_orig(uref, pts_orig);
    uref_clock_get_pts_prog(uref, &pts_prog);
    pts_prog -= cr_dts_delay;
    uref_clock_set_pts_prog(uref, pts_prog);
    uref_clock_get_pts_sys(uref, &pts_sys);
    printf("pts_sys %"PRIu64" cr_dts_delay %"PRIu64" \n", pts_sys, cr_dts_delay);

    upipe_ts_scte104_generator_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_scte104_generator_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))

    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (flow_def_dup == NULL)
        return UBASE_ERR_ALLOC;

    uref_flow_set_def(flow_def_dup, "block.scte104.");

    upipe_ts_scte104_generator_require_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts_scte_104_generator pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_scte104_generator_control(struct upipe *upipe,
                                     int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_ts_scte104_generator_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_ts_scte104_generator_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_scte104_generator_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_ts_scte104_generator_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a s337_encaps pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_scte104_generator_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_scte104_generator_alloc_void(mgr, uprobe, signature,
                                                       args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_ts_scte104_generator_init_urefcount(upipe);
    upipe_ts_scte104_generator_init_output(upipe);
    upipe_ts_scte104_generator_init_input(upipe);
    upipe_ts_scte104_generator_init_ubuf_mgr(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte104_generator_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_scte104_generator_clean_output(upipe);
    upipe_ts_scte104_generator_clean_input(upipe);
    upipe_ts_scte104_generator_clean_urefcount(upipe);
    upipe_ts_scte104_generator_clean_ubuf_mgr(upipe);
    upipe_ts_scte104_generator_free_void(upipe);
}

static struct upipe_mgr upipe_ts_scte104_generator_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SCTE104_GENERATOR_SIGNATURE,

    .upipe_alloc = upipe_ts_scte104_generator_alloc,
    .upipe_input = upipe_ts_scte104_generator_input,
    .upipe_control = upipe_ts_scte104_generator_control
};

/** @This returns the management structure for scte104_generator pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte104_generator_mgr_alloc(void)
{
    return &upipe_ts_scte104_generator_mgr;
}
