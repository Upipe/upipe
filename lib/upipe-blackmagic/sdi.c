#include <stdbool.h>
#include <string.h>

#include <bitstream/smpte/291.h>
#include <bitstream/dvb/vbi.h>

#include "sdi.h"

static const uint8_t reverse_tab[256] = {
    0x00,0x80,0x40,0xC0,0x20,0xA0,0x60,0xE0,0x10,0x90,0x50,0xD0,0x30,0xB0,0x70,0xF0,
    0x08,0x88,0x48,0xC8,0x28,0xA8,0x68,0xE8,0x18,0x98,0x58,0xD8,0x38,0xB8,0x78,0xF8,
    0x04,0x84,0x44,0xC4,0x24,0xA4,0x64,0xE4,0x14,0x94,0x54,0xD4,0x34,0xB4,0x74,0xF4,
    0x0C,0x8C,0x4C,0xCC,0x2C,0xAC,0x6C,0xEC,0x1C,0x9C,0x5C,0xDC,0x3C,0xBC,0x7C,0xFC,
    0x02,0x82,0x42,0xC2,0x22,0xA2,0x62,0xE2,0x12,0x92,0x52,0xD2,0x32,0xB2,0x72,0xF2,
    0x0A,0x8A,0x4A,0xCA,0x2A,0xAA,0x6A,0xEA,0x1A,0x9A,0x5A,0xDA,0x3A,0xBA,0x7A,0xFA,
    0x06,0x86,0x46,0xC6,0x26,0xA6,0x66,0xE6,0x16,0x96,0x56,0xD6,0x36,0xB6,0x76,0xF6,
    0x0E,0x8E,0x4E,0xCE,0x2E,0xAE,0x6E,0xEE,0x1E,0x9E,0x5E,0xDE,0x3E,0xBE,0x7E,0xFE,
    0x01,0x81,0x41,0xC1,0x21,0xA1,0x61,0xE1,0x11,0x91,0x51,0xD1,0x31,0xB1,0x71,0xF1,
    0x09,0x89,0x49,0xC9,0x29,0xA9,0x69,0xE9,0x19,0x99,0x59,0xD9,0x39,0xB9,0x79,0xF9,
    0x05,0x85,0x45,0xC5,0x25,0xA5,0x65,0xE5,0x15,0x95,0x55,0xD5,0x35,0xB5,0x75,0xF5,
    0x0D,0x8D,0x4D,0xCD,0x2D,0xAD,0x6D,0xED,0x1D,0x9D,0x5D,0xDD,0x3D,0xBD,0x7D,0xFD,
    0x03,0x83,0x43,0xC3,0x23,0xA3,0x63,0xE3,0x13,0x93,0x53,0xD3,0x33,0xB3,0x73,0xF3,
    0x0B,0x8B,0x4B,0xCB,0x2B,0xAB,0x6B,0xEB,0x1B,0x9B,0x5B,0xDB,0x3B,0xBB,0x7B,0xFB,
    0x07,0x87,0x47,0xC7,0x27,0xA7,0x67,0xE7,0x17,0x97,0x57,0xD7,0x37,0xB7,0x77,0xF7,
    0x0F,0x8F,0x4F,0xCF,0x2F,0xAF,0x6F,0xEF,0x1F,0x9F,0x5F,0xDF,0x3F,0xBF,0x7F,0xFF,
};

#define REVERSE(x) reverse_tab[(x)]

static const bool parity_tab[256] =
{
#   define P2(n) n, n^1, n^1, n
#   define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#   define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)
    P6(0), P6(1), P6(1), P6(0)
};

void sdi_calc_parity_checksum(uint16_t *buf)
{
    uint16_t checksum = 0;
    uint16_t dc = buf[DC_POS];

    /* +3 = did + sdid + dc itself */
    for (uint16_t i = 0; i < dc+3; i++) {
        uint8_t parity = parity_tab[buf[3+i] & 0xff];
        buf[3+i] |= (!parity << 9) | (parity << 8);

        checksum += buf[3+i] & 0x1ff;
    }

    checksum &= 0x1ff;
    checksum |= (!(checksum >> 8)) << 9;

    buf[ANC_START_LEN+dc] = checksum;
}

void sdi_clear_vbi(uint8_t *dst, int w)
{
    memset(&dst[0], 0x10, w);
    memset(&dst[w], 0x80, w);
}

void sdi_clear_vanc(uint16_t *dst)
{
    for (int i = 0; i < VANC_WIDTH; i++)
        dst[i] = 0x40;

    dst += VANC_WIDTH;

    for (int i = 0; i < VANC_WIDTH; i++)
        dst[i] = 0x200;
}

static void sdi_start_anc(uint16_t *dst, uint16_t did, uint16_t sdid)
{
    dst[0] = S291_ADF1;
    dst[1] = S291_ADF2;
    dst[2] = S291_ADF3;
    dst[3] = did;
    dst[4] = sdid;
    /* DC */
    dst[5] = 0;
}

void sdi_write_cdp(const uint8_t *src, size_t src_size,
        uint16_t *dst, uint16_t *ctr, uint8_t fps)
{
    sdi_clear_vanc(dst);
    sdi_start_anc(dst, S291_CEA708_DID, S291_CEA708_SDID);

    const uint8_t cnt = 9 + src_size + 4;
    const uint16_t hdr_sequence_cntr = (*ctr)++;

    dst[ANC_START_LEN + 0] = 0x96;
    dst[ANC_START_LEN + 1] = 0x69;
    dst[ANC_START_LEN + 2] = cnt;
    dst[ANC_START_LEN + 3] = (fps << 4) | 0xf; // cdp_frame_rate | Reserved
    dst[ANC_START_LEN + 4] = (1 << 6) | (1 << 1) | 1; // ccdata_present | caption_service_active | Reserved
    dst[ANC_START_LEN + 5] = hdr_sequence_cntr >> 8;
    dst[ANC_START_LEN + 6] = hdr_sequence_cntr & 0xff;
    dst[ANC_START_LEN + 7] = 0x72;
    dst[ANC_START_LEN + 8] = (0x7 << 5) | (src_size / 3);

    for (int i = 0; i < src_size; i++)
        dst[ANC_START_LEN + 9 + i] = src[i];

    dst[ANC_START_LEN + 9 + src_size] = 0x74;
    dst[ANC_START_LEN + 9 + src_size + 1] = dst[ANC_START_LEN + 5];
    dst[ANC_START_LEN + 9 + src_size + 2] = dst[ANC_START_LEN + 6];

    uint8_t checksum = 0;
    for (int i = 0; i < cnt-1; i++) // don't include checksum
        checksum += dst[ANC_START_LEN + i];

    dst[ANC_START_LEN + 9 + src_size + 3] = checksum ? 256 - checksum : 0;

    dst[DC_POS] = cnt; // DC
}

static inline uint32_t to_le32(uint32_t a)
{
#ifdef UPIPE_WORDS_BIGENDIAN
    return  ((a << 24) & 0xff000000 ) |
        ((a <<  8) & 0x00ff0000 ) |
        ((a >>  8) & 0x0000ff00 ) |
        ((a >> 24) & 0x000000ff );
#else
    return a;
#endif
}

void sdi_encode_v210_sd(uint32_t *dst, uint8_t *src, int width)
{
    uint8_t *y = src;
    uint8_t *u = &y[width];

#define WRITE_PIXELS8(a, b, c) \
    *dst++ = to_le32((*(a) << 2) | (*(b) << 12) | (*(c) << 22))

    for (int w = 0; w < width; w += 6) {
        WRITE_PIXELS8(u, y, u+1);
        y += 1;
        u += 2;
        WRITE_PIXELS8(y, u, y+1);
        y += 2;
        u += 1;
        WRITE_PIXELS8(u, y, u+1);
        y += 1;
        u += 2;
        WRITE_PIXELS8(y, u, y+1);
        y += 2;
        u += 1;
    }
}

void sdi_encode_v210(uint32_t *dst, uint16_t *src, int width)
{
    /* 1280 isn't mod-6 so long vanc packets will be truncated */
    uint16_t *y = src;
    uint16_t *u = &y[width];

    /* don't clip the v210 anc data */
#define WRITE_PIXELS(a, b, c)           \
    *dst++ = to_le32(*(a) | (*(b) << 10) | (*(c) << 20))

    for (int w = 0; w < width; w += 6) {
        WRITE_PIXELS(u, y, u+1);
        y += 1;
        u += 2;
        WRITE_PIXELS(y, u, y+1);
        y += 2;
        u += 1;
        WRITE_PIXELS(u, y, u+1);
        y += 1;
        u += 2;
        WRITE_PIXELS(y, u, y+1);
        y += 2;
        u += 1;
    }
}

#ifdef UPIPE_HAVE_LIBZVBI_H
int sdi_encode_ttx_sd(uint8_t *buf, const uint8_t *pic_data, vbi_sampling_par *sp)
{
    uint8_t line_offset = dvbvbittx_get_line(&pic_data[DVBVBI_UNIT_HEADER_SIZE]);
    uint8_t f2 = !dvbvbittx_get_field(&pic_data[DVBVBI_UNIT_HEADER_SIZE]);
    uint16_t line = line_offset + PAL_FIELD_OFFSET * f2;

    sp->start[f2] = line;
    sp->count[f2] = 1;
    sp->count[!f2] = 0;

    vbi_sliced sliced;
    sliced.id = VBI_SLICED_TELETEXT_B;
    sliced.line = line;
    for (int i = 0; i < 42; i++)
        sliced.data[i] = REVERSE(pic_data[4+i]);

    if (!vbi_raw_video_image(buf, 720, sp, 0, 0, 0, 0x000000FF, false,
                &sliced, 1)) {
        // error
    }
    return line;
}
#endif

void sdi_encode_ttx(uint16_t *buf, int packets, const uint8_t **packet, uint16_t *ctr)
{
    sdi_start_anc(buf, S291_OP47SDP_DID, S291_OP47SDP_SDID);

    /* 2 identifiers */
    buf[ANC_START_LEN]   = 0x51;
    buf[ANC_START_LEN+1] = 0x15;

    /* Length, populate this last */
    buf[ANC_START_LEN+2] = 0x0;

    /* Format code */
    buf[ANC_START_LEN+3] = 0x2; /* WST Teletext subtitles */

    /* Data Adaption header, 5 packets max */
    memset(&buf[ANC_START_LEN + OP47_INITIAL_WORDS], 0x00, 5 * sizeof(uint16_t));

    for (int j = 0; j < packets; j++) {
        const uint8_t *pic_data = packet[j];

        uint8_t line_offset = dvbvbittx_get_line(&pic_data[DVBVBI_UNIT_HEADER_SIZE]);
        uint8_t f2 = !dvbvbittx_get_field(&pic_data[DVBVBI_UNIT_HEADER_SIZE]);

        /* Write structure A */
        buf[ANC_START_LEN + OP47_INITIAL_WORDS + j]  = ((!f2) << 7) |
            (0x3 << 5) |
            line_offset;

        /* Structure B */
        int idx = OP47_STRUCT_B_OFFSET + 45 * j;

        /* 2x Run in codes */
        buf[idx] = 0x55;
        buf[idx+1] = 0x55;

        /* Framing code, MRAG and the data */
        for (int i = 0; i < 43; i++)
            buf[idx + 2 + i] = REVERSE(pic_data[DVBVBI_UNIT_HEADER_SIZE + 1 /* line/field */ + i]);
    }

    int idx = OP47_STRUCT_B_OFFSET + 45 * packets;
    /* Footer ID */
    buf[idx++] = 0x74;

    /* Sequence counter, MSB and LSB */
    const uint16_t sequence_counter = (*ctr)++;
    buf[idx++] = (sequence_counter >> 8) & 0xff;
    buf[idx++] = (sequence_counter     ) & 0xff;

    /* Write UDW length (includes checksum so do it before) */
    buf[ANC_START_LEN+2] = idx + 1 - ANC_START_LEN;

    /* SDP Checksum */
    uint8_t checksum = 0;
    for (int j = ANC_START_LEN; j < idx; j++)
        checksum += buf[j];
    buf[idx++] = checksum ? 256 - checksum : 0;

    buf[DC_POS] = idx - ANC_START_LEN;

    sdi_calc_parity_checksum(buf);
}
