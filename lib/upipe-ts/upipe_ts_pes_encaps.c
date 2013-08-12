/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module encapsulating access units into PES packets
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
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_pes_encaps.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/pes.h>

/** we only accept blocks */
#define EXPECTED_FLOW_DEF "block."

/** @internal @This is the private context of a ts_pese pipe. */
struct upipe_ts_pese {
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** PES stream ID */
    uint8_t pes_id;
    /** minimum PES header size */
    uint8_t pes_header_size;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_pese, upipe)
UPIPE_HELPER_FLOW(upipe_ts_pese, EXPECTED_FLOW_DEF)
UPIPE_HELPER_UBUF_MGR(upipe_ts_pese, ubuf_mgr)

UPIPE_HELPER_OUTPUT(upipe_ts_pese, output, flow_def, flow_def_sent)

/** @internal @This allocates a ts_pese pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_pese_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_pese_alloc_flow(mgr, uprobe, signature,
                                                   args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    uint8_t pes_id;
    if (!uref_ts_flow_get_pes_id(flow_def, &pes_id)) {
        uref_free(flow_def);
        upipe_ts_pese_free_flow(upipe);
        return NULL;
    }

    struct upipe_ts_pese *upipe_ts_pese = upipe_ts_pese_from_upipe(upipe);
    upipe_ts_pese_init_ubuf_mgr(upipe);
    upipe_ts_pese_init_output(upipe);
    upipe_ts_pese->pes_id = pes_id;
    upipe_ts_pese->pes_header_size = 0;
    uref_ts_flow_get_pes_header(flow_def, &upipe_ts_pese->pes_header_size);
    upipe_throw_ready(upipe);

    const char *def;
    if (unlikely(!uref_flow_get_def(flow_def, &def) ||
                 !uref_flow_set_def_va(flow_def, "block.mpegtspes.%s",
                                       def + strlen(EXPECTED_FLOW_DEF))))
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
    upipe_ts_pese_store_flow_def(upipe, flow_def);
    return upipe;
}

/** @internal @This takes the payload of a TS packet, checks if it may
 * contain part of a PES header, and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_pese_work(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_ts_pese *upipe_ts_pese = upipe_ts_pese_from_upipe(upipe);
    uint64_t pts = UINT64_MAX, dts = UINT64_MAX;
    size_t uref_size;
    if (!uref_block_size(uref, &uref_size) || !uref_size) {
        upipe_warn_va(upipe, "empty packet received");
        uref_free(uref);
        return;
    }

    size_t header_size;
    if (upipe_ts_pese->pes_id != PES_STREAM_ID_PRIVATE_2) {
        uref_clock_get_pts(uref, &pts);
        uref_clock_get_dts(uref, &dts);
        if (pts != UINT64_MAX) {
            if (dts != UINT64_MAX && (pts / 300) != (dts / 300))
                header_size = PES_HEADER_SIZE_PTSDTS;
            else
                header_size = PES_HEADER_SIZE_PTS;
        } else
            header_size = PES_HEADER_SIZE_NOPTS;
    } else
        header_size = PES_HEADER_SIZE;
    if (header_size < upipe_ts_pese->pes_header_size)
        header_size = upipe_ts_pese->pes_header_size;

    struct ubuf *ubuf = ubuf_block_alloc(upipe_ts_pese->ubuf_mgr, header_size);
    if (unlikely(ubuf == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    uint8_t *buffer;
    int size = -1;
    if (!ubuf_block_write(ubuf, 0, &size, &buffer)) {
        uref_free(uref);
        ubuf_free(ubuf);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    pes_init(buffer);
    pes_set_streamid(buffer, upipe_ts_pese->pes_id);
    if (uref_size + header_size - PES_HEADER_SIZE > UINT16_MAX) {
        if (unlikely((upipe_ts_pese->pes_id & PES_STREAM_ID_VIDEO_MPEG) != 
                     PES_STREAM_ID_VIDEO_MPEG))
            upipe_warn(upipe, "PES size > 65535 for a non-video stream");
        pes_set_length(buffer, 0);
    } else
        pes_set_length(buffer, uref_size + header_size - PES_HEADER_SIZE);

    if (upipe_ts_pese->pes_id != PES_STREAM_ID_PRIVATE_2) {
        pes_set_headerlength(buffer, header_size - PES_HEADER_SIZE_NOPTS);
        pes_set_dataalignment(buffer);
        if (pts != UINT64_MAX) {
            pes_set_pts(buffer, pts / 300);
            if (dts != UINT64_MAX && (pts / 300) != (dts / 300))
                pes_set_dts(buffer, dts / 300);
        }
    }
    ubuf_block_unmap(ubuf, 0);

    struct ubuf *payload = uref_detach_ubuf(uref);
    uref_attach_ubuf(uref, ubuf);
    if (unlikely(!uref_block_append(uref, payload))) {
        uref_free(uref);
        ubuf_free(payload);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    upipe_ts_pese_output(upipe, uref, upump);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_pese_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_ts_pese *upipe_ts_pese = upipe_ts_pese_from_upipe(upipe);
    if (unlikely(uref->ubuf == NULL)) {
        upipe_ts_pese_output(upipe, uref, upump);
        return;
    }

    if (unlikely(upipe_ts_pese->ubuf_mgr == NULL))
        upipe_throw_need_ubuf_mgr(upipe, upipe_ts_pese->flow_def);
    if (unlikely(upipe_ts_pese->ubuf_mgr == NULL)) {
        uref_free(uref);
        return;
    }

    upipe_ts_pese_work(upipe, uref, upump);
}

/** @internal @This processes control commands on a ts pese pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_pese_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_ts_pese_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_ts_pese_set_ubuf_mgr(upipe, ubuf_mgr);
        }

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_pese_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_pese_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_pese_set_output(upipe, output);
        }
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pese_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_pese_clean_output(upipe);
    upipe_ts_pese_clean_ubuf_mgr(upipe);
    upipe_ts_pese_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_pese_mgr = {
    .signature = UPIPE_TS_PESE_SIGNATURE,

    .upipe_alloc = upipe_ts_pese_alloc,
    .upipe_input = upipe_ts_pese_input,
    .upipe_control = upipe_ts_pese_control,
    .upipe_free = upipe_ts_pese_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all ts_pese pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pese_mgr_alloc(void)
{
    return &upipe_ts_pese_mgr;
}
