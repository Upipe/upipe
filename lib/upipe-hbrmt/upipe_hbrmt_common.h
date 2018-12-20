#ifndef _UPIPE_MODULES_UPIPE_HBRMT_COMMON_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_HBRMT_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref_pic.h>
#include <upipe/uref_attr.h>

#include <bitstream/ietf/rtp.h>
#include <bitstream/ieee/ethernet.h>
#include <bitstream/ietf/ip.h>
#include <bitstream/ietf/udp.h>
#include <bitstream/smpte/352.h>
#include <bitstream/smpte/2022_6_hbrmt.h>

UREF_ATTR_VOID(block, sdi3g_levelb, "SDI-3G level-B", flag to indicate that format is level B)

#define RAW_HEADER_SIZE (IP_HEADER_MINSIZE + UDP_HEADER_SIZE)
#define HBRMT_DATA_OFFSET (RTP_HEADER_SIZE + HBRMT_HEADER_SIZE)
#define HBRMT_LEN (ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE + RTP_HEADER_SIZE + HBRMT_HEADER_SIZE + HBRMT_DATA_SIZE)

/* EAV Length
 * Start of HANC data, technically HBI since not all lines have HANC */
#define UPIPE_SDI_EAV_LENGTH    4
#define UPIPE_HD_SDI_EAV_LENGTH 16

/* SAV Length */
#define UPIPE_SDI_SAV_LENGTH    4
#define UPIPE_HD_SDI_SAV_LENGTH 8

/* [Field][VBI] */
static const uint16_t sav_fvh_cword[2][2] = {{0x200, 0x2ac}, {0x31c, 0x3b0}};
static const uint16_t eav_fvh_cword[2][2] = {{0x274, 0x2d8}, {0x368, 0x3c4}};

#define UPIPE_SDI_PSF_IDENT_I   0
#define UPIPE_SDI_PSF_IDENT_PSF 1
#define UPIPE_SDI_PSF_IDENT_P   3
#define UPIPE_SDI_PSF_IDENT_SDI3G_LEVELB 4

#define UPIPE_SDI_CHANNELS_PER_GROUP 4

static void sdi_crc_setup(uint32_t crc_lut[8][1024])
{
    #define SDI_CRC_POLY 0x46001
        for (int i = 0; i < 1024; i++) {
            uint32_t current = i;
            for (int j = 0; j < 10; j++) {
                if (current & 1)
                    current ^= SDI_CRC_POLY;
                current >>= 1;
            }
            crc_lut[0][i] = current;
        }

        for (int i = 0; i < 1024; i++)
        {
            crc_lut[1][i] = (crc_lut[0][i] >> 10) ^ crc_lut[0][crc_lut[0][i] & 0x3ff];
            crc_lut[2][i] = (crc_lut[1][i] >> 10) ^ crc_lut[0][crc_lut[1][i] & 0x3ff];
            crc_lut[3][i] = (crc_lut[2][i] >> 10) ^ crc_lut[0][crc_lut[2][i] & 0x3ff];
            crc_lut[4][i] = (crc_lut[3][i] >> 10) ^ crc_lut[0][crc_lut[3][i] & 0x3ff];
            crc_lut[5][i] = (crc_lut[4][i] >> 10) ^ crc_lut[0][crc_lut[4][i] & 0x3ff];
            crc_lut[6][i] = (crc_lut[5][i] >> 10) ^ crc_lut[0][crc_lut[5][i] & 0x3ff];
            crc_lut[7][i] = (crc_lut[6][i] >> 10) ^ crc_lut[0][crc_lut[6][i] & 0x3ff];
        }
}

static inline void sdi_crc_update(uint32_t *sdi_crc_lut, uint32_t *crc, uint16_t data)
{
    const uint32_t c = *crc;
    *crc = (c >> 10) ^ sdi_crc_lut[(c ^ data) & 0x3ff];
}

static inline void sdi_crc_update_blk(uint32_t sdi_crc_lut[8][1024], uint32_t *crc_c, uint32_t *crc_y, const uint16_t *buf)
{
    uint32_t c = *crc_c ^ ((buf[2] << 10) | buf[0]);
    uint32_t y = *crc_y ^ ((buf[3] << 10) | buf[1]);

    *crc_c = sdi_crc_lut[0][buf[14]] ^
             sdi_crc_lut[1][buf[12]] ^
             sdi_crc_lut[2][buf[10]] ^
             sdi_crc_lut[3][buf[8]] ^
             sdi_crc_lut[4][buf[6]] ^
             sdi_crc_lut[5][buf[4]] ^
             sdi_crc_lut[6][c >> 10] ^
             sdi_crc_lut[7][c & 0x3ff];

    *crc_y = sdi_crc_lut[0][buf[15]] ^
             sdi_crc_lut[1][buf[13]] ^
             sdi_crc_lut[2][buf[11]] ^
             sdi_crc_lut[3][buf[9]] ^
             sdi_crc_lut[4][buf[7]] ^
             sdi_crc_lut[5][buf[5]] ^
             sdi_crc_lut[6][y >> 10] ^
             sdi_crc_lut[7][y & 0x3ff];
}

#define NOT_BIT8(x) ((!(((x) >> 8) & 1)) << 9)

static inline void sdi_crc_end(uint32_t *crc_c, uint16_t *dst)
{
    uint16_t crc0, crc1;

    uint32_t crc = *crc_c;
    *crc_c = 0;

    crc0  = crc & 0x1ff;
    crc0 |= NOT_BIT8(crc0);
    crc1  = (crc >> 9) & 0x1ff;
    crc1 |= NOT_BIT8(crc1);

    dst[0] = crc0;
    dst[2] = crc1;
}

struct sdi_line_range {
    uint16_t start;
    uint16_t end;
};

struct sdi_picture_fmt {
    bool sd;

    /* Active picture dimensions */
    uint16_t active_width;
    uint16_t active_height;

    /* Offset between fields.
       Note this is not the field offset between switching lines.  */
    uint16_t field_offset;

    /* SMPTE RP168 Switch Line */
    uint16_t switching_line;

    /* SMPTE 352 Payload ID line */
    uint16_t payload_id_line;

    /* Field 1 (interlaced) or Frame (progressive) line ranges */
    struct sdi_line_range vbi_f1_part1;
    struct sdi_line_range active_f1;
    struct sdi_line_range vbi_f1_part2;

    /* Field 2 (interlaced)  */
    struct sdi_line_range vbi_f2_part1;
    struct sdi_line_range active_f2;
    struct sdi_line_range vbi_f2_part2;
};

struct sdi_offsets_fmt {
    /* Full SDI width and height */
    uint16_t width;
    uint16_t height;

    /* Number of samples (pairs) between EAV and start of active data */
    uint16_t active_offset;

    const struct sdi_picture_fmt *pict_fmt;

    /* 0x0 (Interlace), 0x1 (Segmented frame), 0x3 (Progressive) */
    uint8_t psf_ident;

    uint8_t frame_rate;

    struct urational fps;
};

static inline const struct sdi_offsets_fmt *sdi_get_offsets(struct uref *flow_def)
{
    struct urational fps;

    if (!ubase_check(uref_pic_flow_get_fps(flow_def, &fps)))
        return NULL;

    uint64_t hsize, vsize;
    if (!ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize)) ||
        !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize)))
        return NULL;

    bool interlaced = !ubase_check(uref_pic_get_progressive(flow_def));
    bool sdi3g_levelb = ubase_check(uref_block_get_sdi3g_levelb(flow_def));

    static const struct sdi_picture_fmt pict_fmts[] = {
        /* 1125 Interlaced (1080 active) lines */
        {0, 1920, 1080, 563, 7, 10, {1, 20}, {21, 560}, {561, 563}, {564, 583}, {584, 1123}, {1124, 1125}},
        /* 1125 Progressive (1080 active) lines */
        {0, 1920, 1080, 0, 7, 10, {1, 41}, {42, 1121}, {1122, 1125}, {0, 0}, {0, 0}, {0, 0}},
        /* 750 Progressive (720 active) lines */
        {0, 1280, 720, 0, 7, 10, {1, 25}, {26, 745}, {746, 750}, {0, 0}, {0, 0}, {0, 0}},

        /* PAL */
        {1, 720, 576, 313, 6, 9, {1, 22}, {23, 310}, {311, 312}, {313, 335}, {336, 623}, {624, 625}},
        /* NTSC */
        {1, 720, 486, 266, 10, 13, {4, 19}, {20, 263}, {264, 265}, {266, 282}, {283, 525}, {1, 3}},

        /* SDI-3G */
        {0, 1920, 1080, 0, 0, 0, {1, 40}, {41, 1120}, {1121, 1126}, {1127, 1166}, {1167, 2246}, {2247, 2250}},
    };

    static const struct sdi_offsets_fmt fmts_data[] = {
        /* 1125 Lines */
        { 2640, 1125, 720, &pict_fmts[0], 0x0, S352_PICTURE_RATE_25, { 25, 1} }, /* 25 Hz I */
        { 2640, 1125, 720, &pict_fmts[1], 0x3, S352_PICTURE_RATE_50, { 50, 1} }, /* 50 Hz P */

        { 2200, 1125, 280, &pict_fmts[0], 0x0, S352_PICTURE_RATE_30000_1001, { 30000, 1001 } }, /* 30/1.001 Hz I */
        { 2200, 1125, 280, &pict_fmts[1], 0x3, S352_PICTURE_RATE_60000_1001, { 60000, 1001 } }, /* 60/1.001 Hz P */
        { 2200, 1125, 280, &pict_fmts[1], 0x3, S352_PICTURE_RATE_60, { 60, 1 } },               /* 60 Hz P */

        { 2750, 1125, 830, &pict_fmts[1], 0x3, S352_PICTURE_RATE_24000_1001, { 24000, 1001 } }, /* 24/1.001 Hz */
        { 2750, 1125, 830, &pict_fmts[1], 0x3, S352_PICTURE_RATE_24, { 24, 1 } },               /* 24 Hz */

        { 2750, 1125, 830, &pict_fmts[0], 0x0, S352_PICTURE_RATE_24, { 24, 1 } }, /* 1080i24 */
        { 2200, 1125, 280, &pict_fmts[0], 0x0, S352_PICTURE_RATE_30, { 30, 1 } }, /* 1080i30 */
        { 2200, 1125, 280, &pict_fmts[1], 0x3, S352_PICTURE_RATE_30, { 30, 1 } }, /* 1080p30 */
        { 2640, 1125, 720, &pict_fmts[1], 0x3, S352_PICTURE_RATE_25, { 25, 1 } }, /* 1080p25 */

        /* 750 Lines */
        { 1980, 750, 700, &pict_fmts[2], 0x3, S352_PICTURE_RATE_50, { 50, 1} },                /* 50 Hz P */
        { 1650, 750, 370, &pict_fmts[2], 0x3, S352_PICTURE_RATE_60000_1001, { 60000, 1001 } }, /* 60/1.001 Hz P */
        { 1650, 750, 370, &pict_fmts[2], 0x3, S352_PICTURE_RATE_60, { 60, 1 } },               /* 60 Hz P */

        { 864,  625, 144, &pict_fmts[3], 0x0, S352_PICTURE_RATE_25, { 25, 1} },                /* 625-line 25 Hz I */
        { 858,  525, 138, &pict_fmts[4], 0x0, S352_PICTURE_RATE_30000_1001, { 30000, 1001 } }, /* 525-line 30/1.001 Hz I */
    };

    static const struct sdi_offsets_fmt fmts_data_3g_levelb[] = {
        /* SDI-3G */
        { 2200, 1125, 280, &pict_fmts[5], 0x4, S352_PICTURE_RATE_60, { 60, 1 } }, /* 60 Hz P */
        { 2640, 1125, 720, &pict_fmts[5], 0x4, S352_PICTURE_RATE_50, { 50, 1 } }, /* 50 Hz P */
    };

    if (sdi3g_levelb) {
        for (size_t i = 0; i < sizeof(fmts_data_3g_levelb) / sizeof(fmts_data_3g_levelb[0]); i++)
            if (!urational_cmp(&fps, &fmts_data_3g_levelb[i].fps))
                if (fmts_data_3g_levelb[i].pict_fmt->active_width == hsize)
                    if (fmts_data_3g_levelb[i].pict_fmt->active_height == vsize)
                        return &fmts_data_3g_levelb[i];
        return NULL;
    }

    for (size_t i = 0; i < sizeof(fmts_data) / sizeof(struct sdi_offsets_fmt); i++)
        if (!urational_cmp(&fps, &fmts_data[i].fps))
            if (fmts_data[i].pict_fmt->active_width == hsize)
                if (fmts_data[i].pict_fmt->active_height == vsize)
                    if (interlaced == (fmts_data[i].psf_ident != UPIPE_SDI_PSF_IDENT_P))
                            return &fmts_data[i];

    return NULL;
}

/* These functions check that the EAV marker and "fvh" word is at the address's
 * location, or that the SAV and "fvh" preceed the address's location. */

static inline bool hd_eav_match(const uint16_t *src)
{
    if (src[0] == 0x3ff
            && src[1] == 0x3ff
            && src[2] == 0x000
            && src[3] == 0x000
            && src[4] == 0x000
            && src[5] == 0x000
            && src[6] == src[7]
            && (src[6] == 0x274
                || src[6] == 0x2d8
                || src[6] == 0x368
                || src[6] == 0x3c4))
        return true;
    return false;
}

static inline bool hd_sav_match(const uint16_t *src)
{
    if (src[-8] == 0x3ff
            && src[-7] == 0x3ff
            && src[-6] == 0x000
            && src[-5] == 0x000
            && src[-4] == 0x000
            && src[-3] == 0x000
            && src[-2] == src[-1]
            && (src[-2] == 0x200
                || src[-2] == 0x2ac
                || src[-2] == 0x31c
                || src[-2] == 0x3b0))
        return true;
    return false;
}

static inline bool hd_eav_match_bitpacked(const uint8_t *src)
{
    if (src[0] == 0xff
            && src[1] == 0xff
            && src[2] == 0xf0
            && src[3] == 0
            && src[4] == 0
            && src[5] == 0
            && src[6] == 0
            && ((src[7] == 9 && src[8] == 0xd2 && src[9] == 0x74)
                || (src[7] == 0xb && src[8] == 0x62 && src[9] == 0xd8)
                || (src[7] == 0xd && src[8] == 0xa3 && src[9] == 0x68)
                || (src[7] == 0xf && src[8] == 0x13 && src[9] == 0xc4)))
        return true;
    return false;
}

static inline bool hd_sav_match_bitpacked(const uint8_t *src)
{
    if (src[-10] == 0xff
            && src[-9] == 0xff
            && src[-8] == 0xf0
            && src[-7] == 0
            && src[-6] == 0
            && src[-5] == 0
            && src[-4] == 0
            && ((src[-3] == 8 && src[-2] == 2 && src[-1] == 0)
                || (src[-3] == 0xa && src[-2] == 0xb2 && src[-1] == 0xac)
                || (src[-3] == 0xc && src[-2] == 0x73 && src[-1] == 0x1c)
                || (src[-3] == 0xe && src[-2] == 0xc3 && src[-1] == 0xb0)))
        return true;
    return false;
}

static inline bool sd_eav_match(const uint16_t *src)
{
    if (src[0] == 0x3ff
            && src[1] == 0x000
            && src[2] == 0x000
            && (src[3] == 0x274
                || src[3] == 0x2d8
                || src[3] == 0x368
                || src[3] == 0x3c4))
        return true;
    return false;
}

static inline bool sd_sav_match(const uint16_t *src)
{
    if (src[-4] == 0x3ff
            && src[-3] == 0x000
            && src[-2] == 0x000
            && (src[-1] == 0x200
                || src[-1] == 0x2ac
                || src[-1] == 0x31c
                || src[-1] == 0x3b0))
        return true;
    return false;
}

#ifdef __cplusplus
}
#endif
#endif
