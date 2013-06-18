/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module decapsulating (removing PES header) TS packets
 * containing PES headers
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
#include <upipe/upipe_helper_flow.h>
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

/** @internal @This is the private context of a ts_pesd pipe. */
struct upipe_ts_pesd {
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** next uref to be processed */
    struct uref *next_uref;
    /** true if we have thrown the sync_acquired event */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_pesd, upipe)
UPIPE_HELPER_FLOW(upipe_ts_pesd, EXPECTED_FLOW_DEF)
UPIPE_HELPER_SYNC(upipe_ts_pesd, acquired)

UPIPE_HELPER_OUTPUT(upipe_ts_pesd, output, flow_def, flow_def_sent)

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
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_pesd_alloc_flow(mgr, uprobe, signature,
                                                   args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    upipe_ts_pesd_init_sync(upipe);
    upipe_ts_pesd_init_output(upipe);
    upipe_ts_pesd->next_uref = NULL;
    upipe_throw_ready(upipe);

    const char *def;
    if (unlikely(!uref_flow_get_def(flow_def, &def) ||
                 !uref_flow_set_def_va(flow_def, "block.%s",
                                       def + strlen(EXPECTED_FLOW_DEF))))
        upipe_throw_aerror(upipe);
    upipe_ts_pesd_store_flow_def(upipe, flow_def);
    return upipe;
}

/** @internal @This flushes all input buffers.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pesd_flush(struct upipe *upipe)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    if (upipe_ts_pesd->next_uref != NULL) {
        uref_free(upipe_ts_pesd->next_uref);
        upipe_ts_pesd->next_uref = NULL;
    }
    upipe_ts_pesd_sync_lost(upipe);
}

/** @internal @This parses and removes the PES header of a packet.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_ts_pesd_decaps(struct upipe *upipe, struct upump *upump)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    bool ret;
    uint8_t buffer[PES_HEADER_SIZE];
    const uint8_t *pes_header = uref_block_peek(upipe_ts_pesd->next_uref,
                                                0, PES_HEADER_SIZE, buffer);
    if (unlikely(pes_header == NULL))
        return;

    bool validate = pes_validate(pes_header);
    uint8_t streamid = pes_get_streamid(pes_header);
    uint16_t length = pes_get_length(pes_header);
    ret = uref_block_peek_unmap(upipe_ts_pesd->next_uref, 0, buffer,
                                pes_header);
    assert(ret);

    if (unlikely(!validate)) {
        upipe_warn(upipe, "wrong PES header");
        upipe_ts_pesd_flush(upipe);
        return;
    }

    if (unlikely(streamid == PES_STREAM_ID_PADDING)) {
        upipe_ts_pesd_flush(upipe);
        return;
    }

    if (streamid == PES_STREAM_ID_PSM ||
        streamid == PES_STREAM_ID_PRIVATE_2 ||
        streamid == PES_STREAM_ID_ECM ||
        streamid == PES_STREAM_ID_EMM ||
        streamid == PES_STREAM_ID_PSD ||
        streamid == PES_STREAM_ID_DSMCC ||
        streamid == PES_STREAM_ID_H222_1_E) {
        ret = uref_block_resize(upipe_ts_pesd->next_uref, PES_HEADER_SIZE, -1);
        assert(ret);
        upipe_ts_pesd_sync_acquired(upipe);
        upipe_ts_pesd_output(upipe, upipe_ts_pesd->next_uref, upump);
        upipe_ts_pesd->next_uref = NULL;
        return;
    }

    if (unlikely(length != 0 && length < PES_HEADER_OPTIONAL_SIZE)) {
        upipe_warn(upipe, "wrong PES length");
        upipe_ts_pesd_flush(upipe);
        return;
    }

    uint8_t buffer2[PES_HEADER_SIZE_NOPTS - PES_HEADER_SIZE];
    pes_header = uref_block_peek(upipe_ts_pesd->next_uref, PES_HEADER_SIZE,
                                 PES_HEADER_OPTIONAL_SIZE, buffer2);
    if (unlikely(pes_header == NULL))
        return;

    validate = pes_validate_header(pes_header - PES_HEADER_SIZE);
    bool alignment = pes_get_dataalignment(pes_header - PES_HEADER_SIZE);
    bool has_pts = pes_has_pts(pes_header - PES_HEADER_SIZE);
    bool has_dts = pes_has_dts(pes_header - PES_HEADER_SIZE);
    uint8_t headerlength = pes_get_headerlength(pes_header - PES_HEADER_SIZE);
    ret = uref_block_peek_unmap(upipe_ts_pesd->next_uref, PES_HEADER_SIZE,
                                buffer2, pes_header);
    assert(ret);

    if (unlikely(!validate)) {
        upipe_warn(upipe, "wrong PES optional header");
        upipe_ts_pesd_flush(upipe);
        return;
    }

    if (unlikely((length != 0 &&
                  headerlength + PES_HEADER_OPTIONAL_SIZE > length) ||
                 (has_pts && headerlength < PES_HEADER_SIZE_PTS -
                                            PES_HEADER_SIZE_NOPTS) ||
                 (has_dts && headerlength < PES_HEADER_SIZE_PTSDTS -
                                            PES_HEADER_SIZE_NOPTS))) {
        upipe_warn(upipe, "wrong PES header length");
        upipe_ts_pesd_flush(upipe);
        return;
    }

    size_t gathered_size;
    if (unlikely(!uref_block_size(upipe_ts_pesd->next_uref, &gathered_size))) {
        upipe_throw_aerror(upipe);
        upipe_ts_pesd_flush(upipe);
        return;
    }
    if (gathered_size < PES_HEADER_SIZE_NOPTS + headerlength)
        return;

    if (has_dts || has_pts) {
        uint8_t buffer3[PES_HEADER_TS_SIZE * 2];
        uint64_t pts, dts;
        const uint8_t *ts_fields = uref_block_peek(upipe_ts_pesd->next_uref,
                PES_HEADER_SIZE_NOPTS, PES_HEADER_TS_SIZE * (has_dts ? 2 : 1),
                buffer3);
        if (unlikely(ts_fields == NULL)) {
            upipe_throw_aerror(upipe);
            upipe_ts_pesd_flush(upipe);
            return;
        }

        validate = pes_validate_pts(ts_fields - PES_HEADER_SIZE_NOPTS);
        pts = pes_get_pts(ts_fields - PES_HEADER_SIZE_NOPTS);
        if (has_dts) {
            validate = validate &&
                       pes_validate_dts(ts_fields - PES_HEADER_SIZE_NOPTS);
            dts = pes_get_dts(ts_fields - PES_HEADER_SIZE_NOPTS);
            if (dts > pts)
                dts = pts;
        } else
            dts = pts;
        ret = uref_block_peek_unmap(upipe_ts_pesd->next_uref,
                                    PES_HEADER_SIZE_NOPTS, buffer3, ts_fields);
        assert(ret);

        if (unlikely(!validate)) {
            upipe_warn(upipe, "wrong PES timestamp syntax");
            upipe_ts_pesd_flush(upipe);
            return;
        }

        pts *= UCLOCK_FREQ / 90000;
        dts *= UCLOCK_FREQ / 90000;
        if (unlikely(!uref_clock_set_pts_orig(upipe_ts_pesd->next_uref, pts) ||
                     !uref_clock_set_dts_orig(upipe_ts_pesd->next_uref, dts))) {
            upipe_ts_pesd_flush(upipe);
            upipe_throw_aerror(upipe);
            return;
        }
        upipe_throw_clock_ts(upipe, upipe_ts_pesd->next_uref);
    }

    if (unlikely((alignment &&
                  !uref_block_set_start(upipe_ts_pesd->next_uref)) ||
                 (!alignment &&
                  !uref_block_delete_start(upipe_ts_pesd->next_uref)))) {
        upipe_ts_pesd_flush(upipe);
        upipe_throw_aerror(upipe);
        return;
    }

    ret = uref_block_resize(upipe_ts_pesd->next_uref,
                            PES_HEADER_SIZE_NOPTS + headerlength, -1);
    assert(ret);
    upipe_ts_pesd_sync_acquired(upipe);
    upipe_ts_pesd_output(upipe, upipe_ts_pesd->next_uref, upump);
    upipe_ts_pesd->next_uref = NULL;
}

/** @internal @This takes the payload of a TS packet, checks if it may
 * contain part of a PES header, and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_pesd_work(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    if (unlikely(uref_flow_get_discontinuity(uref)))
        upipe_ts_pesd_flush(upipe);
    if (uref_block_get_start(uref)) {
        if (unlikely(upipe_ts_pesd->next_uref != NULL)) {
            upipe_warn(upipe, "truncated PES header");
            uref_free(upipe_ts_pesd->next_uref);
        }
        upipe_ts_pesd->next_uref = uref;
        upipe_ts_pesd_decaps(upipe, upump);
    } else if (upipe_ts_pesd->next_uref != NULL) {
        struct ubuf *ubuf = uref_detach_ubuf(uref);
        uref_free(uref);
        if (unlikely(!uref_block_append(upipe_ts_pesd->next_uref, ubuf))) {
            ubuf_free(ubuf);
            upipe_ts_pesd_flush(upipe);
            upipe_throw_aerror(upipe);
            return;
        }
        upipe_ts_pesd_decaps(upipe, upump);
    } else if (likely(upipe_ts_pesd->acquired))
        upipe_ts_pesd_output(upipe, uref, upump);
    else
        uref_free(uref);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_pesd_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    if (unlikely(uref->ubuf == NULL)) {
        upipe_ts_pesd_output(upipe, uref, upump);
        return;
    }

    upipe_ts_pesd_work(upipe, uref, upump);
}

/** @internal @This processes control commands on a ts pesd pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_pesd_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_pesd_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_pesd_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_pesd_set_output(upipe, output);
        }
        default:
            return false;
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

    upipe_clean(upipe);
    free(upipe_ts_pesd);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_pesd_mgr = {
    .signature = UPIPE_TS_PESD_SIGNATURE,

    .upipe_alloc = upipe_ts_pesd_alloc,
    .upipe_input = upipe_ts_pesd_input,
    .upipe_control = upipe_ts_pesd_control,
    .upipe_free = upipe_ts_pesd_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all ts_pesd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pesd_mgr_alloc(void)
{
    return &upipe_ts_pesd_mgr;
}
