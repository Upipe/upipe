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
 * @short Upipe module building frames from an ISO 13818-3 or 7 stream
 * This framer supports levels 1, 2, 3 of ISO/IEC 11179-3 and ISO/IEC 13818-3,
 * and ISO/IEC 13818-7 (ADTS AAC) streams
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
#include <upipe-framers/upipe_mpga_framer.h>
#include <upipe-framers/uref_mpga_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/mpga.h>
#include <bitstream/mpeg/aac.h>

/** @This returns the octetrate / 1000 of an MPEG-1 or 2 audio stream. */
static const uint8_t mpeg_octetrate_table[2][3][16] = {
    { /* MPEG-1 */
        /* layer 1 */
        { 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 0 },
        /* layer 2 */
        { 0, 4, 6,  7,  8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 0 },
        /* layer 3 */
        { 0, 4, 5,  6,  7,  8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 0 },
    }, { /* MPEG-2 */
        /* layer 1 */
        { 0, 4, 6,  7,  8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 0 },
        /* layer 2 */
        { 0, 1, 2,  3,  4,  5,  6,  7,  8, 10, 12, 14, 16, 18, 20, 0 },
        /* layer 3 */
        { 0, 1, 2,  3,  4,  5,  6,  7,  8, 10, 12, 14, 16, 18, 20, 0 }
    }
};

/** @This returns the sample rate in Hz of an MPEG-1 or 2 audio stream. */
static const unsigned int mpeg_samplerate_table[2][4] = {
    { 44100, 48000, 32000, 0 }, /* MPEG-1 */
    { 22050, 24000, 16000, 0 }  /* MPEG-2 */
};

/** @This returns the sample rate in Hz of an ADTS AAC audio stream. */
static const unsigned int adts_samplerate_table[16] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000,  7350,  0,     0,     0
};

/** @This represents the coding type of the audio ES. */
enum upipe_mpgaf_type {
    /** MPEG-1 and 2 layers 1, 2 and 3 */
    UPIPE_MPGAF_MP2,
    /** MPEG-2 Advanced Audio Coding */
    UPIPE_MPGAF_AAC,
    /** Unknown */
    UPIPE_MPGAF_UNKNOWN
};

/** @internal @This is the private context of an mpgaf pipe. */
struct upipe_mpgaf {
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
    /** coding type */
    enum upipe_mpgaf_type type;

    /* sync parsing stuff */
    /** number of octets in a frame */
    size_t frame_size;
    /** number of octets in a frame with padding enabled */
    size_t frame_size_padding;
    /** number of samples in a frame */
    size_t samples;
    /** number of samples per second */
    size_t samplerate;
    /** number of channels */
    uint8_t channels;
    /** octet rate */
    uint64_t octetrate;
    /** residue of the duration in 27 MHz units */
    uint64_t duration_residue;
    /** true we have had a discontinuity recently */
    bool got_discontinuity;
    /** sync header */
    uint8_t sync_header[MPGA_HEADER_SIZE + ADTS_HEADER_SIZE]; // to be sure

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
    /** pseudo-packet containing date information for the next picture */
    struct uref au_uref_s;
    /** drift rate of the next picture */
    struct urational drift_rate;
    /** true if we have thrown the sync_acquired event (that means we found a
     * sequence header) */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static void upipe_mpgaf_promote_uref(struct upipe *upipe);

UPIPE_HELPER_UPIPE(upipe_mpgaf, upipe, UPIPE_MPGAF_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_mpgaf, urefcount, upipe_mpgaf_free)
UPIPE_HELPER_VOID(upipe_mpgaf)
UPIPE_HELPER_SYNC(upipe_mpgaf, acquired)
UPIPE_HELPER_UREF_STREAM(upipe_mpgaf, next_uref, next_uref_size, urefs,
                         upipe_mpgaf_promote_uref)

UPIPE_HELPER_OUTPUT(upipe_mpgaf, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_mpgaf, flow_def_input, flow_def_attr)

/** @internal @This flushes all dates.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_mpgaf_flush_dates(struct upipe *upipe)
{
    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);
    uref_clock_set_date_sys(&upipe_mpgaf->au_uref_s, UINT64_MAX,
                            UREF_DATE_NONE);
    uref_clock_set_date_prog(&upipe_mpgaf->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_set_date_orig(&upipe_mpgaf->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_delete_dts_pts_delay(&upipe_mpgaf->au_uref_s);
}

/** @internal @This allocates an mpgaf pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_mpgaf_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_mpgaf_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);
    upipe_mpgaf_init_urefcount(upipe);
    upipe_mpgaf_init_sync(upipe);
    upipe_mpgaf_init_uref_stream(upipe);
    upipe_mpgaf_init_output(upipe);
    upipe_mpgaf_init_flow_def(upipe);
    upipe_mpgaf->type = UPIPE_MPGAF_UNKNOWN;
    upipe_mpgaf->input_latency = 0;
    upipe_mpgaf->samplerate = 0;
    upipe_mpgaf->got_discontinuity = false;
    upipe_mpgaf->next_frame_size = -1;
    uref_init(&upipe_mpgaf->au_uref_s);
    upipe_mpgaf_flush_dates(upipe);
    upipe_mpgaf->drift_rate.num = upipe_mpgaf->drift_rate.den = 0;
    upipe_mpgaf->sync_header[0] = 0x0;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This scans for a sync word.
 *
 * @param upipe description structure of the pipe
 * @param dropped_p filled with the number of octets to drop before the sync
 * @return true if a sync word was found
 */
static bool upipe_mpgaf_scan(struct upipe *upipe, size_t *dropped_p)
{
    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);
    while (ubase_check(uref_block_scan(upipe_mpgaf->next_uref, dropped_p, 0xff))) {
        uint8_t word;
        if (!ubase_check(uref_block_extract(upipe_mpgaf->next_uref,
                        *dropped_p + 1, 1, &word)))
            return false;

        if ((word & 0xe0) == 0xe0)
            return true;
        (*dropped_p)++;
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
static bool upipe_mpgaf_check_frame(struct upipe *upipe, bool *ready_p)
{
    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);
    size_t size;
    *ready_p = false;
    if (!ubase_check(uref_block_size(upipe_mpgaf->next_uref, &size)))
        return false;
    if (size < upipe_mpgaf->next_frame_size)
        return true;

    uint8_t words[2];
    if (!ubase_check(uref_block_extract(upipe_mpgaf->next_uref,
                            upipe_mpgaf->next_frame_size, 2, words))) {
        /* not enough data */
        if (upipe_mpgaf->acquired) /* avoid delaying packets unnecessarily */
            *ready_p = true;
        return true;
    }
    if (words[0] != 0xff || (words[1] & 0xe0) != 0xe0)
        return false;
    *ready_p = true;
    return true;
}

/** @internal @This parses a new MPEG layers 1, 2 or 3 header.
 *
 * @param upipe description structure of the pipe
 * @return false in case the header is inconsistent
 */
static bool upipe_mpgaf_parse_mpeg(struct upipe *upipe)
{
    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);
    uint8_t header[MPGA_HEADER_SIZE];
    if (!ubase_check(uref_block_extract(upipe_mpgaf->next_uref, 0,
                    MPGA_HEADER_SIZE, header)))
        return true; /* not enough data */

    if (likely(mpga_sync_compare(header, upipe_mpgaf->sync_header))) {
        /* identical sync */
        upipe_mpgaf->next_frame_size = mpga_get_padding(header) ?
                                       upipe_mpgaf->frame_size_padding :
                                       upipe_mpgaf->frame_size;
        return true;
    }

    if (mpga_get_bitrate_index(header) == MPGA_BITRATE_INVALID ||
        mpga_get_sampling_freq(header) == MPGA_SAMPLERATE_INVALID ||
        mpga_get_emphasis(header) == MPGA_EMPHASIS_INVALID)
        return false;

    struct uref *flow_def = upipe_mpgaf_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    memcpy(upipe_mpgaf->sync_header, header, MPGA_HEADER_SIZE);

    bool mpeg25 = mpga_get_mpeg25(header);
    uint8_t id = mpga_get_id(header) == MPGA_ID_2 ? 1 : 0;
    uint8_t layer = 4 - mpga_get_layer(header);
    uint8_t octetrate = mpeg_octetrate_table[id][layer - 1]
                            [mpga_get_bitrate_index(header)];
    uint8_t max_octetrate = mpeg_octetrate_table[id][layer - 1][14];
    upipe_mpgaf->samplerate = mpeg_samplerate_table[id]
                                [mpga_get_sampling_freq(header)];
    uint8_t padding = mpga_get_padding(header) ? 1 : 0;
    uint8_t mode = mpga_get_mode(header);
    bool copyright = mpga_get_copyright(header);
    bool original = mpga_get_original(header);
    if (mpeg25)
        upipe_mpgaf->samplerate /= 2;
    if (!octetrate)
        max_octetrate *= 2;

    switch (layer) {
        case 1:
            upipe_mpgaf->frame_size =
                (96000 * octetrate / upipe_mpgaf->samplerate) * 4;
            upipe_mpgaf->frame_size_padding =
                (96000 * octetrate / upipe_mpgaf->samplerate + 1) * 4;
            upipe_mpgaf->samples = 384;
            UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.mp2.sound."))
            break;
        case 2:
            upipe_mpgaf->frame_size =
                1152000 * octetrate / upipe_mpgaf->samplerate;
            upipe_mpgaf->frame_size_padding =
                1152000 * octetrate / upipe_mpgaf->samplerate + 1;
            upipe_mpgaf->samples = 1152;
            UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.mp2.sound."))
            break;
        case 3:
            if (id || mpeg25) {
                upipe_mpgaf->frame_size =
                    576000 * octetrate / upipe_mpgaf->samplerate;
                upipe_mpgaf->frame_size_padding =
                    576000 * octetrate / upipe_mpgaf->samplerate + 1;
                upipe_mpgaf->samples = 576;
            } else {
                upipe_mpgaf->frame_size =
                    1152000 * octetrate / upipe_mpgaf->samplerate;
                upipe_mpgaf->frame_size_padding =
                    1152000 * octetrate / upipe_mpgaf->samplerate + 1;
                upipe_mpgaf->samples = 1152;
            }
            UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.mp3.sound."))
            break;
    }

    upipe_mpgaf->channels = mode == MPGA_MODE_MONO ? 1 : 2;

    UBASE_FATAL(upipe, uref_mpga_flow_set_mode(flow_def, mode))
    UBASE_FATAL(upipe, uref_sound_flow_set_channels(flow_def, upipe_mpgaf->channels))
    UBASE_FATAL(upipe, uref_sound_flow_set_rate(flow_def, upipe_mpgaf->samplerate))
    UBASE_FATAL(upipe, uref_sound_flow_set_samples(flow_def, upipe_mpgaf->samples))
    UBASE_FATAL(upipe, uref_clock_set_latency(flow_def,
                upipe_mpgaf->input_latency +
                UCLOCK_FREQ * upipe_mpgaf->samples / upipe_mpgaf->samplerate))
    upipe_mpgaf->octetrate = (uint64_t)octetrate * 1000;
    if (upipe_mpgaf->octetrate) {
        UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def,
                                                   upipe_mpgaf->octetrate))
    }
    UBASE_FATAL(upipe, uref_block_flow_set_max_octetrate(flow_def,
            (uint64_t)max_octetrate * 1000))
    if (copyright)
        UBASE_FATAL(upipe, uref_flow_set_copyright(flow_def))
    if (original)
        UBASE_FATAL(upipe, uref_flow_set_original(flow_def))

    flow_def = upipe_mpgaf_store_flow_def_attr(upipe, flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    upipe_mpgaf_store_flow_def(upipe, flow_def);
    upipe_mpgaf->next_frame_size = padding ?
                                   upipe_mpgaf->frame_size_padding :
                                   upipe_mpgaf->frame_size;
    return true;
}

/** @internal @This parses a new ADTS AAC header.
 *
 * @param upipe description structure of the pipe
 * @return false in case the header is inconsistent
 */
static bool upipe_mpgaf_parse_adts(struct upipe *upipe)
{
    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);
    uint8_t header[ADTS_HEADER_SIZE];
    if (!ubase_check(uref_block_extract(upipe_mpgaf->next_uref, 0,
                    ADTS_HEADER_SIZE, header)))
        return true; /* not enough data */

    if (likely(adts_sync_compare_formats(header, upipe_mpgaf->sync_header))) {
        /* identical sync */
        goto upipe_mpgaf_parse_adts_shortcut;
    }

    if (!adts_samplerate_table[adts_get_sampling_freq(header)]) {
        upipe_warn(upipe, "invalid samplerate");
        return false;
    }

    if (adts_get_length(header) < ADTS_HEADER_SIZE) {
        upipe_warn(upipe, "invalid header");
        return false;
    }

    memcpy(upipe_mpgaf->sync_header, header, ADTS_HEADER_SIZE);

    struct uref *flow_def = upipe_mpgaf_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    upipe_mpgaf->samplerate =
        adts_samplerate_table[adts_get_sampling_freq(header)];
    upipe_mpgaf->samples = ADTS_SAMPLES_PER_BLOCK *
                           (1 + adts_get_num_blocks(header));
    upipe_mpgaf->channels = adts_get_channels(header);
    if (upipe_mpgaf->channels == 7)
        upipe_mpgaf->channels = 8;

    UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.aac.sound."))
    if (upipe_mpgaf->channels) {
        UBASE_FATAL(upipe, uref_sound_flow_set_channels(flow_def, upipe_mpgaf->channels))
    }
    uint64_t samplerate = upipe_mpgaf->samplerate;
    if (samplerate <= 24000)
        /* assume SBR on low frequency streams */
        samplerate *= 2;
    UBASE_FATAL(upipe, uref_sound_flow_set_rate(flow_def, samplerate))
    UBASE_FATAL(upipe, uref_sound_flow_set_samples(flow_def, upipe_mpgaf->samples))
    UBASE_FATAL(upipe, uref_clock_set_latency(flow_def,
                upipe_mpgaf->input_latency +
                UCLOCK_FREQ * upipe_mpgaf->samples / samplerate))
    if (adts_get_copy(header))
        UBASE_FATAL(upipe, uref_flow_set_copyright(flow_def))
    if (adts_get_home(header))
        UBASE_FATAL(upipe, uref_flow_set_original(flow_def))

    /* Calculate octetrate assuming the stream is CBR. Do not take SBR into
     * account here, as it would * 2 both the samplerate and the number of
     * samples. */
    uint64_t octetrate = adts_get_length(header) * upipe_mpgaf->samplerate /
                         upipe_mpgaf->samples;
    /* Round up to a multiple of 8 kbits/s. */
    octetrate += 999;
    octetrate -= octetrate % 1000;
    upipe_mpgaf->octetrate = octetrate;
    UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def, octetrate))

    flow_def = upipe_mpgaf_store_flow_def_attr(upipe, flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    upipe_mpgaf_store_flow_def(upipe, flow_def);

upipe_mpgaf_parse_adts_shortcut:
    upipe_mpgaf->next_frame_size = adts_get_length(header);
    return true;
}

/** @internal @This parses a new header.
 *
 * @param upipe description structure of the pipe
 * @return false in case the header is inconsistent
 */
static bool upipe_mpgaf_parse_header(struct upipe *upipe)
{
    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);
    uint8_t header[2];
    if (unlikely(!ubase_check(uref_block_extract(upipe_mpgaf->next_uref, 0, 2, header))))
        return true;

    enum upipe_mpgaf_type type;
    switch (mpga_get_layer(header)) {
        case MPGA_LAYER_1:
        case MPGA_LAYER_2:
        case MPGA_LAYER_3:
            type = UPIPE_MPGAF_MP2;
            break;
        case MPGA_LAYER_ADTS:
            type = UPIPE_MPGAF_AAC;
            break;
        default:
            return false;
    }

    if (unlikely(upipe_mpgaf->type != UPIPE_MPGAF_UNKNOWN &&
                 upipe_mpgaf->type != type)) {
        upipe_warn(upipe, "invalid header");
        return false;
    }
    if (type == UPIPE_MPGAF_MP2)
        return upipe_mpgaf_parse_mpeg(upipe);
    return upipe_mpgaf_parse_adts(upipe);
}

/** @internal @This handles and outputs a frame.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_mpgaf_output_frame(struct upipe *upipe,
                                     struct upump **upump_p)
{
    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);

    struct uref au_uref_s = upipe_mpgaf->au_uref_s;
    struct urational drift_rate = upipe_mpgaf->drift_rate;
    /* From now on, PTS declaration only impacts the next frame. */
    upipe_mpgaf_flush_dates(upipe);

    struct uref *uref = upipe_mpgaf_extract_uref_stream(upipe,
            upipe_mpgaf->next_frame_size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    lldiv_t div = lldiv((uint64_t)upipe_mpgaf->samples * UCLOCK_FREQ +
                        upipe_mpgaf->duration_residue,
                        upipe_mpgaf->samplerate);
    uint64_t duration = div.quot;
    upipe_mpgaf->duration_residue = div.rem;

    /* We work on encoded data so in the DTS domain. Rebase on DTS. */
    uint64_t date;
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(&au_uref_s, &date))) {          \
        uref_clock_set_dts_##dv(uref, date);                                \
        if (!ubase_check(uref_clock_get_dts_##dv(&upipe_mpgaf->au_uref_s,   \
                                                 NULL)))                    \
            uref_clock_set_dts_##dv(&upipe_mpgaf->au_uref_s,                \
                                    date + duration);                       \
    } else if (ubase_check(uref_clock_get_dts_##dv(uref, &date)))           \
        uref_clock_set_date_##dv(uref, UINT64_MAX, UREF_DATE_NONE);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    /* PTS = DTS for MPEG audio */
    uref_clock_set_dts_pts_delay(uref, 0);
    if (drift_rate.den)
        uref_clock_set_rate(uref, drift_rate);
    else
        uref_clock_delete_rate(uref);

    UBASE_FATAL(upipe, uref_clock_set_duration(uref, duration))

    if (upipe_mpgaf->got_discontinuity)
        uref_flow_set_discontinuity(uref);
    upipe_mpgaf->got_discontinuity = false;
    upipe_mpgaf_output(upipe, uref, upump_p);
}

/** @internal @This is called back by @ref upipe_mpgaf_append_uref_stream
 * whenever a new uref is promoted in next_uref.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_mpgaf_promote_uref(struct upipe *upipe)
{
    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);
    uint64_t date;
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(upipe_mpgaf->next_uref, &date)))\
        uref_clock_set_dts_##dv(&upipe_mpgaf->au_uref_s, date);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    uref_clock_get_rate(upipe_mpgaf->next_uref, &upipe_mpgaf->drift_rate);
    upipe_mpgaf->duration_residue = 0;
}

/** @internal @This tries to output frames from the queue of input buffers.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_mpgaf_work(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);
    while (upipe_mpgaf->next_uref != NULL) {
        if (unlikely(!upipe_mpgaf->acquired)) {
            size_t dropped = 0;
            bool ret = upipe_mpgaf_scan(upipe, &dropped);
            upipe_mpgaf_consume_uref_stream(upipe, dropped);
            if (!ret)
                return;
        }
        if (upipe_mpgaf->next_frame_size == -1 &&
            !upipe_mpgaf_parse_header(upipe)) {
            upipe_warn(upipe, "invalid header");
            upipe_mpgaf_consume_uref_stream(upipe, 1);
            upipe_mpgaf_sync_lost(upipe);
            continue;
        }
        if (upipe_mpgaf->next_frame_size == -1)
            return; /* not enough data */

        if (!upipe_mpgaf->next_frame_size) {
            /* MPEG free bitrate mode, we have to scan */
            size_t next_frame_size = MPGA_HEADER_SIZE;
            if (!upipe_mpgaf_scan(upipe, &next_frame_size))
                return; /* not enough data */
            upipe_mpgaf->next_frame_size = next_frame_size;
        } else {
            bool ready;
            if (unlikely(!upipe_mpgaf_check_frame(upipe, &ready))) {
                upipe_warn(upipe, "invalid frame");
                upipe_mpgaf_consume_uref_stream(upipe, 1);
                upipe_mpgaf->next_frame_size = -1;
                upipe_mpgaf_sync_lost(upipe);
                continue;
            }
            if (!ready)
                return; /* not enough data */
        }

        upipe_mpgaf_sync_acquired(upipe);
        upipe_mpgaf_output_frame(upipe, upump_p);
        upipe_mpgaf->next_frame_size = -1;
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_mpgaf_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);

    if (unlikely(ubase_check(uref_flow_get_discontinuity(uref)))) {
        /* Drop the current frame and resync. */
        upipe_mpgaf_clean_uref_stream(upipe);
        upipe_mpgaf_init_uref_stream(upipe);
        upipe_mpgaf->got_discontinuity = true;
        upipe_mpgaf->next_frame_size = -1;
        upipe_mpgaf_sync_lost(upipe);
    }

    upipe_mpgaf_append_uref_stream(upipe, uref);
    upipe_mpgaf_work(upipe, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_mpgaf_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 (ubase_ncmp(def, "block.mp2.") &&
                  ubase_ncmp(def, "block.aac.") &&
                  strcmp(def, "block."))))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    struct upipe_mpgaf *upipe_mpgaf = upipe_mpgaf_from_upipe(upipe);
    upipe_mpgaf->input_latency = 0;
    uref_clock_get_latency(flow_def, &upipe_mpgaf->input_latency);
    upipe_mpgaf->type = UPIPE_MPGAF_UNKNOWN;
    if (!ubase_ncmp(def, "block.mp2."))
        upipe_mpgaf->type = UPIPE_MPGAF_MP2;
    else if (!ubase_ncmp(def, "block.aac."))
        upipe_mpgaf->type = UPIPE_MPGAF_AAC;

    if (unlikely(upipe_mpgaf->samplerate &&
                 !ubase_check(uref_clock_set_latency(flow_def_dup,
                                    upipe_mpgaf->input_latency +
                                    UCLOCK_FREQ * upipe_mpgaf->samples /
                                    upipe_mpgaf->samplerate))))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    flow_def = upipe_mpgaf_store_flow_def_input(upipe, flow_def_dup);
    if (flow_def != NULL)
        upipe_mpgaf_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a mpgaf pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_mpgaf_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_mpgaf_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_mpgaf_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_mpgaf_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_mpgaf_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_mpgaf_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_mpgaf_set_output(upipe, output);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_mpgaf_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_mpgaf_clean_uref_stream(upipe);
    upipe_mpgaf_clean_output(upipe);
    upipe_mpgaf_clean_flow_def(upipe);
    upipe_mpgaf_clean_sync(upipe);

    upipe_mpgaf_clean_urefcount(upipe);
    upipe_mpgaf_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_mpgaf_mgr = {
    .refcount = NULL,
    .signature = UPIPE_MPGAF_SIGNATURE,

    .upipe_alloc = upipe_mpgaf_alloc,
    .upipe_input = upipe_mpgaf_input,
    .upipe_control = upipe_mpgaf_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all mpgaf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_mpgaf_mgr_alloc(void)
{
    return &upipe_mpgaf_mgr;
}
