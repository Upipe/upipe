;******************************************************************************
;* SDI SIMD pack
;* Copyright (c) 2016 Kieran Kunhya <kierank@obe.tv>
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with FFmpeg; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86util.asm"

SECTION_RODATA 32

sdi_comp_mask_10:  times 2 db 0xff, 0xc0, 0xf,  0xfc, 0x0,  0xff, 0xc0, 0xf,  0xfc, 0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0

sdi_chroma_shuf_10:  times 2 db  1,  0, -1, -1,  3,  2, -1, -1,  6,  5, -1, -1,  8,  7, -1, -1
sdi_luma_shuf_10:    times 2 db -1, -1,  2,  1, -1, -1,  4,  3, -1, -1,  7,  6, -1, -1,  9,  8

sdi_chroma_mult_10:  times 4 dw 0x400, 0x0, 0x4000, 0x0
sdi_luma_mult_10:    times 4 dw 0x0, 0x800, 0x0, 0x7fff

uyvy_planar_shuf_10: times 2 db 0, 1, 8, 9, 4, 5,12,13, 2, 3, 6, 7,10,11,14,15

uyvy_planar_shuf_8: times 2 db 2, 6, 10, 14, -1, -1, -1, -1, -1, -1, -1, -1, 0, 8, 4, 12

v210_enc_min_10: times 16 dw 0x0004
v210_enc_max_10: times 16 dw 0x3fb

v210_enc_uyvy_chroma_shift_10: times 2 dw 1, 0, 16, 0, 4, 0, 0, 0
v210_enc_uyvy_chroma_shuff_10: times 2 db 0, 1, 4, 5, -1, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1

v210_enc_uyvy_luma_shift_10: times 2 dw 0, 4, 0, 1, 0, 16, 0, 0
v210_enc_uyvy_luma_shuft_10: times 2 db -1, 2, 3, -1, 6, 7, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1

uyvy_to_v210_store_mask: times 2 dq -1, 0

SECTION .text

%macro sdi_unpack_10 0

; sdi_unpack_10(const uint8_t *src, uint16_t *y, int64_t size)
cglobal sdi_unpack_10, 3, 3, 7, src, y, size
    add      srcq, sizeq
    neg      sizeq

    mova     m2, [sdi_comp_mask_10]
    mova     m3, [sdi_chroma_shuf_10]
    mova     m4, [sdi_luma_shuf_10]
    mova     m5, [sdi_chroma_mult_10]
    mova     m6, [sdi_luma_mult_10]

.loop:
    movu     xm0, [srcq+sizeq]
%if cpuflag(avx2)
    vinserti128 m0, m0, [srcq+sizeq+10], 1
%endif

    pandn    m1, m2, m0
    pand     m0, m2

    pshufb   m0, m3
    pshufb   m1, m4

    pmulhuw  m0, m5
    pmulhrsw m1, m6

    por      m0, m1

    mova     [yq], m0

    add      yq,    mmsize
    add      sizeq, (mmsize*5)/8
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
sdi_unpack_10
INIT_YMM avx2
sdi_unpack_10

%macro uyvy_to_planar_8 0

; uyvy_to_planar_8(uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, const int64_t width)
cglobal uyvy_to_planar_8, 5, 5, 8, y, u, v, l, width
    lea        lq, [lq+4*widthq]
    add        yq, widthq
    shr        widthq, 1
    add        uq, widthq
    add        vq, widthq
    neg        widthq

    mova       m6, [uyvy_planar_shuf_8]

.loop:
%if notcpuflag(avx2)
    mova       m0, [lq+8*widthq+0*mmsize]
    mova       m1, [lq+8*widthq+1*mmsize]
    mova       m2, [lq+8*widthq+2*mmsize]
    mova       m3, [lq+8*widthq+3*mmsize]
%else
    mova             xm0, [lq+8*widthq+  0]
    mova             xm1, [lq+8*widthq+ 16]
    mova             xm2, [lq+8*widthq+ 32]
    mova             xm3, [lq+8*widthq+ 48]
    vinserti128  m0,  m0, [lq+8*widthq+ 64], 1
    vinserti128  m1,  m1, [lq+8*widthq+ 80], 1
    vinserti128  m2,  m2, [lq+8*widthq+ 96], 1
    vinserti128  m3,  m3, [lq+8*widthq+112], 1
%endif

    psrlw      m0, 2
    psrlw      m1, 2
    psrlw      m2, 2
    psrlw      m3, 2

    pshufb     m0, m6
    pshufb     m1, m6
    pshufb     m2, m6
    pshufb     m3, m6

    ; all registers have yyyy0000uuvv

    punpckldq  m4, m0, m1 ; yyyy from m0 and yyyy from m1 -> yyyyyyyy
    punpckldq  m5, m2, m3
    punpcklqdq m4, m5

    punpckhwd  m0, m1 ; uuvv from m0 and uuvv from m1 -> uuuuvvvv
    punpckhwd  m2, m3
    punpckhdq  m0, m2 ; eight u's and eight v's

    mova      [yq + 2*widthq], m4
%if notcpuflag(avx2)
    movq      [uq + 1*widthq], m0 ; put half of m0 (eight u's)
    movhps    [vq + 1*widthq], m0 ; put the other half of m0 (eight v's)
%else
    vpermq m0, m0, q3120
    movu [uq + widthq], xm0
    vextracti128 [vq + widthq], m0, 1
%endif

    add       widthq, mmsize/2
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
uyvy_to_planar_8
INIT_XMM avx
uyvy_to_planar_8
INIT_YMM avx2
uyvy_to_planar_8

%macro uyvy_to_planar_10 0

; uyvy_to_planar_10(uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, const int64_t width)
cglobal uyvy_to_planar_10, 5, 5, 6, y, u, v, l, width
    lea       lq, [lq+4*widthq]
    lea       yq, [yq+2*widthq]
    add       uq, widthq
    add       vq, widthq
    neg       widthq

    mova      m5, [uyvy_planar_shuf_10]

.loop:
%if notcpuflag(avx2)
    mova      m0, [lq+4*widthq+0*mmsize]
    mova      m1, [lq+4*widthq+1*mmsize]
    mova      m2, [lq+4*widthq+2*mmsize]
    mova      m3, [lq+4*widthq+3*mmsize]
%else
    mova             xm0, [lq+4*widthq+  0]
    mova             xm1, [lq+4*widthq+ 16]
    vinserti128  m0,  m0, [lq+4*widthq+ 32], 1
    vinserti128  m1,  m1, [lq+4*widthq+ 48], 1
    mova             xm2, [lq+4*widthq+ 64]
    mova             xm3, [lq+4*widthq+ 80]
    vinserti128  m2,  m2, [lq+4*widthq+ 96], 1
    vinserti128  m3,  m3, [lq+4*widthq+112], 1
%endif

    pshufb     m0, m5
    pshufb     m1, m5
    pshufb     m2, m5
    pshufb     m3, m5
    punpckldq  m4, m0, m1
    punpckhqdq m0, m1
    punpckldq  m1, m2, m3
    punpckhqdq m2, m3

    mova       [yq+2*widthq], m0
    mova       [yq+2*widthq+mmsize], m2

    punpckhqdq m0, m4, m1
    punpcklqdq m4, m1
    SWAP       m1, m0

%if cpuflag(avx2)
    vpermq m4, m4, q3120
    vpermq m1, m1, q3120
%endif

    mova       [uq+widthq], m4
    mova       [vq+widthq], m1

    add       widthq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
uyvy_to_planar_10
INIT_XMM avx
uyvy_to_planar_10
INIT_YMM avx2
uyvy_to_planar_10

%macro uyvy_to_v210 0

; uyvy_to_v210(const uint16_t *y, uint8_t *dst, int64_t width)
cglobal uyvy_to_v210, 3, 6, 6+cpuflag(avx2), y, dst, width
    shl     widthq, 2
    add     yq, widthq
    neg     widthq

    mova    m4, [v210_enc_min_10]
    mova    m5, [v210_enc_max_10]

%if cpuflag(avx2)
    mova m6, [uyvy_to_v210_store_mask]
%endif

.loop:
    movu    xm0, [yq+widthq+ 0]
    movu    xm1, [yq+widthq+12]
%if cpuflag(avx2)
    vinserti128 m0, m0, [yq + widthq + 24], 1
    vinserti128 m1, m1, [yq + widthq + 36], 1
%endif

    CLIPW   m0, m4, m5
    CLIPW   m1, m4, m5

    pmullw  m2, m0, [v210_enc_uyvy_luma_shift_10]
    pmullw  m3, m1, [v210_enc_uyvy_luma_shift_10]

    pmullw  m0, [v210_enc_uyvy_chroma_shift_10]
    pshufb  m0, [v210_enc_uyvy_chroma_shuff_10]

    pmullw  m1, [v210_enc_uyvy_chroma_shift_10]
    pshufb  m1, [v210_enc_uyvy_chroma_shuff_10]

    pshufb  m2, [v210_enc_uyvy_luma_shuft_10]
    pshufb  m3, [v210_enc_uyvy_luma_shuft_10]

    por     m0, m2
    por     m1, m3

%if notcpuflag(avx2)
    movu    [dstq+0], m0
    movq    [dstq+8], m1
%else
    pslldq m1, 8
    por m1, m0
    mova [dstq], m1
%endif

    add     dstq, mmsize
    add     widthq, (mmsize*3)/2
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
uyvy_to_v210
INIT_XMM avx
uyvy_to_v210
INIT_YMM avx2
uyvy_to_v210
