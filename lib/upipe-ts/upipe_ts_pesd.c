/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_linear_output.h>
#include <upipe-ts/upipe_ts_pesd.h>

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
    /** true if we have thrown the ready event */
    bool ready;
    /** true if we have thrown the sync_acquired event */
    bool acquired;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_pesd, upipe)

UPIPE_HELPER_LINEAR_OUTPUT(upipe_ts_pesd, output, flow_def, flow_def_sent)

/** @internal @This allocates a ts_pesd pipe.
 *
 * @param mgr common management structure
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_pesd_alloc(struct upipe_mgr *mgr)
{
    struct upipe_ts_pesd *upipe_ts_pesd = malloc(sizeof(struct upipe_ts_pesd));
    if (unlikely(upipe_ts_pesd == NULL))
        return NULL;
    struct upipe *upipe = upipe_ts_pesd_to_upipe(upipe_ts_pesd);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_TS_PESD_SIGNATURE;
    urefcount_init(&upipe_ts_pesd->refcount);
    upipe_ts_pesd_init_output(upipe);
    upipe_ts_pesd->next_uref = NULL;
    upipe_ts_pesd->ready = false;
    upipe_ts_pesd->acquired = false;
    return upipe;
}

/** @internal @This sends the pesd_lost event if it has not already been sent.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pesd_lost(struct upipe *upipe)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    if (upipe_ts_pesd->acquired) {
        upipe_ts_pesd->acquired = false;
        upipe_throw_sync_lost(upipe);
    }
}

/** @internal @This sends the pesd_acquired event if it has not already been
 * sent.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pesd_acquired(struct upipe *upipe)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    if (!upipe_ts_pesd->acquired) {
        upipe_ts_pesd->acquired = true;
        upipe_throw_sync_acquired(upipe);
    }
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
    upipe_ts_pesd_lost(upipe);
}

/** @internal @This parses and removes the PES header of a packet.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pesd_decaps(struct upipe *upipe)
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
    ret = uref_block_peek_unmap(upipe_ts_pesd->next_uref, 0, PES_HEADER_SIZE,
                                buffer, pes_header);
    assert(ret);

    if (unlikely(!validate)) {
        ulog_warning(upipe->ulog, "wrong PES header");
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
        upipe_ts_pesd_acquired(upipe);
        upipe_ts_pesd_output(upipe, upipe_ts_pesd->next_uref);
        upipe_ts_pesd->next_uref = NULL;
        return;
    }

    if (unlikely(length != 0 && length < PES_HEADER_OPTIONAL_SIZE)) {
        ulog_warning(upipe->ulog, "wrong PES length");
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
                                PES_HEADER_OPTIONAL_SIZE, buffer2, pes_header);
    assert(ret);

    if (unlikely(!validate)) {
        ulog_warning(upipe->ulog, "wrong PES optional header");
        upipe_ts_pesd_flush(upipe);
        return;
    }

    if (unlikely(headerlength + PES_HEADER_OPTIONAL_SIZE > length ||
                 (has_pts && headerlength < PES_HEADER_SIZE_PTS -
                                            PES_HEADER_SIZE_NOPTS) ||
                 (has_dts && headerlength < PES_HEADER_SIZE_PTSDTS -
                                            PES_HEADER_SIZE_NOPTS))) {
        ulog_warning(upipe->ulog, "wrong PES header length");
        upipe_ts_pesd_flush(upipe);
        return;
    }

    size_t gathered_size;
    if (unlikely(!uref_block_size(upipe_ts_pesd->next_uref, &gathered_size))) {
        ulog_aerror(upipe->ulog);
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
            ulog_aerror(upipe->ulog);
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
        } else
            dts = pts;
        ret = uref_block_peek_unmap(upipe_ts_pesd->next_uref,
                PES_HEADER_SIZE_NOPTS, PES_HEADER_TS_SIZE * (has_dts ? 2 : 1),
                buffer3, ts_fields);
        assert(ret);

        if (unlikely(!validate)) {
            ulog_warning(upipe->ulog, "wrong PES timestamp syntax");
            upipe_ts_pesd_flush(upipe);
            return;
        }

        if (unlikely(!uref_clock_set_pts_orig(upipe_ts_pesd->next_uref, pts))) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            upipe_ts_pesd_flush(upipe);
            return;
        }
        if (unlikely(pts > dts &&
                     !uref_clock_set_dtsdelay(upipe_ts_pesd->next_uref,
                                              pts - dts))) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            upipe_ts_pesd_flush(upipe);
            return;
        }
    }

    if (unlikely((alignment &&
                  !uref_block_set_start(upipe_ts_pesd->next_uref)) ||
                 (!alignment &&
                  !uref_block_delete_start(upipe_ts_pesd->next_uref)))) {
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        upipe_ts_pesd_flush(upipe);
        return;
    }

    ret = uref_block_resize(upipe_ts_pesd->next_uref,
                            PES_HEADER_SIZE_NOPTS + headerlength, -1);
    assert(ret);
    upipe_ts_pesd_acquired(upipe);
    upipe_ts_pesd_output(upipe, upipe_ts_pesd->next_uref);
    upipe_ts_pesd->next_uref = NULL;
}

/** @internal @This takes the payload of a TS packet, checks if it may
 * contain part of a PES header, and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_ts_pesd_work(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    if (unlikely(uref_block_get_discontinuity(uref)))
        upipe_ts_pesd_flush(upipe);
    if (uref_block_get_start(uref)) {
        if (unlikely(upipe_ts_pesd->next_uref != NULL)) {
            ulog_warning(upipe->ulog, "truncated PES header");
            uref_free(upipe_ts_pesd->next_uref);
        }
        upipe_ts_pesd->next_uref = uref;
        upipe_ts_pesd_decaps(upipe);
    } else if (upipe_ts_pesd->next_uref != NULL) {
        struct ubuf *ubuf = uref_detach_ubuf(uref);
        uref_free(uref);
        if (unlikely(!uref_block_append(upipe_ts_pesd->next_uref, ubuf))) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            ubuf_free(ubuf);
            upipe_ts_pesd_flush(upipe);
            return;
        }
        upipe_ts_pesd_decaps(upipe);
    } else if (likely(upipe_ts_pesd->acquired))
        upipe_ts_pesd_output(upipe, uref);
    else
        uref_free(uref);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_ts_pesd_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);

    const char *flow, *def, *def_flow;
    if (unlikely(!uref_flow_get_name(uref, &flow))) {
       ulog_warning(upipe->ulog, "received a buffer outside of a flow");
       uref_free(uref);
       return false;
    }

    if (unlikely(uref_flow_get_delete(uref))) {
        upipe_ts_pesd_set_flow_def(upipe, NULL);
        uref_free(uref);
        upipe_ts_pesd_flush(upipe);
        return true;
    }

    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(upipe_ts_pesd->flow_def != NULL))
            ulog_warning(upipe->ulog,
                         "received flow definition without delete first");
        upipe_ts_pesd_flush(upipe);

        if (unlikely(strncmp(def, EXPECTED_FLOW_DEF,
                             strlen(EXPECTED_FLOW_DEF)))) {
            ulog_warning(upipe->ulog,
                         "received an incompatible flow definition");
            uref_free(uref);
            upipe_ts_pesd_set_flow_def(upipe, NULL);
            return false;
        }

        ulog_debug(upipe->ulog, "flow definition for %s: %s", flow, def);
        uref_flow_set_def_va(uref, "block.%s", def + strlen(EXPECTED_FLOW_DEF));
        upipe_ts_pesd_set_flow_def(upipe, uref);
        return true;
    }

    if (unlikely(upipe_ts_pesd->flow_def == NULL)) {
        ulog_warning(upipe->ulog, "pipe has no registered input flow");
        uref_free(uref);
        return false;
    }

    bool ret = uref_flow_get_name(upipe_ts_pesd->flow_def, &def_flow);
    assert(ret);
    if (unlikely(strcmp(def_flow, flow))) {
        ulog_warning(upipe->ulog,
                     "received a buffer not matching the current flow");
        uref_free(uref);
        return false;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return true;
    }

    upipe_ts_pesd_work(upipe, uref);
    return true;
}

/** @internal @This processes control commands on a ts pesd pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_ts_pesd_control(struct upipe *upipe,
                                   enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_LINEAR_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_pesd_get_output(upipe, p);
        }
        case UPIPE_LINEAR_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_pesd_set_output(upipe, output);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on a ts pesd pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_pesd_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    if (likely(command == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        return upipe_ts_pesd_input(upipe, uref);
    }

    if (unlikely(!_upipe_ts_pesd_control(upipe, command, args)))
        return false;

    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    if (likely(!upipe_ts_pesd->ready)) {
        upipe_ts_pesd->ready = true;
        upipe_throw_ready(upipe);
    }

    return true;
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pesd_use(struct upipe *upipe)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    urefcount_use(&upipe_ts_pesd->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pesd_release(struct upipe *upipe)
{
    struct upipe_ts_pesd *upipe_ts_pesd = upipe_ts_pesd_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_ts_pesd->refcount))) {
        upipe_ts_pesd_clean_output(upipe);

        if (upipe_ts_pesd->next_uref != NULL)
            uref_free(upipe_ts_pesd->next_uref);

        upipe_clean(upipe);
        urefcount_clean(&upipe_ts_pesd->refcount);
        free(upipe_ts_pesd);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_pesd_mgr = {
    .upipe_alloc = upipe_ts_pesd_alloc,
    .upipe_control = upipe_ts_pesd_control,
    .upipe_use = upipe_ts_pesd_use,
    .upipe_release = upipe_ts_pesd_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all ts_pesds
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pesd_mgr_alloc(void)
{
    return &upipe_ts_pesd_mgr;
}
