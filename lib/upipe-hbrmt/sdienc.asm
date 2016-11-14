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

uyvy_enc_min_10: times 16 dw 0x0004
uyvy_enc_max_10: times 16 dw 0x3fb

uyvy_enc_min_8: times 16 dw 0x0101
uyvy_enc_max_8: times 16 dw 0xFEFE

sdi_blank: times 4 dw 0x200, 0x40, 0x200, 0x40, 0x200, 0x40, 0x200, 0x40

uyvy_planar_shuf_10: times 2 db 0, 1, 8, 9, 4, 5,12,13, 2, 3, 6, 7,10,11,14,15

uyvy_planar_shuf_8: times 2 db 2, 6, 10, 14, -1, -1, -1, -1, -1, -1, -1, -1, 0, 8, 4, 12

sdi_enc_mult_10: times 4 dw 64, 16, 4, 1
sdi_chroma_shuf_10: times 2 db 1, 0, 5, 4, -1, 9, 8, 13, 12, -1, -1, -1, -1, -1, -1, -1
sdi_luma_shuf_10: times 2 db -1, 3, 2, 7, 6, -1, 11, 10, 15, 14, -1, -1, -1, -1, -1, -1

planar_8_y_shuf: db 0, -1, 1, -1, 2, -1, 3, -1, 4, -1, 5, -1, 6, -1, -1, -1
planar_8_y_mult: dw 0x40, 0x4, 0x40, 0x4, 0x40, 0x4, 0x40, 0x0
planar_8_y_shuf_after: db -1, 1, 0, 3, 2, -1, 5, 4, 7, 6, -1, 9, 8, 11, 10, -1

planar_8_u_shuf: db 0, -1, -1, -1, -1, 1, -1, -1, -1, -1, 2, -1, -1, -1, -1, -1

planar_8_v_shuf: db 0, -1, 2, -1, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
planar_8_v_shuf_after: db -1, -1, 1, 0, -1, -1, -1, 3, 2, -1, -1, -1, 5, 4, -1, -1

planar_10_y_shift:  dw 0x10, 0x1, 0x10, 0x1, 0x10, 0x1, 0x10, 0x1
planar_10_uv_shift: dw 0x40, 0x40, 0x40, 0x40, 0x4, 0x4, 0x4, 0x4

planar_10_y_shuf:  db -1, 1, 0, 3, 2, -1, 5, 4, 7, 6, -1, 9, 8, 11, 10, -1
planar_10_uv_shuf: db 1, 0, 9, 8, -1, 3, 2, 11, 10, -1, 5, 4, 13, 12, -1, -1

pb_0: times 32 db 0

SECTION .text

%macro sdi_pack_10 0

; sdi_pack_10(uint8_t *dst, const uint8_t *y, int64_t size)
cglobal sdi_pack_10, 3, 4, 3, dst, y, size
    add     sizeq, sizeq
    add     yq, sizeq
    neg     sizeq
    mova    m2, [sdi_enc_mult_10]

.loop:
    pmullw  m0, m2, [yq+sizeq]
    pshufb  m1, m0, [sdi_chroma_shuf_10]
    pshufb  m0, [sdi_luma_shuf_10]
    por     m0, m1

    movu    [dstq], xm0
%if cpuflag(avx2)
    vextracti128 [dstq+10], m0, 1
%endif

    add     dstq, (mmsize*5)/8
    add     sizeq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
sdi_pack_10
INIT_XMM avx
sdi_pack_10
INIT_YMM avx2
sdi_pack_10

%macro sdi_blank 0

; sdi_blank(uint16_t *dst, int64_t size)
cglobal sdi_blank, 2, 2, 1, dst, size
    shl     sizeq, 2
    add     dstq, sizeq
    neg     sizeq

    mova    m0, [sdi_blank]

.loop:
    mova    [dstq+sizeq], m0

    add     sizeq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM avx
sdi_blank

%macro planar_to_uyvy_8 0

; planar_to_uyvy_8(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, const int64_t width)
cglobal planar_to_uyvy_8, 5, 5, 8+3*ARCH_X86_64, dst, y, u, v, width
    shr       widthq, 1
    lea       yq, [yq+2*widthq]
    lea       dstq, [dstq+8*widthq]
    add       uq, widthq
    add       vq, widthq
    neg       widthq

%if ARCH_X86_64
    pxor      m10, m10

    mova      m8, [uyvy_enc_min_8]
    mova      m9, [uyvy_enc_max_8]
%else
    %define m8  [uyvy_enc_min_8]
    %define m9  [uyvy_enc_max_8]
    %define m10 [pb_0]
%endif ; ARCH_X86_64

.loop:
    mova      m0, [yq+2*widthq]
    mova      m1, [yq+2*widthq+mmsize]
    mova      m2, [uq+widthq]
    mova      m3, [vq+widthq]

    CLIPUB    m0, m8, m9
    CLIPUB    m1, m8, m9
    CLIPUB    m2, m8, m9
    CLIPUB    m3, m8, m9

    punpckhbw m5, m2, m3
    punpcklbw m4, m2, m3
    punpckhbw m2, m4, m0
    punpcklbw m0, m4, m0
    punpckhbw m6, m5, m1
    punpcklbw m4, m5, m1

    punpckhbw m1, m0, m10
    psllw     m1, 2
    punpcklbw m0, m10
    psllw     m0, 2
    punpckhbw m3, m2, m10
    psllw     m3, 2
    punpcklbw m2, m10
    psllw     m2, 2
    punpckhbw m5, m4, m10
    psllw     m5, 2
    punpcklbw m4, m10
    psllw     m4, 2
    punpckhbw m7, m6, m10
    psllw     m7, 2
    punpcklbw m6, m10
    psllw     m6, 2

    mova      [dstq+8*widthq+0*mmsize], m0
    mova      [dstq+8*widthq+1*mmsize], m1
    mova      [dstq+8*widthq+2*mmsize], m2
    mova      [dstq+8*widthq+3*mmsize], m3
    mova      [dstq+8*widthq+4*mmsize], m4
    mova      [dstq+8*widthq+5*mmsize], m5
    mova      [dstq+8*widthq+6*mmsize], m6
    mova      [dstq+8*widthq+7*mmsize], m7

    add       widthq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM avx
planar_to_uyvy_8

%macro planar_to_uyvy_10 0

; planar_to_uyvy_10(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const int64_t width)
cglobal planar_to_uyvy_10, 5, 5, 8+2*ARCH_X86_64, dst, y, u, v, width
    lea       yq, [yq+2*widthq]
    lea       dstq, [dstq+4*widthq]
    add       uq, widthq
    add       vq, widthq
    neg       widthq

%if ARCH_X86_64
    mova      m8, [uyvy_enc_min_10]
    mova      m9, [uyvy_enc_max_10]
%else
    %define m8  [uyvy_enc_min_8]
    %define m9  [uyvy_enc_max_8]
%endif ; ARCH_X86_64

.loop:
    mova      m0, [yq+2*widthq]
    mova      m1, [yq+2*widthq+mmsize]
    mova      m2, [uq+widthq]
    mova      m3, [vq+widthq]

    CLIPW     m0, m8, m9
    CLIPW     m1, m8, m9
    CLIPW     m2, m8, m9
    CLIPW     m3, m8, m9

    punpcklwd m4, m2, m3
    punpckhwd m5, m2, m3
    punpcklwd m7, m4, m0
    punpcklwd m6, m5, m1
    punpckhwd m4, m0
    punpckhwd m5, m1

    mova      [dstq+4*widthq+0*mmsize], m7
    mova      [dstq+4*widthq+1*mmsize], m4
    mova      [dstq+4*widthq+2*mmsize], m6
    mova      [dstq+4*widthq+3*mmsize], m5

    add       widthq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM sse2
planar_to_uyvy_10
INIT_XMM avx
planar_to_uyvy_10

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
    mova       m0, [lq+8*widthq+0*mmsize]
    mova       m1, [lq+8*widthq+1*mmsize]
    mova       m2, [lq+8*widthq+2*mmsize]
    mova       m3, [lq+8*widthq+3*mmsize]

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
    movq      [uq + 1*widthq], m0 ; put half of m0 (eight u's)
    movhps    [vq + 1*widthq], m0 ; put the other half of m0 (eight v's)

    add       widthq, 8
    jl .loop

    RET
%endmacro

INIT_XMM avx
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
    mova      m0, [lq+4*widthq+0*mmsize]
    mova      m1, [lq+4*widthq+1*mmsize]
    mova      m2, [lq+4*widthq+2*mmsize]
    mova      m3, [lq+4*widthq+3*mmsize]

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

    mova       [uq+widthq], m4
    mova       [vq+widthq], m1

    add       widthq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM avx
uyvy_to_planar_10

%macro planar_to_sdi_8 0

; planar_to_sdi_8(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width)
cglobal planar_to_sdi_8, 5, 5, 3, y, u, v, l, width, size
    shr    widthq, 1
    lea    yq, [yq + 2*widthq]
    add    uq, widthq
    add    vq, widthq

    neg    widthq

.loop:
    movq   m0, [yq + widthq*2]
    movd   m1, [uq + widthq*1]
    movu   m2, [vq + widthq*1]

    pshufb m0, [planar_8_y_shuf]
    pmullw m0, [planar_8_y_mult]
    pshufb m0, [planar_8_y_shuf_after]

    pshufb m1, [planar_8_u_shuf]

    por    m0, m1

    pshufb m2, [planar_8_v_shuf]
    psllw  m2, 4
    pshufb m2, [planar_8_v_shuf_after]

    por    m0, m2

    movu   [lq], m0

    add    lq, 15
    add    widthq, 3
    jl .loop

    RET
%endmacro

INIT_XMM avx
planar_to_sdi_8

%macro planar_to_sdi_10 0

; planar_to_sdi_10(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width)
cglobal planar_to_sdi_10, 5, 5, 3, y, u, v, l, width, size
    lea    yq, [yq + 2*widthq]
    add    uq, widthq
    add    vq, widthq

    neg    widthq

.loop:
    movu   m0, [yq + widthq*2]
    movq   m1, [uq + widthq*1]
    movhps m1, [vq + widthq*1]

    pmullw m0, [planar_10_y_shift]
    pmullw m1, [planar_10_uv_shift]

    pshufb m0, [planar_10_y_shuf]
    pshufb m1, [planar_10_uv_shuf]

    por    m0, m1

    movu   [lq], m0

    add    lq, 15
    add    widthq, 6
    jl .loop

    RET
%endmacro

INIT_XMM avx
planar_to_sdi_10

%macro planar_10_to_planar_8 0

; planar_10_to_planar_8(const uint16_t *y, const uint8_t *y8, const int64_t width)
cglobal planar_10_to_planar_8, 3, 3, 3, y, y8, width
    lea      yq, [yq + 2*widthq]
    add      y8q, widthq

    neg      widthq

.loop:
    mova     m0, [yq + widthq*2 + 0*mmsize]
    mova     m1, [yq + widthq*2 + 1*mmsize]

    psrlw    m0, 2
    psrlw    m1, 2

    packuswb m0, m1

    mova     [y8q + widthq], m0

    add      widthq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM avx
planar_10_to_planar_8
