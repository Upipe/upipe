#ifndef _UPIPE_MODULES_UPIPE_HBRMT_COMMON_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_HBRMT_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <bitstream/ietf/rtp.h>
#include <bitstream/ieee/ethernet.h>
#include <bitstream/ietf/ip.h>
#include <bitstream/ietf/udp.h>

#define HBRMT_HEADER_ONLY_SIZE 8
#define HBRMT_DATA_SIZE 1376
#define RAW_HEADER_SIZE (IP_HEADER_MINSIZE + UDP_HEADER_SIZE)
#define HBRMT_DATA_OFFSET (RTP_HEADER_SIZE + HBRMT_HEADER_ONLY_SIZE)
#define HBRMT_LEN (ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE + RTP_HEADER_SIZE + HBRMT_HEADER_ONLY_SIZE + HBRMT_DATA_SIZE)

#define UPIPE_SDI_CHROMA_BLANKING_START    4
#define UPIPE_HDSDI_CHROMA_BLANKING_START 16

#define UPIPE_SDI_PSF_IDENT_I   0
#define UPIPE_SDI_PSF_IDENT_PSF 1
#define UPIPE_SDI_PSF_IDENT_P   3

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
    *crc_c ^= (buf[2] << 10) | buf[0];
    *crc_y ^= (buf[3] << 10) | buf[1];

    *crc_c = sdi_crc_lut[0][buf[14]] ^
             sdi_crc_lut[1][buf[12]] ^
             sdi_crc_lut[2][buf[10]] ^
             sdi_crc_lut[3][buf[8]] ^
             sdi_crc_lut[4][buf[6]] ^
             sdi_crc_lut[5][buf[4]] ^
             sdi_crc_lut[6][*crc_c >> 10] ^
             sdi_crc_lut[7][*crc_c & 0x3ff];

    *crc_y = sdi_crc_lut[0][buf[15]] ^
             sdi_crc_lut[1][buf[13]] ^
             sdi_crc_lut[2][buf[11]] ^
             sdi_crc_lut[3][buf[9]] ^
             sdi_crc_lut[4][buf[7]] ^
             sdi_crc_lut[5][buf[5]] ^
             sdi_crc_lut[6][*crc_y >> 10] ^
             sdi_crc_lut[7][*crc_y & 0x3ff];
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
    
    /* Offset between fields */
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

    /* frame_rate
     * 0x0 Undefined
     * 0x1 Reserved
     * 0x2 24/1.001Hz
     * 0x3 24Hz
     * 0x4 Reserved
     * 0x5 25Hz
     * 0x6 30/1.001 Hz
     * 0x7 30Hz
     * 0x8 Reserved
     * 0x9 50Hz
     * 0xA 60/1.001 Hz
     */
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

    static const struct sdi_picture_fmt pict_fmts[] = {
        /* 1125 Interlaced (1080 active) lines */
        {0, 1920, 1080, 562, 7, 10, {1, 20}, {21, 560}, {561, 563}, {564, 583}, {584, 1123}, {1124, 1125}},
        /* 1125 Progressive (1080 active) lines */
        {0, 1920, 1080, 0, 7, 10, {1, 41}, {42, 1121}, {1122, 1125}, {0, 0}, {0, 0}, {0, 0}},
        /* 750 Progressive (720 active) lines */
        {0, 1280, 720, 0, 7, 10, {1, 25}, {26, 745}, {746, 750}, {0, 0}, {0, 0}, {0, 0}},

        /* PAL */
        {1, 720, 576, 313, 6, 9, {1, 22}, {23, 310}, {311, 312}, {313, 335}, {336, 623}, {624, 625}},
        /* NTSC TODO */
    };

    static const struct sdi_offsets_fmt fmts_data[] = {
        /* 1125 Lines */
        { 2640, 1125, 720, &pict_fmts[0], 0x0, 0x5, { 25, 1} },        /* 25 Hz I */
        { 2640, 1125, 720, &pict_fmts[1], 0x3, 0x9, { 50, 1} },        /* 50 Hz P */

        { 2200, 1125, 280, &pict_fmts[0], 0x0, 0x6, { 30000, 1001 } }, /* 30/1.001 Hz I */
        { 2200, 1125, 280, &pict_fmts[1], 0x3, 0xA, { 60000, 1001 } }, /* 60/1.001 Hz P */

        { 2750, 1125, 830, &pict_fmts[0], 0x3, 0x2, { 24000, 1001 } }, /* 24/1.001 Hz */
        { 2750, 1125, 830, &pict_fmts[0], 0x3, 0x3, { 24, 1 } },       /* 24 Hz */

        /* 750 Lines */
        { 1980, 750, 700, &pict_fmts[2], 0x3, 0x9, { 50, 1} },        /* 50 Hz P */
        { 1650, 750, 370, &pict_fmts[2], 0x3, 0xA, { 60000, 1001 } }, /* 60/1.001 Hz P */

        { 864,  625, 144, &pict_fmts[3], 0x0, 0x5, { 25, 1} },        /* 625-line 25 Hz I */
        
    };

    for (size_t i = 0; i < sizeof(fmts_data) / sizeof(struct sdi_offsets_fmt); i++)
        if (!urational_cmp(&fps, &fmts_data[i].fps))
            if (fmts_data[i].pict_fmt->active_width == hsize)
                if (fmts_data[i].pict_fmt->active_height == vsize)
                    return &fmts_data[i];

    return NULL;
}

#ifdef __cplusplus
}
#endif
#endif
