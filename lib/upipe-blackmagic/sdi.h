#ifndef SDI_H_
#define SDI_H_
#include <upipe/config.h>

#include <inttypes.h>

#ifdef UPIPE_HAVE_LIBZVBI_H
#include <libzvbi.h>
#endif

#define CC_LINE 9
#define AFD_LINE 11
#define OP47_LINE1 12
#define OP47_LINE2 (OP47_LINE1+563)

#define PAL_FIELD_OFFSET 313

#define ANC_START_LEN   6
#define DC_POS          5
#define OP47_INITIAL_WORDS 4
#define OP47_STRUCT_A_LEN 5
#define OP47_STRUCT_B_OFFSET (ANC_START_LEN+OP47_INITIAL_WORDS+OP47_STRUCT_A_LEN)

#define VANC_WIDTH 1920

void sdi_calc_parity_checksum(uint16_t *buf);

void sdi_clear_vbi(uint8_t *dst, int w);

void upipe_sdi_blank_c(uint16_t *dst, uintptr_t pixels);

void sdi_write_cdp(const uint8_t *src, size_t src_size,
        uint16_t *dst, uint8_t gap, uint16_t *ctr, uint8_t fps);

void sdi_encode_v210_sd(uint32_t *dst, uint8_t *src, int width);
void sdi_encode_v210(uint32_t *dst, uint16_t *src, int width);

#ifdef UPIPE_HAVE_LIBZVBI_H
int sdi_encode_ttx_sd(uint8_t *buf, const uint8_t *pic_data, vbi_sampling_par *sp);
#endif
void sdi_encode_ttx(uint16_t *buf, int packets, const uint8_t **packet, uint16_t *ctr);
#endif
