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
#include <bitstream/dvb/vbi.h>

#include <libavutil/common.h>
#include <libavutil/bswap.h>

#include <upipe-hbrmt/upipe_sdi_enc.h>
#include "upipe_hbrmt_common.h"

#include "sdienc.h"
#include "sdi.h"

#define UPIPE_SDI_MAX_PLANES 3
#define UPIPE_SDI_MAX_CHANNELS 16

static void upipe_sdi_blank_c(uint16_t *dst, uintptr_t pixels)
{
    for (int w = 0; w < pixels; w++) {
        dst[2*w+0] = 0x200;
        dst[2*w+1] = 0x40;
    }
}

/* [Field][VBI] */
static const uint16_t sav_fvh_cword[2][2] = {{0x200, 0x2ac}, {0x31c, 0x3b0}};
static const uint16_t eav_fvh_cword[2][2] = {{0x274, 0x2d8}, {0x368, 0x3c4}};

static const bool parity_tab[512] = {
#   define P2(n) n, n^1, n^1, n
#   define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#   define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)
    P6(0), P6(1), P6(1), P6(0),
    P6(1), P6(0), P6(0), P6(1)
};

/** input subpipe */
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

    /** AES */
    bool s337;

    /** stereo pair position */
    uint8_t channel_idx;

    /** audio or subpic */
    bool sound;

    /** public upipe structure */
    struct upipe upipe;
};

/** upipe_sdi_enc structure with sdi_enc parameters */
struct upipe_sdi_enc {
    /** Fills a uyvy image with 0x40 (Y), 0x200 (U, V) */
    void (*blank)(uint16_t *dst, uintptr_t size);

    /** Converts planar 8 bit to UYVY */
    void (*planar_to_uyvy_8)(uint16_t *dst, const uint8_t *y, const uint8_t *u,
                             const uint8_t *v, uintptr_t width);

    /** Converts planar 10 bit to UYVY */
    void (*planar_to_uyvy_10)(uint16_t *dst, const uint16_t *y, const uint16_t *u,
                              const uint16_t *v, uintptr_t width);

    /** Converts v210 to UYVY */
    void (*v210_to_uyvy)(const uint32_t *src, uint16_t *uyvy, uintptr_t width);

    /** refcount management structure */
    struct urefcount urefcount;

    /** list of input subpipes */
    struct uchain subs;
    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** subpic subpipe */
    struct upipe_sdi_enc_sub subpic_subpipe;

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

    /* Sample Position */
    int sample_pos;

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

    /* sample offset for dolby E to be on the right line */
    unsigned dolby_offset;

    /* popped uref that still has samples */
    struct uref *uref_audio;

    /** OP47 teletext sequence counter **/
    uint16_t op47_sequence_counter[2];

    /** vbi **/
    vbi_sampling_par sp;

    /* teletext data */
    const uint8_t *ttx_packet[2][5];
    int ttx_packets[2];
    int ttx_line[2][5];

    /** teletext option */
    bool ttx;

    /** public upipe structure */
    struct upipe upipe;
};

static void sdi_init_crc_channel_status(uint8_t *data)
{
    static const uint8_t sdi_aes_crc_table[256] =
    {
        0x00, 0x1d, 0x3a, 0x27, 0x74, 0x69, 0x4e, 0x53, 0xe8, 0xf5, 0xd2, 0xcf, 0x9c, 0x81, 0xa6, 0xbb,
        0xcd, 0xd0, 0xf7, 0xea, 0xb9, 0xa4, 0x83, 0x9e, 0x25, 0x38, 0x1f, 0x02, 0x51, 0x4c, 0x6b, 0x76,
        0x87, 0x9a, 0xbd, 0xa0, 0xf3, 0xee, 0xc9, 0xd4, 0x6f, 0x72, 0x55, 0x48, 0x1b, 0x06, 0x21, 0x3c,
        0x4a, 0x57, 0x70, 0x6d, 0x3e, 0x23, 0x04, 0x19, 0xa2, 0xbf, 0x98, 0x85, 0xd6, 0xcb, 0xec, 0xf1,
        0x13, 0x0e, 0x29, 0x34, 0x67, 0x7a, 0x5d, 0x40, 0xfb, 0xe6, 0xc1, 0xdc, 0x8f, 0x92, 0xb5, 0xa8,
        0xde, 0xc3, 0xe4, 0xf9, 0xaa, 0xb7, 0x90, 0x8d, 0x36, 0x2b, 0x0c, 0x11, 0x42, 0x5f, 0x78, 0x65,
        0x94, 0x89, 0xae, 0xb3, 0xe0, 0xfd, 0xda, 0xc7, 0x7c, 0x61, 0x46, 0x5b, 0x08, 0x15, 0x32, 0x2f,
        0x59, 0x44, 0x63, 0x7e, 0x2d, 0x30, 0x17, 0x0a, 0xb1, 0xac, 0x8b, 0x96, 0xc5, 0xd8, 0xff, 0xe2,
        0x26, 0x3b, 0x1c, 0x01, 0x52, 0x4f, 0x68, 0x75, 0xce, 0xd3, 0xf4, 0xe9, 0xba, 0xa7, 0x80, 0x9d,
        0xeb, 0xf6, 0xd1, 0xcc, 0x9f, 0x82, 0xa5, 0xb8, 0x03, 0x1e, 0x39, 0x24, 0x77, 0x6a, 0x4d, 0x50,
        0xa1, 0xbc, 0x9b, 0x86, 0xd5, 0xc8, 0xef, 0xf2, 0x49, 0x54, 0x73, 0x6e, 0x3d, 0x20, 0x07, 0x1a,
        0x6c, 0x71, 0x56, 0x4b, 0x18, 0x05, 0x22, 0x3f, 0x84, 0x99, 0xbe, 0xa3, 0xf0, 0xed, 0xca, 0xd7,
        0x35, 0x28, 0x0f, 0x12, 0x41, 0x5c, 0x7b, 0x66, 0xdd, 0xc0, 0xe7, 0xfa, 0xa9, 0xb4, 0x93, 0x8e,
        0xf8, 0xe5, 0xc2, 0xdf, 0x8c, 0x91, 0xb6, 0xab, 0x10, 0x0d, 0x2a, 0x37, 0x64, 0x79, 0x5e, 0x43,
        0xb2, 0xaf, 0x88, 0x95, 0xc6, 0xdb, 0xfc, 0xe1, 0x5a, 0x47, 0x60, 0x7d, 0x2e, 0x33, 0x14, 0x09,
        0x7f, 0x62, 0x45, 0x58, 0x0b, 0x16, 0x31, 0x2c, 0x97, 0x8a, 0xad, 0xb0, 0xe3, 0xfe, 0xd9, 0xc4,
    };

    memset(data, 0, 23);

    data[0] = 0x80; /* Only indicates professional use and LPCM */
    data[1] = 0x40; /* Stereophonic Mode */
    data[2] = 0x20; /* Forces 24 bits, leaves Level regulation default */

    uint8_t crc = 0xff;
    for (int i = 0; i < 23; i++)
        crc = sdi_aes_crc_table[crc ^ data[i]];

    data[23] = crc;
}

/** DBN */
static void sdi_increment_dbn(uint8_t *dbn)
{
    (*dbn)++;
    if (*dbn == 0)
        *dbn = 1;
}

/** Ancillary Parity and Checksum */
static inline void sdi_fill_anc_parity_checksum(uint16_t *buf, bool do_parity,
                                                const int gap)
{
    uint16_t checksum = 0;
    int len = buf[2*gap] + 3; /* Data count + 3 = did + sdid + dc + udw */
    int i;
    bool parity;

    /* DID + SDID (DBN) + DC are parity */
    for (i = 0; i < gap*3; i += gap) {
        parity = parity_tab[buf[i] & 0xff];
        buf[i] |= (parity << 8);

        /* Checksum applies to only 9 bits */
        checksum += buf[i];
        buf[i] |= (!parity << 9);
    }

    for ( ; i < gap*len; i += gap) {
        if (do_parity) {
            parity = parity_tab[buf[i] & 0xff];
            buf[i] |= (parity << 8);
        } else {
            /* do not calculate parity bit, just set bit9 to !bit8 */
            parity = (buf[i] & 0x100) >> 8;
        }

        /* Checksum applies to only 9 bits */
        checksum += buf[i];
        buf[i] |= (!parity << 9);
    }

    checksum &= 0x1ff;
    checksum |= NOT_BIT8(checksum);

    buf[gap*len] = checksum;
}

#define SDI_FILL_ANC_PARITY(type, gap)                                      \
static void sdi_fill_anc_parity_checksum_##type(uint16_t *buf, bool parity) \
{                                                                           \
    sdi_fill_anc_parity_checksum(buf, parity, gap);                         \
}

SDI_FILL_ANC_PARITY(sd, 1)
SDI_FILL_ANC_PARITY(hd, 2)

static inline void put_payload_identifier(uint16_t *dst, const struct sdi_offsets_fmt *f,
                                          int gap)
{
    /* ADF */
    dst[gap*0] = S291_ADF1;
    dst[gap*1] = S291_ADF2;
    dst[gap*2] = S291_ADF3;

    /* DID */
    dst[gap*3] = S291_PAYLOADID_DID;

    /* SDID */
    dst[gap*4] = S291_PAYLOADID_SDID;

    /* DC */
    dst[gap*5] = 4;

    /* UDW */
    dst[gap*6] = f->pict_fmt->sd ? 0x81 : f->height == 750 ? 0x84 : 0x85;
    dst[gap*7] = (f->psf_ident << 6) | f->frame_rate;
    dst[gap*8] = 0x80;
    dst[gap*9] = 0x01;

    /* Parity + CS */
    sdi_fill_anc_parity_checksum_hd(&dst[gap*3], true);
}

#define PUT_PAYLOAD_IDENTIFIER(type, gap)                      \
static void put_payload_identifier_##type(uint16_t *dst, const struct sdi_offsets_fmt *f) \
{                                                              \
    put_payload_identifier(dst, f, gap);                       \
}

PUT_PAYLOAD_IDENTIFIER(sd, 1)
PUT_PAYLOAD_IDENTIFIER(hd, 2)

static int put_audio_control_packet(struct upipe_sdi_enc *upipe_sdi_enc,
                                    uint16_t *dst, int ch_group)
{
    /* ADF */
    dst[0] = S291_ADF1;
    dst[2] = S291_ADF2;
    dst[4] = S291_ADF3;

    /* DID */
    dst[6] = S291_HD_AUDIOCONTROL_GROUP1_DID - ch_group;

    /* DBN */
    dst[8] = upipe_sdi_enc->dbn[4+ch_group];
    sdi_increment_dbn(&upipe_sdi_enc->dbn[4+ch_group]);

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

    sdi_fill_anc_parity_checksum_hd(&dst[6], true);

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
static int put_sd_audio_data_packet(struct upipe_sdi_enc *upipe_sdi_enc, uint16_t *dst,
                                    int ch_group, int num_samples)
{
    union {
        uint32_t u;
        int32_t  i;
    } sample;
    int sample_pos = upipe_sdi_enc->sample_pos;
    uint64_t total_samples = upipe_sdi_enc->total_audio_samples_put;

    /* ADF */
    dst[0] = S291_ADF1;
    dst[1] = S291_ADF2;
    dst[2] = S291_ADF3;

    /* DID */
    dst[3] = 0xff - (ch_group << 1);

    /* DBN */
    dst[4] = upipe_sdi_enc->dbn[ch_group];
    sdi_increment_dbn(&upipe_sdi_enc->dbn[ch_group]);

    /* DC */
    dst[5] = 3 * UPIPE_SDI_CHANNELS_PER_GROUP * num_samples;

    uint16_t *audio_words = &dst[6];
    for (int j = 0; j < num_samples; j++) {
        for (int i = 0; i < UPIPE_SDI_CHANNELS_PER_GROUP; i++) {
            sample.i   = upipe_sdi_enc->audio_buf[(sample_pos+j)*UPIPE_SDI_MAX_CHANNELS + (ch_group*4 + i)];
            sample.u >>= 12;

            /* Channel status */
            uint8_t byte_pos = (total_samples % 192)/8;
            uint8_t bit_pos = (7 - ((total_samples % 24) % 8));
            uint8_t ch_stat = (upipe_sdi_enc->aes_channel_status[byte_pos] >> bit_pos) & 1;

            /* Block sync bit, channel status and validity
             * SMPTE 272 says both pairs must have Z=1 */
            uint8_t aes_block_sync = !(total_samples % 192);
            /* P (calculated later) | C | U | V */
            uint8_t aes_status_validity = (ch_stat << 2) | (0 << 1) | 0;

            audio_words[0] = ((sample.u & 0x3f   ) <<  3) | (i << 1) | aes_block_sync;
            audio_words[1] = ((sample.u & 0x7fc0 ) >>  6) ;
            audio_words[2] = ((sample.u & 0xf8000) >> 15) | (aes_status_validity << 5);

            /* Not the AES parity bit */
            uint8_t par = 0;
            par += parity_tab[audio_words[0] & 0x1ff];
            par += parity_tab[audio_words[1] & 0x1ff];
            par += parity_tab[audio_words[2] & 0xff];
            audio_words[2] |= (par & 1) << 8;
            audio_words += 3;
        }

        total_samples++;
    }

    sdi_fill_anc_parity_checksum_sd(&dst[3], false);

    return 6 + 3 * UPIPE_SDI_CHANNELS_PER_GROUP * num_samples + 1;
}

/* NOTE: ch_group is zero indexed */
static int put_hd_audio_data_packet(struct upipe_sdi_enc *upipe_sdi_enc, uint16_t *dst,
                                    int ch_group, uint8_t mpf_bit, uint16_t clk)
{
    union {
        uint32_t u;
        int32_t  i;
    } sample;
    int sample_pos = upipe_sdi_enc->sample_pos;
    uint64_t total_samples = upipe_sdi_enc->total_audio_samples_put;

    /* Clock */
    uint8_t clock_1 = clk & 0xff, clock_2 = (clk & 0x1F00) >> 8;
    uint8_t ecc[6] = {0};

    /* ADF */
    dst[0] = S291_ADF1;
    dst[2] = S291_ADF2;
    dst[4] = S291_ADF3;

    /* DID */
    dst[6] = S291_HD_AUDIO_GROUP1_DID - ch_group ;

    /* DBN */
    dst[8] = upipe_sdi_enc->dbn[ch_group];
    sdi_increment_dbn(&upipe_sdi_enc->dbn[ch_group]);

    /* DC */
    dst[10] = 24;

    /* UDW */

    /* CLK */
    dst[12] = clock_1;
    dst[14] = ((clock_2 & 0x10) << 1) | (mpf_bit << 4) | (clock_2 & 0xF);

    /* CHn */
    for (int i = 0; i < UPIPE_SDI_CHANNELS_PER_GROUP; i++) {
        /* Each channel group has 4 samples from 4 channels, and in total
         * there are 16 channels at all times in the buffer */
        sample.i   = upipe_sdi_enc->audio_buf[sample_pos*UPIPE_SDI_MAX_CHANNELS +
                                              (ch_group*UPIPE_SDI_CHANNELS_PER_GROUP + i)];
        sample.u >>= 8;

        /* Channel status */
        uint8_t byte_pos = (total_samples % 192)/8;
        uint8_t bit_pos = (7 - ((total_samples % 24) % 8));
        uint8_t ch_stat = (upipe_sdi_enc->aes_channel_status[byte_pos] >> bit_pos) & 1;

        /* Block sync bit, channel status and validity
         * Table 4 of SMPTE 299 makes it clear the second channel has Z=0 */
        uint8_t aes_block_sync = (!(total_samples % 192)) && (!(i & 1));
        /* P (calculated later) | C | U | V */
        uint8_t aes_status_validity = (ch_stat << 2) | (0 << 1) | 0;

        uint16_t word0 = ((sample.u & 0xf     ) <<  4) | (aes_block_sync      << 3);
        uint16_t word1 = ((sample.u & 0xff0   ) >>  4) ;
        uint16_t word2 = ((sample.u & 0xff000 ) >> 12) ;
        uint16_t word3 = ((sample.u & 0xf00000) >> 20) | (aes_status_validity << 4);

        /* AES parity bit */
        uint8_t par = 0;
        par += parity_tab[word0 & 0xf0];
        par += parity_tab[word1 & 0xff];
        par += parity_tab[word2 & 0xff];
        par += parity_tab[word3 & 0x7f];
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

    sdi_fill_anc_parity_checksum_hd(&dst[6], true);

    /* Total amount to increment the destination including the luma words,
     * so in total it's 31 chroma words */
    return 62;
}

/** @hidden */
static int upipe_sdi_enc_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_sdi_enc, upipe, UPIPE_SDI_ENC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_sdi_enc, urefcount, upipe_sdi_enc_free);
UPIPE_HELPER_OUTPUT(upipe_sdi_enc, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_sdi_enc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_sdi_enc_check,
                      upipe_sdi_enc_register_output_request,
                      upipe_sdi_enc_unregister_output_request)

UPIPE_HELPER_UPIPE(upipe_sdi_enc_sub, upipe, UPIPE_SDI_ENC_SUB_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_sdi_enc_sub, urefcount, upipe_sdi_enc_sub_free);
UPIPE_HELPER_VOID(upipe_sdi_enc_sub);

UBASE_FROM_TO(upipe_sdi_enc, upipe_sdi_enc_sub, subpic_subpipe, subpic_subpipe)

UPIPE_HELPER_SUBPIPE(upipe_sdi_enc, upipe_sdi_enc_sub, sub, sub_mgr, subs, uchain)

static int upipe_sdi_enc_sub_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_sdi_enc_sub *sdi_enc_sub = upipe_sdi_enc_sub_from_upipe(upipe);

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            if (flow == NULL)
                return UBASE_ERR_INVALID;

            if (!sdi_enc_sub->sound)
                return UBASE_ERR_NONE;

            if (!ubase_check(uref_attr_get_small_unsigned(flow, &sdi_enc_sub->channel_idx,
                            UDICT_TYPE_SMALL_UNSIGNED, "channel_idx"))) {
                upipe_err(upipe, "Could not read channel_idx");
                return UBASE_ERR_INVALID;
            }

            uint8_t planes;
            UBASE_RETURN(uref_flow_match_def(flow, "sound.s32."))
            sdi_enc_sub->s337 = ubase_check(uref_flow_match_def(flow, "sound.s32.s337."));
            UBASE_RETURN(uref_sound_flow_get_channels(flow, &sdi_enc_sub->channels))
            UBASE_RETURN(uref_sound_flow_get_planes(flow, &planes))
            if (planes != 1)
                return UBASE_ERR_INVALID;
            if (sdi_enc_sub->channels > UPIPE_SDI_MAX_CHANNELS)
                return UBASE_ERR_INVALID;
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_sdi_enc_clean_urefs(struct uchain *urefs)
{
    for (;;) {
        struct uchain *uchain = ulist_pop(urefs);
        if (!uchain)
            return;
        struct uref *uref = uref_from_uchain(uchain);
        uref_free(uref);
    }
}

static void upipe_sdi_enc_sub_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_sdi_enc_sub *sdi_enc_sub = upipe_sdi_enc_sub_from_upipe(upipe);
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_sub_mgr(upipe->mgr);

    if (!upipe_sdi_enc->ttx && !sdi_enc_sub->sound) {
        uref_free(uref);
        return;
    }
    ulist_add(&sdi_enc_sub->urefs, uref_to_uchain(uref));
    upipe_verbose_va(upipe, "sub urefs: %zu", ++sdi_enc_sub->n);
}

/** @internal @This initializes an subpipe of a sdi enc pipe.
 *
 * @param upipe pointer to subpipe
 * @param sub_mgr manager of the subpipe
 * @param uprobe structure used to raise events by the subpipe
 */
static void upipe_sdi_enc_sub_init(struct upipe *upipe,
        struct upipe_mgr *sub_mgr, struct uprobe *uprobe, bool static_pipe)
{
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_sub_mgr(sub_mgr);

    if (static_pipe) {
        upipe_init(upipe, sub_mgr, uprobe);
        /* increment super pipe refcount only when the static pipes are retrieved */
        upipe_mgr_release(sub_mgr);
        upipe->refcount = &upipe_sdi_enc->urefcount;
    } else
        upipe_sdi_enc_sub_init_urefcount(upipe);

    struct upipe_sdi_enc_sub *sdi_enc_sub = upipe_sdi_enc_sub_from_upipe(upipe);

    upipe_sdi_enc_sub_init_sub(upipe);

    sdi_enc_sub->sound = !static_pipe;

    ulist_init(&sdi_enc_sub->urefs);
    sdi_enc_sub->n = 0;
    sdi_enc_sub->s337 = false;

    upipe_throw_ready(upipe);
}

static struct upipe *upipe_sdi_enc_sub_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_sdi_enc_sub_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_sdi_enc_sub_init(upipe, mgr, uprobe, false);

    return upipe;
}

static void upipe_sdi_enc_sub_free(struct upipe *upipe)
{
    struct upipe_sdi_enc_sub *sdi_enc_sub = upipe_sdi_enc_sub_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_sdi_enc_clean_urefs(&sdi_enc_sub->urefs);
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
    size_t input_hsize, size_t input_vsize)
{
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_upipe(upipe);
    const struct sdi_offsets_fmt *f = upipe_sdi_enc->f;
    const struct sdi_picture_fmt *p = upipe_sdi_enc->p;
    uint16_t *active_start = &dst[2*f->active_offset];
    bool vbi = 0, f2 = 0, special_case = 0, ntsc;
    ntsc = p->active_height == 486;

    input_hsize = p->active_width;

    /* Returns the total amount of samples per channel that can be put on
     * a line, so convert that to packets (multiplying it by 4 since you have
     * 4 channel groups) */
    unsigned max_audio_packets_per_line = 4 * audio_packets_per_line(f);

    // FIXME factor out common code

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

    /* Treat NTSC Line 20 (Field 1) as a special case like BMD does */
    if (ntsc && line_num == 20)
        special_case = 1;

    /* EAV */
    dst[0] = 0x3ff;
    dst[1] = 0x000;
    dst[2] = 0x000;
    dst[3] = eav_fvh_cword[f2][vbi];

    /* HBI */
    const uint8_t hanc_start = UPIPE_SDI_EAV_LENGTH;
    upipe_sdi_enc->blank(&dst[hanc_start], f->active_offset - UPIPE_SDI_SAV_LENGTH);
    dst += hanc_start;

    /* Ideal number of samples that should've been put */
    unsigned samples_put_target = samples * (line_num) / f->height;

    /* All channel groups should have the same samples to put on a line */
    int samples_to_put = samples_put_target - upipe_sdi_enc->sample_pos;

    for (int ch_group = 0; ch_group < UPIPE_SDI_CHANNELS_PER_GROUP; ch_group++) {
        dst += put_sd_audio_data_packet(upipe_sdi_enc, dst, ch_group, samples_to_put);
    }
    upipe_sdi_enc->total_audio_samples_put += samples_to_put;
    upipe_sdi_enc->sample_pos += samples_to_put;

    /* SAV */
    active_start[-4] = 0x3ff;
    active_start[-3] = 0x000;
    active_start[-2] = 0x000;
    active_start[-1] = sav_fvh_cword[f2][vbi];

    if(vbi || special_case) {
        /* black */
        upipe_sdi_enc->blank(active_start, input_hsize);
    } else {
        const uint8_t *y = planes[f2][0];
        const uint8_t *u = planes[f2][1];
        const uint8_t *v = planes[f2][2];

        if (upipe_sdi_enc->input_is_v210)
            upipe_sdi_enc->v210_to_uyvy((uint32_t *)y, active_start, input_hsize);
        else if (upipe_sdi_enc->input_bit_depth == 10)
            upipe_sdi_enc->planar_to_uyvy_10(active_start, (uint16_t *)y, (uint16_t *)u, (uint16_t *)v, input_hsize);
        else
            upipe_sdi_enc->planar_to_uyvy_8 (active_start, y, u, v, input_hsize);
    }

    /* Progressive SD not supported */
    if (!vbi && !special_case)
        for (int i = 0; i < UPIPE_SDI_MAX_PLANES; i++)
            planes[f2][i] += input_strides[i] * 2;
}

static void upipe_hd_sdi_enc_encode_line(struct upipe *upipe, int line_num, uint16_t *dst,
    const uint8_t *planes[2][UPIPE_SDI_MAX_PLANES], int *input_strides, const unsigned int samples,
    size_t input_hsize, size_t input_vsize)
{
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_upipe(upipe);
    const struct sdi_offsets_fmt *f = upipe_sdi_enc->f;
    const struct sdi_picture_fmt *p = upipe_sdi_enc->p;
    uint16_t switching_line_offset = p->field_offset - 1;
    uint16_t *active_start = &dst[2*f->active_offset];
    const uint8_t hanc_start = UPIPE_HD_SDI_EAV_LENGTH;

    bool vbi = 0, f2 = 0;

    input_hsize = p->active_width;

    /* Returns the total amount of samples per channel that can be put on
     * a line, so convert that to packets */
    unsigned max_audio_packets_per_line = UPIPE_SDI_CHANNELS_PER_GROUP * audio_packets_per_line(f);

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
    dst[8] = (line_num & 0x7f) << 2;
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
    upipe_sdi_enc->blank(&dst[hanc_start], f->active_offset - UPIPE_HD_SDI_SAV_LENGTH);

    /* These packets are written in the first Luma sample after SAV */
    /* Payload identifier */
    if ((line_num == p->payload_id_line) ||
        (f->psf_ident != UPIPE_SDI_PSF_IDENT_P && line_num == p->payload_id_line + switching_line_offset)) {
        put_payload_identifier_hd(&dst[hanc_start+1], f);
    }
    /* Audio control packet on Switching Line + 2 */
    else if ((line_num == p->switching_line + 2) ||
             (f->psf_ident != UPIPE_SDI_PSF_IDENT_P && line_num == p->switching_line + 2 + switching_line_offset)) {
        int dst_pos = hanc_start + 1;
        for (int i = 0; i < UPIPE_SDI_CHANNELS_PER_GROUP; i++) {
            dst_pos += put_audio_control_packet(upipe_sdi_enc, &dst[dst_pos], i);
        }
    }

    /* Ideal number of samples that should've been put */
    unsigned samples_put_target = samples * (line_num) / f->height;

    /* All channel groups should have the same samples to put on a line */
    int samples_to_put = samples_put_target - upipe_sdi_enc->sample_pos;

    /* Clip this to something reasonable */
    if (samples_to_put > 2)
        samples_to_put = 2;

    /* Chroma packets */
    /* Audio can go anywhere but the switching lines+1 */
    if (!(line_num == p->switching_line + 1) &&
        !(p->field_offset && line_num == p->switching_line + p->field_offset + 1)) {
        int packets_put = 0;

        /* Start counting the destination from the start of the
         * chroma horizontal blanking */
        int dst_pos = hanc_start;

        /* If more than a single audio packet must be put on a line
         * then the following sequence will be sent: 1 2 3 4 1 2 3 4 */
        for (int sample = 0; sample < samples_to_put; sample++) {
            /* Check if too many packets have been put on the line */
            if ((packets_put + 1) > max_audio_packets_per_line) {
                upipe_err(upipe, "too many audio packets per line");
                break;
            }
            for (int ch_group = 0; ch_group < UPIPE_SDI_CHANNELS_PER_GROUP; ch_group++) {
                /* Packet belongs to another line */
                uint8_t mpf_bit = 0;

                /* mpf bit was set, which means that the packet was meant
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

                dst_pos += put_hd_audio_data_packet(upipe_sdi_enc, &dst[dst_pos],
                                                    ch_group, mpf_bit, sample_clock);
                packets_put++;
            }
            upipe_sdi_enc->total_audio_samples_put++;
            upipe_sdi_enc->sample_pos++;
        }
    } else {
        /* The current line is a switching line, so mark the next sample_diff
         * amount of packets for each audio group to be belonging to a line before */
        for (int ch_group = 0; ch_group < UPIPE_SDI_CHANNELS_PER_GROUP; ch_group++) {
            /* Difference between current samples actually put and the target */
            upipe_sdi_enc->mpf_packet_bits[ch_group] = samples - upipe_sdi_enc->sample_pos;
        }
    }

    /* SAV */
    active_start[-8] = 0x3ff;
    active_start[-7] = 0x3ff;
    active_start[-6] = 0x000;
    active_start[-5] = 0x000;
    active_start[-4] = 0x000;
    active_start[-3] = 0x000;
    active_start[-2] = sav_fvh_cword[f2][vbi];
    active_start[-1] = sav_fvh_cword[f2][vbi];

    if(vbi) {
        /* black */
        upipe_sdi_enc->blank(active_start, input_hsize);
        const uint8_t **ttx = NULL;
        int num_ttx = 0;
        if (line_num == OP47_LINE1 + 563*f2) {
            num_ttx = upipe_sdi_enc->ttx_packets[f2];
            if (num_ttx)
                ttx = &upipe_sdi_enc->ttx_packet[f2][0];
        }
        if (ttx) {
            uint16_t buf[input_hsize];
            memset(buf, 0, sizeof(buf));

            sdi_encode_ttx(buf, num_ttx, ttx, &upipe_sdi_enc->op47_sequence_counter[f2]);
            // TODO: make sdi_encode_ttx work in place
            for (int i = 0; i < input_hsize; i++)
                active_start[2*i] = buf[i];
        }
    } else {
        const uint8_t *y = planes[f2][0];
        const uint8_t *u = planes[f2][1];
        const uint8_t *v = planes[f2][2];

        if (upipe_sdi_enc->input_is_v210)
            upipe_sdi_enc->v210_to_uyvy((uint32_t *)y, active_start, input_hsize);
        else if (upipe_sdi_enc->input_bit_depth == 10)
            upipe_sdi_enc->planar_to_uyvy_10(active_start, (uint16_t *)y, (uint16_t *)u, (uint16_t *)v, input_hsize);
        else
            upipe_sdi_enc->planar_to_uyvy_8 (active_start, y, u, v, input_hsize);
    }

    /* Update CRCs */
    for (int i = 0; i < 2*input_hsize; i+=16) {
        const uint16_t *crc_src = &active_start[i];
        sdi_crc_update_blk(upipe_sdi_enc->crc_lut, &upipe_sdi_enc->crc_c, &upipe_sdi_enc->crc_y, crc_src);
    }

    /* FIXME: support PSF */
    if (!vbi)
        for (int i = 0; i < UPIPE_SDI_MAX_PLANES; i++)
            planes[f2][i] += input_strides[i] * (1 + (f2 ? 1 : !f->psf_ident));
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

    ulist_add(&upipe_sdi_enc->urefs, uref_to_uchain(uref)); // buffer uref
    upipe_verbose_va(upipe, "urefs: %zu", ++upipe_sdi_enc->n);

    uref = uref_from_uchain(ulist_pop(&upipe_sdi_enc->urefs));
    if (!uref) {
        upipe_err_va(upipe, "no vid uref");
        return;
    }

    // FIXME: make sure all tracks have same # of samples
    unsigned samples = audio_samples_count(upipe_sdi_enc);

    struct uchain *uchain = NULL;
    ulist_foreach(&upipe_sdi_enc->subs, uchain) {
        if (!uchain)
            break;
        struct upipe_sdi_enc_sub *sdi_enc_sub = upipe_sdi_enc_sub_from_uchain(uchain);

        struct uref *uref_audio = uref_from_uchain(ulist_pop(&sdi_enc_sub->urefs));
        if (uref_audio) {
            const uint8_t channels = sdi_enc_sub->channels;

            size_t size = 0;
            uref_sound_size(uref_audio, &size, NULL);
            if (size > samples)
                samples = size;

            const int32_t *buf;
            uref_sound_read_int32_t(uref_audio, 0, -1, &buf, 1);

            int32_t *dst = &upipe_sdi_enc->audio_buf[sdi_enc_sub->channel_idx];
            if (sdi_enc_sub->s337) {
                size_t offset = upipe_sdi_enc->dolby_offset;
                if (offset > size)
                    offset = size;
                dst += offset * UPIPE_SDI_MAX_CHANNELS;
                size -= offset;
            }
            for (size_t i = 0; i < size; i++) {
                memcpy(dst, buf, sizeof(int32_t) * channels);
                dst += UPIPE_SDI_MAX_CHANNELS;
                buf += channels;
            }

            uref_sound_unmap(uref_audio, 0, -1, 1);
            uref_free(uref_audio);
        }
    }

    size_t input_hsize, input_vsize;
    if (!ubase_check(uref_pic_size(uref, &input_hsize, &input_vsize, NULL))) {
        upipe_warn(upipe, "invalid buffer received, can not read picture size");
        uref_dump(uref, upipe->uprobe);
        uref_free(uref);
        return;
    }

    const struct sdi_offsets_fmt *f = upipe_sdi_enc->f;
    if (input_hsize != f->pict_fmt->active_width ||
        input_vsize != f->pict_fmt->active_height) {
        upipe_warn(upipe, "invalid picture received, size does not match");
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

    /* FIXME FIXME: When doing 23.94/24 fps the assembly packing will overwrite
     * so alignment here would be required to make them work */
    struct ubuf *ubuf = ubuf_block_alloc(upipe_sdi_enc->ubuf_mgr, f->width * f->height * sizeof(uint16_t) * 2);
    assert(ubuf);

    int size = -1;
    uint8_t *buf;
    ubase_assert(ubuf_block_write(ubuf, 0, &size, &buf));
    uint16_t *dst = (uint16_t*)buf;

    sdi_init_crc_channel_status(upipe_sdi_enc->aes_channel_status);

    const uint8_t *planes[2][UPIPE_SDI_MAX_PLANES];

    /* NTSC is bff, invert fields */
    bool bff = upipe_sdi_enc->p->active_height == 486;
    for (int i = 0; i < UPIPE_SDI_MAX_PLANES; i++) {
        planes[ bff][i] = input_planes[i];
        planes[!bff][i] = input_planes[i] + input_strides[i];
    }

    upipe_sdi_enc->sample_pos = 0;

    upipe_sdi_enc->ttx_packets[0] = 0;
    upipe_sdi_enc->ttx_packets[1] = 0;

    struct uref *subpic = NULL;
    /* buffered uref if any */
    struct upipe_sdi_enc_sub *subpic_sub = &upipe_sdi_enc->subpic_subpipe;
    struct uchain *uchain_subpic = ulist_pop(&subpic_sub->urefs);
    if (uchain_subpic) {
        subpic = uref_from_uchain(uchain_subpic);

        const uint8_t *buf;
        int size = -1;
        if (ubase_check(uref_block_read(subpic, 0, &size, &buf))) {
            bool sd = upipe_sdi_enc->p->sd;
            const uint8_t *pic_data = buf;
            int pic_data_size = size;

            const uint8_t *packet[2][5] = {0};
            int packets[2] = {0};

            if (pic_data[0] != DVBVBI_DATA_IDENTIFIER)
                return;

            pic_data++;
            pic_data_size--;

            static const unsigned dvb_unit_size = DVBVBI_UNIT_HEADER_SIZE + DVBVBI_LENGTH;
            for (; pic_data_size >= dvb_unit_size; pic_data += dvb_unit_size, pic_data_size -= dvb_unit_size) {
                uint8_t data_unit_id  = pic_data[0];
                uint8_t data_unit_len = pic_data[1];

                if (data_unit_id != DVBVBI_ID_TTX_SUB && data_unit_id != DVBVBI_ID_TTX_NONSUB)
                    continue;

                if (data_unit_len != DVBVBI_LENGTH)
                    continue;

                uint8_t line_offset = dvbvbittx_get_line(&pic_data[DVBVBI_UNIT_HEADER_SIZE]);

                uint8_t f2 = !dvbvbittx_get_field(&pic_data[DVBVBI_UNIT_HEADER_SIZE]);
                if (f2 == 0 && line_offset == 0) // line == 0
                    continue;

                if (upipe_sdi_enc->ttx_packets[f2] < sd ? 5 : 1) {
                    if (sd)
                        upipe_sdi_enc->ttx_line[f2][upipe_sdi_enc->ttx_packets[f2]] =
                            line_offset + PAL_FIELD_OFFSET * f2;
                    upipe_sdi_enc->ttx_packet[f2][upipe_sdi_enc->ttx_packets[f2]++] = pic_data;
                }
            }
        } else {
            upipe_err(upipe, "Could not map subpic");
            uref_free(subpic);
            subpic = NULL;
        }
    }

    for (int h = 0; h < f->height; h++) {
        /* Note conversion to 1-indexed line-number */
        uint16_t *dst_line = &dst[h * f->width * 2];

        if (upipe_sdi_enc->p->sd) {
            upipe_sdi_enc_encode_line(upipe, h+1, dst_line,
                                      planes, input_strides, samples,
                                      input_hsize, input_vsize);
        }
        else {
            upipe_hd_sdi_enc_encode_line(upipe, h+1, dst_line,
                                         planes, input_strides, samples,
                                         input_hsize, input_vsize);
            upipe_sdi_enc->eav_clock += f->width;
        }
    }

    if (subpic) {
        uref_block_unmap(subpic, 0);
        uref_free(subpic);
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

    upipe_sdi_enc_clean_urefs(&upipe_sdi_enc->urefs);
    upipe_sdi_enc->f = sdi_get_offsets(flow_def);
    if (!upipe_sdi_enc->f) {
        upipe_err(upipe, "Could not figure out SDI offsets");
        return UBASE_ERR_INVALID;
    }
    upipe_sdi_enc->p = upipe_sdi_enc->f->pict_fmt;

    if (upipe_sdi_enc->p->active_height == 576) {
        upipe_sdi_enc->sp.scanning         = 625; /* PAL */
        upipe_sdi_enc->sp.sampling_format  = VBI_PIXFMT_YUV420;
        upipe_sdi_enc->sp.sampling_rate    = 13.5e6;
        upipe_sdi_enc->sp.bytes_per_line   = 720;
        upipe_sdi_enc->sp.start[0]     = 6;
        upipe_sdi_enc->sp.count[0]     = 17;
        upipe_sdi_enc->sp.start[1]     = 319;
        upipe_sdi_enc->sp.count[1]     = 17;
        upipe_sdi_enc->sp.interlaced   = FALSE;
        upipe_sdi_enc->sp.synchronous  = FALSE;
        upipe_sdi_enc->sp.offset       = 128;
    } else if (upipe_sdi_enc->p->active_height == 486) {
        upipe_sdi_enc->sp.scanning         = 525; /* NTSC */
        upipe_sdi_enc->sp.sampling_format  = VBI_PIXFMT_YUV420;
        upipe_sdi_enc->sp.sampling_rate    = 13.5e6;
        upipe_sdi_enc->sp.bytes_per_line   = 720;
        upipe_sdi_enc->sp.interlaced   = FALSE;
        upipe_sdi_enc->sp.synchronous  = TRUE;
    }

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

    upipe_sdi_enc->dolby_offset = 0;
    if (upipe_sdi_enc->f->height == 1125) { /* Full HD */
        static const struct urational pal = { 25, 1 };
        static const struct urational ntsc = { 30000, 1001 };
        if (!urational_cmp(&upipe_sdi_enc->fps, &pal)) {
            upipe_sdi_enc->dolby_offset = 34;
        } else if (!urational_cmp(&upipe_sdi_enc->fps, &ntsc)) {
            upipe_sdi_enc->dolby_offset = 32;
        }
    UBASE_RETURN(uref_pic_flow_get_fps(flow_def, &upipe_sdi_enc->fps))
    }

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
    uref_block_flow_set_append(flow_def_dup, 256); /* worst case for asm */
    uref_pic_flow_set_fps(flow_def_dup, upipe_sdi_enc->fps);

    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This requires a ubuf manager by proxy, and amends the flow
 * format.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_sdi_enc_amend_ubuf_mgr(struct upipe *upipe,
                                        struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);

    uint64_t align;
    if (!ubase_check(uref_pic_flow_get_align(flow_format, &align)) || !align) {
        uref_pic_flow_set_align(flow_format, 32);
        align = 32;
    }


    if (align % 32) {
        align = align * 32 / ubase_gcd(align, 32);
        uref_pic_flow_set_align(flow_format, align);
    }

    struct urequest ubuf_mgr_request;
    urequest_set_opaque(&ubuf_mgr_request, request);
    urequest_init_ubuf_mgr(&ubuf_mgr_request, flow_format,
                           upipe_sdi_enc_provide_output_proxy, NULL);
    upipe_throw_provide_request(upipe, &ubuf_mgr_request);
    urequest_clean(&ubuf_mgr_request);
    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_sdi_enc_provide_flow_format(struct upipe *upipe,
                                              struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);

    uref_pic_flow_clear_format(flow_format);

    uref_pic_flow_set_macropixel(flow_format, 1);

    uint8_t plane;
    if (ubase_check(uref_pic_flow_find_chroma(request->uref, "y10l", &plane))) {
        uref_pic_flow_add_plane(flow_format, 1, 1, 2, "y10l");
        uref_pic_flow_add_plane(flow_format, 2, 1, 2, "u10l");
        uref_pic_flow_add_plane(flow_format, 2, 1, 2, "v10l");
    } else if (ubase_check(uref_pic_flow_find_chroma(request->uref, "y8", &plane))) {
        uref_pic_flow_add_plane(flow_format, 1, 1, 1, "y8");
        uref_pic_flow_add_plane(flow_format, 2, 1, 1, "u8");
        uref_pic_flow_add_plane(flow_format, 2, 1, 1, "v8");
    } else {
        uref_pic_flow_set_macropixel(flow_format, 6);
        uref_pic_flow_add_plane(flow_format, 1, 1, 16, "u10y10v10y10u10y10v10y10u10y10v10y10");
    }

    return urequest_provide_flow_format(request, flow_format);
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
    struct upipe_sdi_enc *upipe_sdi_enc = upipe_sdi_enc_from_upipe(upipe);

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
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_sdi_enc_amend_ubuf_mgr(upipe, request);
            if (request->type == UREQUEST_FLOW_FORMAT)
				return upipe_sdi_enc_provide_flow_format(upipe, request);
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
        case UPIPE_SDI_ENC_GET_SUBPIC_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SDI_ENC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p =  upipe_sdi_enc_sub_to_upipe(
                    upipe_sdi_enc_to_subpic_subpipe(
                        upipe_sdi_enc_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_SET_OPTION: {
            const char *k = va_arg(args, const char *);
            const char *v = va_arg(args, const char *);
            if (!strcmp(k, "teletext")) {
                upipe_sdi_enc->ttx = strcmp(v, "0");
            } else
                return UBASE_ERR_INVALID;

            return UBASE_ERR_NONE;
        }


        default:
            return UBASE_ERR_UNHANDLED;
    }
}

#define CLIP8(c) (av_clip((*(c)), 1,  254))
#define CLIP(c)  (av_clip((*(c)), 4, 1019))

static void planar_to_uyvy_8_c(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, const uintptr_t width)
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

static void planar_to_uyvy_10_c(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const uintptr_t width)
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

static void v210_uyvy_unpack_c(const uint32_t *src, uint16_t *uyvy, uintptr_t width)
{
    uint32_t val;
    int i;

    for( i = 0; i < width; i += 6 ){
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
static struct upipe *_upipe_sdi_enc_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    if (signature != UPIPE_SDI_ENC_SIGNATURE)
        return NULL;

    struct uprobe *uprobe_subpic = va_arg(args, struct uprobe *);

    struct upipe_sdi_enc *upipe_sdi_enc = calloc(1, sizeof(*upipe_sdi_enc));
    if (unlikely(upipe_sdi_enc == NULL))
        return NULL;

    struct upipe *upipe = &upipe_sdi_enc->upipe;
    upipe_init(upipe, mgr, uprobe);

    ulist_init(&upipe_sdi_enc->urefs);
    upipe_sdi_enc->n = 0;
    upipe_sdi_enc->dolby_offset = 0;
    upipe_sdi_enc->ttx = false;

    upipe_sdi_enc->blank             = upipe_sdi_blank_c;
    upipe_sdi_enc->planar_to_uyvy_8  = planar_to_uyvy_8_c;
    upipe_sdi_enc->planar_to_uyvy_10 = planar_to_uyvy_10_c;
    upipe_sdi_enc->v210_to_uyvy      = v210_uyvy_unpack_c;

#if !defined(__APPLE__) /* macOS clang doesn't support that builtin yet */
    if (__builtin_cpu_supports("sse2")) {
        upipe_sdi_enc->planar_to_uyvy_8 = upipe_planar_to_uyvy_8_unaligned_sse2;
        upipe_sdi_enc->planar_to_uyvy_10 = upipe_planar_to_uyvy_10_unaligned_sse2;
    }

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
        upipe_sdi_enc->v210_to_uyvy      = upipe_v210_to_uyvy_unaligned_ssse3;
    }

    if (__builtin_cpu_supports("avx")) {
        upipe_sdi_enc->blank             = upipe_sdi_blank_avx;
        upipe_sdi_enc->planar_to_uyvy_8  = upipe_planar_to_uyvy_8_unaligned_avx;
        upipe_sdi_enc->planar_to_uyvy_10 = upipe_planar_to_uyvy_10_unaligned_avx;
        upipe_sdi_enc->v210_to_uyvy      = upipe_v210_to_uyvy_unaligned_avx;
    }

    if (__builtin_cpu_supports("avx2")) {
        upipe_sdi_enc->planar_to_uyvy_8  = upipe_planar_to_uyvy_8_unaligned_avx2;
        upipe_sdi_enc->planar_to_uyvy_10 = upipe_planar_to_uyvy_10_unaligned_avx2;
        upipe_sdi_enc->v210_to_uyvy      = upipe_v210_to_uyvy_unaligned_avx2;
    }
#endif

    upipe_sdi_enc_init_urefcount(upipe);
    upipe_sdi_enc_init_ubuf_mgr(upipe);
    upipe_sdi_enc_init_output(upipe);
    upipe_sdi_enc_init_sub_mgr(upipe);
    upipe_sdi_enc_init_sub_subs(upipe);

    /* Initalise subpipes */
    upipe_sdi_enc_sub_init(upipe_sdi_enc_sub_to_upipe(upipe_sdi_enc_to_subpic_subpipe(upipe_sdi_enc)),
            &upipe_sdi_enc->sub_mgr, uprobe_subpic, true);

    upipe_sdi_enc->crc_c = 0;
    upipe_sdi_enc->crc_y = 0;

    upipe_sdi_enc->eav_clock = 0;
    upipe_sdi_enc->sample_pos = 0;
    upipe_sdi_enc->total_audio_samples_put = 0;
    for (int i = 0; i < UPIPE_SDI_CHANNELS_PER_GROUP; i++)
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
    upipe_sdi_enc_clean_urefs(&upipe_sdi_enc->urefs);
    uref_free(upipe_sdi_enc->uref_audio);
    upipe_sdi_enc_clean_output(upipe);
    upipe_sdi_enc_clean_ubuf_mgr(upipe);
    upipe_sdi_enc_clean_urefcount(upipe);
    upipe_sdi_enc_clean_sub_subs(upipe);
    free(upipe_sdi_enc);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_sdi_enc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SDI_ENC_SIGNATURE,

    .upipe_alloc = _upipe_sdi_enc_alloc,
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
