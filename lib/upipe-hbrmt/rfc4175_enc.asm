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

SECTION .text

%macro v210_to_planar_10 1

; v210_planar_unpack(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int64_t width)
cglobal v210_to_planar_10_%1, 5, 5, 7, src, y, u, v, width
    lea    yq, [yq+2*widthq]
    add    uq, widthq
    add    vq, widthq
    neg    widthq

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
        movu   [yq + 2*widthq], m2

        shufps m1, m1, m0, 0xd8     ; u0 v0 v1 u2 u1 __ v2 __
        pshufb m1, m6           ; u0 u1 u2 __ v0 v1 v2 __
        movq   [uq + widthq], xm1
        movhps [vq + widthq], xm1

    %if cpuflag(avx2)
        vextracti128 [yq + 2*widthq + 12], m2, 1
        vextracti128 xm0, m1, 1
        movq   [uq + widthq + 6], xm0
        movhps [vq + widthq + 6], xm0
    %endif

        add srcq, mmsize
        add widthq, (6*mmsize)/16
    jl  .loop
RET

%endmacro

INIT_XMM ssse3
v210_to_planar_10 aligned
INIT_XMM avx
v210_to_planar_10 aligned
INIT_YMM avx2
v210_to_planar_10 aligned
