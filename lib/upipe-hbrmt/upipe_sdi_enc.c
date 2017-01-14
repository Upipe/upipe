
#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/upipe.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>

#include <bitstream/smpte/291.h>

#include <libavutil/common.h>
#include <libavutil/bswap.h>

#include <upipe-hbrmt/upipe_sdi_enc.h>
#include "upipe_hbrmt_common.h"

#include "sdienc.h"

#define UPIPE_SDI_MAX_PLANES 3
#define UPIPE_SDI_MAX_CHANNELS 16

/* 16 is the start of chroma horizontal blanking, where
 * audio packets must go in */
#define UPIPE_SDI_SAV_LENGTH 8

static void upipe_sdi_blank_c(uint16_t *dst, int64_t size)
{
    for (int w = 0; w < size; w++) {
        dst[2*w+0] = 0x200;
        dst[2*w+1] = 0x40;
    }
}

/* [Field][VBI] */
static const uint16_t sav_fvh_cword[2][2] = {{0x200, 0x2ac}, {0x31c, 0x3b0}};
static const uint16_t eav_fvh_cword[2][2] = {{0x274, 0x2d8}, {0x368, 0x3c4}};

static const bool parity_tab[256] = {
#   define P2(n) n, n^1, n^1, n
#   define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#   define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)
    P6(0), P6(1), P6(1), P6(0)
};

/** upipe_sdi_enc structure with sdi_enc parameters */
struct upipe_sdi_enc {
    /** Fills a uyvy image with 0x200 (Y), 0x40 (U, V) */
    void (*blank)(uint16_t *dst, int64_t size);

    /** Converts planar 8 bit to UYVY */
    void (*planar_to_uyvy_8)(uint16_t *dst, const uint8_t *y, const uint8_t *u,
                             const uint8_t *v, const int64_t width);

    /** Converts planar 10 bit to UYVY */
    void (*planar_to_uyvy_10)(uint16_t *dst, const uint16_t *y, const uint16_t *u,
                              const uint16_t *v, const int64_t width);

    /** Converts v210 to UYVY */
    void (*v210_to_uyvy)(const uint32_t *src, uint16_t *uyvy, int64_t width);

    /** refcount management structure */
    struct urefcount urefcount;

    /** list of input subpipes */
    struct uchain subs;
    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

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

    /** buffered urefs */
    struct uchain urefs;
    size_t n;

    /** input bit depth **/
    int input_bit_depth;

    /** v210 input **/
    bool input_is_v210;

    /** input chroma map */
    const char *input_chroma_map[UPIPE_SDI_MAX_PLANES];

    /* CRC LUT */
    uint32_t crc_lut[8][1024];

    /* Chroma CRC context */
    uint32_t crc_c;
    /* Luma CRC context */
    uint32_t crc_y;

    /* AES channel status bitstream */
    uint8_t aes_channel_status[24];

    /* Clocks */
    uint64_t eav_clock;
    uint64_t total_audio_samples_put;

    /* mpf bits which need to be set for each packet after a switching point
     * on each packet until it syncs back up */
    int mpf_packet_bits[4];

    /* data block number for each audio control and data group */
    uint8_t dbn[8];

    /* SDI offsets */
    const struct sdi_offsets_fmt *f;
    const struct sdi_picture_fmt *p;

    /* fps */
    struct urational fps;

    /* video frame index (modulo 5) */
    uint8_t frame_idx;

    /* worst case audio buffer (~128kB) */
    int32_t audio_buf[UPIPE_SDI_MAX_CHANNELS /* channels */ * 48000 * 1001 / 24000];

    /* */
    bool started;

    /* popped uref that still has samples */
    struct uref *uref_audio;

    /** public upipe structure */
    struct upipe upipe;
};

/** audio input subpipe */
struct upipe_sdi_enc_sub {
    /** refcount management structure */
    struct urefcount urefcount;

    /** buffered urefs */
    struct uchain urefs;
    size_t n;

    /** structure for double-linked lists */
    struct uchain uchain;

    /** channels */
    uint8_t channels;

    /** public upipe structure */
    struct upipe upipe;
};

/* TODO: Make static const once verified it's correct */
static void sdi_init_crc_channel_status(uint8_t *data)
{
    static const uint8_t sdi_aes_crc_table[256] = {
        0x00, 0x64, 0xc8, 0xac, 0xe1, 0x85, 0x29, 0x4d, 0xb3, 0xd7, 0x7b, 0x1f, 0x52, 0x36, 0x9a, 0xfe,
        0x17, 0x73, 0xdf, 0xbb, 0xf6, 0x92, 0x3e, 0x5a, 0xa4, 0xc0, 0x6c, 0x08, 0x45, 0x21, 0x8d, 0xe9,
        0x2e, 0x4a, 0xe6, 0x82, 0xcf, 0xab, 0x07, 0x63, 0x9d, 0xf9, 0x55, 0x31, 0x7c, 0x18, 0xb4, 0xd0,
        0x39, 0x5d, 0xf1, 0x95, 0xd8, 0xbc, 0x10, 0x74, 0x8a, 0xee, 0x42, 0x26, 0x6b, 0x0f, 0xa3, 0xc7,
        0x5c, 0x38, 0x94, 0xf0, 0xbd, 0xd9, 0x75, 0x11, 0xef, 0x8b, 0x27, 0x43, 0x0e, 0x6a, 0xc6, 0xa2,
        0x4b, 0x2f, 0x83, 0xe7, 0xaa, 0xce, 0x62, 0x06, 0xf8, 0x9c, 0x30, 0x54, 0x19, 0x7d, 0xd1, 0xb5,
        0x72, 0x16, 0xba, 0xde, 0x93, 0xf7, 0x5b, 0x3f, 0xc1, 0xa5, 0x09, 0x6d, 0x20, 0x44, 0xe8, 0x8c,
        0x65, 0x01, 0xad, 0xc9, 0x84, 0xe0, 0x4c, 0x28, 0xd6, 0xb2, 0x1e, 0x7a, 0x37, 0x53, 0xff, 0x9b,
        0xb8, 0xdc, 0x70, 0x14, 0x59, 0x3d, 0x91, 0xf5, 0x0b, 0x6f, 0xc3, 0xa7, 0xea, 0x8e, 0x22, 0x46,
        0xaf, 0xcb, 0x67, 0x03, 0x4e, 0x2a, 0x86, 0xe2, 0x1c, 0x78, 0xd4, 0xb0, 0xfd, 0x99, 0x35, 0x51,
        0x96, 0xf2, 0x5e, 0x3a, 0x77, 0x13, 0xbf, 0xdb, 0x25, 0x41, 0xed, 0x89, 0xc4, 0xa0, 0x0c, 0x68,
        0x81, 0xe5, 0x49, 0x2d, 0x60, 0x04, 0xa8, 0xcc, 0x32, 0x56, 0xfa, 0x9e, 0xd3, 0xb7, 0x1b, 0x7f,
        0xe4, 0x80, 0x2c, 0x48, 0x05, 0x61, 0xcd, 0xa9, 0x57, 0x33, 0x9f, 0xfb, 0xb6, 0xd2, 0x7e, 0x1a,
        0xf3, 0x97, 0x3b, 0x5f, 0x12, 0x76, 0xda, 0xbe, 0x40, 0x24, 0x88, 0xec, 0xa1, 0xc5, 0x69, 0x0d,
        0xca, 0xae, 0x02, 0x66, 0x2b, 0x4f, 0xe3, 0x87, 0x79, 0x1d, 0xb1, 0xd5, 0x98, 0xfc, 0x50, 0x34,
        0xdd, 0xb9, 0x15, 0x71, 0x3c, 0x58, 0xf4, 0x90, 0x6e, 0x0a, 0xa6, 0xc2, 0x8f, 0xeb, 0x47, 0x23
    };

    memset(data, 23, sizeof(uint8_t));

    data[0] = 0x03; /* Only indicates professional use and LPCM */
    data[2] = 0x29; /* Forces 24 bits, leaves Level regulation default */

    uint8_t crc = 0xff;
    for (int i = 0; i < 23; i++)
        crc = sdi_aes_crc_table[crc ^ data[i]];

    data[23] = crc;
}

/** Ancillary Parity and Checksum */
static inline void sdi_fill_anc_parity_checksum(uint16_t *buf, int gap)
{
    uint16_t checksum = 0;
    int len = buf[4] + 3; /* Data count + 3 = did + sdid + dc + udw */

    for (int i = 0; i < gap*len; i += gap) {
        bool parity = parity_tab[buf[i] & 0xff];
        buf[i] |= (parity << 8);

        checksum += buf[i];
        buf[i] |= (!parity << 9);
    }

    checksum &= 0x1ff;
    checksum |= NOT_BIT8(checksum);

    buf[gap*len] = checksum;
}

static void sdi_fill_anc_parity_checksum_hd(uint16_t *buf)
{
    return sdi_fill_anc_parity_checksum(buf, 1);
}

static void sdi_fill_anc_parity_checksum_hd(uint16_t *buf)
{
    return sdi_fill_anc_parity_checksum(buf, 2);
}

static void put_payload_identifier(uint16_t *dst, const struct sdi_offsets_fmt *f)
{
    /* ADF */
    dst[0] = S291_ADF1;
    dst[2] = S291_ADF2;
    dst[4] = S291_ADF3;

    /* DID */
    dst[6] = S291_PAYLOADID_DID;

    /* SDID */
    dst[8] = S291_PAYLOADID_SDID;

    /* DC */
    dst[10] = 4;

    /* UDW */
    dst[12] = 0x04;
    dst[14] = (f->psf_ident << 6) | f->frame_rate;
    dst[16] = f->psf_ident == 0x0 ? 0x1 : 0x5;
    dst[18] = 0x00;

    /* Parity + CS */
    sdi_fill_anc_parity_checksum_hd(&dst[6]);
}

static int put_audio_control_packet(uint16_t *dst, int ch_group, uint8_t dbn)
{
    /* ADF */
    dst[0] = S291_ADF1;
    dst[2] = S291_ADF2;
    dst[4] = S291_ADF3;

    /* DID */
    dst[6] = 0xE4 - (ch_group + 1);

    /* DBN */
    dst[8] = dbn;

    /* DC */
    dst[10] = 11;

    /* UDW */
    dst[12] = 0x00; /* No frame numbering available */
    dst[14] = 0x00; /* 48 Khz sample rate, synchronous */
    dst[16] = 0x0F; /* All channel groups active */

    dst[18] = 0x0; /* Delay word 1 */
    dst[20] = 0x0;
    dst[22] = 0x0;
    dst[24] = 0x0;
    dst[26] = 0x0;
    dst[28] = 0x0; /* Delay word 6 */

    dst[30] = 0x0; /* Reserved */
    dst[32] = 0x0; /* Reserved */

    sdi_fill_anc_parity_checksum_hd(&dst[6]);

    /* Total amount to increment the destination including the luma words,
     * so in total it's 18 chroma words */
    return 36;
}

static unsigned audio_packets_per_line(const struct sdi_offsets_fmt *f)
{
    unsigned samples_per_frame = (48000 * f->fps.den  + f->fps.num - 1) / f->fps.num;
    unsigned active_lines = f->height - 2;

    return (samples_per_frame + active_lines - 1) / active_lines;
}

/* NOTE: ch_group is zero indexed */
static int put_audio_data_packet(uint16_t *dst, struct upipe_sdi_enc *s,
                                 int audio_sample, int ch_group,
                                 uint8_t mpf_bit, uint16_t clk, uint8_t dbn)
{
    union {
        uint32_t u;
        int32_t  i;
    } sample;

    /* Clock */
    uint8_t clock_1 = clk & 0xff, clock_2 = (clk & 0x1F00) >> 8;
    uint8_t ecc[6] = {0};

    /* ADF */
    dst[0] = S291_ADF1;
    dst[2] = S291_ADF2;
    dst[4] = S291_ADF3;

    /* DID */
    dst[6] = 0xE8 - (ch_group + 1);

    /* DBN */
    dst[8] = dbn;

    /* DC */
    dst[10] = 24;

    /* UDW */

    /* CLK */
    dst[12] = clock_1;
    dst[14] = ((clock_2 & 0x10) << 1) | (mpf_bit << 4) | (clock_2 & 0xF);

    /* CHn */
    for (int i = 0; i < 4; i++) {
        /* Each channel group has 4 samples from 4 channels, and in total
         * there are 16 channels at all times in the buffer */
        sample.i   = s->audio_buf[audio_sample*UPIPE_SDI_MAX_CHANNELS + (ch_group*4 + i)];
        sample.u >>= 8;

        /* Channel status */
        uint8_t byte_pos = (audio_sample % 192)/8;
        uint8_t bit_pos = (audio_sample % 24) % 8;
        uint8_t ch_stat = !!(s->aes_channel_status[byte_pos] & (1 << bit_pos));

        /* Block sync bit, channel status and validity */
        uint8_t aes_block_sync = (!(audio_sample % 24)) && (!(i & 1));
        uint8_t aes_status_validity = (ch_stat << 2) | (0 << 1) | 1;

        uint16_t word0 = ((sample.u & 0xf     ) <<  4) | (aes_block_sync      << 3);
        uint16_t word1 = ((sample.u & 0xff0   ) >>  4) ;
        uint16_t word2 = ((sample.u & 0xff000 ) >> 12) ;
        uint16_t word3 = ((sample.u & 0xf00000) >> 20) | (aes_status_validity << 4);

        /* AES parity bit */
        uint8_t par = 0;
        par += parity_tab[word0 & 0xff];
        par += parity_tab[word1 & 0xff];
        par += parity_tab[word2 & 0xff];
        par += parity_tab[word3 & 0xff];
        word3 |= (par & 1) << 7;

        dst[16 + i*8 + 0] = word0;
        dst[16 + i*8 + 2] = word1;
        dst[16 + i*8 + 4] = word2;
        dst[16 + i*8 + 6] = word3;
    }

    for (int i = 0; i < 48; i += 2) {
        const uint8_t in = ecc[0] ^ (dst[i] & 0xff);
        ecc[0] = ecc[1] ^ in;
        ecc[1] = ecc[2];
        ecc[2] = ecc[3] ^ in;
        ecc[3] = ecc[4] ^ in;
        ecc[4] = ecc[5] ^ in;
        ecc[5] = in;
    }

    /* ECC - BCH codes */
    dst[48] = ecc[0];
    dst[50] = ecc[1];
    dst[52] = ecc[2];
    dst[54] = ecc[3];
    dst[56] = ecc[4];
    dst[58] = ecc[5];

    sdi_fill_anc_parity_checksum_hd(&dst[6]);

    /* Total amount to increment the destination including the luma words,
     * so in total it's 31 chroma words */
    return 62;
}

/** @hidden */
static int upipe_sdi_enc_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_sdi_enc, upipe, UPIPE_SDI_ENC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_sdi_enc, urefcount, upipe_sdi_enc_free);
UPIPE_HELPER_VOID(upipe_sdi_enc);
UPIPE_HELPER_OUTPUT(upipe_sdi_enc, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_sdi_enc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_sdi_enc_check,
                      upipe_sdi_enc_register_output_request,
                      upipe_sdi_enc_unregister_output_request)

UPIPE_HELPER_UPIPE(upipe_sdi_enc_sub, upipe, UPIPE_SDI_ENC_SUB_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_sdi_enc_sub, urefcount, upipe_sdi_enc_sub_free);
UPIPE_HELPER_VOID(upipe_sdi_enc_sub);

UPIPE_HELPER_SUBPIPE(upipe_sdi_enc, upipe_sdi_enc_sub, sub, sub_mgr, subs, uchain)

static int upipe_sdi_enc_sub_control(struct upipe *upipe, int command, va_list args)
{
        struct upipe_sdi_enc_sub *sdi_enc_sub = upipe_sdi_enc_sub_from_upipe(upipe);

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            if (flow == NULL)
                return UBASE_ERR_INVALID;
            UBASE_RETURN(uref_flow_match_def(flow, "sound."))
            UBASE_RETURN(uref_sound_flow_get_channels(flow, &sdi_enc_sub->channels))
            if (sdi_enc_sub->channels > UPIPE_SDI_MAX_CHANNELS)
                return UBASE_ERR_INVALID;
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_sdi_enc_sub_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_sdi_enc_sub *sdi_enc_sub = upipe_sdi_enc_sub_from_upipe(upipe);
    upipe_verbose_va(upipe, "receiving audio uref");
    ulist_add(&sdi_enc_sub->urefs, uref_to_uchain(uref));
    upipe_verbose_va(upipe, "sub urefs: %zu", ++sdi_enc_sub->n);
}

static struct upipe *upipe_sdi_enc_sub_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_sdi_enc_sub_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_sdi_enc_sub *sdi_enc_sub = upipe_sdi_enc_sub_from_upipe(upipe);
    ulist_init(&sdi_enc_sub->urefs);
    sdi_enc_sub->n = 0;
    upipe_sdi_enc_sub_init_sub(upipe);
    upipe_sdi_enc_sub_init_urefcount(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

static void upipe_sdi_enc_sub_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_sdi_enc_sub_clean_sub(upipe);
    upipe_sdi_enc_sub_clean_urefcount(upipe);
    upipe_sdi_enc_sub_free_void(upipe);
}

static void upipe_sdi_enc_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_sdi_enc->sub_mgr;
    sub_mgr->refcount = upipe_sdi_enc_to_urefcount(upipe_sdi_enc);
    sub_mgr->signature = UPIPE_SDI_ENC_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_sdi_enc_sub_alloc;
    sub_mgr->upipe_input = upipe_sdi_enc_sub_input;
    sub_mgr->upipe_control = upipe_sdi_enc_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

static inline unsigned audio_samples_count(struct upipe_sdi_enc *upipe_sdi_enc)
{
    struct urational *fps = &upipe_sdi_enc->fps;
    const unsigned samples = (uint64_t)48000 * fps->den / fps->num;

    /* fixed number of samples for 48kHz */
    if (fps->den != 1001 || unlikely(fps->num == 24000))
        return samples;

    if (unlikely(fps->num != 30000 && fps->num != 60000)) {
        upipe_err_va(&upipe_sdi_enc->upipe,
                "Unsupported rate %"PRIu64"/%"PRIu64, fps->num, fps->den);
        return samples;
    }

    /* cyclic loop of 5 different sample counts */
    if (++upipe_sdi_enc->frame_idx == 5)
        upipe_sdi_enc->frame_idx = 0;

    static const uint8_t samples_increment[2][5] = {
        { 1, 0, 1, 0, 1 }, /* 30000 / 1001 */
        { 1, 1, 1, 1, 0 }  /* 60000 / 1001 */
    };

    bool rate5994 = !!(fps->num == 60000);

    return samples + samples_increment[rate5994][upipe_sdi_enc->frame_idx];
}

static float get_pts(uint64_t pts)
{
    static uint64_t first_pts;
    if (!first_pts) {
        first_pts = pts;
        return 0.;
    }

    return (float)((int64_t)pts - (int64_t)first_pts) / UCLOCK_FREQ;
}

static void upipe_sdi_enc_encode_line(struct upipe *upipe, int line_num, uint16_t *dst,
    const uint8_t *planes[2][UPIPE_SDI_MAX_PLANES], int *input_strides, const unsigned int samples,
    int *sample_number, size_t input_hsize, size_t input_vsize)
{
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_upipe(upipe);
    const struct sdi_offsets_fmt *f = upipe_sdi_enc->f;
    const struct sdi_picture_fmt *p = upipe_sdi_enc->p;
    bool vbi = 0, f2 = 0;

    input_hsize = p->active_width;

    /* Returns the total amount of samples per channel that can be put on
     * a line, so convert that to packets (multiplying it by 4 since you have
     * 4 channel groups) */
    unsigned max_audio_packets_per_line = 4 * audio_packets_per_line(f);

    /** Line Number can never be between [0, 0] so this will work for progressive */
    /* VBI F1 part 1 */
    if(line_num >= p->vbi_f1_part1.start && line_num <= p->vbi_f1_part1.end) {
        vbi = 1;
        f2 = 0;
    }
    /* ACTIVE F1 */
    else if(line_num >= p->active_f1.start && line_num <= p->active_f1.end) {
        vbi = 0;
        f2 = 0;
    }
    /* VBI F1 part 2 */
    else if(line_num >= p->vbi_f1_part2.start && line_num <= p->vbi_f1_part2.end) {
        vbi = 1;
        f2 = 0;
    }
    /* VBI F2 part 1 */
    else if(line_num >= p->vbi_f2_part1.start && line_num <= p->vbi_f2_part1.end) {
        vbi = 1;
        f2 = 1;
    }
    /* ACTIVE F2 */
    else if(line_num >= p->active_f2.start && line_num <= p->active_f2.end) {
        vbi = 0;
        f2 = 1;
    }
    /* VBI F2 part 2 */
    else if(line_num >= p->vbi_f2_part2.start && line_num <= p->vbi_f2_part2.end) {
        vbi = 1;
        f2 = 1;
    }

    /* EAV */
    dst[0] = 0x3ff;
    dst[1] = 0x3ff;
    dst[2] = 0x000;
    dst[3] = 0x000;
    dst[4] = 0x000;
    dst[5] = 0x000;
    dst[6] = eav_fvh_cword[f2][vbi];
    dst[7] = eav_fvh_cword[f2][vbi];
    dst[8] = (line_num & 0x3f) << 2;
    dst[8] |= NOT_BIT8(dst[8]);
    dst[9] = dst[8];
    dst[10] = (1 << 9) | (((line_num >> 7) & 0xf) << 2);
    dst[11] = dst[10];

    /* update CRC */
    for (int i = 0; i < 12; i += 2) {
        sdi_crc_update(upipe_sdi_enc->crc_lut[0], &upipe_sdi_enc->crc_c, dst[i + 0]);
        sdi_crc_update(upipe_sdi_enc->crc_lut[0], &upipe_sdi_enc->crc_y, dst[i + 1]);
    }

    /* finalize, reset, and encode the CRCs */
    sdi_crc_end(&upipe_sdi_enc->crc_c, &dst[12]);
    sdi_crc_end(&upipe_sdi_enc->crc_y, &dst[13]);

    /* HBI */
    const uint8_t chroma_blanking = p->sd ? UPIPE_SDI_CHROMA_BLANKING_START : UPIPE_HDSDI_CHROMA_BLANKING_START;
    upipe_sdi_enc->blank(&dst[chroma_blanking], f->active_offset - UPIPE_SDI_SAV_LENGTH);

    /* These packets are written in the first Luma sample after SAV */
    /* Payload identifier */
    if ((line_num == p->payload_id_line) ||
        (f->psf_ident != UPIPE_SDI_PSF_IDENT_P && line_num == p->payload_id_line + p->field_offset)) {
        put_payload_identifier(&dst[chroma_blanking+1], f);
    }
    /* Audio control packet on Switching Line + 2 */
    else if ((line_num == p->switching_line + 2) ||
             (f->psf_ident != UPIPE_SDI_PSF_IDENT_P && line_num == p->switching_line + p->field_offset + 2)) { 
        int dst_pos = chroma_blanking + 1;
        for (int i = 0; i < 4; i++) {
            dst_pos += put_audio_control_packet(&dst[dst_pos], i, upipe_sdi_enc->dbn[4+i]++);
            if (upipe_sdi_enc->dbn[4+i] == 0)
                upipe_sdi_enc->dbn[4+i] = 1;
        }
    }

    /* Ideal number of samples that should've been put */
    unsigned samples_put_target = samples * (line_num) / f->height;

    /* All channel groups should have the same samples to put on a line */
    int sample_diff = samples_put_target - sample_number[0];

    if (sample_diff > 2)
        sample_diff = 2;

    /* Chroma packets */
    /* Audio can go anywhere but the switching lines+1 */
    if (!(line_num == p->switching_line + 1) &&
        !(p->field_offset && line_num == p->switching_line + p->field_offset + 1)) {
        int packets_put = 0;

        /* Start counting the destination from the start of the
         * chroma horizontal blanking */
        int dst_pos = chroma_blanking;

        /* If more than a single audio packed must be put on a line
         * then the following sequence will be sent: 1 1 2 2 3 3 4 4 */
        for (int ch_group = 0; ch_group < 4; ch_group++) {
            /* Check if too many packets have been put on the line */
            if ((packets_put + 1) > max_audio_packets_per_line) {
                upipe_err(upipe, "too many audio packets per line");
                break;
            }

            for (int samples_to_put = 0; samples_to_put < sample_diff; samples_to_put++) {
                /* Packet belongs to another line */
                uint8_t mpf_bit = 0;

                /* mpf bit was set, which means that the packet was mean
                 * to arrive on the previous line */
                if (upipe_sdi_enc->mpf_packet_bits[ch_group]) {
                    mpf_bit = 1;
                    upipe_sdi_enc->mpf_packet_bits[ch_group]--;
                }

                /* If the mpf bit is set roll the clock back to the previous line and
                 * signal the bit in the packet to indicate it was meant to arrive on
                 * the previous line which happened to be a switching point */
                uint64_t eav_clock = upipe_sdi_enc->eav_clock - mpf_bit*f->width;

                /* Clock is the samples times the pixel clock divided by the audio
                 * clockrate */
                uint16_t aud_clock = upipe_sdi_enc->total_audio_samples_put * f->width * f->height * f->fps.num / f->fps.den / 48000;

                /* Phase offset is the difference between the audio clock and the
                 * EAV pixel clock */
                uint16_t sample_clock = aud_clock - upipe_sdi_enc->eav_clock;

                dst_pos += put_audio_data_packet(&dst[dst_pos], upipe_sdi_enc,
                        sample_number[ch_group], ch_group, mpf_bit, sample_clock,
                        upipe_sdi_enc->dbn[ch_group]++);
                if (upipe_sdi_enc->dbn[ch_group] == 0)
                    upipe_sdi_enc->dbn[ch_group] = 1;
                sample_number[ch_group]++;
                packets_put++;
            }
            upipe_sdi_enc->total_audio_samples_put += sample_diff;
        }
    } else {
        /* The current line is a switching line, so mark the next sample_diff
         * amount of packets for each audio group to be belonging to a line before */
        for (int ch_group = 0; ch_group < 4; ch_group++) {
            /* Difference between current samples actually put and the target */
            upipe_sdi_enc->mpf_packet_bits[ch_group] = samples - sample_number[ch_group];
        }
    }

    /* SAV */
    dst[2*f->active_offset-8] = 0x3ff;
    dst[2*f->active_offset-7] = 0x3ff;
    dst[2*f->active_offset-6] = 0x000;
    dst[2*f->active_offset-5] = 0x000;
    dst[2*f->active_offset-4] = 0x000;
    dst[2*f->active_offset-3] = 0x000;
    dst[2*f->active_offset-2] = sav_fvh_cword[f2][vbi];
    dst[2*f->active_offset-1] = sav_fvh_cword[f2][vbi];

    if(vbi) {
        /* black */
        upipe_sdi_enc->blank(&dst[2*f->active_offset], input_hsize);
    } else {
        const uint8_t *y = planes[f2][0];
        const uint8_t *u = planes[f2][1];
        const uint8_t *v = planes[f2][2];

        if (upipe_sdi_enc->input_is_v210)
            upipe_sdi_enc->v210_to_uyvy((uint32_t *)y, (uint16_t *)&dst[2*f->active_offset], input_hsize);
        else if (upipe_sdi_enc->input_bit_depth == 10)
            upipe_sdi_enc->planar_to_uyvy_10(&dst[2*f->active_offset], (uint16_t *)y, (uint16_t *)u, (uint16_t *)v, input_hsize);
        else
            upipe_sdi_enc->planar_to_uyvy_8 (&dst[2*f->active_offset], y, u, v, input_hsize);
    }

    /* Update CRCs */
    for (int i = 0; i < 2*input_hsize; i+=16) {
        const uint16_t *crc_src = &dst[2*f->active_offset + i];
        sdi_crc_update_blk(upipe_sdi_enc->crc_lut, &upipe_sdi_enc->crc_c, &upipe_sdi_enc->crc_y, crc_src);
    }

    /* FIXME: support PSF */
    if (!vbi)
        for (int i = 0; i < UPIPE_SDI_MAX_PLANES; i++)
            planes[f2][i] += input_strides[i] * (1 + (f2 ? 1 : !f->psf_ident));
}

static struct uref *upipe_sdi_enc_avsync(struct upipe *upipe, unsigned samples)
{
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_upipe(upipe);
    struct uref *uref = uref_from_uchain(ulist_peek(&upipe_sdi_enc->urefs));

    uint64_t pts;
    if (!ubase_check(uref_clock_get_pts_sys(uref, &pts))) {
        abort();
    }
    get_pts(pts);

    if (ulist_empty(&upipe_sdi_enc->subs)) {
        upipe_err(upipe, "no audio subpipe");
        return NULL;
    }

    if (!ulist_is_last(&upipe_sdi_enc->subs, upipe_sdi_enc->subs.next)) {
        upipe_err(upipe, "more than one audio subpipe");
        return NULL;
    }

    struct upipe_sdi_enc_sub *sdi_enc_sub = upipe_sdi_enc_sub_from_uchain(upipe_sdi_enc->subs.next);
    struct upipe *sub = &sdi_enc_sub->upipe;

    uint64_t next_pts = pts +
        UCLOCK_FREQ * upipe_sdi_enc->fps.den / upipe_sdi_enc->fps.num;
    upipe_verbose_va(upipe, "VID PTS %.2f (next %.2f) - started %d",
            get_pts(pts), get_pts(next_pts), upipe_sdi_enc->started);

    struct uchain *uchain, *uchain_tmp;
    bool first = true;

    if (ulist_empty(&sdi_enc_sub->urefs)) {
        upipe_verbose_va(upipe, "waiting for audio");
        return NULL;
    }

    if (!upipe_sdi_enc->started)
        ulist_delete_foreach(&sdi_enc_sub->urefs, uchain, uchain_tmp) {
            struct uref *uref_audio = uref_from_uchain(uchain);
            uint64_t pts_audio = 0;
            ubase_assert(uref_clock_get_pts_sys(uref_audio, &pts_audio));
            upipe_verbose_va(upipe, "AUD PTS %.2f", get_pts(pts_audio));

            size_t size = 0;
            uref_sound_size(uref_audio, &size, NULL);

            /* if the first audio uref is later than vid pts, drop video */
            if (first && pts_audio - UCLOCK_FREQ / 50 > next_pts) {
                upipe_verbose_va(upipe,
                        "audio > video (%.2f), dropping video, diff %.2f",
                        get_pts(next_pts), get_pts(pts_audio) - get_pts(next_pts));
                uref_free(uref_from_uchain(ulist_pop(&upipe_sdi_enc->urefs)));
                upipe_verbose_va(upipe, "urefs: %zu", --upipe_sdi_enc->n);
                return NULL;
            }

            uint64_t next_pts_audio = pts_audio + size * UCLOCK_FREQ / 48000;

            /* if audio is earlier than video, drop audio */
            if (next_pts_audio + UCLOCK_FREQ / 50 < pts) {
                ulist_delete(uchain);
                uref_free(uref_audio);
                upipe_verbose_va(sub,
                    "audio (%.2f) < video, dropping audio, diff %.2f",
                    get_pts(next_pts_audio), get_pts(next_pts_audio) - get_pts(pts));
                continue;
            }

            first = false;

            /* drop a few samples */
            if (pts_audio < pts) {
                uint64_t pts_diff = pts - pts_audio;
                size_t samples = pts_diff * 48000 / UCLOCK_FREQ;
                ubase_assert(uref_sound_resize(uref_audio, samples, -1));
                uref_clock_set_pts_sys(uref_audio, pts);
                upipe_verbose_va(sub, "Resized audio, removed %zu samples", samples);
            }

            /* we have audio for 2 video frames */
            if (next_pts + UCLOCK_FREQ * upipe_sdi_enc->fps.den / upipe_sdi_enc->fps.num < pts_audio) {
                upipe_sdi_enc->started = true;
                break;
            }
        }

    /* we can't fill the video frame with all the audio we have */
    if (!upipe_sdi_enc->started)
        return NULL; /* wait for more audio */

    uref = uref_from_uchain(ulist_pop(&upipe_sdi_enc->urefs));
    upipe_verbose_va(upipe, "urefs: %zu", --upipe_sdi_enc->n);

    /* start with previously buffered uref */
    struct uref *uref_audio = upipe_sdi_enc->uref_audio;
    upipe_sdi_enc->uref_audio = NULL;

    const uint8_t channels = sdi_enc_sub->channels;
    for (unsigned idx = 0; idx < samples; ) {
        if (!uref_audio) {
            uref_audio = uref_from_uchain(ulist_pop(&sdi_enc_sub->urefs));
            upipe_verbose_va(upipe, "sub urefs: %zu", --sdi_enc_sub->n);
        }
        //assert(uref_audio);
        if (!uref_audio)
            break;

        size_t size = 0;
        uref_sound_size(uref_audio, &size, NULL);

        const int32_t *buf;
        uref_sound_read_int32_t(uref_audio, 0, -1, &buf, 1);

        bool overlap = samples - idx < size;
        if (overlap)
            size = samples - idx;

        memcpy(&upipe_sdi_enc->audio_buf[idx * channels], buf, sizeof(int32_t) * size * channels);

        uref_sound_unmap(uref_audio, 0, -1, 1);

        idx += size;

        /* uref has more audio than we want */
        if (overlap) {
            /* resize */
            ubase_assert(uref_sound_resize(uref_audio, size, -1));
            /* buffer */
            upipe_sdi_enc->uref_audio = uref_audio;
            /* we're done */
            break;
        }

        uref_free(uref_audio);
        uref_audio = NULL;
    }

    upipe_verbose_va(sub, "READ %u samples", samples);

    return uref;
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_sdi_enc_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_upipe(upipe);

    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_sdi_enc_store_flow_def(upipe, NULL);
        upipe_sdi_enc_require_ubuf_mgr(upipe, uref);
        return;
    }

    if (upipe_sdi_enc->flow_def == NULL) {
        uref_free(uref);
        return;
    }

    uint64_t pts;
    if (!ubase_check(uref_clock_get_pts_sys(uref, &pts))) {
        uref_dump(uref, upipe->uprobe);
        upipe_err(upipe, "dropping untimed uref");
        uref_free(uref);
        return;
    }

    unsigned samples = audio_samples_count(upipe_sdi_enc);

    ulist_add(&upipe_sdi_enc->urefs, uref_to_uchain(uref)); // buffer uref
    upipe_verbose_va(upipe, "urefs: %zu", ++upipe_sdi_enc->n);

    uref = upipe_sdi_enc_avsync(upipe, samples);
    if (!uref)
        return;

    size_t input_hsize, input_vsize;
    if (!ubase_check(uref_pic_size(uref, &input_hsize, &input_vsize, NULL))) {
        upipe_warn(upipe, "invalid buffer received, can not read picture size");
        uref_dump(uref, upipe->uprobe);
        uref_free(uref);
        return;
    }

    /* map input */
    const uint8_t *input_planes[UPIPE_SDI_MAX_PLANES];
    int input_strides[UPIPE_SDI_MAX_PLANES];
    for (int i = 0; i < UPIPE_SDI_MAX_PLANES; i++) {
        const char *chroma = upipe_sdi_enc->input_chroma_map[i];
        if (chroma == NULL)
            break;

        const uint8_t *data;
        size_t stride;
        if (unlikely(!ubase_check(uref_pic_plane_read(uref,
                            chroma, 0, 0, -1, -1, &data)) ||
                     !ubase_check(uref_pic_plane_size(uref,
                             chroma, &stride, NULL, NULL, NULL)))) {
            upipe_warn(upipe, "invalid buffer received");
            uref_free(uref);
            return;
        }
        input_planes[i] = data;
        input_strides[i] = stride;
    }

    const struct sdi_offsets_fmt *f = upipe_sdi_enc->f;

    /* FIXME FIXME: When doing 23.94/24 fps the assembly packing will overwrite
     * so alignment here would be required to make them work */
    struct ubuf *ubuf = ubuf_block_alloc(upipe_sdi_enc->ubuf_mgr, f->width * f->height * sizeof(uint16_t) * 2);
    assert(ubuf);

    int size = -1;
    uint8_t *buf;
    ubase_assert(ubuf_block_write(ubuf, 0, &size, &buf));

    sdi_init_crc_channel_status(upipe_sdi_enc->aes_channel_status);

    const uint8_t *planes[2][UPIPE_SDI_MAX_PLANES] = {
        {input_planes[0], input_planes[1], input_planes[2]},
        {input_planes[0] + input_strides[0],
         input_planes[1] + input_strides[1],
         input_planes[2] + input_strides[2]},
    };

    uint16_t *dst = (uint16_t*)buf;

    /* map input audio */
    int sample_number[4] = {0, 0, 0, 0}; /* Counter for each channel group */

    for (int h = 0; h < f->height; h++) {
        upipe_sdi_enc_encode_line(upipe, h+1, &dst[h * f->width * 2],
                                  planes, &input_strides[0], samples,
                                  &sample_number[0], input_hsize, input_vsize);
        upipe_sdi_enc->eav_clock += f->width;
    }

    ubuf_block_unmap(ubuf, 0);

    /* unmap pictures */
    for (int i = 0; i < UPIPE_SDI_MAX_PLANES; i++) {
        const char *chroma = upipe_sdi_enc->input_chroma_map[i];
        if (chroma == NULL)
            break;
        uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
    }

    // XXX : apparently we can't re-use bmd urefs?!
    struct uref *uref2 = uref_dup(uref);
    uref_free(uref);
    uref_attach_ubuf(uref2, ubuf);
    upipe_sdi_enc_output(upipe, uref2, upump_p);
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_sdi_enc_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_sdi_enc_store_flow_def(upipe, flow_format);

    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_sdi_enc_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))

    upipe_sdi_enc->f = sdi_get_offsets(flow_def);
    if (!upipe_sdi_enc->f) {
        upipe_err(upipe, "Could not figure out SDI offsets");
        return UBASE_ERR_INVALID;
    }
    upipe_sdi_enc->p = upipe_sdi_enc->f->pict_fmt;

    struct uref *flow_def_dup;

    UBASE_RETURN(uref_pic_flow_get_fps(flow_def, &upipe_sdi_enc->fps))

    uint8_t macropixel;
    if (!ubase_check(uref_pic_flow_get_macropixel(flow_def, &macropixel)))
        return UBASE_ERR_INVALID;

#define u ubase_check
    if (!(((u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) &&
             u(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "u8")) &&
             u(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "v8"))) ||
            (u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y10l")) &&
             u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "u10l")) &&
             u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "v10l"))) ||
            (u(uref_pic_flow_check_chroma(flow_def, 1, 1, 16, "u10y10v10y10u10y10v10y10u10y10v10y10")))))) {
        upipe_err(upipe, "incompatible input flow def");
        return UBASE_ERR_EXTERNAL;
    }

    upipe_sdi_enc->input_bit_depth = u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) ? 8 : 10;
    upipe_sdi_enc->input_is_v210 = u(uref_pic_flow_check_chroma(flow_def, 1, 1, 16, "u10y10v10y10u10y10v10y10u10y10v10y10"));
#undef u

    if (upipe_sdi_enc->input_is_v210) {
        upipe_sdi_enc->input_chroma_map[0] = "u10y10v10y10u10y10v10y10u10y10v10y10";
        upipe_sdi_enc->input_chroma_map[1] = NULL;
        upipe_sdi_enc->input_chroma_map[2] = NULL;
    }
    else if (upipe_sdi_enc->input_bit_depth == 8) {
        upipe_sdi_enc->input_chroma_map[0] = "y8";
        upipe_sdi_enc->input_chroma_map[1] = "u8";
        upipe_sdi_enc->input_chroma_map[2] = "v8";
    }
    else {
        upipe_sdi_enc->input_chroma_map[0] = "y10l";
        upipe_sdi_enc->input_chroma_map[1] = "u10l";
        upipe_sdi_enc->input_chroma_map[2] = "v10l";
    }

    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;

    uref_flow_set_def(flow_def_dup, "block.");
    uref_pic_flow_set_fps(flow_def_dup, upipe_sdi_enc->fps);

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
static int upipe_sdi_enc_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_sdi_enc_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sdi_enc_iterate_sub(upipe, p);
        }
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_sdi_enc_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_sdi_enc_free_output_proxy(upipe, request);
        }

        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sdi_enc_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_sdi_enc_set_output(upipe, output);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_sdi_enc_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_sdi_enc_set_flow_def(upipe, flow);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

#define CLIP8(c) (av_clip((*(c)), 1,  254))
#define CLIP(c)  (av_clip((*(c)), 4, 1019))

static void planar_to_uyvy_8_c(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, const int64_t width)
{
    int j;
    for (j = 0; j < width/2; j++) {
        dst[0] = CLIP8(u++) << 2;
        dst[1] = CLIP8(y++) << 2;
        dst[2] = CLIP8(v++) << 2;
        dst[3] = CLIP8(y++) << 2;
        dst += 4;
    }
}

static void planar_to_uyvy_10_c(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const int64_t width)
{
    int j;
    for (j = 0; j < width/2; j++) {
        dst[0] = CLIP(u++);
        dst[1] = CLIP(y++);
        dst[2] = CLIP(v++);
        dst[3] = CLIP(y++);
        dst += 4;
    }
}

#define READ_PIXELS(a, b, c)         \
    do {                             \
        val  = av_le2ne32(*src++);   \
        *a++ =  val & 0x3FF;         \
        *b++ = (val >> 10) & 0x3FF;  \
        *c++ = (val >> 20) & 0x3FF;  \
    } while (0)

static void v210_uyvy_unpack_c(const uint32_t *src, uint16_t *uyvy, int64_t width)
{
    uint32_t val;
    int i;

    for( i = 0; i < width-5; i += 6 ){
        READ_PIXELS(uyvy, uyvy, uyvy);
        READ_PIXELS(uyvy, uyvy, uyvy);
        READ_PIXELS(uyvy, uyvy, uyvy);
        READ_PIXELS(uyvy, uyvy, uyvy);
    }
}

/** @internal @This allocates a sdi_enc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_sdi_enc_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_sdi_enc_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_upipe(upipe);

    ulist_init(&upipe_sdi_enc->urefs);
    upipe_sdi_enc->n = 0;

    upipe_sdi_enc->blank             = upipe_sdi_blank_c;
    upipe_sdi_enc->planar_to_uyvy_8  = planar_to_uyvy_8_c;
    upipe_sdi_enc->planar_to_uyvy_10 = planar_to_uyvy_10_c;
    upipe_sdi_enc->v210_to_uyvy      = v210_uyvy_unpack_c;

#if !defined(__APPLE__) /* macOS clang doesn't support that builtin yet */
    if (__builtin_cpu_supports("sse2"))
        upipe_sdi_enc->planar_to_uyvy_10 = upipe_planar_to_uyvy_10_sse2;

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
        upipe_sdi_enc->v210_to_uyvy      = upipe_v210_uyvy_unpack_aligned_ssse3;
    }

    if (__builtin_cpu_supports("avx")) {
        upipe_sdi_enc->blank             = upipe_sdi_blank_avx;
        upipe_sdi_enc->planar_to_uyvy_8  = upipe_planar_to_uyvy_8_avx;
        upipe_sdi_enc->planar_to_uyvy_10 = upipe_planar_to_uyvy_10_avx;
        upipe_sdi_enc->v210_to_uyvy      = upipe_v210_uyvy_unpack_aligned_avx;
    }

    if (__builtin_cpu_supports("avx2")) {
        upipe_sdi_enc->planar_to_uyvy_8  = upipe_planar_to_uyvy_8_avx2;
        upipe_sdi_enc->planar_to_uyvy_10 = upipe_planar_to_uyvy_10_avx2;
        upipe_sdi_enc->v210_to_uyvy      = upipe_v210_uyvy_unpack_aligned_avx2;
    }
#endif

    upipe_sdi_enc_init_urefcount(upipe);
    upipe_sdi_enc_init_ubuf_mgr(upipe);
    upipe_sdi_enc_init_output(upipe);
    upipe_sdi_enc_init_sub_mgr(upipe);
    upipe_sdi_enc_init_sub_subs(upipe);

    upipe_sdi_enc->crc_c = 0;
    upipe_sdi_enc->crc_y = 0;

    upipe_sdi_enc->eav_clock = 0;
    upipe_sdi_enc->total_audio_samples_put = 0;
    for (int i = 0; i < 4; i++)
        upipe_sdi_enc->mpf_packet_bits[i] = 0;
    for (int i = 0; i < 8; i++)
        upipe_sdi_enc->dbn[i] = 1;

    sdi_crc_setup(upipe_sdi_enc->crc_lut);

    upipe_sdi_enc->frame_idx = 0;
    upipe_sdi_enc->uref_audio = NULL;
    upipe_sdi_enc->started = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_sdi_enc_free(struct upipe *upipe)
{
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_upipe(upipe);
    upipe_throw_dead(upipe);
    uref_free(upipe_sdi_enc->uref_audio);
    upipe_sdi_enc_clean_output(upipe);
    upipe_sdi_enc_clean_ubuf_mgr(upipe);
    upipe_sdi_enc_clean_urefcount(upipe);
    upipe_sdi_enc_clean_sub_subs(upipe);
    upipe_sdi_enc_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_sdi_enc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SDI_ENC_SIGNATURE,

    .upipe_alloc = upipe_sdi_enc_alloc,
    .upipe_input = upipe_sdi_enc_input,
    .upipe_control = upipe_sdi_enc_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for sdi_enc pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sdi_enc_mgr_alloc(void)
{
    return &upipe_sdi_enc_mgr;
}
