/*
 * Copyright (C) 2014 Open Broadcast Systems Ltd
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
 * @short Upipe module building frames from an Opus stream
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uclock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe-framers/upipe_opus_framer.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#define OPUS_TS_HEADER     0x7FE0        // 0x3ff (11 bits)
#define OPUS_TS_MASK       0xFFE0        // top 11 bits

#define OPUS_FRAME_SAMPLES 960 // FIXME clarify variable frame sizes in the spec

/** @internal @This is the private context of an opusf pipe. */
struct upipe_opusf {
    /** refcount management structure */
    struct urefcount urefcount;

    /* output stuff */
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;
    /** input flow definition packet */
    struct uref *flow_def_input;
    /** attributes in the sequence header */
    struct uref *flow_def_attr;
    /** latency in the input flow */
    uint64_t input_latency;

    /* sync parsing stuff */
    /** number of samples per second */
    size_t samplerate;
    /** residue of the duration in 27 MHz units */
    uint64_t duration_residue;
    /** true we have had a discontinuity recently */
    bool got_discontinuity;

    /* octet stream stuff */
    /** next uref to be processed */
    struct uref *next_uref;
    /** original size of the next uref */
    size_t next_uref_size;
    /** urefs received after next uref */
    struct uchain urefs;

    /* octet stream parser stuff */
    /** current size of next frame (in next_uref) */
    ssize_t next_frame_size;
    int consume_bytes;
    /** pseudo-packet containing date information for the next picture */
    struct uref au_uref_s;
    /** true if we have thrown the sync_acquired event (that means we found a
     * sequence header) */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static void upipe_opusf_promote_uref(struct upipe *upipe);

UPIPE_HELPER_UPIPE(upipe_opusf, upipe, UPIPE_OPUSF_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_opusf, urefcount, upipe_opusf_free)
UPIPE_HELPER_VOID(upipe_opusf)
UPIPE_HELPER_SYNC(upipe_opusf, acquired)
UPIPE_HELPER_UREF_STREAM(upipe_opusf, next_uref, next_uref_size, urefs,
                         upipe_opusf_promote_uref)

UPIPE_HELPER_OUTPUT(upipe_opusf, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_opusf, flow_def_input, flow_def_attr)

/** @internal @This flushes all dates.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_opusf_flush_dates(struct upipe *upipe)
{
    struct upipe_opusf *upipe_opusf = upipe_opusf_from_upipe(upipe);
    uref_clock_set_date_sys(&upipe_opusf->au_uref_s, UINT64_MAX,
                            UREF_DATE_NONE);
    uref_clock_set_date_prog(&upipe_opusf->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_set_date_orig(&upipe_opusf->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_delete_dts_pts_delay(&upipe_opusf->au_uref_s);
}

/** @internal @This allocates an opusf pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_opusf_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_opusf_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_opusf *upipe_opusf = upipe_opusf_from_upipe(upipe);
    upipe_opusf_init_urefcount(upipe);
    upipe_opusf_init_sync(upipe);
    upipe_opusf_init_uref_stream(upipe);
    upipe_opusf_init_output(upipe);
    upipe_opusf_init_flow_def(upipe);
    upipe_opusf->input_latency = 0;
    upipe_opusf->samplerate = 0;
    upipe_opusf->got_discontinuity = false;
    upipe_opusf->next_frame_size = -1;
    uref_init(&upipe_opusf->au_uref_s);
    upipe_opusf_flush_dates(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This scans for a sync word.
 *
 * @param upipe description structure of the pipe
 * @param dropped_p filled with the number of octets to drop before the sync
 * @return true if a sync word was found
 */
static bool upipe_opusf_scan(struct upipe *upipe, size_t *dropped_p)
{
    struct upipe_opusf *upipe_opusf = upipe_opusf_from_upipe(upipe);
    while (ubase_check(uref_block_scan(upipe_opusf->next_uref, dropped_p, 0x7f))) {
        uint8_t header;
        if (!ubase_check(uref_block_extract(upipe_opusf->next_uref,
                                            *dropped_p+1, 1, &header)))
            return false;

        if ((header & 0xe0) == 0xe0)
            return true;
        (*dropped_p) += 2;
    }
    return false;
}

/** @internal @This checks if a sync word begins just after the end of the
 * next frame.
 *
 * @param upipe description structure of the pipe
 * @param ready_p filled with true if a sync word was found
 * @param false if no sync word was found and resync is needed
 */
static bool upipe_opusf_check_frame(struct upipe *upipe, bool *ready_p)
{
    struct upipe_opusf *upipe_opusf = upipe_opusf_from_upipe(upipe);
    size_t size;
    *ready_p = false;
    if (!ubase_check(uref_block_size(upipe_opusf->next_uref, &size)))
        return false;
    if (size < upipe_opusf->next_frame_size + upipe_opusf->consume_bytes)
        return true;

    uint8_t header[2];
    if (!ubase_check(uref_block_extract(upipe_opusf->next_uref,
                            upipe_opusf->next_frame_size + upipe_opusf->consume_bytes, 2, header))) {
        /* not enough data */
        if (upipe_opusf->acquired) {/* avoid delaying packets unnecessarily */
            *ready_p = true;
        }
        return true;
    }
    if (header[0] != 0x70 && (header[1] & 0xe0) != 0xe0) {
        return false;
    }

    *ready_p = true;
    return true;
}

/** @internal @This parses a new header.
 *
 * @param upipe description structure of the pipe
 * @return false in case the header is inconsistent
 */
static bool upipe_opusf_parse_header(struct upipe *upipe)
{
    struct upipe_opusf *upipe_opusf = upipe_opusf_from_upipe(upipe);
    int frame_size = 0, idx = 0;
    uint8_t size;
    do {
        if (!ubase_check(uref_block_extract(upipe_opusf->next_uref,
                                            idx + 2, 1, &size))) {
            return false;
        }
        frame_size += size;
        idx++;
    }
    while( size == 0xff );
    upipe_opusf->consume_bytes = idx+2 ;

    /* frame size */
    upipe_opusf->next_frame_size = frame_size;

    uint64_t octetrate = frame_size * 50; // FIXME inaccurate
    upipe_opusf->samplerate = 48000;

    struct uref *flow_def = upipe_opusf_alloc_flow_def_attr(upipe);
    if (unlikely(!flow_def)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.opus.sound."))
    UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def, octetrate))
    UBASE_FATAL(upipe, uref_sound_flow_set_rate(flow_def, upipe_opusf->samplerate))

    flow_def = upipe_opusf_store_flow_def_attr(upipe, flow_def);
    if (unlikely(!flow_def)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    upipe_opusf_store_flow_def(upipe, flow_def);

    return true;
}

/** @internal @This handles and outputs a frame.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_opusf_output_frame(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_opusf *upipe_opusf = upipe_opusf_from_upipe(upipe);

    struct uref au_uref_s = upipe_opusf->au_uref_s;
    /* From now on, PTS declaration only impacts the next frame. */
    upipe_opusf_flush_dates(upipe);

    upipe_opusf_consume_uref_stream(upipe, upipe_opusf->consume_bytes);

    struct uref *uref = upipe_opusf_extract_uref_stream(upipe,
            upipe_opusf->next_frame_size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    lldiv_t div = lldiv(OPUS_FRAME_SAMPLES * UCLOCK_FREQ +
                        upipe_opusf->duration_residue,
                        upipe_opusf->samplerate);
    uint64_t duration = div.quot;
    upipe_opusf->duration_residue = div.rem;

    /* We work on encoded data so in the DTS domain. Rebase on DTS. */
    uint64_t date;
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(&au_uref_s, &date))) {          \
        uref_clock_set_dts_##dv(uref, date);                                \
        uref_clock_set_dts_##dv(&upipe_opusf->au_uref_s, date + duration);   \
    } else if (ubase_check(uref_clock_get_dts_##dv(uref, &date)))           \
        uref_clock_set_date_##dv(uref, UINT64_MAX, UREF_DATE_NONE);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    uref_clock_set_dts_pts_delay(uref, 0);

    UBASE_FATAL(upipe, uref_clock_set_duration(uref, duration))

    if (upipe_opusf->got_discontinuity)
        uref_flow_set_discontinuity(uref);
    upipe_opusf->got_discontinuity = false;
    upipe_opusf_output(upipe, uref, upump_p);
}

/** @internal @This is called back by @ref upipe_opusf_append_uref_stream
 * whenever a new uref is promoted in next_uref.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_opusf_promote_uref(struct upipe *upipe)
{
    struct upipe_opusf *upipe_opusf = upipe_opusf_from_upipe(upipe);
    uint64_t date;
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(upipe_opusf->next_uref, &date))) \
        uref_clock_set_dts_##dv(&upipe_opusf->au_uref_s, date);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    upipe_opusf->duration_residue = 0;
}

/** @internal @This tries to output frames from the queue of input buffers.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_opusf_work(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_opusf *upipe_opusf = upipe_opusf_from_upipe(upipe);
    while (upipe_opusf->next_uref != NULL) {
        if (unlikely(!upipe_opusf->acquired)) {
            size_t dropped = 0;
            bool ret = upipe_opusf_scan(upipe, &dropped);
            upipe_opusf_consume_uref_stream(upipe, dropped);
            if (!ret)
                return;
        }
        if (upipe_opusf->next_frame_size == -1 &&
            !upipe_opusf_parse_header(upipe)) {
            upipe_warn(upipe, "invalid header");
            upipe_opusf_consume_uref_stream(upipe, 1);
            upipe_opusf_sync_lost(upipe);
            continue;
        }
        if (upipe_opusf->next_frame_size == -1)
            return; /* not enough data */

        bool ready;
        if (unlikely(!upipe_opusf_check_frame(upipe, &ready))) {
            upipe_warn(upipe, "invalid frame");
            upipe_opusf_consume_uref_stream(upipe, 1);
            upipe_opusf->next_frame_size = -1;
            upipe_opusf_sync_lost(upipe);
            continue;
        }

        if (!ready)
            return; /* not enough data */

        upipe_opusf_sync_acquired(upipe);
        upipe_opusf_output_frame(upipe, upump_p);
        upipe_opusf->next_frame_size = -1;
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_opusf_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_opusf *upipe_opusf = upipe_opusf_from_upipe(upipe);
    if (unlikely(uref->ubuf == NULL)) {
        upipe_opusf_output(upipe, uref, upump_p);
        return;
    }

    if (unlikely(ubase_check(uref_flow_get_discontinuity(uref)))) {
        /* Drop the current frame and resync. */
        upipe_opusf_clean_uref_stream(upipe);
        upipe_opusf_init_uref_stream(upipe);
        upipe_opusf->got_discontinuity = true;
        upipe_opusf->next_frame_size = -1;
        upipe_opusf_sync_lost(upipe);
    }

    upipe_opusf_append_uref_stream(upipe, uref);
    upipe_opusf_work(upipe, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_opusf_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "block."))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    struct upipe_opusf *upipe_opusf = upipe_opusf_from_upipe(upipe);
    upipe_opusf->input_latency = 0;
    uref_clock_get_latency(flow_def, &upipe_opusf->input_latency);

    if (unlikely(upipe_opusf->samplerate &&
                 !ubase_check(uref_clock_set_latency(flow_def_dup,
                                    upipe_opusf->input_latency +
                                    UCLOCK_FREQ * OPUS_FRAME_SAMPLES /
                                    upipe_opusf->samplerate))))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    flow_def = upipe_opusf_store_flow_def_input(upipe, flow_def_dup);
    if (flow_def != NULL)
        upipe_opusf_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a opusf pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_opusf_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_opusf_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_opusf_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_opusf_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_opusf_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_opusf_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_opusf_set_output(upipe, output);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_opusf_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_opusf_clean_uref_stream(upipe);
    upipe_opusf_clean_output(upipe);
    upipe_opusf_clean_flow_def(upipe);
    upipe_opusf_clean_sync(upipe);

    upipe_opusf_clean_urefcount(upipe);
    upipe_opusf_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_opusf_mgr = {
    .refcount = NULL,
    .signature = UPIPE_OPUSF_SIGNATURE,

    .upipe_alloc = upipe_opusf_alloc,
    .upipe_input = upipe_opusf_input,
    .upipe_control = upipe_opusf_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all opusf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_opusf_mgr_alloc(void)
{
    return &upipe_opusf_mgr;
}
