/*
 * SDI decoder
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/** @file
 * @short Upipe sdi_dec module
 */

#include <config.h>

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/upipe.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_block.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <bitstream/smpte/291.h>
#include <bitstream/smpte/337.h>

#include <upipe-hbrmt/upipe_sdi_dec.h>

#include "sdidec.h"
#include "upipe_hbrmt_common.h"

#define UPIPE_SDI_DEC_MAX_PLANES 3
#define UPIPE_SDI_MAX_CHANNELS 16

/** audio input subpipe */
struct upipe_sdi_dec_sub {
    /** refcount management structure */
    struct urefcount urefcount;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** structure for double-linked lists */
    struct uchain uchain;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** number of samples output */
    uint64_t samples;

    /** public upipe structure */
    struct upipe upipe;
};

/** upipe_sdi_dec structure with sdi_dec parameters */
struct upipe_sdi_dec {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** list of input subpipes */
    struct uchain subs;
    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** output mode */
    bool output_is_v210;
    int output_bit_depth; /* Used if output_is_v210 is false */

    /** output chroma map */
    const char *output_chroma_map[UPIPE_SDI_DEC_MAX_PLANES];

    /** UYVY to V210 */
    void (*uyvy_to_v210)(const uint16_t *y, uint8_t *dst, uintptr_t width);

    /** UYVY to 8-bit Planar */
    void (*uyvy_to_planar_8)(uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, uintptr_t width);

    /** UYVY to 10-bit Planar */
    void (*uyvy_to_planar_10)(uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, uintptr_t width);

    /* SDI offsets */
    const struct sdi_offsets_fmt *f;
    const struct sdi_picture_fmt *p;

    /* CRC LUT */
    uint32_t crc_lut[8][1024];

    /* Chroma CRC context */
    uint32_t crc_c;
    /* Luma CRC context */
    uint32_t crc_y;

    /* Enable CPU-intensive debugging (CRC) */
    int debug;

    /* check DBN sequence for each Type 1 packet */
    uint8_t dbn[0x80];

    /* presence of an AES stream */
    int aes_detected[8];

    int32_t aes_preamble[8][4];

    int64_t eav_clock;
    /* Per audio group number of samples written */
    uint64_t audio_samples[UPIPE_SDI_CHANNELS_PER_GROUP];

    /** Average errors when getting incomplete audio frames.
     * they might come from a converter using a corrupted SDI source e.g. when switching */
    uint8_t audio_fix;

    /** used to generate PTS */
    uint64_t frame_num;

    /** vanc output */
    struct upipe_sdi_dec_sub vanc;

    /** vbi output */
    struct upipe_sdi_dec_sub vbi;

    /** audio output */
    struct upipe_sdi_dec_sub audio;

    /** latency */
    uint64_t latency;

    /** public upipe structure */
    struct upipe upipe;
};

struct audio_ctx {
    int32_t *buf_audio;
    size_t group_offset[UPIPE_SDI_CHANNELS_PER_GROUP];
    int aes[8];
};

/** @hidden */
static bool upipe_sdi_dec_handle(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p);
/** @hidden */
static int upipe_sdi_dec_check(struct upipe *upipe, struct uref *flow_format);
/** @hidden */
static int upipe_sdi_dec_sub_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_sdi_dec, upipe, UPIPE_SDI_DEC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_sdi_dec, urefcount, upipe_sdi_dec_free);
UPIPE_HELPER_VOID(upipe_sdi_dec);
UPIPE_HELPER_OUTPUT(upipe_sdi_dec, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_sdi_dec, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_sdi_dec_check,
                      upipe_sdi_dec_register_output_request,
                      upipe_sdi_dec_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_sdi_dec, urefs, nb_urefs, max_urefs, blockers, upipe_sdi_dec_handle)

UPIPE_HELPER_UPIPE(upipe_sdi_dec_sub, upipe, UPIPE_SDI_DEC_SUB_SIGNATURE)
UPIPE_HELPER_OUTPUT(upipe_sdi_dec_sub, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_sdi_dec_sub, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_sdi_dec_sub_check,
                      upipe_sdi_dec_sub_register_output_request,
                      upipe_sdi_dec_sub_unregister_output_request)

UBASE_FROM_TO(upipe_sdi_dec, upipe_mgr, sub_mgr, sub_mgr)

static int upipe_sdi_dec_set_option(struct upipe *upipe, const char *option,
        const char *value)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);

    if (!option || !value)
        return UBASE_ERR_INVALID;

    if (strcmp(option, "debug")) {
        upipe_err_va(upipe, "Unknown option %s", option);
        return UBASE_ERR_INVALID;
    }

    upipe_sdi_dec->debug = atoi(value);
    return UBASE_ERR_NONE;
}

static int upipe_sdi_dec_sub_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_sdi_dec_sub_get_flow_def(upipe, p);
        }

        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sdi_dec_sub_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_sdi_dec_sub_set_output(upipe, output);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static struct upipe *upipe_sdi_dec_sub_init(struct upipe *upipe,
        struct upipe_mgr *mgr, struct uprobe *uprobe)
{
    upipe_init(upipe, mgr, uprobe);

    struct upipe_sdi_dec_sub *upipe_sdi_dec_sub =
        upipe_sdi_dec_sub_from_upipe(upipe);
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_sub_mgr(mgr);

    upipe_sdi_dec_sub_init_output(upipe);
    upipe_sdi_dec_sub_init_ubuf_mgr(upipe);
    upipe->refcount = &upipe_sdi_dec->urefcount;

    upipe_sdi_dec_sub->samples = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

static void upipe_sdi_dec_sub_clean(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_sdi_dec_sub_clean_ubuf_mgr(upipe);
    upipe_sdi_dec_sub_clean_output(upipe);
    upipe_clean(upipe);
}

static void upipe_sdi_dec_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_sdi_dec->sub_mgr;
    sub_mgr->refcount = upipe_sdi_dec_to_urefcount(upipe_sdi_dec);
    sub_mgr->signature = UPIPE_SDI_DEC_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = NULL;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_sdi_dec_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

static const bool parity_tab[512] = {
#   define P2(n) n, n^1, n^1, n
#   define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#   define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)
    P6(0), P6(1), P6(1), P6(0),
    P6(1), P6(0), P6(0), P6(1)
};

static inline void extract_sd_audio_group(struct upipe *upipe, int32_t *dst,
        const uint16_t *data)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);

    union {
        uint32_t u;
        int32_t  i;
    } sample;

    for (int i = 0; i < UPIPE_SDI_CHANNELS_PER_GROUP; i++) {
        uint8_t channel_idx = (data[0+3*i] & 0x6) >> 1;
        sample.u  = (data[0+3*i] & 0x1F8) << 9;
        sample.u |= (data[1+3*i] & 0x1FF) << 18;
        sample.u |= (data[2+3*i] & 0x01F) << 27;

        if (upipe_sdi_dec->debug) {
            uint8_t parity = 0;
            parity += parity_tab[data[0+3*i] & 0x1ff];
            parity += parity_tab[data[1+3*i] & 0x1ff];
            parity += parity_tab[data[2+3*i] & 0x0ff];

            if ((parity & 1) != ((data[2+3*i] >> 8) & 1)) {
                upipe_err_va(upipe, "wrong audio parity: 0x%.3x 0x%.3x 0x%.3x",
                        data[0+3*i], data[1+3*i], data[2+3*i]);
            }
        }

        dst[channel_idx] = sample.i;
    }
}

static inline int32_t extract_hd_audio_sample(struct upipe *upipe,
        const uint16_t *data)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);

    union {
        uint32_t u;
        int32_t  i;
    } sample = {0};

    sample.u |= (data[0] & 0xF0) <<  4;
    sample.u |= (data[2] & 0xFF) << 12;
    sample.u |= (data[4] & 0xFF) << 20;
    sample.u |= (data[6] & 0x0F) << 28;

    if (upipe_sdi_dec->debug) {
        uint8_t parity = 0;
        parity += parity_tab[data[0] & 0xf0];
        parity += parity_tab[data[2] & 0xff];
        parity += parity_tab[data[4] & 0xff];
        parity += parity_tab[data[6] & 0x7f];

        if ((parity & 1) != ((data[6] >> 7) & 1)) {
            upipe_err_va(upipe, "wrong audio parity: 0x%.2x 0x%.2x 0x%.2x 0x%.2x",
                    data[0] & 0xff, data[2] & 0xff, data[4] & 0xff, data[6] & 0xff);
        }
    }

    return sample.i;
}

static int aes_parse(struct upipe *upipe, int32_t *buf, size_t samples, int pair, int line)
{
    int data_type = -1;

    static const char *data_type_str[32] = {
        [ 0] = "Null data",
        [ 1] = "ATSC A/52B, (AC-3) data (audio)",
        [ 2] = "Time stamp data",
        [ 3] = "Pause data",
        [ 4] = "Reserved MPEG-1 layer 1 data (audio)",
        [ 5] = "Reserved MPEG-1 layer 2 or 3 audio, MPEG-2 data without extension (audio)",
        [ 6] = "Reserved MPEG-2 data with extension (audio)",
        [ 7] = "Reserved",
        [ 8] = "Reserved MPEG-2 layer 1 data low-sampling frequency (audio)",
        [ 9] = "Reserved MPEG-2 layer 2 or 3 data low-sampling frequency (audio)",
        [10] = "Reserved for MPEG-4 AAC data",
        [11] = "Reserved for MPEG-4 HE-AAC data",
        [12] = "Reserved",
        [13] = "Reserved",
        [14] = "Reserved",
        [15] = "Reserved",
        [16] = "ATSC A/52B, (Enhanced AC-3) data (audio)",
        [17] = "Reserved",
        [18] = "Reserved",
        [19] = "Reserved",
        [20] = "Reserved",
        [21] = "Reserved",
        [22] = "Reserved",
        [23] = "Reserved",
        [24] = "Reserved",
        [25] = "Reserved",
        [26] = "Utility data type (V sync)",
        [27] = "Reserved SMPTE KLV data",
        [28] = "Reserved Dolby E data (audio)",
        [29] = "Captioning data",
        [30] = "User defined data",
        [31] = "Reserved",
    };

    static const char *data_mode_str[4] = {
        [0] = "16-bit",
        [1] = "20-bit",
        [2] = "24-bit",
        [3] = "Reserved",
    };

    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);
    for (size_t n = 0; n < samples; n++) {
        int32_t pa = buf[UPIPE_SDI_MAX_CHANNELS * n + 2 * pair];
        int32_t pb = buf[UPIPE_SDI_MAX_CHANNELS * n + 2 * pair + 1];

        int bits = 0;
        if (       pa == 0x6f872000 &&  pb ==  0x54e1f000) {
            bits = 20;
        } else if (pa ==  0xf8720000 && pb ==   0x4e1f0000) {
            bits = 16;
        } else if (pa == 0x96f87200 &&  pb == 0xa54e1f00) {
            bits = 24;
        } else {
            continue;
        }

        if (n == samples - 1) {
            upipe_err(upipe, "AES synchro was found on last sample");
            break;
        }

        /* read next samples */
        int32_t pc = buf[UPIPE_SDI_MAX_CHANNELS * (n+1) + 2 * pair]; // burst_info
        int32_t pd = buf[UPIPE_SDI_MAX_CHANNELS * (n+1) + 2 * pair + 1]; // length_code

        int32_t preamble[4] = { pa, pb, pc, pd };
        if (!memcmp(preamble, upipe_sdi_dec->aes_preamble[pair], sizeof(preamble))) {
            return (pc >> (32 - bits)) & 0x1f;
        }

        memcpy(upipe_sdi_dec->aes_preamble[pair], preamble, sizeof(preamble));

        pc >>= 16;
        pd >>= 32 - bits;

        unsigned data_stream_number =  pc >> 13;
        //unsigned data_type_dependent= (pc >>  8) & 0x1f;
        unsigned error_flag         = (pc >>  7) & 0x1;
        unsigned data_mode          = (pc >>  5) & 0x3;
        data_type                   = (pc >>  0) & 0x1f;

        const int frame_bits = samples * 2 * bits;

        upipe_notice_va(upipe, "[%d] line %d: AES (%d bits) stream %d (error=%d), mode %s, type %s (length %d/%d bits)",
            pair,
            line,
            bits,
            data_stream_number, error_flag,
            data_mode_str[data_mode],
            data_type_str[data_type],
            pd, frame_bits
            );

        if (pd + 40 > frame_bits) {
            upipe_err_va(upipe, "AES frame probably truncated, need %d bits, only got %d",
                pd, frame_bits);
        }

        break;
    }

    return data_type;
}

static void extract_hd_audio(struct upipe *upipe, const uint16_t *packet, int line_num,
                            struct audio_ctx *ctx)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);
    const struct sdi_offsets_fmt *f = upipe_sdi_dec->f;
    const struct sdi_picture_fmt *p = upipe_sdi_dec->p;
    uint16_t switching_line_offset = p->field_offset - 1;

    int data_count = packet[10] & 0xff;

    int audio_group = S291_HD_AUDIO_GROUP1_DID - (packet[6] & 0xff);
    if (data_count != 0x18) {
        upipe_warn_va(upipe, "Invalid data count 0x%x", data_count);
        return;
    }

    /* Audio packets are not allowed on the switching line + 1 */
    if (upipe_sdi_dec->debug && (line_num == p->switching_line + 1 ||
        (p->field_offset && line_num == p->switching_line + 1 + switching_line_offset))) {
        upipe_warn_va(upipe, "Audio packet on invalid line %d", line_num);
    }

    if (upipe_sdi_dec->debug) {
        /* FIXME: extract this to a generic HD validation function */
        uint16_t checksum = 0;
        int len = data_count + 3 /* DID / DBN / DC */;
        for (int i = 0; i < len/3; i++) {
            checksum += packet[6 + 2*3*i+0];
            checksum += packet[6 + 2*3*i+2];
            checksum += packet[6 + 2*3*i+4];
        }
        checksum &= 0x1ff;

        uint16_t stream_checksum = packet[6+len*2] & 0x1ff;
        if (checksum != stream_checksum) {
            upipe_err_va(upipe, "Invalid checksum: 0x%.3x != 0x%.3x",
                    checksum, stream_checksum
                    );
        }

        /* read ECC from bitstream */
        uint8_t stream_ecc[6];
        for (int i = 0; i < 6; i++)
            stream_ecc[i] = packet[48 + 2*i] & 0xff;

        /* calculate expected ECC */
        uint8_t ecc[6] = { 0 };
        for (int i = 0; i < 48; i += 2) {
            const uint8_t in = ecc[0] ^ (packet[i] & 0xff);
            ecc[0] = ecc[1] ^ in;
            ecc[1] = ecc[2];
            ecc[2] = ecc[3] ^ in;
            ecc[3] = ecc[4] ^ in;
            ecc[4] = ecc[5] ^ in;
            ecc[5] = in;
        }

        if (memcmp(ecc, stream_ecc, sizeof(ecc))) {
            upipe_dbg_va(upipe, "Wrong ECC, %.2x%.2x%.2x%.2x%.2x%.2x != %.2x%.2x%.2x%.2x%.2x%.2x",
                    ecc[0], ecc[1], ecc[2], ecc[3], ecc[4], ecc[5],
                    stream_ecc[0], stream_ecc[1], stream_ecc[2], stream_ecc[3], stream_ecc[4], stream_ecc[5]);
        }

        uint16_t clock = packet[12] & 0xff;
        clock |= (packet[14] & 0x0f) << 8;
        clock |= (packet[14] & 0x20) << 7;
        bool mpf = packet[14] & 0x10;

        /* FIXME */
        if ((line_num >= 9 && line_num <= 9 + 5) || (line_num >= 571 && line_num <= 571 + 5)) {
        } else
            mpf = false;

        uint64_t audio_clock = upipe_sdi_dec->audio_samples[audio_group] *
            f->width * f->height * upipe_sdi_dec->f->fps.num /
            upipe_sdi_dec->f->fps.den / 48000;

        if (unlikely(upipe_sdi_dec->eav_clock == 0))
            upipe_sdi_dec->eav_clock -= clock; // initial phase offset

        int64_t offset = audio_clock -
            (upipe_sdi_dec->eav_clock - (mpf ? f->width : 0));

        if (offset + 1 < clock || offset - 1 > clock) {
            upipe_sdi_dec->eav_clock -= clock - offset;
            if (0) upipe_notice_va(upipe,
                    "audio group %d on line %d: wrong audio phase (mpf %d) CLK %d != %" PRId64 " => %"PRId64"",
                    audio_group, line_num, mpf, clock, offset, offset - clock);
        }
    }

    if (ctx->buf_audio)
        for (int i = 0; i < UPIPE_SDI_CHANNELS_PER_GROUP; i++) {
            int32_t s = extract_hd_audio_sample(upipe, &packet[UPIPE_SDI_MAX_CHANNELS + i * 8]);
            ctx->buf_audio[ctx->group_offset[audio_group] * UPIPE_SDI_MAX_CHANNELS + 4 * audio_group + i] = s;

            if (upipe_sdi_dec->debug && (i & 0x01)) { // check 2nd syncword
                size_t prev = ctx->group_offset[audio_group] * 16 + 4 * audio_group + i - 1;
                if ((s == 0xa54e1f00   && ctx->buf_audio[prev] == 0x96f87200) ||
                        (s ==  0x54e1f000  && ctx->buf_audio[prev] ==  0x6f872000) ||
                        (s ==   0x4e1f0000 && ctx->buf_audio[prev] ==   0xf8720000)) {
                    uint8_t pair = audio_group * 2 + (i >> 1);
                    if (ctx->aes[pair] != -1) {
                        upipe_err_va(upipe, "SMPTE 337 sync at line %d AND %d", ctx->aes[pair], line_num);
                    }
                    ctx->aes[pair] = line_num;
                }
            }
        }

    upipe_sdi_dec->audio_samples[audio_group]++;
    ctx->group_offset[audio_group]++;
}

static inline void validate_dbn(struct upipe *upipe, uint8_t did, uint8_t dbn, int line_num)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);

    assert(did >= 0x80);
    did -= 0x80; /* we only store dbn for type 1 packets */

    if (likely(upipe_sdi_dec->dbn[did] != 0)) {
        uint8_t expected_dbn = upipe_sdi_dec->dbn[did] + 1;
        if (unlikely(expected_dbn == 0))
            expected_dbn = 1; /* DBN cycles from 255 to 1 */

        if (expected_dbn != dbn)
            upipe_err_va(upipe, "[%u] [DID 0x%.2x] Wrong DBN: 0x%.2x -> 0x%.2x",
                line_num, did + 0x80, upipe_sdi_dec->dbn[did], dbn);
    } else if (dbn != 0) {
        upipe_dbg_va(upipe, "[DID 0x%.2x] Checking DBN", did + 0x80);
    }

    upipe_sdi_dec->dbn[did] = dbn;
}

static int parse_hd_hanc(struct upipe *upipe, const uint16_t *packet, int line_num,
                         struct audio_ctx *ctx)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);
    uint8_t did = packet[6] & 0xff;

    if (upipe_sdi_dec->debug && did >= 0x80) { /* type 1 packet */
        validate_dbn(upipe, did, packet[8] & 0xff, line_num);
    }

    switch (did) {
    case S291_HD_AUDIO_GROUP1_DID:
    case S291_HD_AUDIO_GROUP2_DID:
    case S291_HD_AUDIO_GROUP3_DID:
    case S291_HD_AUDIO_GROUP4_DID:
        extract_hd_audio(upipe, packet, line_num, ctx);
        break;

    default:
        break;
    }

    return 2 * (S291_HEADER_SIZE + (packet[10] & 0xff) + S291_FOOTER_SIZE);
}

static void extract_sd_audio(struct upipe *upipe, const uint16_t *packet,
                            struct audio_ctx *ctx)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);

    int data_count = packet[5] & 0xff;
    if (data_count % 12) {
        upipe_err_va(upipe, "Invalid data count %d", data_count);
        return;
    }

    /* Slightly different to HD */
    int audio_group = (S291_SD_AUDIO_GROUP1_DID - (packet[3] & 0xff)) >> 1;

    /* FIXME: extract this to a generic SD validation function */
    uint16_t checksum = 0;
    int len = data_count + 3 /* DID / DBN / DC */;
    for (int i = 0; i < len; i++)
        checksum += packet[3+i];
    checksum &= 0x1ff;

    uint16_t stream_checksum = packet[3+len] & 0x1ff;
    if (upipe_sdi_dec->debug && checksum != stream_checksum) {
        upipe_err_va(upipe, "Invalid checksum: 0x%.3x != 0x%.3x",
                     checksum, stream_checksum);
        return;
    }

    const uint16_t *src = &packet[6];
    for (int i = 0; i < data_count/3; i += UPIPE_SDI_CHANNELS_PER_GROUP) {
        int32_t *dst = &ctx->buf_audio[ctx->group_offset[audio_group] * UPIPE_SDI_MAX_CHANNELS +
                                       audio_group * UPIPE_SDI_CHANNELS_PER_GROUP];
        extract_sd_audio_group(upipe, dst, &src[3*i]);

        upipe_sdi_dec->audio_samples[audio_group]++;
        ctx->group_offset[audio_group]++;
    }
}

static int parse_sd_hanc(struct upipe *upipe, const uint16_t *packet,
                         struct audio_ctx *ctx)
{
    switch (packet[3] & 0xff) {
    case S291_SD_AUDIO_GROUP1_DID:
    case S291_SD_AUDIO_GROUP2_DID:
    case S291_SD_AUDIO_GROUP3_DID:
    case S291_SD_AUDIO_GROUP4_DID:
        extract_sd_audio(upipe, packet, ctx);
        break;

    default:
        break;
    }

    return S291_HEADER_SIZE + (packet[5] & 0xff) + S291_FOOTER_SIZE;
}

static inline bool validate_anc_len(const uint16_t *packet, int left, bool sd)
{
    int data_count = (sd ? packet[5] : packet[10]) & 0xff;
    int total_size =  S291_HEADER_SIZE + data_count + S291_FOOTER_SIZE;

    if (!sd)
        total_size *= 2;

    return left >= total_size;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_sdi_dec_handle(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_sdi_dec_store_flow_def(upipe, NULL);
        upipe_sdi_dec_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (!upipe_sdi_dec->ubuf_mgr)
        return false;

    const struct sdi_offsets_fmt *f = upipe_sdi_dec->f;
    const struct sdi_picture_fmt *p = upipe_sdi_dec->p;
    const size_t output_hsize = p->active_width, output_vsize = p->active_height;

    /* map input */
    int input_size = -1;
    const uint8_t *input_buf = NULL;
    if (unlikely(!ubase_check(uref_block_read(uref, 0, &input_size, &input_buf)))) {
        upipe_warn(upipe, "unable to map input");
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    const struct urational *fps = &upipe_sdi_dec->f->fps;
    uint64_t pts = UINT32_MAX + upipe_sdi_dec->frame_num++ *
        UCLOCK_FREQ * fps->den / fps->num;

    uref_clock_set_pts_prog(uref, pts);
    uref_clock_set_pts_orig(uref, pts);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_dts_delay(uref, 0);
    bool discontinuity = ubase_check(uref_flow_get_discontinuity(uref));
    upipe_throw_clock_ref(upipe, uref, pts, discontinuity);
    upipe_throw_clock_ts(upipe, uref);

    if (!p->sd && upipe_sdi_dec->debug) {
        for (int h = 0; h < f->height; h++) {
            const uint16_t *src = (uint16_t*)&input_buf[h * 2 * sizeof(uint16_t) * f->width];
            uint16_t crc[4];
            bool first_line = upipe_sdi_dec->crc_c == 0 &&
                              upipe_sdi_dec->crc_y == 0;

            if (!first_line ) {
                for (int i = 0; i < 12; i += 2) {
                    sdi_crc_update(upipe_sdi_dec->crc_lut[0], &upipe_sdi_dec->crc_c, src[i + 0]);
                    sdi_crc_update(upipe_sdi_dec->crc_lut[0], &upipe_sdi_dec->crc_y, src[i + 1]);
                }

                sdi_crc_end(&upipe_sdi_dec->crc_c, &crc[0]);
                sdi_crc_end(&upipe_sdi_dec->crc_y, &crc[1]);

                uint16_t stream_crc[4];
                for (int i = 0; i < 4; i++) {
                    stream_crc[i] = src[12+i];
                }

                if (memcmp(crc, stream_crc, sizeof(crc))) {
                    upipe_err_va(upipe, "Line %d CRC does not match: "
                            "0x%.4x%.4x%.4x%.4x != 0x%.4x%.4x%.4x%.4x", h+1,
                            crc[0], crc[1], crc[2], crc[3],
                            stream_crc[0], stream_crc[1], stream_crc[2], stream_crc[3]);
                }

            }

            for (int i = 0; i < 2*output_hsize; i+=16) {
                const uint16_t *crc_src = &src[2*f->active_offset + i];
                sdi_crc_update_blk(upipe_sdi_dec->crc_lut, &upipe_sdi_dec->crc_c, &upipe_sdi_dec->crc_y, crc_src);
            }
        }
    }

    /* allocate dest ubuf */
    size_t aligned_output_hsize = ((output_hsize + 5) / 6) * 6;
    struct ubuf *ubuf = ubuf_pic_alloc(upipe_sdi_dec->ubuf_mgr,
                                       aligned_output_hsize, output_vsize);
    if (unlikely(ubuf == NULL)) {
        upipe_warn(upipe, "unable to allocate output");
        uref_block_unmap(uref, 0);
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    /* map output */
    uint8_t *fields[2][UPIPE_SDI_DEC_MAX_PLANES] = { {0}, {0} };
    size_t output_stride[UPIPE_SDI_DEC_MAX_PLANES] = {0};
    for (int i = 0; i < UPIPE_SDI_DEC_MAX_PLANES; i++) {
        const char *c = upipe_sdi_dec->output_chroma_map[i];
        if (c == NULL)
            break;
        if (unlikely(!ubase_check(ubuf_pic_plane_write(ubuf, c,
                                           0, 0, -1, -1, &fields[0][i])) ||
                     !ubase_check(ubuf_pic_plane_size(ubuf, c,
                                           &output_stride[i], NULL, NULL, NULL)))) {
            upipe_warn(upipe, "unable to map output");
            ubuf_free(ubuf);
            uref_free(uref);
            return true;
        }
    }

    bool ntsc = p->active_height == 486;
    for (int i = 0; i < UPIPE_SDI_DEC_MAX_PLANES; i++)
        if (fields[0][i]) {
            /* NTSC is bottom field first */
            if (ntsc) {
                fields[1][i] = fields[0][i];
                fields[0][i] += output_stride[i];
            }
            else {
                fields[1][i] = fields[0][i] + output_stride[i];
            }
        }

    if (!f->psf_ident) {
        output_stride[0] *= 2;
        output_stride[1] *= 2;
        output_stride[2] *= 2;
    }

    struct uref *uref_audio = NULL;

    struct audio_ctx audio_ctx = {0};
    for (int i = 0; i < 8; i++)
        audio_ctx.aes[i] = -1;

    struct uref *uref_vbi = NULL;
    int vbi_line = 0;
    uint8_t *vbi_buf = NULL;
    struct upipe_sdi_dec_sub *vbi_sub = &upipe_sdi_dec->vbi;
    if (p->sd) {
        if (!vbi_sub->ubuf_mgr) {
            struct uref *vbi_flow_def = uref_sibling_alloc(uref);
            uref_flow_set_def(vbi_flow_def, "pic.");
            UBASE_RETURN(uref_pic_flow_set_macropixel(vbi_flow_def, 1))
                UBASE_RETURN(uref_pic_flow_add_plane(vbi_flow_def, 1, 1, 1, "y8"))
                upipe_sdi_dec_sub_require_ubuf_mgr(&vbi_sub->upipe, vbi_flow_def);
        }

        uref_vbi = uref_dup(uref);
        struct ubuf *ubuf_vbi = ubuf_pic_alloc(vbi_sub->ubuf_mgr, 720,
                f->height - f->pict_fmt->active_height);

        if (unlikely(ubuf_vbi == NULL)) {
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
            uref_free(uref_vbi);
            uref_vbi = NULL;
        } else {
            uref_attach_ubuf(uref_vbi, ubuf_vbi);
            if (unlikely(!ubase_check(uref_pic_plane_write(uref_vbi, "y8",
                                0, 0, -1, -1, &vbi_buf)))) {
                uref_free(uref_vbi);
                uref_vbi = NULL;
                upipe_err(upipe, "Could not map vbi buffer");
            }
        }
    }

    struct upipe_sdi_dec_sub *vanc_sub = &upipe_sdi_dec->vanc;
    if (!vanc_sub->ubuf_mgr) {
        struct uref *vanc_flow_def = uref_sibling_alloc(uref);
        uref_flow_set_def(vanc_flow_def, "pic.");
        UBASE_RETURN(uref_pic_flow_set_macropixel(vanc_flow_def, 1))
        UBASE_RETURN(uref_pic_flow_add_plane(vanc_flow_def, 1, 1, 2, "x10"))
        upipe_sdi_dec_sub_require_ubuf_mgr(&vanc_sub->upipe, vanc_flow_def);
    }

    struct uref *uref_vanc = uref_dup(uref);
    struct ubuf *ubuf_vanc = NULL;
    if (!p->sd) {
        ubuf_vanc = ubuf_pic_alloc(vanc_sub->ubuf_mgr,
            f->pict_fmt->active_width * 2,
            f->height - f->pict_fmt->active_height);
        if (unlikely(ubuf_vanc == NULL))
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
    }

    size_t vanc_stride;
    uint8_t *vanc_buf = NULL;
    if (unlikely(ubuf_vanc == NULL)) {
        uref_free(uref_vanc);
        uref_vanc = NULL;
    } else {
        uref_attach_ubuf(uref_vanc, ubuf_vanc);
        if (unlikely(!ubase_check(uref_pic_plane_size(uref_vanc, "x10",
                            &vanc_stride, NULL, NULL, NULL)) ||
                    !ubase_check(uref_pic_plane_write(uref_vanc, "x10",
                            0, 0, -1, -1, &vanc_buf)))) {
            uref_free(uref_vanc);
            uref_vanc = NULL;
            upipe_err(upipe, "Could not map vanc buffer");
        }
    }

    struct upipe_sdi_dec_sub *audio_sub = &upipe_sdi_dec->audio;
    uref_audio = uref_dup(uref);
    if (!audio_sub->ubuf_mgr) {
        uref_flow_set_def(uref_audio, "sound.s32.");
        uref_sound_flow_add_plane(uref_audio, "lrcLRS0123456789");
        uref_sound_flow_set_channels(uref_audio, 16);
        uref_sound_flow_set_rate(uref_audio, 48000);
        uref_sound_flow_set_sample_size(uref_audio, sizeof(int32_t) * 16);
        upipe_sdi_dec_sub_require_ubuf_mgr(&audio_sub->upipe,
                uref_dup(uref_audio));
        uref_flow_delete_def(uref_audio);
    }

    struct ubuf *ubuf_sound = NULL;
    if (audio_sub->ubuf_mgr) {
        ubuf_sound = ubuf_sound_alloc(audio_sub->ubuf_mgr, 1125*2);
        if (!ubuf_sound)
            upipe_err(upipe, "Unable to allocate a sound buffer");
    }
    if (unlikely(!ubuf_sound)) {
        uref_free(uref_audio);
        uref_audio = NULL;
    } else {
        uref_attach_ubuf(uref_audio, ubuf_sound);
        if (unlikely(!ubase_check(uref_sound_plane_write_int32_t(uref_audio,
                            "lrcLRS0123456789", 0, -1, &audio_ctx.buf_audio)))) {
            uref_free(uref_audio);
            uref_audio = NULL;
            upipe_err(upipe, "Could not map audio buffer");
        }
    }

    uint8_t vbi[720 * 32];

    /* Parse the whole frame */
    for (int h = 0; h < f->height; h++) {
        /* HANC starts at end of EAV */
        const uint8_t hanc_start = p->sd ? UPIPE_SDI_EAV_LENGTH : UPIPE_HD_SDI_EAV_LENGTH;
        const uint8_t sav_len = p->sd ? UPIPE_SDI_SAV_LENGTH : UPIPE_HD_SDI_SAV_LENGTH;
        const int hanc_len = 2 * f->active_offset - hanc_start - sav_len;
        int line_num = h + 1;

        /* Use wraparound arithmetic to start at line 4 */
        if (ntsc)
            line_num = ((line_num + 2) % 525) + 1;

        /* Horizontal Blanking */
        uint16_t *line = (uint16_t *)input_buf + h * f->width * 2 + hanc_start;
        if (p->sd) {
            for (int v = 0; v < hanc_len; v++) {
                const uint16_t *packet = line + v;
                int left = hanc_len - v;

                if (packet[0] == S291_ADF1 && packet[1] == S291_ADF2 && packet[2] == S291_ADF3 &&
                    validate_anc_len(packet, left, true))
                {
                    /* - 1 to compensate for v++ above */
                    v += parse_sd_hanc(upipe, packet, &audio_ctx) - 1;
                }
                else
                {
                    /* Ancillary data packets must be contiguous and left aligned */
                    break;
                }
            }
        } else {
            for (int v = 0; v < hanc_len; v++) {
                const uint16_t *packet = line + v;
                int left = hanc_len - v;

                if (packet[0] == S291_ADF1 && packet[2] == S291_ADF2 && packet[4] == S291_ADF3 &&
                    validate_anc_len(packet, left, false))
                {
                    /* - 1 to compensate for v++ above */
                    v += parse_hd_hanc(upipe, packet, line_num, &audio_ctx) - 1;
                }
                else
                {
                    /* Ancillary data packets must be contiguous and left aligned */
                    break;
                }
            }
        }

        bool active = 0, f2 = 0, special_case = 0;
        /* ACTIVE F1 */
        if (line_num >= p->active_f1.start && line_num <= p->active_f1.end)
            active = 1;

        /* Treat NTSC Line 20 (Field 1) as a special case like BMD does */
        if (ntsc && line_num == 20)
            special_case = 1;

        if (!f->psf_ident) {
            f2 = line_num >= p->vbi_f2_part1.start;
            /* ACTIVE F2 */
            if (line_num >= p->active_f2.start && line_num <= p->active_f2.end)
                active = 1;
        }

        if (upipe_sdi_dec->debug) {
            const uint16_t *src = (uint16_t*)&input_buf[h * 2 * sizeof(uint16_t) * f->width];
            const uint16_t *active_start = &src[2*f->active_offset];
            bool vbi = !active;

            if (p->sd) {
                if (src[0] != 0x3ff
                        || src[1] != 0x000
                        || src[2] != 0x000
                        || src[3] != eav_fvh_cword[f2][vbi])
                    upipe_err_va(upipe, "SD EAV incorrect, line %d", h);

                if (active_start[-4] != 0x3ff
                        || active_start[-3] != 0x000
                        || active_start[-2] != 0x000
                        || active_start[-1] != sav_fvh_cword[f2][vbi])
                    upipe_err_va(upipe, "SD SAV incorrect, line %d", h);
            } else {
                if (src[0] != 0x3ff
                        || src[1] != 0x3ff
                        || src[2] != 0x000
                        || src[3] != 0x000
                        || src[4] != 0x000
                        || src[5] != 0x000
                        || src[6] != eav_fvh_cword[f2][vbi]
                        || src[7] != eav_fvh_cword[f2][vbi])
                    upipe_err_va(upipe, "HD EAV incorrect, line %d", h);

                int line_num_check[2] = {
                    (line_num & 0x7f) << 2,
                    (1 << 9) | (((line_num >> 7) & 0xf) << 2),
                };
                line_num_check[0] |= NOT_BIT8(line_num_check[0]);

                if (src[8] != line_num_check[0]
                        || src[ 9] != line_num_check[0]
                        || src[10] != line_num_check[1]
                        || src[11] != line_num_check[1])
                    upipe_err_va(upipe, "HD line num incorrect, line %d", h);

                if ( active_start[-8] != 0x3ff
                        || active_start[-7] != 0x3ff
                        || active_start[-6] != 0x000
                        || active_start[-5] != 0x000
                        || active_start[-4] != 0x000
                        || active_start[-3] != 0x000
                        || active_start[-2] != sav_fvh_cword[f2][vbi]
                        || active_start[-1] != sav_fvh_cword[f2][vbi])
                    upipe_err_va(upipe, "HD SAV incorrect, line %d", h);
            }
        }

        uint16_t *src_line = (uint16_t*)input_buf + (h * f->width + f->active_offset) * 2;
        if (!active || special_case) {
            // deinterleave for vanc_filter
            if (p->sd) {
                /* Only part 1 of vbi */
                if ((!f2 && line_num <= p->vbi_f1_part1.end) ||
                        (f2 && line_num <= p->vbi_f2_part1.end)) {
                    for (int i = 0; i < 720; i++) {
                        vbi_buf[720 * vbi_line + i] =
                            (src_line[2*i + 1] >> 2) & 0xff;
                    }
                    vbi_line++;
                }
            } else {
#if defined(HAVE_X86ASM)
#if defined(__i686__) || defined(__x86_64__)
                if (__builtin_cpu_supports("ssse3"))
                    upipe_sdi_vanc_deinterleave_ssse3(vanc_buf, vanc_stride, src_line, 0);
                else
#endif
#endif
                {
                uint16_t *vanc_dst = (uint16_t*)vanc_buf;
                if (uref_vanc) {
                    for (unsigned i = 0; i < vanc_stride / 4; i++) {
                        vanc_dst[                i] = src_line[2*i  ]; // Y
                        vanc_dst[vanc_stride/4 + i] = src_line[2*i+1];  // C
                    }
                    vanc_buf += vanc_stride;
                }
                }
            }
        } else {
            uint8_t *y = fields[f2][0];
            uint8_t *u = fields[f2][1];
            uint8_t *v = fields[f2][2];

            if (upipe_sdi_dec->output_is_v210)
                upipe_sdi_dec->uyvy_to_v210(src_line, y, output_hsize);
            else if (upipe_sdi_dec->output_bit_depth == 8)
                upipe_sdi_dec->uyvy_to_planar_8(y, u, v, src_line, output_hsize);
            else
                upipe_sdi_dec->uyvy_to_planar_10((uint16_t *)y, (uint16_t *)u, (uint16_t *)v, src_line, output_hsize);

            fields[f2][0] += output_stride[0];
            fields[f2][1] += output_stride[1];
            fields[f2][2] += output_stride[2];
        }
        upipe_sdi_dec->eav_clock += f->width;
    }

    if (uref_audio) {
        // FIXME: ntsc - a/v pts need to be mostly equal, in case we receive
        // frames without audio
        //uint64_t pts = UINT32_MAX + audio_sub->samples * UCLOCK_FREQ / 48000;
        //uref_clock_set_pts_prog(uref_audio, pts);
        //uref_clock_set_pts_orig(uref_audio, pts);
        //uref_clock_set_dts_pts_delay(uref_audio, 0);

        int samples_received = audio_ctx.group_offset[0];
        for (int i = 1; i < UPIPE_SDI_CHANNELS_PER_GROUP; i++) {
            if (audio_ctx.group_offset[i] == samples_received)
                continue;

            if (samples_received < audio_ctx.group_offset[i])
                samples_received = audio_ctx.group_offset[i];
            //upipe_err_va(upipe, "%zu samples on group %d", audio_ctx.group_offset[i], i);
        }

        unsigned expected = 48000 * fps->den / fps->num;
        if (samples_received < expected) {
            unsigned wrong_samples = samples_received;
            samples_received = expected;

            if (fps->den == 1001 && fps->num != 24000) {
                if (unlikely(fps->num != 30000 && fps->num != 60000)) {
                    upipe_err_va(upipe, "Unsupported rate %" PRIu64"/%" PRIu64,
                            fps->num, fps->den);
                } else {
                    /* cyclic loop of 5 different sample counts */
                    if (++upipe_sdi_dec->audio_fix == 5)
                        upipe_sdi_dec->audio_fix = 0;

                    static const uint8_t samples_increment[2][5] = {
                        { 1, 0, 1, 0, 1 }, /* 30000 / 1001 */
                        { 1, 1, 1, 1, 0 }  /* 60000 / 1001 */
                    };

                    bool rate5994 = !!(fps->num == 60000);

                    samples_received += samples_increment[rate5994][upipe_sdi_dec->audio_fix];
                }
            }
            upipe_dbg_va(upipe, "Not enough audio samples correcting %u to %u",
                wrong_samples, samples_received);
        }

        if (upipe_sdi_dec->debug) {
            for (int i = 0; i < 8; i++) {
                if (audio_ctx.aes[i] != -1) {
                    int data_type = aes_parse(upipe, audio_ctx.buf_audio, samples_received, i, audio_ctx.aes[i]);
                    if (data_type == S337_TYPE_A52 || data_type == S337_TYPE_A52E || data_type == -1)
                        audio_ctx.aes[i] = data_type;
                }
                if (audio_ctx.aes[i] != upipe_sdi_dec->aes_detected[i]) {
                    if (upipe_sdi_dec->aes_detected[i] > 0) {
                        upipe_err_va(upipe, "[%d] : %s AES 337 stream %d -> %d)",
                                i,
                                (audio_ctx.aes[i] != -1) ? "moved" : "lost",
                                upipe_sdi_dec->aes_detected[i], audio_ctx.aes[i]);
                        if (audio_ctx.aes[i] == -1)
                            memset(upipe_sdi_dec->aes_preamble[i], 0, sizeof(upipe_sdi_dec->aes_preamble[i]));
                    }
                    upipe_sdi_dec->aes_detected[i] = audio_ctx.aes[i];
                }
            }
        }

        audio_sub->samples += samples_received;;
        uref_sound_plane_unmap(uref_audio, "lrcLRS0123456789", 0, -1);
        uref_sound_resize(uref_audio, 0, samples_received);

        if (samples_received == 0)
            uref_free(uref_audio);
        else
            upipe_sdi_dec_sub_output(&audio_sub->upipe, uref_audio, upump_p);
    }

    if (uref_vbi) {
        uref_pic_plane_unmap(uref_vbi, "y8", 0, 0, -1, -1);
        upipe_sdi_dec_sub_output(&vbi_sub->upipe, uref_vbi, upump_p);
    }

    if (uref_vanc) {
        uref_pic_plane_unmap(uref_vanc, "x10", 0, 0, -1, -1);
        upipe_sdi_dec_sub_output(&vanc_sub->upipe, uref_vanc, upump_p);
    }

    /* unmap input */
    uref_block_unmap(uref, 0);

    /* unmap output */
    for (int i = 0; i < UPIPE_SDI_DEC_MAX_PLANES; i++) {
        const char *c = upipe_sdi_dec->output_chroma_map[i];
        if (c == NULL)
            break;
        ubuf_pic_plane_unmap(ubuf, c, 0, 0, -1, -1);
    }

    uref_attach_ubuf(uref, ubuf);
    upipe_sdi_dec_output(upipe, uref, upump_p);

    return true;
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_sdi_dec_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    if (!upipe_sdi_dec_check_input(upipe)) {
        upipe_sdi_dec_hold_input(upipe, uref);
        upipe_sdi_dec_block_input(upipe, upump_p);
    } else if (!upipe_sdi_dec_handle(upipe, uref, upump_p)) {
        upipe_sdi_dec_hold_input(upipe, uref);
        upipe_sdi_dec_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_sdi_dec_sub_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL) {
        struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_sub_mgr(upipe->mgr);
        uref_clock_set_latency(flow_format, upipe_sdi_dec->latency);
        upipe_sdi_dec_sub_store_flow_def(upipe, flow_format);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_sdi_dec_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_sdi_dec_store_flow_def(upipe, flow_format);

    if (upipe_sdi_dec->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_sdi_dec_check_input(upipe);
    upipe_sdi_dec_output_input(upipe);
    upipe_sdi_dec_unblock_input(upipe);
    if (was_buffered && upipe_sdi_dec_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_sdi_dec_input. */
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
static int upipe_sdi_dec_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);

    upipe_sdi_dec->f = sdi_get_offsets(flow_def);
    if (!upipe_sdi_dec->f) {
        upipe_err(upipe, "Could not figure out SDI offsets");
        return UBASE_ERR_INVALID;
    }
    upipe_sdi_dec->p = upipe_sdi_dec->f->pict_fmt;

    if (!ubase_check(uref_clock_get_latency(flow_def, &upipe_sdi_dec->latency)))
        upipe_sdi_dec->latency = 0;

    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;

    uref_flow_set_def(flow_def_dup, "pic.");

    uref_pic_flow_set_align(flow_def_dup, 64);
    if (upipe_sdi_dec->output_is_v210) {
        upipe_sdi_dec->output_chroma_map[0] = "u10y10v10y10u10y10v10y10u10y10v10y10";
        upipe_sdi_dec->output_chroma_map[1] = NULL;
        upipe_sdi_dec->output_chroma_map[2] = NULL;
        uref_pic_flow_set_planes(flow_def_dup, 1);
        uref_pic_flow_set_macropixel(flow_def_dup, 6);
        uref_pic_flow_set_macropixel_size(flow_def_dup, 16, 0);
        uref_pic_flow_set_chroma(flow_def_dup, upipe_sdi_dec->output_chroma_map[0], 0);
    } else if (upipe_sdi_dec->output_bit_depth == 8) {
        upipe_sdi_dec->output_chroma_map[0] = "y8";
        upipe_sdi_dec->output_chroma_map[1] = "u8";
        upipe_sdi_dec->output_chroma_map[2] = "v8";
        UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def_dup, 1))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 1, 1, 1, "y8"))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 2, 1, 1, "u8"))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 2, 1, 1, "v8"))
    } else {
        upipe_sdi_dec->output_chroma_map[0] = "y10l";
        upipe_sdi_dec->output_chroma_map[1] = "u10l";
        upipe_sdi_dec->output_chroma_map[2] = "v10l";
        UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def_dup, 1))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 1, 1, 2, "y10l"))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 2, 1, 2, "u10l"))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 2, 1, 2, "v10l"))
    }

    uref_pic_flow_set_fps(flow_def_dup, upipe_sdi_dec->f->fps);

    uref_pic_flow_set_hsize(flow_def_dup, upipe_sdi_dec->p->active_width);
    uref_pic_flow_set_vsize(flow_def_dup, upipe_sdi_dec->p->active_height);

    if (upipe_sdi_dec->f->psf_ident)
        uref_pic_set_progressive(flow_def_dup);

    // FIXME is this correct
    uref_pic_flow_set_hsubsampling(flow_def_dup, 1, 0);
    uref_pic_flow_set_vsubsampling(flow_def_dup, 1, 0);

    upipe_input(upipe, flow_def_dup, NULL);

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_sdi_dec_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);

    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_sdi_dec_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_sdi_dec_free_output_proxy(upipe, request);
        }

        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sdi_dec_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_sdi_dec_set_output(upipe, output);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_sdi_dec_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_sdi_dec_set_flow_def(upipe, flow);
        }
        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *value  = va_arg(args, const char *);
            return upipe_sdi_dec_set_option(upipe, option, value);
        }
        case UPIPE_SDI_DEC_GET_VANC_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SDI_DEC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_sdi_dec_sub_to_upipe(&upipe_sdi_dec->vanc);
            return UBASE_ERR_NONE;
        }
        case UPIPE_SDI_DEC_GET_VBI_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SDI_DEC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_sdi_dec_sub_to_upipe(&upipe_sdi_dec->vbi);
            return UBASE_ERR_NONE;
        }
        case UPIPE_SDI_DEC_GET_AUDIO_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SDI_DEC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_sdi_dec_sub_to_upipe(&upipe_sdi_dec->audio);
            return UBASE_ERR_NONE;
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            if (*p == NULL) {
                *p = &upipe_sdi_dec->vanc.upipe;
            } else if (*p == &upipe_sdi_dec->vanc.upipe) {
                *p = &upipe_sdi_dec->vbi.upipe;
            } else if (*p == &upipe_sdi_dec->vbi.upipe) {
                *p = &upipe_sdi_dec->audio.upipe;
            } else {
                *p = NULL;
            }
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a sdi_dec pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_sdi_dec_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    if (signature != UPIPE_SDI_DEC_SIGNATURE)
        return NULL;

    struct uprobe *uprobe_vanc = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_vbi = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_audio = va_arg(args, struct uprobe *);
    struct uref *flow_def = uref_dup(va_arg(args, struct uref *));
    struct upipe_sdi_dec *upipe_sdi_dec = malloc(sizeof(struct upipe_sdi_dec));
    if (unlikely(upipe_sdi_dec == NULL)) {
        uprobe_release(uprobe_vanc);
        uprobe_release(uprobe_vbi);
        uprobe_release(uprobe_audio);
        uref_free(flow_def);
        return NULL;
    }

    struct upipe *upipe = upipe_sdi_dec_to_upipe(upipe_sdi_dec);
    upipe_init(upipe, mgr, uprobe);

    /* Get the given flow format and enable v210 as appropriate */
    upipe_sdi_dec->output_is_v210 = ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 16, "u10y10v10y10u10y10v10y10u10y10v10y10"));
    if (!upipe_sdi_dec->output_is_v210)
         upipe_sdi_dec->output_bit_depth = ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) ? 8 : 10;

    uref_free(flow_def);

    upipe_sdi_dec->uyvy_to_v210 = upipe_uyvy_to_v210_c;
    upipe_sdi_dec->uyvy_to_planar_8 = upipe_uyvy_to_planar_8_c;
    upipe_sdi_dec->uyvy_to_planar_10 = upipe_uyvy_to_planar_10_c;

#if defined(HAVE_X86ASM)
#if defined(__i686__) || defined(__x86_64__)
    if (__builtin_cpu_supports("ssse3")) {
        upipe_sdi_dec->uyvy_to_v210 = upipe_uyvy_to_v210_ssse3;
        upipe_sdi_dec->uyvy_to_planar_8 = upipe_uyvy_to_planar_8_ssse3;
        upipe_sdi_dec->uyvy_to_planar_10 = upipe_uyvy_to_planar_10_ssse3;
    }

    if (__builtin_cpu_supports("avx")) {
        upipe_sdi_dec->uyvy_to_v210 = upipe_uyvy_to_v210_avx;
        upipe_sdi_dec->uyvy_to_planar_8 = upipe_uyvy_to_planar_8_avx;
        upipe_sdi_dec->uyvy_to_planar_10 = upipe_uyvy_to_planar_10_avx;
    }

    if (__builtin_cpu_supports("avx2")) {
        upipe_sdi_dec->uyvy_to_v210 = upipe_uyvy_to_v210_avx2;
        upipe_sdi_dec->uyvy_to_planar_8 = upipe_uyvy_to_planar_8_avx2;
        upipe_sdi_dec->uyvy_to_planar_10 = upipe_uyvy_to_planar_10_avx2;
    }
#endif
#endif

    upipe_sdi_dec->audio_fix = 0;

    upipe_sdi_dec->crc_y = 0;
    upipe_sdi_dec->crc_c = 0;

    sdi_crc_setup(upipe_sdi_dec->crc_lut);

    upipe_sdi_dec->debug = 0;
    for (int i = 0; i < UPIPE_SDI_CHANNELS_PER_GROUP; i++)
        upipe_sdi_dec->audio_samples[i] = 0;
    for (int i = 0; i < 8; i++)
        upipe_sdi_dec->aes_detected[i] = -1;
    upipe_sdi_dec->eav_clock = 0;
    upipe_sdi_dec->frame_num = 0;

    for (int i = 0; i < 8; i++)
        for (int j = 0; j < UPIPE_SDI_CHANNELS_PER_GROUP; j++)
            upipe_sdi_dec->aes_preamble[i][j] = 0;

    memset(upipe_sdi_dec->dbn, 0, sizeof(upipe_sdi_dec->dbn));

    upipe_sdi_dec_init_urefcount(upipe);
    upipe_sdi_dec_init_ubuf_mgr(upipe);
    upipe_sdi_dec_init_output(upipe);
    upipe_sdi_dec_init_sub_mgr(upipe);
    upipe_sdi_dec_init_input(upipe);

    upipe_sdi_dec_sub_init(upipe_sdi_dec_sub_to_upipe(&upipe_sdi_dec->vanc),
                              &upipe_sdi_dec->sub_mgr, uprobe_vanc);
    upipe_sdi_dec_sub_init(upipe_sdi_dec_sub_to_upipe(&upipe_sdi_dec->vbi),
                              &upipe_sdi_dec->sub_mgr, uprobe_vbi);
    upipe_sdi_dec_sub_init(upipe_sdi_dec_sub_to_upipe(&upipe_sdi_dec->audio),
                              &upipe_sdi_dec->sub_mgr, uprobe_audio);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_sdi_dec_free(struct upipe *upipe)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);

    upipe_throw_dead(upipe);

    upipe_sdi_dec_sub_clean(upipe_sdi_dec_sub_to_upipe(&upipe_sdi_dec->vanc));
    upipe_sdi_dec_sub_clean(upipe_sdi_dec_sub_to_upipe(&upipe_sdi_dec->vbi));
    upipe_sdi_dec_sub_clean(upipe_sdi_dec_sub_to_upipe(&upipe_sdi_dec->audio));

    upipe_sdi_dec_clean_input(upipe);
    upipe_sdi_dec_clean_output(upipe);
    upipe_sdi_dec_clean_ubuf_mgr(upipe);
    upipe_sdi_dec_clean_urefcount(upipe);
    upipe_clean(upipe);
    free(upipe_sdi_dec);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_sdi_dec_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SDI_DEC_SIGNATURE,

    .upipe_alloc = _upipe_sdi_dec_alloc,
    .upipe_input = upipe_sdi_dec_input,
    .upipe_control = upipe_sdi_dec_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for sdi_dec pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sdi_dec_mgr_alloc(void)
{
    return &upipe_sdi_dec_mgr;
}

