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

v210_uyvy_mask1: times 4 db 0xff, 0x03, 0xf0, 0x3f, 0x00, 0xfc, 0x0f, 0xc0
v210_uyvy_mask2: times 4 db 0xf0, 0x3f, 0x00, 0xfc, 0x0f, 0xc0, 0xff, 0x03

v210_uyvy_chroma_shuf1: times 2 db  0, 1,-1,-1, 2, 3,-1,-1, 5, 6,-1,-1, 8, 9,-1,-1
v210_uyvy_luma_shuf1:   times 2 db -1,-1, 1, 2,-1,-1, 4, 5,-1,-1, 6, 7,-1,-1, 9,10
v210_uyvy_chroma_shuf2: times 2 db  0, 1,-1,-1, 3, 4,-1,-1, 6, 7,-1,-1, 8, 9,-1,-1
v210_uyvy_luma_shuf2:   times 2 db -1,-1, 2, 3,-1,-1, 4, 5,-1,-1, 7, 8,-1,-1,10,11
v210_uyvy_chroma_shuf3: times 2 db  5, 6,-1,-1, 8, 9,-1,-1,10,11,-1,-1,13,14,-1,-1
v210_uyvy_luma_shuf3:   times 2 db -1,-1, 6, 7,-1,-1, 9,10,-1,-1,12,13,-1,-1,14,15

v210_uyvy_mult1: times 2 dw 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000
v210_uyvy_mult2: times 2 dw 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff
v210_uyvy_mult3: times 2 dw 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800

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
    movu    [dstq+sizeq], m0

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
%if notcpuflag(avx2)
    mova      m0, [yq+2*widthq]
    mova      m1, [yq+2*widthq+mmsize]
    mova      m2, [uq+widthq]
    mova      m3, [vq+widthq]
%else
    mova        xm0, [yq+2*widthq]
    mova        xm1, [yq+2*widthq+16]
    mova        xm2, [uq+widthq]
    mova        xm3, [vq+widthq]
    vinserti128  m0, m0, [yq + 2*widthq + 32], 1
    vinserti128  m1, m1, [yq + 2*widthq + 48], 1
    vinserti128  m2, m2, [uq +   widthq + 16], 1
    vinserti128  m3, m3, [vq +   widthq + 16], 1
%endif

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

%if notcpuflag(avx2)
    mova      [dstq+8*widthq+0*mmsize], m0
    mova      [dstq+8*widthq+1*mmsize], m1
    mova      [dstq+8*widthq+2*mmsize], m2
    mova      [dstq+8*widthq+3*mmsize], m3
    mova      [dstq+8*widthq+4*mmsize], m4
    mova      [dstq+8*widthq+5*mmsize], m5
    mova      [dstq+8*widthq+6*mmsize], m6
    mova      [dstq+8*widthq+7*mmsize], m7
%else
    mova         [dstq + 8*widthq +  0*16], xm0
    mova         [dstq + 8*widthq +  1*16], xm1
    mova         [dstq + 8*widthq +  2*16], xm2
    mova         [dstq + 8*widthq +  3*16], xm3
    mova         [dstq + 8*widthq +  4*16], xm4
    mova         [dstq + 8*widthq +  5*16], xm5
    mova         [dstq + 8*widthq +  6*16], xm6
    mova         [dstq + 8*widthq +  7*16], xm7
    vextracti128 [dstq + 8*widthq +  8*16],  m0, 1
    vextracti128 [dstq + 8*widthq +  9*16],  m1, 1
    vextracti128 [dstq + 8*widthq + 10*16],  m2, 1
    vextracti128 [dstq + 8*widthq + 11*16],  m3, 1
    vextracti128 [dstq + 8*widthq + 12*16],  m4, 1
    vextracti128 [dstq + 8*widthq + 13*16],  m5, 1
    vextracti128 [dstq + 8*widthq + 14*16],  m6, 1
    vextracti128 [dstq + 8*widthq + 15*16],  m7, 1
%endif

    add       widthq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM avx
planar_to_uyvy_8
INIT_YMM avx2
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
%if notcpuflag(avx2)
    mova      m0, [yq+2*widthq]
    mova      m1, [yq+2*widthq+mmsize]
    mova      m2, [uq+widthq]
    mova      m3, [vq+widthq]
%else
    mova        xm0, [yq+2*widthq]
    mova        xm1, [yq+2*widthq+16]
    mova        xm2, [uq+widthq]
    mova        xm3, [vq+widthq]
    vinserti128  m0, m0, [yq + 2*widthq + 32], 1
    vinserti128  m1, m1, [yq + 2*widthq + 48], 1
    vinserti128  m2, m2, [uq +   widthq + 16], 1
    vinserti128  m3, m3, [vq +   widthq + 16], 1
%endif

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

%if notcpuflag(avx2)
    mova      [dstq+4*widthq+0*mmsize], m7
    mova      [dstq+4*widthq+1*mmsize], m4
    mova      [dstq+4*widthq+2*mmsize], m6
    mova      [dstq+4*widthq+3*mmsize], m5
%else
    mova         [dstq + 4*widthq +   0], xm7
    mova         [dstq + 4*widthq +  16], xm4
    mova         [dstq + 4*widthq +  32], xm6
    mova         [dstq + 4*widthq +  48], xm5
    vextracti128 [dstq + 4*widthq +  64], m7, 1
    vextracti128 [dstq + 4*widthq +  80], m4, 1
    vextracti128 [dstq + 4*widthq +  96], m6, 1
    vextracti128 [dstq + 4*widthq + 112], m5, 1
%endif

    add       widthq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM sse2
planar_to_uyvy_10
INIT_XMM avx
planar_to_uyvy_10
INIT_YMM avx2
planar_to_uyvy_10

%macro v210_uyvy_unpack 1

; v210_uyvy_unpack(const uint32_t *src, uint16_t *uyvy, int64_t width)
cglobal v210_uyvy_unpack_%1, 3, 3, 8+7*ARCH_X86_64
    shl    r2, 2
    add    r1, r2
    neg    r2

    mova  m4, [v210_uyvy_mask1]
    mova  m5, [v210_uyvy_mask2]
    mova  m6, [v210_uyvy_luma_shuf1]
    mova  m7, [v210_uyvy_chroma_shuf1]
%if ARCH_X86_64
    mova  m8, [v210_uyvy_luma_shuf2]
    mova  m9, [v210_uyvy_chroma_shuf2]
    mova m10, [v210_uyvy_luma_shuf3]
    mova m11, [v210_uyvy_chroma_shuf3]
    mova m12, [v210_uyvy_mult1]
    mova m13, [v210_uyvy_mult2]
    mova m14, [v210_uyvy_mult3]
%else
    %define  m8 [v210_uyvy_luma_shuf2]
    %define  m9 [v210_uyvy_chroma_shuf2]
    %define m10 [v210_uyvy_luma_shuf3]
    %define m11 [v210_uyvy_chroma_shuf3]
    %define m12 [v210_uyvy_mult1]
    %define m13 [v210_uyvy_mult2]
    %define m14 [v210_uyvy_mult3]
%endif ; ARCH_X86_64

.loop:
%ifidn %1, unaligned
    movu   xm0, [r0]
    movu   xm2, [r0+16]
%else
    mova   xm0, [r0]
    mova   xm2, [r0+16]
%endif
%if cpuflag(avx2)
    vinserti128 m0, m0, [r0+32], 1
    vinserti128 m2, m2, [r0+48], 1
%endif
    palignr  m1, m2, m0, 10

    pandn    m3, m4, m0
    pand     m0, m4
    pshufb   m3, m6
    pshufb   m0, m7
    por      m0, m3
    pmulhrsw m0, m12
    mova [r1+r2], xm0

%if cpuflag(avx2)
    vextracti128 [r1 + r2 + 3*16], m0, 1
%endif

    pandn    m3, m5, m1
    pand     m1, m5
    pshufb   m3, m8
    pshufb   m1, m9
    por      m1, m3
    pmulhrsw m1, m13
    mova [r1+r2+16], xm1

%if cpuflag(avx2)
    vextracti128 [r1 + r2 + 4*16], m1, 1
%endif

    pandn    m3, m4, m2
    pand     m2, m4
    pshufb   m3, m10
    pshufb   m2, m11
    por      m2, m3
    pmulhrsw m2, m14
    mova [r1+r2+2*16], xm2

%if cpuflag(avx2)
    vextracti128 [r1 + r2 + 5*16], m2, 1
%endif

    add r0, 2*mmsize
    add r2, 3*mmsize
    jl  .loop

    REP_RET
%endmacro

INIT_XMM ssse3
v210_uyvy_unpack unaligned
INIT_XMM avx
v210_uyvy_unpack unaligned
INIT_YMM avx2
v210_uyvy_unpack unaligned

INIT_XMM ssse3
v210_uyvy_unpack aligned
INIT_XMM avx
v210_uyvy_unpack aligned
INIT_YMM avx2
v210_uyvy_unpack aligned

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
