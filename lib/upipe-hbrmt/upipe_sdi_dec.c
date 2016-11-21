/*
 * SDI decoder
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
 *
 * This file is based on the implementation in FFmpeg.
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

#include <libavutil/common.h>
#include <libavutil/intreadwrite.h>

#define UPIPE_SDI_DEC_MAX_PLANES 3
#define UPIPE_SDI_MAX_CHANNELS 16

#define ZERO_IDX(x) (x-1)

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
    void (*uyvy_to_v210)(const uint16_t *y, uint8_t *dst, int64_t width);

    /** UYVY to 8-bit Planar */
    void (*uyvy_to_planar_8)(uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, const int64_t width);

    /** UYVY to 10-bit Planar */
    void (*uyvy_to_planar_10)(uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, const int64_t width);

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

    /* presence of an AES stream */
    int aes_detected[8];

    int32_t aes_preamble[8][4];

    int64_t eav_clock;
    /* Per audio group number of samples written */
    uint64_t audio_samples[4];

    /** first pts */
    uint64_t first_pts;

    /** public upipe structure */
    struct upipe upipe;
};

struct audio_ctx {
    int32_t *buf_audio;
    size_t group_offset[4];
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
UPIPE_HELPER_FLOW(upipe_sdi_dec, NULL);
UPIPE_HELPER_OUTPUT(upipe_sdi_dec, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_sdi_dec, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_sdi_dec_check,
                      upipe_sdi_dec_register_output_request,
                      upipe_sdi_dec_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_sdi_dec, urefs, nb_urefs, max_urefs, blockers, upipe_sdi_dec_handle)

UPIPE_HELPER_UPIPE(upipe_sdi_dec_sub, upipe, UPIPE_SDI_DEC_SUB_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_sdi_dec_sub, urefcount, upipe_sdi_dec_sub_free)
UPIPE_HELPER_VOID(upipe_sdi_dec_sub)
UPIPE_HELPER_OUTPUT(upipe_sdi_dec_sub, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_sdi_dec_sub, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_sdi_dec_sub_check,
                      upipe_sdi_dec_sub_register_output_request,
                      upipe_sdi_dec_sub_unregister_output_request)

UPIPE_HELPER_SUBPIPE(upipe_sdi_dec, upipe_sdi_dec_sub, sub, sub_mgr, subs, uchain)

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
    struct upipe_sdi_dec_sub *sdi_dec_sub = upipe_sdi_dec_sub_from_upipe(upipe);

    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_sdi_dec_sub_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_sdi_dec_sub_free_output_proxy(upipe, request);
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

static struct upipe *upipe_sdi_dec_sub_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_sdi_dec_sub_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_sdi_dec_sub *upipe_sdi_dec_sub =
        upipe_sdi_dec_sub_from_upipe(upipe);

    upipe_sdi_dec_sub_init_sub(upipe);
    upipe_sdi_dec_sub_init_output(upipe);
    upipe_sdi_dec_sub_init_ubuf_mgr(upipe);
    upipe_sdi_dec_sub_init_urefcount(upipe);

    upipe_sdi_dec_sub->samples = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

static void upipe_sdi_dec_sub_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_sdi_dec_sub_clean_ubuf_mgr(upipe);
    upipe_sdi_dec_sub_clean_sub(upipe);
    upipe_sdi_dec_sub_clean_output(upipe);
    upipe_sdi_dec_sub_clean_urefcount(upipe);
    upipe_sdi_dec_sub_free_void(upipe);
}

static void upipe_sdi_dec_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_sdi_dec->sub_mgr;
    sub_mgr->refcount = upipe_sdi_dec_to_urefcount(upipe_sdi_dec);
    sub_mgr->signature = UPIPE_SDI_DEC_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_sdi_dec_sub_alloc;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_sdi_dec_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

static inline int32_t extract_sd_audio_sample(uint16_t *data)
{
    union {
        uint32_t u;
        int32_t  i;
    } sample = {0};

    sample.u |= (data[0] & 0x1F8) << 9;
    sample.u |= (data[1] & 0x1FF) << 18;
    sample.u |= (data[2] & 0x01F) << 27;

    return sample.i;
}

static inline int32_t extract_hd_audio_sample(const uint16_t *data)
{
    union {
        uint32_t u;
        int32_t  i;
    } sample = {0};

    sample.u |= (data[0] & 0xF0) <<  4;
    sample.u |= (data[2] & 0xFF) << 12;
    sample.u |= (data[4] & 0xFF) << 20;
    sample.u |= (data[6] & 0x0F) << 28;

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
        unsigned data_type_dependent= (pc >>  8) & 0x1f;
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

static void extract_hd_audio(struct upipe *upipe, const uint16_t *packet, int h,
                             struct audio_ctx *ctx)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);
    const struct sdi_offsets_fmt *f = upipe_sdi_dec->f;

    int data_count = packet[10] & 0xff;

    int audio_group = S291_HD_AUDIO_GROUP1_DID - (packet[6] & 0xff);
    if (data_count != 0x18) {
        upipe_warn_va(upipe, "Invalid data count 0x%x", data_count);
        return;
    }

    // FIXME this is wrong
    if (h == 7 /*|| h == 569 - allowed for progressive */) // SMPTE RP 168
        upipe_warn_va(upipe, "Audio packet at invalid line %d", h + 1);

    uint16_t checksum = 0;
    int len = data_count + 3 /* DID / DBN / DC */;
    for (int i = 0; i < len; i++)
        checksum += packet[6 + 2*i] & 0x1ff;
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
        upipe_err_va(upipe, "Wrong ECC, %.2x%.2x%.2x%.2x%.2x%.2x != %.2x%.2x%.2x%.2x%.2x%.2x",
                ecc[0], ecc[1], ecc[2], ecc[3], ecc[4], ecc[5],
                stream_ecc[0], stream_ecc[1], stream_ecc[2], stream_ecc[3], stream_ecc[4], stream_ecc[5]);
    }
    uint16_t clock = packet[12] & 0xff;
    clock |= (packet[14] & 0x0f) << 8;
    clock |= (packet[14] & 0x20) << 7;
    bool mpf = packet[14] & 0x10;

    /* wtf */
    if ((h >= 8 && h <= 8 + 5) || (h >= 570 && h <= 570 + 5)) {
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
                "audio group %d on line %d: wrong audio phase (mpf %d) CLK %d != %d => %"PRId64"",
                audio_group, h, mpf, clock, offset, offset - clock);
    }

    if (ctx->buf_audio)
        for (int i = 0; i < 4; i++) {
            int32_t s = extract_hd_audio_sample(&packet[UPIPE_SDI_MAX_CHANNELS + i * 8]);
            ctx->buf_audio[ctx->group_offset[audio_group] * UPIPE_SDI_MAX_CHANNELS + 4 * audio_group + i] = s;

            if (i & 0x01) { // check 2nd syncword
                size_t prev = ctx->group_offset[audio_group] * 16 + 4 * audio_group + i - 1;
                if ((s == 0xa54e1f00   && ctx->buf_audio[prev] == 0x96f87200) ||
                        (s ==  0x54e1f000  && ctx->buf_audio[prev] ==  0x6f872000) ||
                        (s ==   0x4e1f0000 && ctx->buf_audio[prev] ==   0xf8720000)) {
                    uint8_t pair = audio_group * 2 + (i >> 1);
                    if (ctx->aes[pair] != -1) {
                        upipe_err_va(upipe, "AES at line %d AND %d", ctx->aes[pair], h);
                    }
                    ctx->aes[pair] = h;
                }
            }
        }

    upipe_sdi_dec->audio_samples[audio_group]++;
    ctx->group_offset[audio_group]++;
}

static void parse_hd_hanc(struct upipe *upipe, const uint16_t *packet, int h,
                         struct audio_ctx *ctx)
{
    switch (packet[6] & 0xff) {
    case S291_HD_AUDIO_GROUP1_DID:
    case S291_HD_AUDIO_GROUP2_DID:
    case S291_HD_AUDIO_GROUP3_DID:
    case S291_HD_AUDIO_GROUP4_DID:
        extract_hd_audio(upipe, packet, h, ctx);
        break;

    default:
        break;
    }
}

static void extract_sd_audio(struct upipe *upipe, const uint16_t *packet, int h,
                             struct audio_ctx *ctx)
{
    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);
    const struct sdi_offsets_fmt *f = upipe_sdi_dec->f;

    int data_count = packet[5] & 0xff;

    int audio_group = S291_SD_AUDIO_GROUP1_DID - (packet[3] & 0xff);
}

static void parse_sd_hanc(struct upipe *upipe, const uint16_t *packet, int h,
                         struct audio_ctx *ctx)
{
    switch (packet[3] & 0xff) {
    case S291_SD_AUDIO_GROUP1_DID:
    case S291_SD_AUDIO_GROUP2_DID:
    case S291_SD_AUDIO_GROUP3_DID:
    case S291_SD_AUDIO_GROUP4_DID:
        extract_sd_audio(upipe, packet, h, ctx);
        break;

    default:
        break;
    }
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

    if (unlikely(upipe_sdi_dec->first_pts == 0)) {
        if (!ubase_check(uref_clock_get_pts_prog(uref,
                        &upipe_sdi_dec->first_pts))) {
            upipe_err(upipe, "undated uref");
            uref_free(uref);
            return true;
        }
    }

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
                            "0x%.4x%.4x%.4x%.4x != 0x%.4x%.4x%.4x%.4x", h,
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
    size_t aligned_output_hsize = ((output_hsize + 47) / 48) * 48;
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
    uint8_t *fields[2][UPIPE_SDI_DEC_MAX_PLANES] = {0};
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

    for (int i = 0; i < UPIPE_SDI_DEC_MAX_PLANES; i++)
        if (fields[0][i])
            fields[1][i] = fields[0][i] + output_stride[i];

    struct uref *uref_audio = NULL;
    struct upipe_sdi_dec_sub *upipe_sdi_dec_sub = NULL;

    struct audio_ctx audio_ctx = {0};
    for (int i = 0; i < 8; i++)
        audio_ctx.aes[i] = -1;

    if (!ulist_empty(&upipe_sdi_dec->subs)) {
        upipe_sdi_dec_sub =
            upipe_sdi_dec_sub_from_uchain(ulist_peek(&upipe_sdi_dec->subs));
        uref_audio = uref_dup(uref);
        if (!upipe_sdi_dec_sub->ubuf_mgr) {
            uref_flow_set_def(uref_audio, "sound.s32.");
            uref_sound_flow_add_plane(uref_audio, "ALL");
            uref_sound_flow_set_channels(uref_audio, 16);
            uref_sound_flow_set_rate(uref_audio, 48000);
            uref_sound_flow_set_sample_size(uref_audio, 4 * 16);
            upipe_sdi_dec_sub_require_ubuf_mgr(&upipe_sdi_dec_sub->upipe,
                    uref_audio);
            uref_flow_delete_def(uref_audio);
            assert(upipe_sdi_dec_sub->ubuf_mgr); // FIXME
        }

        struct ubuf *ubuf = ubuf_sound_alloc(upipe_sdi_dec_sub->ubuf_mgr, 1125*2);
        if (unlikely(!ubuf)) {
            upipe_throw_fatal(upipe, "Unable to allocate a sound buffer");
            uref_free(uref_audio);
            uref_audio = NULL;
        } else {
            uref_attach_ubuf(uref_audio, ubuf);
            if (unlikely(!ubase_check(uref_sound_plane_write_int32_t(uref_audio,
                                "ALL", 0, -1, &audio_ctx.buf_audio)))) {
                uref_free(uref_audio);
                uref_audio = NULL;
                upipe_throw_fatal(upipe, "Could not map audio buffer");
            }
        }
    }

    /* Parse the whole frame */
    for (int h = 0; h < f->height; h++) {
        const uint8_t chroma_blanking = p->sd ? UPIPE_SDI_CHROMA_BLANKING_START : UPIPE_HDSDI_CHROMA_BLANKING_START;
        /* Horizontal Blanking */
        uint16_t *line = (uint16_t *)input_buf + h * f->width * 2 + chroma_blanking;
        for (int v = 0; v < 2 * f->active_offset - chroma_blanking; v++) {
            const uint16_t *packet = line + v;

            if (p->sd) {
                if (packet[0] == S291_ADF1 && packet[1] == S291_ADF2 && packet[2] == S291_ADF3)
                    parse_sd_hanc(upipe, packet, h, &audio_ctx);
            } else {
                if (packet[0] == S291_ADF1 && packet[2] == S291_ADF2 && packet[4] == S291_ADF3)
                    parse_hd_hanc(upipe, packet, h, &audio_ctx);
            }
        }

        bool active = 0, f2 = 0;     
        /* Progressive */
        if (f->psf_ident) {
            // FIXME
            f2 = 0;
        }
        else {
            /* ACTIVE F1 */
            if (h >= ZERO_IDX(p->active_f1.start) && h <= ZERO_IDX(p->active_f1.end)) {
                active = 1;
            }
            /* ACTIVE F2 */
            else if (h >= ZERO_IDX(p->active_f2.start) && h <= ZERO_IDX(p->active_f2.end)) {
                active = 1;
                f2 = 1;
            }
        }

        if (!active) {
            // Parse VBI
        } else {
            uint16_t *src_line = (uint16_t*)input_buf + (h * f->width + f->active_offset) * 2;
            uint8_t *y = fields[f2][0];
            uint8_t *u = fields[f2][1];
            uint8_t *v = fields[f2][2];

            if (upipe_sdi_dec->output_is_v210)
                upipe_sdi_dec->uyvy_to_v210(src_line, y, output_hsize);
            else if (upipe_sdi_dec->output_bit_depth == 8)
                upipe_sdi_dec->uyvy_to_planar_8(y, u, v, src_line, output_hsize);
            else
                upipe_sdi_dec->uyvy_to_planar_10((uint16_t *)y, (uint16_t *)u, (uint16_t *)v, src_line, output_hsize);

            fields[f2][0] += output_stride[0] * 2;
            fields[f2][1] += output_stride[1] * 2;
            fields[f2][2] += output_stride[2] * 2;
        }
        upipe_sdi_dec->eav_clock += f->width;
    }

    if (uref_audio) {
        uint64_t pts = upipe_sdi_dec->first_pts +
            upipe_sdi_dec_sub->samples * UCLOCK_FREQ / 48000;
        uref_clock_set_pts_prog(uref_audio, pts);
        uref_clock_set_pts_orig(uref_audio, pts);
        uref_clock_set_dts_pts_delay(uref_audio, 0);
        upipe_throw_clock_ts(upipe, uref);

        int samples_received = audio_ctx.group_offset[0];
        for (int i = 1; i < 4; i++) {
            if (audio_ctx.group_offset[i] == samples_received)
                continue;

            if (samples_received < audio_ctx.group_offset[i])
                samples_received = audio_ctx.group_offset[i];
            upipe_err_va(upipe, "%zu samples on group %d", audio_ctx.group_offset[i], i);
        }

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

        upipe_sdi_dec_sub->samples += samples_received;;
        uref_sound_plane_unmap(uref_audio, "ALL", 0, -1);
        uref_sound_resize(uref_audio, 0, samples_received);

        if (samples_received == 0)
            uref_free(uref_audio);
        else
            upipe_sdi_dec_sub_output(&upipe_sdi_dec_sub->upipe, uref_audio, upump_p);
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
    struct upipe_sdi_dec_sub *upipe_sdi_dec_sub = upipe_sdi_dec_sub_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_sdi_dec_sub_store_flow_def(upipe, flow_format);

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

    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_sibling_alloc(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;

    uref_flow_set_def(flow_def_dup, "pic.");

    uref_pic_flow_set_align(flow_def_dup, 32);
    if (upipe_sdi_dec->output_is_v210) {
        upipe_sdi_dec->output_chroma_map[0] = "u10y10v10y10u10y10v10y10u10y10v10y10";
        upipe_sdi_dec->output_chroma_map[1] = NULL;
        upipe_sdi_dec->output_chroma_map[2] = NULL;
        uref_pic_flow_set_planes(flow_def_dup, 1);
        uref_pic_flow_set_macropixel(flow_def_dup, 48);
        uref_pic_flow_set_macropixel_size(flow_def_dup, 128, 0);
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
    switch (command) {
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_sdi_dec_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sdi_dec_iterate_sub(upipe, p);
        }
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

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void uyvy_to_planar_8_c(uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, const int64_t width)
{
    int j;
    for (j = 0; j < width; j++) {
        u[0] = l[0] >> 2;
        y[0] = l[1] >> 2;
        v[0] = l[2] >> 2;
        y[1] = l[3] >> 2;
        l += 4;
        y += 2;
        u += 1;
        v += 1;
    }
}

static void uyvy_to_planar_10_c(uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, const int64_t width)
{
    int j;
    for (j = 0; j < width/2; j++) {
        u[0] = l[0];
        y[0] = l[1];
        v[0] = l[2];
        y[1] = l[3];
        l += 4;
        y += 2;
        u += 1;
        v += 1;
    }
}

#define CLIP(v) av_clip(v, 4, 1019)

#define WRITE_PIXELS_UYVY(a)            \
    do {                                \
        val  = CLIP(*a++);              \
        tmp1 = CLIP(*a++);              \
        tmp2 = CLIP(*a++);              \
        val |= (tmp1 << 10) |           \
               (tmp2 << 20);            \
        AV_WL32(dst, val);              \
        dst += 4;                       \
    } while (0)

static void uyvy_to_v210_c(const uint16_t *y, uint8_t *dst, int64_t width)
{
    uint32_t val, tmp1, tmp2;
    int i;

    for (i = 0; i < width - 5; i += 6) {
        WRITE_PIXELS_UYVY(y);
        WRITE_PIXELS_UYVY(y);
        WRITE_PIXELS_UYVY(y);
        WRITE_PIXELS_UYVY(y);
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
static struct upipe *upipe_sdi_dec_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_sdi_dec_alloc_flow(mgr, uprobe, signature,
                                                   args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_sdi_dec *upipe_sdi_dec = upipe_sdi_dec_from_upipe(upipe);

    /* Get the given flow format and enable v210 as appropriate */
    upipe_sdi_dec->output_is_v210 = ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 16, "u10y10v10y10u10y10v10y10u10y10v10y10"));
    if (!upipe_sdi_dec->output_is_v210)
         upipe_sdi_dec->output_bit_depth = ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) ? 8 : 10;

    uref_free(flow_def);

    upipe_sdi_dec->uyvy_to_v210 = uyvy_to_v210_c;
    upipe_sdi_dec->uyvy_to_planar_8 = uyvy_to_planar_8_c;
    upipe_sdi_dec->uyvy_to_planar_10 = uyvy_to_planar_10_c;

#if !defined(__APPLE__) /* macOS clang doesn't support that builtin yet */
#if defined(__clang__) && /* clang 3.8 doesn't know ssse3 */ \
     (__clang_major__ < 3 || (__clang_major__ == 3 && __clang_minor__ <= 8))
# ifdef __SSSE3__
    if (1)
# else
    if (0)
# endif
#else
    if (__builtin_cpu_supports("ssse3"))
#endif
    {
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

    upipe_sdi_dec->crc_y = 0;
    upipe_sdi_dec->crc_c = 0;

    sdi_crc_setup(upipe_sdi_dec->crc_lut);
    
    upipe_sdi_dec->debug = 0;
    for (int i = 0; i < 4; i++)
        upipe_sdi_dec->audio_samples[i] = 0;
    for (int i = 0; i < 8; i++)
        upipe_sdi_dec->aes_detected[i] = -1;
    upipe_sdi_dec->eav_clock = 0;
    upipe_sdi_dec->first_pts = 0;

    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++)
            upipe_sdi_dec->aes_preamble[i][j] = 0;

    upipe_sdi_dec_init_urefcount(upipe);
    upipe_sdi_dec_init_ubuf_mgr(upipe);
    upipe_sdi_dec_init_output(upipe);
    upipe_sdi_dec_init_sub_mgr(upipe);
    upipe_sdi_dec_init_sub_subs(upipe);
    upipe_sdi_dec_init_input(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_sdi_dec_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_sdi_dec_clean_input(upipe);
    upipe_sdi_dec_clean_output(upipe);
    upipe_sdi_dec_clean_ubuf_mgr(upipe);
    upipe_sdi_dec_clean_urefcount(upipe);
    upipe_sdi_dec_clean_sub_subs(upipe);
    upipe_sdi_dec_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_sdi_dec_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SDI_DEC_SIGNATURE,

    .upipe_alloc = upipe_sdi_dec_alloc,
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

