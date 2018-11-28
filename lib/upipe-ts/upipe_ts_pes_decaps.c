/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe module decapsulating (removing) PES header of packets
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_pes_decaps.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/pes.h>

/** we only accept formerly TS packets that contain PES headers when unit
 * start is true */
#define EXPECTED_FLOW_DEF "block.mpegtspes."
/** 2^33 (max resolution of PCR, PTS and DTS) */
#define POW2_33 UINT64_C(8589934592)
/** max DTS/PTS delay */
#define MAX_DELAY (UCLOCK_FREQ * 60)

/** @internal @This is the private context of a ts_pesd pipe. */
struct upipe_ts_pesd {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** next uref to be processed */
    struct uref *next_uref;
    /** size of next uref */
    size_t next_uref_size;
    /** size of next PES */
    size_t next_pes_size;
    /** true if we have thrown the sync_acquired event */
    bool acquired;
    /** true if subsequent (non-start) packets have to be dropped */
    bool drop;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_pesd, upipe, UPIPE_TS_PESD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_pesd, urefcount, upipe_ts_pesd_free)
UPIPE_HELPER_VOID(upipe_ts_pesd)
UPIPE_HELPER_SYNC(upipe_ts_pesd, acquired)
UPIPE_HELPER_OUTPUT(upipe_ts_pesd, output, flow_def, output_state, request_list)

/** @internal @This allocates a ts_pesd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_pesd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_pesd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    upipe_ts_pesd_init_urefcount(upipe);
    upipe_ts_pesd_init_sync(upipe);
    upipe_ts_pesd_init_output(upipe);
    upipe_ts_pesd->drop = true;
    upipe_ts_pesd->next_uref = NULL;
    upipe_ts_pesd->next_uref_size = 0;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This flushes all input buffers.
 *
 * @param upipe description structure of the pipe
 * @param lost true if the sync was lost
 */
static void upipe_ts_pesd_flush(struct upipe *upipe, bool lost)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    if (upipe_ts_pesd->next_uref != NULL) {
        uref_free(upipe_ts_pesd->next_uref);
        upipe_ts_pesd->next_uref = NULL;
        upipe_ts_pesd->next_uref_size = 0;
    }
    if (lost)
        upipe_ts_pesd_sync_lost(upipe);
    upipe_ts_pesd->drop = true;
}

/** @internal @This outputs a PES chunk, and checks if it is the end of the PES.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_pesd_check_output(struct upipe *upipe,
                                       struct upump **upump_p)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    upipe_ts_pesd_sync_acquired(upipe);
    upipe_ts_pesd->drop = false;
    if (upipe_ts_pesd->next_uref_size == upipe_ts_pesd->next_pes_size) {
        uref_block_set_end(upipe_ts_pesd->next_uref);
        upipe_ts_pesd->next_uref_size = upipe_ts_pesd->next_pes_size = 0;
    }
    upipe_ts_pesd_output(upipe, upipe_ts_pesd->next_uref, upump_p);
    upipe_ts_pesd->next_uref = NULL;
}

/** @internal @This parses and removes the PES header of a packet.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_pesd_decaps(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    uint8_t buffer[PES_HEADER_SIZE];
    const uint8_t *pes_header = uref_block_peek(upipe_ts_pesd->next_uref,
                                                0, PES_HEADER_SIZE, buffer);
    if (unlikely(pes_header == NULL))
        return;

    bool validate = pes_validate(pes_header);
    uint8_t streamid = pes_get_streamid(pes_header);
    uint16_t length = pes_get_length(pes_header);
    UBASE_FATAL(upipe, uref_block_peek_unmap(upipe_ts_pesd->next_uref, 0,
                                             buffer, pes_header))

    if (unlikely(!validate)) {
        upipe_warn(upipe, "wrong PES header");
        upipe_ts_pesd_flush(upipe, true);
        return;
    }

    if (unlikely(streamid == PES_STREAM_ID_PADDING)) {
        upipe_ts_pesd_flush(upipe, false);
        return;
    }

    if (length)
        upipe_ts_pesd->next_pes_size = length + PES_HEADER_SIZE;
    else
        upipe_ts_pesd->next_pes_size = 0;

    if (streamid == PES_STREAM_ID_PSM ||
        streamid == PES_STREAM_ID_PRIVATE_2 ||
        streamid == PES_STREAM_ID_ECM ||
        streamid == PES_STREAM_ID_EMM ||
        streamid == PES_STREAM_ID_PSD ||
        streamid == PES_STREAM_ID_DSMCC ||
        streamid == PES_STREAM_ID_H222_1_E) {
        UBASE_FATAL(upipe, uref_block_resize(upipe_ts_pesd->next_uref,
                                             PES_HEADER_SIZE, -1))
        upipe_ts_pesd_check_output(upipe, upump_p);
        return;
    }

    if (unlikely(length != 0 && length < PES_HEADER_OPTIONAL_SIZE)) {
        upipe_warn(upipe, "wrong PES length");
        upipe_ts_pesd_flush(upipe, true);
        return;
    }

    uint8_t buffer2[PES_HEADER_SIZE_NOPTS - PES_HEADER_SIZE];
    pes_header = uref_block_peek(upipe_ts_pesd->next_uref, PES_HEADER_SIZE,
                                 PES_HEADER_OPTIONAL_SIZE, buffer2);
    if (unlikely(pes_header == NULL))
        return;

    validate = pes_validate_header(pes_header - PES_HEADER_SIZE);
    bool has_pts = pes_has_pts(pes_header - PES_HEADER_SIZE);
    bool has_dts = pes_has_dts(pes_header - PES_HEADER_SIZE);
    uint8_t headerlength = pes_get_headerlength(pes_header - PES_HEADER_SIZE);
    UBASE_FATAL(upipe, uref_block_peek_unmap(upipe_ts_pesd->next_uref,
                PES_HEADER_SIZE, buffer2, pes_header))

    if (unlikely(!validate)) {
        upipe_warn(upipe, "wrong PES optional header");
        upipe_ts_pesd_flush(upipe, true);
        return;
    }

    if (unlikely((length != 0 &&
                  headerlength + PES_HEADER_OPTIONAL_SIZE > length) ||
                 (has_pts && headerlength < PES_HEADER_SIZE_PTS -
                                            PES_HEADER_SIZE_NOPTS) ||
                 (has_dts && headerlength < PES_HEADER_SIZE_PTSDTS -
                                            PES_HEADER_SIZE_NOPTS))) {
        upipe_warn(upipe, "wrong PES header length");
        upipe_ts_pesd_flush(upipe, true);
        return;
    }

    if (upipe_ts_pesd->next_uref_size < PES_HEADER_SIZE_NOPTS + headerlength)
        return;

    if (has_pts) {
        uint8_t buffer3[PES_HEADER_TS_SIZE * 2];
        uint64_t pts, dts;
        const uint8_t *ts_fields = uref_block_peek(upipe_ts_pesd->next_uref,
                PES_HEADER_SIZE_NOPTS, PES_HEADER_TS_SIZE * (has_dts ? 2 : 1),
                buffer3);
        if (unlikely(ts_fields == NULL)) {
            upipe_ts_pesd_flush(upipe, false);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        validate = pes_validate_pts(ts_fields - PES_HEADER_SIZE_NOPTS);
        pts = pes_get_pts(ts_fields - PES_HEADER_SIZE_NOPTS);
        if (has_dts) {
            validate = validate &&
                       pes_validate_dts(ts_fields - PES_HEADER_SIZE_NOPTS);
            dts = pes_get_dts(ts_fields - PES_HEADER_SIZE_NOPTS);
        } else
            dts = pts;
        UBASE_FATAL(upipe, uref_block_peek_unmap(upipe_ts_pesd->next_uref,
                                    PES_HEADER_SIZE_NOPTS, buffer3, ts_fields))

        if (unlikely(!validate)) {
            upipe_warn(upipe, "wrong PES timestamp syntax");
#if 0
            /* disable this because it is a common syntax error */
            upipe_ts_pesd_flush(upipe, true);
            return;
#endif
        }

        uint64_t dts_pts_delay = (POW2_33 + pts - dts) % POW2_33;
        dts_pts_delay *= UCLOCK_FREQ / 90000;
        if (dts_pts_delay > MAX_DELAY) {
            upipe_warn_va(upipe, "invalid PTS field (%"PRIu64" < %"PRIu64")",
                          pts, dts);
            dts_pts_delay = 0;
        }
        dts *= UCLOCK_FREQ / 90000;
        uref_clock_set_dts_orig(upipe_ts_pesd->next_uref, dts);
        uref_clock_set_dts_pts_delay(upipe_ts_pesd->next_uref, dts_pts_delay);
        upipe_throw_clock_ts(upipe, upipe_ts_pesd->next_uref);
    }

    UBASE_FATAL(upipe, uref_block_resize(upipe_ts_pesd->next_uref,
                            PES_HEADER_SIZE_NOPTS + headerlength, -1))
    upipe_ts_pesd_check_output(upipe, upump_p);
}

/** @internal @This takes the payload of a TS packet, checks if it may
 * contain part of a PES header, and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_pesd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    size_t uref_size;
    if (unlikely(!ubase_check(uref_block_size(uref, &uref_size)))) {
        upipe_warn(upipe, "invalid PES chunk");
        uref_free(uref);
        return;
    }

    if (ubase_check(uref_block_get_start(uref))) {
        if (unlikely(upipe_ts_pesd->next_uref != NULL)) {
            upipe_warn(upipe, "truncated PES header");
            uref_free(upipe_ts_pesd->next_uref);
        }
        upipe_ts_pesd->next_uref = uref;
        upipe_ts_pesd->next_uref_size = uref_size;
        upipe_ts_pesd_decaps(upipe, upump_p);

    } else if (upipe_ts_pesd->next_uref != NULL) {
        struct ubuf *ubuf = uref_detach_ubuf(uref);
        uref_free(uref);
        if (unlikely(!ubase_check(uref_block_append(upipe_ts_pesd->next_uref,
                                                    ubuf)))) {
            ubuf_free(ubuf);
            upipe_ts_pesd_flush(upipe, false);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        upipe_ts_pesd->next_uref_size += uref_size;
        upipe_ts_pesd_decaps(upipe, upump_p);
    } else if (likely(!upipe_ts_pesd->drop)) {
        upipe_ts_pesd->next_uref = uref;
        upipe_ts_pesd->next_uref_size += uref_size;
        upipe_ts_pesd_check_output(upipe, upump_p);
    } else
        uref_free(uref);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_pesd_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    if (unlikely(!ubase_check(uref_flow_set_def_va(flow_def_dup, "block.%s",
                                       def + strlen(EXPECTED_FLOW_DEF)))))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    upipe_ts_pesd_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts pesd pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_pesd_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_pesd_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_pesd_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pesd_free(struct upipe *upipe)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_ts_pesd_clean_output(upipe);
    upipe_ts_pesd_clean_sync(upipe);

    if (upipe_ts_pesd->next_uref != NULL)
        uref_free(upipe_ts_pesd->next_uref);
    upipe_ts_pesd_clean_urefcount(upipe);
    upipe_ts_pesd_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_pesd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_PESD_SIGNATURE,

    .upipe_alloc = upipe_ts_pesd_alloc,
    .upipe_input = upipe_ts_pesd_input,
    .upipe_control = upipe_ts_pesd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_pesd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pesd_mgr_alloc(void)
{
    return &upipe_ts_pesd_mgr;
}
