/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
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
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
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
/** 2^33 (max resolution of PCR, PTS and DTS) */
#define POW2_33 UINT64_C(8589934592)
/** ratio between Upipe freq and MPEG freq */
#define CLOCK_SCALE (UCLOCK_FREQ / 90000)

/** @hidden */
static bool upipe_ts_pese_handle(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p);
/** @hidden */
static int upipe_ts_pese_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_pese pipe. */
struct upipe_ts_pese {
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
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;
    /** latency in the input flow */
    uint64_t input_latency;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** PES stream ID */
    uint8_t pes_id;
    /** minimum PES header size */
    uint8_t pes_header_size;
    /** minimum PES duration */
    uint64_t pes_min_duration;

    /** buffered incomplete PES */
    struct uchain next_pes;
    /** size of the upcoming PES */
    size_t next_pes_size;
    /** duration of the upcoming PES */
    uint64_t next_pes_duration;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_pese, upipe, UPIPE_TS_PESE_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_pese, urefcount, upipe_ts_pese_free)
UPIPE_HELPER_VOID(upipe_ts_pese)

UPIPE_HELPER_OUTPUT(upipe_ts_pese, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_pese, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_pese_check,
                      upipe_ts_pese_register_output_request,
                      upipe_ts_pese_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_ts_pese, urefs, nb_urefs, max_urefs, blockers, upipe_ts_pese_handle)

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
    struct upipe *upipe = upipe_ts_pese_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_pese *upipe_ts_pese = upipe_ts_pese_from_upipe(upipe);
    upipe_ts_pese_init_urefcount(upipe);
    upipe_ts_pese_init_ubuf_mgr(upipe);
    upipe_ts_pese_init_output(upipe);
    upipe_ts_pese_init_input(upipe);
    upipe_ts_pese->input_latency = 0;
    upipe_ts_pese->pes_id = 0;
    upipe_ts_pese->pes_header_size = 0;
    upipe_ts_pese->pes_min_duration = 0;
    ulist_init(&upipe_ts_pese->next_pes);
    upipe_ts_pese->next_pes_size = 0;
    upipe_ts_pese->next_pes_duration = 0;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This prepends a PES header to a logical unit.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_pese_work(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_ts_pese *upipe_ts_pese = upipe_ts_pese_from_upipe(upipe);
    if (ulist_empty(&upipe_ts_pese->next_pes))
        return;

    uint64_t pts = UINT64_MAX, dts = UINT64_MAX;
    struct uref *uref = uref_from_uchain(ulist_pop(&upipe_ts_pese->next_pes));

    size_t header_size;
    if (upipe_ts_pese->pes_id != PES_STREAM_ID_PRIVATE_2) {
        uref_clock_get_pts_prog(uref, &pts);
        uref_clock_get_dts_prog(uref, &dts);
        if (pts != UINT64_MAX) {
            if (dts != UINT64_MAX &&
                ((pts / CLOCK_SCALE) % POW2_33) !=
                    ((dts / CLOCK_SCALE) % POW2_33))
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
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        goto upipe_ts_pese_work_err;
    }

    uint8_t *buffer;
    int size = -1;
    if (unlikely(!ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer)))) {
        ubuf_free(ubuf);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        goto upipe_ts_pese_work_err;
    }

    pes_init(buffer);
    pes_set_streamid(buffer, upipe_ts_pese->pes_id);
    size_t pes_length = upipe_ts_pese->next_pes_size + header_size -
                        PES_HEADER_SIZE;
    if (pes_length > UINT16_MAX) {
        if (unlikely((upipe_ts_pese->pes_id & PES_STREAM_ID_VIDEO_MPEG) != 
                     PES_STREAM_ID_VIDEO_MPEG))
            upipe_warn(upipe, "PES length > 65535 for a non-video stream");
        pes_set_length(buffer, 0);
    } else
        pes_set_length(buffer, pes_length);

    if (upipe_ts_pese->pes_id != PES_STREAM_ID_PRIVATE_2) {
        pes_set_headerlength(buffer, header_size - PES_HEADER_SIZE_NOPTS);
        pes_set_dataalignment(buffer);
        if (pts != UINT64_MAX) {
            pes_set_pts(buffer, (pts / CLOCK_SCALE) % POW2_33);
            if (dts != UINT64_MAX &&
                ((pts / CLOCK_SCALE) % POW2_33) !=
                    ((dts / CLOCK_SCALE) % POW2_33))
                pes_set_dts(buffer, (dts / CLOCK_SCALE) % POW2_33);
        }
    }
    ubuf_block_unmap(ubuf, 0);

    struct ubuf *payload = uref_detach_ubuf(uref);
    uref_attach_ubuf(uref, ubuf);
    if (unlikely(!ubase_check(uref_block_append(uref, payload)))) {
        uref_free(uref);
        ubuf_free(payload);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* intended pass-through */
upipe_ts_pese_work_err:
    uref_block_set_start(uref);
    upipe_ts_pese_output(upipe, uref, upump_p);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_ts_pese->next_pes)) != NULL)
        upipe_ts_pese_output(upipe, uref_from_uchain(uchain), upump_p);
    upipe_ts_pese->next_pes_size = 0;
    upipe_ts_pese->next_pes_duration = 0;
}

/** @internal @This merges the new access unit into a possibly existing
 * incomplete PES, and outputs the PES if possible.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_ts_pese_handle(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_ts_pese *upipe_ts_pese = upipe_ts_pese_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_ts_pese_work(upipe, NULL);

        uref_ts_flow_get_pes_id(uref, &upipe_ts_pese->pes_id);
        upipe_ts_pese->pes_header_size = 0;
        uref_ts_flow_get_pes_header(uref, &upipe_ts_pese->pes_header_size);
        upipe_ts_pese->pes_min_duration = 0;
        uref_ts_flow_get_pes_min_duration(uref,
                                          &upipe_ts_pese->pes_min_duration);
        upipe_ts_pese->input_latency = 0;
        uref_clock_get_latency(uref, &upipe_ts_pese->input_latency);
        if (unlikely(!ubase_check(uref_clock_set_latency(uref,
                                        upipe_ts_pese->input_latency +
                                        upipe_ts_pese->pes_min_duration))))
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        upipe_ts_pese_store_flow_def(upipe, NULL);
        upipe_ts_pese_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_ts_pese->flow_def == NULL)
        return false;

    uint64_t uref_duration = upipe_ts_pese->pes_min_duration;
    uref_clock_get_duration(uref, &uref_duration);
    size_t uref_size = 0;
    uref_block_size(uref, &uref_size);
    uref_block_delete_start(uref);

    ulist_add(&upipe_ts_pese->next_pes, uref_to_uchain(uref));
    upipe_ts_pese->next_pes_duration += uref_duration;
    upipe_ts_pese->next_pes_size += uref_size;

    if (upipe_ts_pese->next_pes_duration >= upipe_ts_pese->pes_min_duration)
        upipe_ts_pese_work(upipe, upump_p);
    return true;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_pese_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    if (!upipe_ts_pese_check_input(upipe)) {
        upipe_ts_pese_hold_input(upipe, uref);
        upipe_ts_pese_block_input(upipe, upump_p);
    } else if (!upipe_ts_pese_handle(upipe, uref, upump_p)) {
        upipe_ts_pese_hold_input(upipe, uref);
        upipe_ts_pese_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This checks if the input may start.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_pese_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_ts_pese *upipe_ts_pese = upipe_ts_pese_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_ts_pese_store_flow_def(upipe, flow_format);

    if (upipe_ts_pese->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_ts_pese_check_input(upipe);
    upipe_ts_pese_output_input(upipe);
    upipe_ts_pese_unblock_input(upipe);
    if (was_buffered && upipe_ts_pese_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_ts_pese_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_pese_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    uint8_t pes_id;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF) ||
        !ubase_check(uref_ts_flow_get_pes_id(flow_def, &pes_id)))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (unlikely(!ubase_check(uref_flow_set_def_va(flow_def_dup, "block.mpegtspes.%s",
                                       def + strlen(EXPECTED_FLOW_DEF)))))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts pese pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_pese_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_pese_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_pese_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pese_free(struct upipe *upipe)
{
    upipe_ts_pese_work(upipe, NULL);

    upipe_throw_dead(upipe);

    upipe_ts_pese_clean_input(upipe);
    upipe_ts_pese_clean_output(upipe);
    upipe_ts_pese_clean_ubuf_mgr(upipe);
    upipe_ts_pese_clean_urefcount(upipe);
    upipe_ts_pese_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_pese_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_PESE_SIGNATURE,

    .upipe_alloc = upipe_ts_pese_alloc,
    .upipe_input = upipe_ts_pese_input,
    .upipe_control = upipe_ts_pese_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_pese pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pese_mgr_alloc(void)
{
    return &upipe_ts_pese_mgr;
}
