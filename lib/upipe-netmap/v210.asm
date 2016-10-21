;******************************************************************************
;* V210 SIMD unpack
;* Copyright (c) 2011 Loren Merritt <lorenm@u.washington.edu>
;* Copyright (c) 2011 Kieran Kunhya <kieran@kunhya.com>
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

SECTION_RODATA

v210_mask: times 4 dd 0x3ff
v210_mult: dw 64,4,64,4,64,4,64,4
v210_luma_shuf: db 8,9,0,1,2,3,12,13,4,5,6,7,-1,-1,-1,-1
v210_chroma_shuf: db 0,1,8,9,6,7,-1,-1,2,3,4,5,12,13,-1,-1

v210_uyvy_mask1: times 2 db 0xff, 0x03, 0xf0, 0x3f, 0x00, 0xfc, 0x0f, 0xc0
v210_uyvy_mask2: times 2 db 0xf0, 0x3f, 0x00, 0xfc, 0x0f, 0xc0, 0xff, 0x03

v210_uyvy_chroma_shuf1: db  0, 1,-1,-1, 2, 3,-1,-1, 5, 6,-1,-1, 8, 9,-1,-1
v210_uyvy_luma_shuf1:   db -1,-1, 1, 2,-1,-1, 4, 5,-1,-1, 6, 7,-1,-1, 9,10
v210_uyvy_chroma_shuf2: db  0, 1,-1,-1, 3, 4,-1,-1, 6, 7,-1,-1, 8, 9,-1,-1
v210_uyvy_luma_shuf2:   db -1,-1, 2, 3,-1,-1, 4, 5,-1,-1, 7, 8,-1,-1,10,11
v210_uyvy_chroma_shuf3: db  5, 6,-1,-1, 8, 9,-1,-1,10,11,-1,-1,13,14,-1,-1
v210_uyvy_luma_shuf3:   db -1,-1, 6, 7,-1,-1, 9,10,-1,-1,12,13,-1,-1,14,15

v210_uyvy_mult1: dw 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000
v210_uyvy_mult2: dw 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff
v210_uyvy_mult3: dw 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800

v210_sdi_shuf_easy:       db -1, -1, -1, 5, 4, -1, 7, 6, -1, -1, 11, 10, 14, 13, -1, -1
v210_sdi_mult_easy:       dw 1, 1, 1, 1, 1, 4, 1, 1
v210_sdi_mask_easy:       db 0x00, 0x00, 0x00, 0x03, 0xff, 0x00, 0x3f, 0xf0, 0x00, 0x00, 0xff, 0xc0, 0x0f, 0xfc, 0x00, 0x00

v210_sdi_shuf_hard_1:     db 1, 2, 5, 6, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1
v210_sdi_mult_hard_1:     dw 4, 16, 4, 16, 1, 1, 1, 1
v210_sdi_mask_hard_1:     db 0xf0, 0x3f, 0xc0, 0xff, 0xfc, 0x0f, 0xf0, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
v210_sdi_shuf_hard_end_1: db -1, 1, 0, -1, -1, 3, 2, 5, 4, -1, -1, 7, 6, -1, -1, -1

v210_sdi_shuf_hard_2:     db 0, 1, 2, 3, 9, 10, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1
v210_sdi_rshift_hard_2:   dw 0x2000, 0x2000, 0x2000, 0x800, 0x7fff, 0x7fff, 0x7fff, 0x7fff
v210_sdi_mask_hard_2:     db 0xff, 0xc0, 0xfc, 0x0f, 0xff, 0x03, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
v210_sdi_shuf_hard_end_2: db 0, 1, 3, 2, -1, -1, -1, -1, 5, 4, -1, -1, -1, 7, 6, -1

SECTION .text

%macro v210_planar_unpack 1

; v210_planar_unpack(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int width)
cglobal v210_planar_unpack_%1, 5, 5, 7
    movsxdifnidn r4, r4d
    lea    r1, [r1+2*r4]
    add    r2, r4
    add    r3, r4
    neg    r4

    mova   m3, [v210_mult]
    mova   m4, [v210_mask]
    mova   m5, [v210_luma_shuf]
    mova   m6, [v210_chroma_shuf]
.loop:
%ifidn %1, unaligned
    movu   m0, [r0]
%else
    mova   m0, [r0]
%endif

    pmullw m1, m0, m3
    psrld  m0, 10
    psrlw  m1, 6  ; u0 v0 y1 y2 v1 u2 y4 y5
    pand   m0, m4 ; y0 __ u1 __ y3 __ v2 __

    shufps m2, m1, m0, 0x8d ; y1 y2 y4 y5 y0 __ y3 __
    pshufb m2, m5 ; y0 y1 y2 y3 y4 y5 __ __
    movu   [r1+2*r4], m2

    shufps m1, m0, 0xd8 ; u0 v0 v1 u2 u1 __ v2 __
    pshufb m1, m6 ; u0 u1 u2 __ v0 v1 v2 __
    movq   [r2+r4], m1
    movhps [r3+r4], m1

    add r0, mmsize
    add r4, 6
    jl  .loop

    REP_RET
%endmacro

INIT_XMM ssse3
v210_planar_unpack unaligned

INIT_XMM avx
v210_planar_unpack unaligned

INIT_XMM ssse3
v210_planar_unpack aligned

INIT_XMM avx
v210_planar_unpack aligned

%macro v210_uyvy_unpack 1

; v210_uyvy_unpack(const uint32_t *src, uint16_t *uyvy, int64_t width)
cglobal v210_uyvy_unpack_%1, 3, 3, 15
    shl    r2, 2
    add    r1, r2
    neg    r2

    mova  m4, [v210_uyvy_mask1]
    mova  m5, [v210_uyvy_mask2]
    mova  m6, [v210_uyvy_luma_shuf1]
    mova  m7, [v210_uyvy_chroma_shuf1]
    mova  m8, [v210_uyvy_luma_shuf2]
    mova  m9, [v210_uyvy_chroma_shuf2]
    mova m10, [v210_uyvy_luma_shuf3]
    mova m11, [v210_uyvy_chroma_shuf3]
    mova m12, [v210_uyvy_mult1]
    mova m13, [v210_uyvy_mult2]
    mova m14, [v210_uyvy_mult3]

.loop:
%ifidn %1, unaligned
    movu   m0, [r0]
    movu   m2, [r0+mmsize]
%else
    mova   m0, [r0]
    mova   m2, [r0+mmsize]
%endif
    palignr  m1, m2, m0, 10

    pandn    m3, m4, m0
    pand     m0, m4
    pshufb   m3, m6
    pshufb   m0, m7
    por      m0, m3
    pmulhrsw m0, m12
    mova [r1+r2], m0

    pandn    m3, m5, m1
    pand     m1, m5
    pshufb   m3, m8
    pshufb   m1, m9
    por      m1, m3
    pmulhrsw m1, m13
    mova [r1+r2+mmsize], m1

    pandn    m3, m4, m2
    pand     m2, m4
    pshufb   m3, m10
    pshufb   m2, m11
    por      m2, m3
    pmulhrsw m2, m14
    mova [r1+r2+2*mmsize], m2

    add r0, 2*mmsize
    add r2, 3*mmsize
    jl  .loop

    REP_RET
%endmacro

INIT_XMM ssse3
v210_uyvy_unpack unaligned

INIT_XMM avx
v210_uyvy_unpack unaligned

INIT_XMM ssse3
v210_uyvy_unpack aligned

INIT_XMM avx
v210_uyvy_unpack aligned

%macro v210_sdi_unpack_aligned 0

; v210_sdi_unpack_aligned(const uint32_t *src, uint8_t *dst, int64_t width)
cglobal v210_sdi_unpack_aligned, 3, 3, 14, src, dst, width
    lea      widthq, [3*widthq]
    add      srcq, widthq
    neg      widthq

    mova     m3,  [v210_sdi_shuf_easy]
    mova     m4,  [v210_sdi_mult_easy]
    mova     m5,  [v210_sdi_mask_easy]
    mova     m6,  [v210_sdi_shuf_hard_1]
    mova     m7,  [v210_sdi_mult_hard_1]
    mova     m8,  [v210_sdi_mask_hard_1]
    mova     m9,  [v210_sdi_shuf_hard_end_1]
    mova     m10, [v210_sdi_shuf_hard_2]
    mova     m11, [v210_sdi_rshift_hard_2]
    mova     m12, [v210_sdi_mask_hard_2]
    mova     m13, [v210_sdi_shuf_hard_end_2]

.loop:
    mova     m0, [srcq+widthq]

    pshufb   m1, m0, m3
    pshufb   m2, m0, m6
    pshufb   m0, m10

    pmullw   m1, m4
    pmullw   m2, m7
    pmulhrsw m0, m11

    pand     m1, m5        ; Y2, Y3, U3, V3
    pand     m2, m8
    pand     m0, m12

    pshufb   m2, m9        ; Y1, U2, V2, Y5
    pshufb   m0, m13       ; U1, V1, Y4, Y6

    por      m1, m2
    por      m1, m0

    movu     [dstq], m1

    add      dstq, 15
    add      widthq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM avx
v210_sdi_unpack_aligned
