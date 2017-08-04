/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
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
 * @short Upipe module building frames from chunks of a SMPTE 337 stream
 * This pipe only supports the 16-bit mode.
 *
 * Normative references:
 *  - SMPTE 337-2008 (non-PCM in AES3)
 *  - SMPTE 338-2008 (non-PCM in AES3 - data types)
 *  - SMPTE 340-2008 (non-PCM in AES3 - ATSC A/52B)
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
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe-framers/upipe_s337_framer.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/smpte/337.h>

/** @internal @This is the private context of an s337f pipe. */
struct upipe_s337f {
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

    /* sync parsing stuff */
    /** sample rate */
    uint64_t sample_rate;
    /** data type of the last non-discarded frame */
    uint8_t data_type;
    /** data stream of the last non-discarded frame */
    uint8_t data_stream;

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
    /** true if next frame payload should be discarded */
    bool next_frame_discard;
    /** true if we have thrown the sync_acquired event (that means we found a
     * header) */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_s337f, upipe, UPIPE_S337F_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_s337f, urefcount, upipe_s337f_free)
UPIPE_HELPER_VOID(upipe_s337f)
UPIPE_HELPER_SYNC(upipe_s337f, acquired)
UPIPE_HELPER_UREF_STREAM(upipe_s337f, next_uref, next_uref_size, urefs, NULL)

UPIPE_HELPER_OUTPUT(upipe_s337f, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_s337f, flow_def_input, flow_def_attr)

/** @internal @This allocates an s337f pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_s337f_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_s337f_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_s337f *upipe_s337f = upipe_s337f_from_upipe(upipe);
    upipe_s337f_init_urefcount(upipe);
    upipe_s337f_init_sync(upipe);
    upipe_s337f_init_uref_stream(upipe);
    upipe_s337f_init_output(upipe);
    upipe_s337f_init_flow_def(upipe);
    upipe_s337f->data_type = UINT8_MAX;
    upipe_s337f->data_stream = UINT8_MAX;
    upipe_s337f->next_frame_size = -1;
    upipe_s337f->next_frame_discard = true;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This scans for a sync word.
 *
 * @param upipe description structure of the pipe
 * @param dropped_p filled with the number of octets to drop before the sync
 * @return true if a sync word was found
 */
static bool upipe_s337f_scan(struct upipe *upipe, size_t *dropped_p)
{
    struct upipe_s337f *upipe_s337f = upipe_s337f_from_upipe(upipe);
    while (ubase_check(uref_block_scan(upipe_s337f->next_uref, dropped_p,
                                       S337_PREAMBLE_A1))) {
        uint8_t preamble[3];
        if (!ubase_check(uref_block_extract(upipe_s337f->next_uref,
                        *dropped_p + 1, 3, preamble)))
            return false;

        if (preamble[0] == S337_PREAMBLE_A2 &&
            preamble[1] == S337_PREAMBLE_B1 &&
            preamble[2] == S337_PREAMBLE_B2)
            return true;
        (*dropped_p)++;
    }
    return false;
}

/** @internal @This checks if a burst is complete.
 *
 * @param upipe description structure of the pipe
 * @param true if the burst is complete
 */
static bool upipe_s337f_check_frame(struct upipe *upipe)
{
    struct upipe_s337f *upipe_s337f = upipe_s337f_from_upipe(upipe);
    size_t size;
    if (!ubase_check(uref_block_size(upipe_s337f->next_uref, &size))) {
        upipe_s337f->next_frame_discard = true;
        return true;
    }
    return size >= upipe_s337f->next_frame_size + S337_PREAMBLE_SIZE;
}

/** @internal @This parses a new s337 header.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_s337f_parse_preamble(struct upipe *upipe)
{
    struct upipe_s337f *upipe_s337f = upipe_s337f_from_upipe(upipe);
    uint8_t preamble[S337_PREAMBLE_SIZE];
    if (!ubase_check(uref_block_extract(upipe_s337f->next_uref, 0,
                    S337_PREAMBLE_SIZE, preamble)))
        return; /* not enough data */

    uint8_t data_type = s337_get_data_type(preamble);
    uint8_t data_mode = s337_get_data_mode(preamble);
    bool error = s337_get_error(preamble);
    uint8_t data_stream = s337_get_data_stream(preamble);
    uint8_t data_type_dep = s337_get_data_type_dep(preamble);
    upipe_s337f->next_frame_size = s337_get_length(preamble) + 7;
    upipe_s337f->next_frame_size /= 8;

    if (data_type != S337_TYPE_A52 && data_type != S337_TYPE_A52E) {
        upipe_s337f->next_frame_discard = true;
        return;
    }

    if (data_mode != S337_MODE_16) {
        upipe_err_va(upipe, "unsupported data mode (%"PRIu8")", data_mode);
        upipe_s337f->next_frame_discard = true;
        return;
    }

    upipe_s337f->next_frame_discard = false;
    if (error)
        upipe_warn(upipe, "error flag set");

    if (upipe_s337f->data_stream != data_stream) {
        upipe_dbg_va(upipe, "now following stream %"PRIu8, data_stream);
        upipe_s337f->data_stream = data_stream;
    }

    if (upipe_s337f->data_type == data_type)
        return;
    upipe_s337f->data_type = data_type;

    if (data_type_dep & S337_TYPE_A52_REP_RATE_FLAG)
        upipe_warn(upipe, "repetition rate flag set");
    if (data_type_dep & S337_TYPE_A52_NOT_FULL_SVC)
        upipe_warn(upipe, "not full service flag set");

    struct uref *flow_def = upipe_s337f_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    if (data_type == S337_TYPE_A52)
        UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.ac3.sound."))
    else
        UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.eac3.sound."))

    flow_def = upipe_s337f_store_flow_def_attr(upipe, flow_def);
    if (unlikely(!flow_def)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_s337f_store_flow_def(upipe, flow_def);
}

/** @internal @This handles and outputs a frame.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_s337f_output_frame(struct upipe *upipe,
                                     struct upump **upump_p)
{
    struct upipe_s337f *upipe_s337f = upipe_s337f_from_upipe(upipe);
    /* assume repetition rate and shift dts backwards */
    size_t samples = upipe_s337f->next_uref_size / 4;
    uint64_t dts_shift = (uint64_t)samples * UCLOCK_FREQ /
                         upipe_s337f->sample_rate;
    struct uref au_uref_s = *upipe_s337f->next_uref;

    /* consume burst preamble */
    upipe_s337f_consume_uref_stream(upipe, S337_PREAMBLE_SIZE);

    /* extract burst payload */
    struct uref *uref = upipe_s337f_extract_uref_stream(upipe,
            upipe_s337f->next_frame_size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint64_t date;
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(&au_uref_s, &date)))            \
        uref_clock_set_dts_##dv(uref, date - dts_shift);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    upipe_s337f_output(upipe, uref, upump_p);
}

/** @internal @This tries to output frames from the queue of input buffers.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_s337f_work(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_s337f *upipe_s337f = upipe_s337f_from_upipe(upipe);
    while (upipe_s337f->next_uref != NULL) {
        if (upipe_s337f->next_frame_size == -1) {
            size_t dropped = 0;
            bool ret = upipe_s337f_scan(upipe, &dropped);
            upipe_s337f_consume_uref_stream(upipe, dropped);
            if (!ret)
                return;
        }

        if (upipe_s337f->next_frame_size == -1) {
            upipe_s337f_parse_preamble(upipe);
            if (upipe_s337f->next_frame_size == -1)
                return; /* not enough data */
        }

        if (!upipe_s337f_check_frame(upipe))
            return;

        if (upipe_s337f->next_frame_discard) {
            upipe_s337f_consume_uref_stream(upipe,
                                            upipe_s337f->next_frame_size);
        } else {
            upipe_s337f_sync_acquired(upipe);
            upipe_s337f_output_frame(upipe, upump_p);
        }
        upipe_s337f->next_frame_size = -1;
        upipe_s337f->next_frame_discard = true;
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_s337f_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    upipe_s337f_append_uref_stream(upipe, uref);
    upipe_s337f_work(upipe, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_s337f_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    uint64_t rate;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 (ubase_ncmp(def, "block.s337.") &&
                  strcmp(def, "block.")) ||
                 !ubase_check(uref_sound_flow_get_rate(flow_def, &rate))))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    struct upipe_s337f *upipe_s337f = upipe_s337f_from_upipe(upipe);
    upipe_s337f->sample_rate = rate;
    flow_def = upipe_s337f_store_flow_def_input(upipe, flow_def_dup);
    if (flow_def != NULL)
        upipe_s337f_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a s337f pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_s337f_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_s337f_control_output(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_s337f_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_s337f_free(struct upipe *upipe)
{
    struct upipe_s337f *upipe_s337f = upipe_s337f_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_s337f_clean_uref_stream(upipe);
    upipe_s337f_clean_output(upipe);
    upipe_s337f_clean_flow_def(upipe);
    upipe_s337f_clean_sync(upipe);

    upipe_s337f_clean_urefcount(upipe);
    upipe_s337f_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_s337f_mgr = {
    .refcount = NULL,
    .signature = UPIPE_S337F_SIGNATURE,

    .upipe_alloc = upipe_s337f_alloc,
    .upipe_input = upipe_s337f_input,
    .upipe_control = upipe_s337f_control,

    .upipe_mgr_control = NULL

};

/** @This returns the management structure for all s337f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_s337f_mgr_alloc(void)
{
    return &upipe_s337f_mgr;
}
