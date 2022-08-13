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

sdi_shuf_10:         times 2 db 1, 0, 2, 1, 3, 2, 4, 3, 6, 5, 7, 6, 8, 7, 9, 8

sdi_mask_10:         times 2 db 0xc0, 0xff, 0xf0, 0x3f, 0xfc, 0x0f, 0xff, 0x03, 0xc0, 0xff, 0xf0, 0x3f, 0xfc, 0x0f, 0xff, 0x03

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

sdi_vanc_shuf1: db 0,1,4,5,8,9,12,13, 2,3,6,7,10,11,14,15

SECTION .text

INIT_XMM ssse3

cglobal sdi_vanc_deinterleave, 3, 5, 3, vanc_buf_, vanc_stride_, src_
    mov            r3, vanc_buf_q
    sar vanc_stride_q, 1
    add            r3, vanc_stride_q
    mov            r4, r3

    mova m0, [sdi_vanc_shuf1]
    .loop:
        movu             m1, [src_q]
        pshufb           m1, m0
        movq           [r3], m1
        MOVHL            m2, m1
        movq   [vanc_buf_q], m2
        add           src_q, mmsize
        add              r3, mmsize/2
        add      vanc_buf_q, mmsize/2
        cmp      vanc_buf_q, r4
    jl .loop
RET

%macro sdi_to_uyvy 0

; sdi_to_uyvy(const uint8_t *src, uint16_t *y, int64_t size)
cglobal sdi_to_uyvy, 3, 3, 7, src, y, pixels
    lea yq,     [yq + 4*pixelsq]
    neg pixelsq

    mova     m2, [sdi_shuf_10]
    mova     m3, [sdi_mask_10]
    mova     m4, [sdi_chroma_mult_10]
    mova     m5, [sdi_luma_mult_10]

.loop:
    movu     xm0, [srcq]
%if cpuflag(avx2)
    vinserti128 m0, m0, [srcq + 10], 1
%endif

    pshufb m0, m2
    pand   m0, m3

    pmulhuw  m1, m0, m4
    pmulhrsw m0, m5

    por      m0, m1

    movu     [yq + 4*pixelsq], m0

    add    srcq, (mmsize*5)/8
    add pixelsq, mmsize/4
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
sdi_to_uyvy
INIT_YMM avx2
sdi_to_uyvy

%macro uyvy_to_planar_8 0

; uyvy_to_planar_8(uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, const int64_t width)
cglobal uyvy_to_planar_8, 5, 5, 7, y, u, v, l, pixels
    lea        lq, [lq+4*pixelsq]
    add        yq, pixelsq
    shr        pixelsq, 1
    add        uq, pixelsq
    add        vq, pixelsq
    neg        pixelsq

    mova       m6, [uyvy_planar_shuf_8]

.loop:
%if notcpuflag(avx2)
    movu       m0, [lq+8*pixelsq+0*mmsize]
    movu       m1, [lq+8*pixelsq+1*mmsize]
    movu       m2, [lq+8*pixelsq+2*mmsize]
    movu       m3, [lq+8*pixelsq+3*mmsize]
%else
    movu             xm0, [lq+8*pixelsq+  0]
    movu             xm1, [lq+8*pixelsq+ 16]
    movu             xm2, [lq+8*pixelsq+ 32]
    movu             xm3, [lq+8*pixelsq+ 48]
    vinserti128  m0,  m0, [lq+8*pixelsq+ 64], 1
    vinserti128  m1,  m1, [lq+8*pixelsq+ 80], 1
    vinserti128  m2,  m2, [lq+8*pixelsq+ 96], 1
    vinserti128  m3,  m3, [lq+8*pixelsq+112], 1
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

    movu      [yq + 2*pixelsq], m4
%if notcpuflag(avx2)
    movq      [uq + 1*pixelsq], m0 ; put half of m0 (eight u's)
    movhps    [vq + 1*pixelsq], m0 ; put the other half of m0 (eight v's)
%else
    vpermq m0, m0, q3120
    movu [uq + pixelsq], xm0
    vextracti128 [vq + pixelsq], m0, 1
%endif

    add       pixelsq, mmsize/2
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
cglobal uyvy_to_planar_10, 5, 5, 6, y, u, v, l, pixels
    lea       lq, [lq+4*pixelsq]
    lea       yq, [yq+2*pixelsq]
    add       uq, pixelsq
    add       vq, pixelsq
    neg       pixelsq

    mova      m5, [uyvy_planar_shuf_10]

.loop:
%if notcpuflag(avx2)
    movu      m0, [lq+4*pixelsq+0*mmsize]
    movu      m1, [lq+4*pixelsq+1*mmsize]
    movu      m2, [lq+4*pixelsq+2*mmsize]
    movu      m3, [lq+4*pixelsq+3*mmsize]
%else
    movu             xm0, [lq+4*pixelsq+  0]
    movu             xm1, [lq+4*pixelsq+ 16]
    vinserti128  m0,  m0, [lq+4*pixelsq+ 32], 1
    vinserti128  m1,  m1, [lq+4*pixelsq+ 48], 1
    movu             xm2, [lq+4*pixelsq+ 64]
    movu             xm3, [lq+4*pixelsq+ 80]
    vinserti128  m2,  m2, [lq+4*pixelsq+ 96], 1
    vinserti128  m3,  m3, [lq+4*pixelsq+112], 1
%endif

    pshufb     m0, m5
    pshufb     m1, m5
    pshufb     m2, m5
    pshufb     m3, m5
    punpckldq  m4, m0, m1
    punpckhqdq m0, m1
    punpckldq  m1, m2, m3
    punpckhqdq m2, m3

    movu       [yq+2*pixelsq], m0
    movu       [yq+2*pixelsq+mmsize], m2

    punpckhqdq m0, m4, m1
    punpcklqdq m4, m1
    SWAP       m1, m0

%if cpuflag(avx2)
    vpermq m4, m4, q3120
    vpermq m1, m1, q3120
%endif

    movu       [uq+pixelsq], m4
    movu       [vq+pixelsq], m1

    add       pixelsq, mmsize
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
cglobal uyvy_to_v210, 3, 6, 6+cpuflag(avx2), y, dst, pixels
    shl     pixelsq, 2
    add     yq, pixelsq
    neg     pixelsq

    mova    m4, [v210_enc_min_10]
    mova    m5, [v210_enc_max_10]

%if cpuflag(avx2)
    mova m6, [uyvy_to_v210_store_mask]
%endif

.loop:
    movu    xm0, [yq+pixelsq+ 0]
    movu    xm1, [yq+pixelsq+12]
%if cpuflag(avx2)
    vinserti128 m0, m0, [yq + pixelsq + 24], 1
    vinserti128 m1, m1, [yq + pixelsq + 36], 1
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
    movq    [dstq+0], m0
    movq    [dstq+8], m1
%else
    pslldq m1, 8
    por m1, m0
    movu [dstq], m1
%endif

    add     dstq, mmsize
    add     pixelsq, (mmsize*3)/2
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
uyvy_to_v210
INIT_XMM avx
uyvy_to_v210
INIT_YMM avx2
uyvy_to_v210
