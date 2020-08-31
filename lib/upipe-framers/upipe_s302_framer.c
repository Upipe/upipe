/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
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
 * @short Upipe module building frames from chunks of a SMPTE 302 stream
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
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe-framers/upipe_s302_framer.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

/** SMPTE 302 frequency */
#define S302_FREQUENCY 48000

/** SMPTE 302 header size */
#define S302_HEADER_SIZE 4

/* Length in bytes of two audio samples */
static const uint8_t pair_lengths[] = {5, 6, 7, 0};

#define S302_MAX_PAIR_LENGTH 7
#define S302_MAX_CHANNELS 8

/** @internal @This is the private context of an s302f pipe. */
struct upipe_s302f {
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

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** currently detected frame rate */
    struct urational fps;
    /** currently detected octet rate */
    uint64_t octetrate;

    /** next uref to be processed */
    struct uref *next_uref;
    /** size of the next uref */
    size_t next_uref_size;

    /** pseudo-packet containing date information for the next picture */
    struct uref au_uref_s;
    /** true if we have thrown the sync_acquired event (that means we found a
     * sequence header) */
    bool acquired;

    uint8_t scratch_buffer[S302_MAX_PAIR_LENGTH * S302_MAX_CHANNELS/2];
    uint8_t scratch_buffer_count;

    bool have_valid_header;
    uint8_t pair_length, num_channels;
    uint8_t header[S302_HEADER_SIZE];

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_s302f, upipe, UPIPE_S302F_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_s302f, urefcount, upipe_s302f_free)
UPIPE_HELPER_VOID(upipe_s302f)
UPIPE_HELPER_SYNC(upipe_s302f, acquired)

UPIPE_HELPER_OUTPUT(upipe_s302f, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_s302f, flow_def_input, flow_def_attr)

static int upipe_s302f_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL)
        upipe_s302f_store_flow_def(upipe, flow_format);
    return UBASE_ERR_NONE;
}

UPIPE_HELPER_UBUF_MGR(upipe_s302f, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_s302f_check,
                      upipe_s302f_register_output_request,
                      upipe_s302f_unregister_output_request)

/** @internal @This allocates an s302f pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_s302f_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_s302f_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_s302f *upipe_s302f = upipe_s302f_from_upipe(upipe);
    upipe_s302f_init_urefcount(upipe);
    upipe_s302f_init_sync(upipe);
    upipe_s302f_init_output(upipe);
    upipe_s302f_init_flow_def(upipe);
    upipe_s302f_init_ubuf_mgr(upipe);
    upipe_s302f->octetrate = 0;
    upipe_s302f->next_uref = NULL;
    upipe_s302f->next_uref_size = 0;
    upipe_s302f->scratch_buffer_count = 0;
    upipe_s302f->have_valid_header = false;
    upipe_s302f->pair_length = upipe_s302f->num_channels = 0;
    uref_init(&upipe_s302f->au_uref_s);
    upipe_throw_ready(upipe);
    return upipe;
}

static void set_dates(struct uref *uref, struct uref *au_uref_s, int num_samples)
{
    uint64_t duration = num_samples * UCLOCK_FREQ / S302_FREQUENCY;

    /* We work on encoded data so in the DTS domain. Rebase on DTS */
    uint64_t date;
    struct urational drift_rate;
    drift_rate.den = 0;
    uref_clock_get_rate(uref, &drift_rate);
#define SET_DATE(dv)                                              \
    if (ubase_check(uref_clock_get_dts_##dv(uref, &date)))        \
        uref_clock_set_dts_##dv(au_uref_s, date);                 \
    if (ubase_check(uref_clock_get_dts_##dv(au_uref_s, &date))) { \
        uref_clock_set_dts_##dv(uref, date);                      \
        uref_clock_set_dts_##dv(au_uref_s, date + duration);      \
    }
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    uref_clock_set_dts_pts_delay(uref, 0);
    if (drift_rate.den)
        uref_clock_set_rate(uref, drift_rate);
}

/** @internal @This works on a s302 frame and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_s302f_work(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_s302f *upipe_s302f = upipe_s302f_from_upipe(upipe);
    uint64_t octetrate;
    uint8_t header[S302_HEADER_SIZE];
    int audio_packet_size;
    uint8_t num_channels;
    uint8_t bits_per_sample;
    uint8_t pair_length;
    int num_samples;

    if (!ubase_check(uref_block_extract(upipe_s302f->next_uref,
                                        0, S302_HEADER_SIZE, header))) {
        uref_free(upipe_s302f->next_uref);
        goto upipe_s302f_work_err;
    }
    audio_packet_size = (header[0] << 8) | header[1];
    num_channels = ((header[2] >> 6) + 1) * 2;
    bits_per_sample = (header[3] >> 4) & 0x3;

    if (audio_packet_size + S302_HEADER_SIZE < upipe_s302f->next_uref_size) {
        uref_free(upipe_s302f->next_uref);
        goto upipe_s302f_work_err;
    }

    if (audio_packet_size + S302_HEADER_SIZE > upipe_s302f->next_uref_size) {
        uref_free(upipe_s302f->next_uref);
        goto upipe_s302f_work_err;
    }

    pair_length = pair_lengths[bits_per_sample];
    if (!pair_length)
        goto upipe_s302f_work_err;

    num_samples = audio_packet_size / (pair_length * (num_channels / 2));
    octetrate = (uint64_t)S302_FREQUENCY * audio_packet_size / num_samples;

    /* Avoid jitter on NTSC patterns */
    if ((octetrate > upipe_s302f->octetrate + 500) ||
        (octetrate < upipe_s302f->octetrate - 500)) {
        upipe_s302f->octetrate = octetrate;
    }

    struct uref *flow_def = upipe_s302f_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(upipe_s302f->next_uref);
        goto upipe_s302f_work_err;
    }

    UBASE_FATAL(upipe, uref_flow_set_complete(flow_def))
    UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.s302m.sound."))
    UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def,
                            upipe_s302f->octetrate))
    UBASE_FATAL(upipe, uref_sound_flow_set_rate(flow_def,
                            S302_FREQUENCY))
    UBASE_FATAL(upipe, uref_sound_flow_set_channels(flow_def,
                            num_channels))

    flow_def = upipe_s302f_store_flow_def_attr(upipe, flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(upipe_s302f->next_uref);
        goto upipe_s302f_work_err;
    }
    upipe_s302f_store_flow_def(upipe, flow_def);
    upipe_s302f_sync_acquired(upipe);

    set_dates(upipe_s302f->next_uref, &upipe_s302f->au_uref_s, num_samples);

    upipe_s302f_output(upipe, upipe_s302f->next_uref, upump_p);
upipe_s302f_work_err:
    upipe_s302f->next_uref = NULL;
    upipe_s302f->next_uref_size = 0;
}

static int reframe(struct upipe_s302f *upipe_s302f, struct uref *uref, int num_samples, int uref_size, int uref_offset)
{
    struct upipe *upipe = upipe_s302f_to_upipe(upipe_s302f);

    int audio_packet_size = num_samples * (upipe_s302f->pair_length * (upipe_s302f->num_channels / 2));
    upipe_s302f->header[0] = audio_packet_size >> 8;
    upipe_s302f->header[1] = audio_packet_size;

    /* Create new block. */
    struct ubuf *ubuf = ubuf_block_alloc(upipe_s302f->ubuf_mgr,
            S302_HEADER_SIZE + audio_packet_size);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    int size = -1;
    uint8_t *data;
    int ret = ubuf_block_write(ubuf, 0, &size, &data);
    if (!ubase_check(ret)) {
        upipe_throw_error(upipe, ret);
        ubuf_free(ubuf);
        return ret;
    }

    /* Copy header. */
    memcpy(data, upipe_s302f->header, S302_HEADER_SIZE);
    data += S302_HEADER_SIZE;

    /* Copy tail of previous packet. */
    memcpy(data, upipe_s302f->scratch_buffer, upipe_s302f->scratch_buffer_count);
    data += upipe_s302f->scratch_buffer_count;

    /* Copy audio data. */
    ret = uref_block_extract(uref, uref_offset, audio_packet_size - upipe_s302f->scratch_buffer_count, data);
    if (!ubase_check(ret)) {
        upipe_throw_error(upipe, ret);
        ubuf_block_unmap(ubuf, 0);
        ubuf_free(ubuf);
        return ret;
    }
    uref_offset += audio_packet_size - upipe_s302f->scratch_buffer_count;

    /* Store tail of data that must be output next time. */
    ret = uref_block_extract(uref, uref_offset, -1, upipe_s302f->scratch_buffer);
    if (!ubase_check(ret)) {
        upipe_throw_error(upipe, ret);
        ubuf_block_unmap(ubuf, 0);
        ubuf_free(ubuf);
        return ret;
    }
    upipe_s302f->scratch_buffer_count = uref_size - uref_offset;

    ubuf_block_unmap(ubuf, 0);
    uref_attach_ubuf(uref, ubuf);

    set_dates(uref, &upipe_s302f->au_uref_s, num_samples);

    return UBASE_ERR_NONE;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_s302f_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_s302f *upipe_s302f = upipe_s302f_from_upipe(upipe);
    size_t uref_size;
    if (!ubase_check(uref_block_size(uref, &uref_size)) || !uref_size) {
        uref_free(uref);
        return;
    }
    bool end = ubase_check(uref_block_get_end(uref));

#define LOW_LATENCY 1
    if (LOW_LATENCY) {
        if (ubase_check(uref_block_get_start(uref))) {
            uint8_t num_channels;
            uint8_t bits_per_sample;
            uint8_t pair_length;
            int num_samples;

            /* This should be 0 when a start attribute is set.  Packet loss
             * might cause it to be otherwise.  Reset to ensure that the rest of
             * the stream isn't garbage. */
            upipe_s302f->scratch_buffer_count = 0;

            /* Get header properties. */
            if (!ubase_check(uref_block_extract(uref, 0, S302_HEADER_SIZE, upipe_s302f->header))) {
                uref_free(uref);
                upipe_s302f->have_valid_header = false;
                return;
            }
            upipe_s302f->num_channels = num_channels = ((upipe_s302f->header[2] >> 6) + 1) * 2;
            bits_per_sample = (upipe_s302f->header[3] >> 4) & 0x3;

            upipe_s302f->pair_length = pair_length = pair_lengths[bits_per_sample];
            if (!pair_length) {
                uref_free(uref);
                upipe_s302f->have_valid_header = false;
                return;
            }
            upipe_s302f->have_valid_header = true;

            /* Make/update output flow_def. */
            struct uref *flow_def = upipe_s302f_alloc_flow_def_attr(upipe);
            if (unlikely(flow_def == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                uref_free(uref);
                return;
            }

            UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.s302m.sound."))
            UBASE_FATAL(upipe, uref_sound_flow_set_rate(flow_def, S302_FREQUENCY))
            UBASE_FATAL(upipe, uref_sound_flow_set_channels(flow_def, num_channels))

            flow_def = upipe_s302f_store_flow_def_attr(upipe, flow_def);
            if (unlikely(flow_def == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                uref_free(uref);
                return;
            }
            upipe_s302f_sync_acquired(upipe);

            /* Get a ubuf_mgr to alloc new blocks. */
            if (!upipe_s302f->ubuf_mgr
                    || !ubase_check(ubuf_mgr_check(upipe_s302f->ubuf_mgr, flow_def)))
                upipe_s302f_require_ubuf_mgr(upipe, flow_def);
            else
                uref_free(flow_def);

            if (!upipe_s302f->ubuf_mgr) {
                upipe_verbose(upipe, "no ubuf_mgr, dropping uref");
                uref_free(uref);
                return;
            }

            /* Set header size to the max data that is in the uref. */
            num_samples = (uref_size - S302_HEADER_SIZE) / (pair_length * (num_channels / 2));

            if (ubase_check(reframe(upipe_s302f, uref, num_samples, uref_size, S302_HEADER_SIZE)))
                upipe_s302f_output(upipe, uref, upump_p);
            else
                uref_free(uref);
        }

        else if (upipe_s302f->have_valid_header) {
            /* Number of whole samples in uref and scratch buffer. */
            int num_samples = (upipe_s302f->scratch_buffer_count + uref_size) / (upipe_s302f->pair_length * (upipe_s302f->num_channels / 2));

            if (ubase_check(reframe(upipe_s302f, uref, num_samples, uref_size, 0)))
                upipe_s302f_output(upipe, uref, upump_p);
            else
                uref_free(uref);
        }

        else {
            upipe_verbose(upipe, "no valid header, dropping uref");
            uref_free(uref);
        }

        return;
    }
#undef LOW_LATENCY

    if (ubase_check(uref_block_get_start(uref))) {
        if (upipe_s302f->next_uref != NULL)
            upipe_s302f_work(upipe, upump_p);

        upipe_s302f->next_uref = uref;
        upipe_s302f->next_uref_size = uref_size;
    } else if (likely(upipe_s302f->next_uref != NULL)) {
        struct ubuf *ubuf = uref_detach_ubuf(uref);
        uref_free(uref);
        if (unlikely(!ubase_check(uref_block_append(upipe_s302f->next_uref,
                                                    ubuf)))) {
            ubuf_free(ubuf);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        upipe_s302f->next_uref_size += uref_size;
    } else {
        uref_free(uref);
        return;
    }

    if (end)
        upipe_s302f_work(upipe, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_s302f_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 (ubase_ncmp(def, "block.s302m.sound.") &&
                  strcmp(def, "block."))))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    flow_def = upipe_s302f_store_flow_def_input(upipe, flow_def_dup);
    if (flow_def != NULL)
        upipe_s302f_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a s302f pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_s302f_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_s302f_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_s302f_alloc_output_proxy(upipe, request);
        }

        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_s302f_free_output_proxy(upipe, request);
        }

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_s302f_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_s302f_free(struct upipe *upipe)
{
    struct upipe_s302f *upipe_s302f = upipe_s302f_from_upipe(upipe);
    upipe_throw_dead(upipe);

    uref_free(upipe_s302f->next_uref);
    upipe_s302f_clean_output(upipe);
    upipe_s302f_clean_flow_def(upipe);
    upipe_s302f_clean_sync(upipe);

    upipe_s302f_clean_ubuf_mgr(upipe);
    upipe_s302f_clean_urefcount(upipe);
    upipe_s302f_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_s302f_mgr = {
    .refcount = NULL,
    .signature = UPIPE_S302F_SIGNATURE,

    .upipe_alloc = upipe_s302f_alloc,
    .upipe_input = upipe_s302f_input,
    .upipe_control = upipe_s302f_control,

    .upipe_mgr_control = NULL

};

/** @This returns the management structure for all s302f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_s302f_mgr_alloc(void)
{
    return &upipe_s302f_mgr;
}
