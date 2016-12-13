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

v210_mask:        times 8 dd 0x3ff
v210_mult:        times 2 dw 64, 4, 64, 4, 64, 4, 64, 4
v210_luma_shuf:   times 2 db 8, 9, 0, 1, 2, 3,12,13, 4, 5, 6, 7,-1,-1,-1,-1
v210_chroma_shuf: times 2 db 0, 1, 8, 9, 6, 7,-1,-1, 2, 3, 4, 5,12,13,-1,-1

;1:  V1 Y4 Y6
;2:  U1 Y2 Y3 U3 V3
;3:  Y1 U2 V2 Y5

v210_sdi_mask_1: times 2 db 0, 0, 0xf0, 0x3f, 0, 0, 0, 0, 0, 0xfc, 0xf, 0, 0, 0, 0xf0, 0x3f
v210_sdi_mask_2: times 2 db 0xff, 0x3, 0, 0, 0xff, 0x3, 0xf0, 0x3f, 0, 0, 0xf0, 0x3f, 0, 0xfc, 0xf, 0
v210_sdi_mask_3: times 2 db 0, 0xfc, 0xf, 0, 0, 0xfc, 0xf, 0, 0xff, 0x3, 0, 0, 0xff, 0x3, 0, 0

v210_sdi_shuf_1: times 2 db 2,3, 9,10, 14,15, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1
v210_sdi_shuf_2: times 2 db 0,1, 4,5, 6,7, 10,11, 13,14, -1,-1,-1,-1,-1,-1
v210_sdi_shuf_3: times 2 db 1,2, 5,6, 8,9, 12,13, -1,-1,-1,-1,-1,-1,-1,-1

v210_sdi_mul_1: times 2 dw 8192, 8192, 2048, 0,0,0,0,0
v210_sdi_mul_2: times 2 dw 64, 1, 1, 4, 1, 0,0,0
v210_sdi_mul_3: times 2 dw 4, 16, 4, 16, 0,0,0,0

v210_sdi_shuf_after_1: times 2 db -1,-1, 1, 0,-1,-1,-1,-1, 3, 2,-1,-1,-1, 5, 4,-1
v210_sdi_shuf_after_2: times 2 db  1, 0,-1, 3, 2,-1, 5, 4,-1,-1, 7, 6, 9, 8,-1,-1
v210_sdi_shuf_after_3: times 2 db -1, 1, 0,-1,-1, 3, 2, 5, 4,-1,-1, 7, 6,-1,-1,-1

v210_to_planar8_mask1:  times 16 db 0, 255
v210_to_planar8_mask2:  times  8 db 0, 0, 255, 0
v210_to_planar8_shuf_y: times  2 db 2, 5, 7,10,13,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
v210_to_planar8_shuf_u: times  2 db 1, 6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
v210_to_planar8_shuf_v: times  2 db 3, 9,14,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1

; Only 16 bytes
planar_8_avx2_shuf1:   db 0, 1, 2, 8, 9,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1

SECTION .text

%macro v210_to_planar_10 1

; v210_planar_unpack(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int64_t width)
cglobal v210_to_planar_10_%1, 5, 5, 7, src, y, u, v, pixels
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq
    neg    pixelsq

    mova   m3, [v210_mult]
    mova   m4, [v210_mask]
    mova   m5, [v210_luma_shuf]
    mova   m6, [v210_chroma_shuf]

    .loop:
%ifidn %1, unaligned
        movu   m0, [srcq]
%else
        mova   m0, [srcq]
%endif

        pmullw m1, m0, m3
        psrld  m0, 10
        psrlw  m1, 6            ; u0 v0 y1 y2 v1 u2 y4 y5
        pand   m0, m4           ; y0 __ u1 __ y3 __ v2 __

        shufps m2, m1, m0, 0x8d ; y1 y2 y4 y5 y0 __ y3 __
        pshufb m2, m5           ; y0 y1 y2 y3 y4 y5 __ __
        movu   [yq + 2*pixelsq], m2

        shufps m1, m1, m0, 0xd8     ; u0 v0 v1 u2 u1 __ v2 __
        pshufb m1, m6           ; u0 u1 u2 __ v0 v1 v2 __
        movq   [uq + pixelsq], xm1
        movhps [vq + pixelsq], xm1

    %if cpuflag(avx2)
        vextracti128 [yq + 2*pixelsq + 12], m2, 1
        vextracti128 xm0, m1, 1
        movq   [uq + pixelsq + 6], xm0
        movhps [vq + pixelsq + 6], xm0
    %endif

        add srcq, mmsize
        add pixelsq, (6*mmsize)/16
    jl  .loop
RET

%endmacro

INIT_XMM ssse3
v210_to_planar_10 aligned
INIT_XMM avx
v210_to_planar_10 aligned
INIT_YMM avx2
v210_to_planar_10 aligned

%macro v210_to_planar_8 1

cglobal v210_to_planar_8_%1, 5, 5, 7, src, y, u, v, pixels
    shr pixelsq, 1
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq
    neg    pixelsq

    mova   m3, [v210_mult]

    .loop:
%ifidn %1, unaligned
        movu   m0, [srcq]
%else
        mova   m0, [srcq]
%endif

        pmullw m1, m0, m3
        pslld  m0, 4
        pand   m1, [v210_to_planar8_mask1] ; __ u0 __ v0  __ y1 __ y2  __ v1 __ u2  __ y4 __ y5
        pand   m0, [v210_to_planar8_mask2] ; __ __ y0 __  __ __ u1 __  __ __ y3 __  __ __ v2 __

        por m0, m1                         ; __ u0 y0 v0  __ y1 u1 y2  __ v1 y3 u2  __ y4 v2 y5

        pshufb m2, m0, [v210_to_planar8_shuf_y]
        pshufb m1, m0, [v210_to_planar8_shuf_u]
        pshufb m0, m0, [v210_to_planar8_shuf_v]
        movq [yq + 2*pixelsq], xm2

%if notcpuflag(avx2)
        movd [uq + pixelsq],   xm1
        movd [vq + pixelsq],   xm0
%else
        vextracti128 [yq + 2*pixelsq + 6], m2, 1
        vpermq m1, m1, q3120
        vpermq m0, m0, q3120
        pshufb xm1, xm1, [planar_8_avx2_shuf1]
        pshufb xm0, xm0, [planar_8_avx2_shuf1]
        movq [uq + pixelsq],   xm1
        movq [vq + pixelsq],   xm0
%endif

        add srcq, mmsize
        add pixelsq, (3*mmsize)/16
    jl  .loop
RET

%endmacro

INIT_XMM ssse3
v210_to_planar_8 aligned
INIT_XMM avx
v210_to_planar_8 aligned
INIT_YMM avx2
v210_to_planar_8 aligned

%macro v210_to_sdi 0

; v210_to_sdi(const uint32_t *src, uint8_t *dst, int64_t width)
cglobal v210_to_sdi, 3, 3, 8 + 7*ARCH_X86_64, src, dst, pixels
    neg      pixelsq

    mova     m3,  [v210_sdi_mask_1]
    mova     m4,  [v210_sdi_mask_2]
    mova     m5,  [v210_sdi_mask_3]
    mova     m6,  [v210_sdi_shuf_1]
    mova     m7,  [v210_sdi_shuf_2]
%if ARCH_X86_64
    mova     m8,  [v210_sdi_shuf_3]
    mova     m9,  [v210_sdi_mul_1]
    mova     m10, [v210_sdi_mul_2]
    mova     m11, [v210_sdi_mul_3]
    mova     m12, [v210_sdi_shuf_after_1]
    mova     m13, [v210_sdi_shuf_after_2]
    mova     m14, [v210_sdi_shuf_after_3]
%else
    %define  m8,  [v210_sdi_shuf_3]
    %define  m9,  [v210_sdi_mul_1]
    %define  m10, [v210_sdi_mul_2]
    %define  m11, [v210_sdi_mul_3]
    %define  m12, [v210_sdi_shuf_after_1]
    %define  m13, [v210_sdi_shuf_after_2]
    %define  m14, [v210_sdi_shuf_after_3]
%endif

    .loop:
        mova     m0, [srcq]

        pand m2, m0, m3
        pand m1, m0, m4
        pand m0, m0, m5

        pshufb m2, m6
        pshufb m1, m7
        pshufb m0, m8

        pmulhrsw m2, m9
        pmullw   m1, m10
        pmullw   m0, m11

        pshufb m2, m12
        pshufb m1, m13
        pshufb m0, m14

        por      m1, m2
        por      m1, m0

        movu     [dstq], m1
%if cpuflag(avx2)
        vextracti128 [dstq + 15], m1, 1
%endif

        add      dstq, (15*mmsize)/16
        add srcq, mmsize
        add      pixelsq, (6*mmsize)/16
    jl .loop
RET

%endmacro

INIT_XMM ssse3
v210_to_sdi
INIT_XMM avx
v210_to_sdi
INIT_YMM avx2
v210_to_sdi
