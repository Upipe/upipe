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

planar_8_y_shuf1: times 2 db 1, -1, 0, -1, 3, -1, 2, -1,  5, -1,  4, -1, -1, -1, -1, -1
planar_8_y_shuf2: times 2 db 7, -1, 6, -1, 9, -1, 8, -1, 11, -1, 10, -1, -1, -1, -1, -1
planar_8_y_mult: times 8 dw 4, 64
planar_8_y_shift: times 8 dw 2, 6

planar_8_uv_shuf1: times 2 db -1,  8, -1, -1, -1,  9, -1, -1, -1, 10, -1, -1, 0, 1, 2, -1
planar_8_uv_shuf2: times 2 db -1, 11, -1, -1, -1, 12, -1, -1, -1, 13, -1, -1, 3, 4, 5, -1

planar_8_uv_mult: times 2 dd 16, 16, 16, 1
planar_8_uv_shift: times 2 dd 4, 4, 4, 0

planar_8_shuf_final: times 2 db 12, 3, 2, 1, 0, 13, 7, 6, 5, 4, 14, 11, 10, 9, 8, -1

planar_10_y_mult:  times 2 dw 0x10, 0x1, 0x10, 0x1, 0x10, 0x1, 0x10, 0x1
planar_10_uv_mult: times 2 dw 0x40, 0x40, 0x40, 0x40, 0x4, 0x4, 0x4, 0x4

planar_10_y_shuf:  times 2 db -1, 1, 0, 3, 2, -1, 5, 4, 7, 6, -1, 9, 8, 11, 10, -1
planar_10_uv_shuf: times 2 db 1, 0, 9, 8, -1, 3, 2, 11, 10, -1, 5, 4, 13, 12, -1, -1

SECTION .text

%macro planar_to_sdi_8 0

; planar_to_sdi_8(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width)
cglobal planar_to_sdi_8, 5, 5, 4, y, u, v, l, pixels
    shr    pixelsq, 1
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq
    neg    pixelsq

.loop:
    movu   xm0, [yq + pixelsq*2] ; yyyy yyyy yyyy xxxx
    movq   xm1, [uq + pixelsq*1] ; uuuu uuxx
    movhps xm1, [vq + pixelsq*1] ; uuuu uuxx vvvv vvxx
%if cpuflag(avx2)
    vinserti128 m0, m0, [yq + pixelsq*2 + 12], 1 ; yyyy yyyy yyyy xxxx yyyy yyyy yyyy xxxx
    movq   xm2, [uq + pixelsq*1 + 6]
    movhps xm2, [vq + pixelsq*1 + 6]
    vinserti128 m1, m1, xm2, 1 ; uuuu uuxx vvvv vvxx uuuu uuxx vvvv vvxx
%endif

    pshufb    m3, m0, [planar_8_y_shuf2] ; y7 0 y6 0 y9 0 y8 0 y11 0 y10 0 0 0 0 0
    pshufb    m0, [planar_8_y_shuf1]     ; y1 0 y0 0 y3 0 y2 0  y5 0  y4 0 0 0 0 0

%if cpuflag(avx512)
    vpsllvw m3, m3, [planar_8_y_shift]
    vpsllvw m0, m0, [planar_8_y_shift] ; words y1<<2 y0<<6 ...
%else
    pmullw m3, [planar_8_y_mult]
    pmullw m0, [planar_8_y_mult] ; words y1*4 y0*64 ...
%endif

    pshufb m2, m1, [planar_8_uv_shuf2] ; 0 v3 0 0 0 v4 0 0 0 v5 0 0 u3 u4 u5 0
    pshufb m1, [planar_8_uv_shuf1]     ; 0 v0 0 0 0 v1 0 0 0 v2 0 0 u0 u1 u2 0

%if cpuflag(avx512)
    vpsllvd m2, m2, [planar_8_uv_shift]
    vpsllvd m1, m1, [planar_8_uv_shift] ; dwords v0<<4 ...
%else
    pmulld m2, [planar_8_uv_mult]
    pmulld m1, [planar_8_uv_mult] ; dwords v0*16 ...
%endif

    por    m3, m2
    por    m0, m1 ; dwords low y1|v0|y0 high y3|v1|y2 y5|v2|y4 uuu0

    pshufb m3, [planar_8_shuf_final]
    pshufb m0, [planar_8_shuf_final] ; insert u and endian swap dwords

    movu   [lq], xm0
    movu   [lq+15], xm3

%if cpuflag(avx2)
    vextracti128 [lq+((15*mmsize)/16)], m0, 1
    vextracti128 [lq+((15*mmsize)/16)+15], m3, 1
%endif

    add    lq, (30*mmsize)/16
    add    pixelsq, (6*mmsize)/16
    jl .loop

    RET

cglobal planar_to_sdi_8_2, 5, 5, 4, y, u, v, dst1, dst2, pixels
    shr    pixelsq, 1
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq

    neg    pixelsq

    pxor m3, m3

    .loop:

    movu   xm0, [yq + pixelsq*2]
    movq   xm1, [uq + pixelsq*1]
    movhps xm1, [vq + pixelsq*1]
%if cpuflag(avx2)
    vinserti128 m0, m0, [yq + pixelsq*2 + 12], 1
    movq   xm2, [uq + pixelsq*1 + 6]
    movhps xm2, [vq + pixelsq*1 + 6]
    vinserti128 m1, m1, xm2, 1
%endif

    pshufb    m4, m0, [planar_8_y_shuf2]
    pshufb    m0, [planar_8_y_shuf1]

%if cpuflag(avx512)
    vpsllvw m4, m4, [planar_8_y_shift]
    vpsllvw m0, m0, [planar_8_y_shift]
%else
    pmullw m4, [planar_8_y_mult]
    pmullw m0, [planar_8_y_mult]
%endif

    pshufb m5, m1, [planar_8_uv_shuf2]
    pshufb m1, [planar_8_uv_shuf1]

%if cpuflag(avx512)
    vpsllvd m5, m5, [planar_8_uv_shift]
    vpsllvd m1, m1, [planar_8_uv_shift]
%else
    pmulld m5, [planar_8_uv_mult]
    pmulld m1, [planar_8_uv_mult]
%endif

    por    m4, m5
    por    m0, m1

    pshufb m4, [planar_8_shuf_final]
    pshufb m0, [planar_8_shuf_final]

    movu   [dst1q], xm0
    movu   [dst2q], xm0
    movu   [dst1q+15], xm4
    movu   [dst2q+15], xm4

%if cpuflag(avx2)
    vextracti128 [dst1q+((15*mmsize)/16)], m0, 1
    vextracti128 [dst2q+((15*mmsize)/16)], m0, 1
    vextracti128 [dst1q+((15*mmsize)/16)+15], m4, 1
    vextracti128 [dst2q+((15*mmsize)/16)+15], m4, 1
%endif

        add    dst1q, (30*mmsize)/16
        add    dst2q, (30*mmsize)/16
        add    pixelsq, (6*mmsize)/16
    jl .loop
RET

%endmacro

INIT_XMM avx
planar_to_sdi_8
INIT_YMM avx2
planar_to_sdi_8
INIT_YMM avx512
planar_to_sdi_8

%macro planar_to_sdi_10 0

; planar_to_sdi_10(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width)
cglobal planar_to_sdi_10, 5, 5, 2+cpuflag(avx2), y, u, v, l, pixels
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq

    neg    pixelsq

.loop:
    movu   xm0, [yq + pixelsq*2]
    movq   xm1, [uq + pixelsq*1]
    movhps xm1, [vq + pixelsq*1]
%if cpuflag(avx2)
    vinserti128 m0, m0, [yq + pixelsq*2 + 12], 1
    movq   xm2, [uq + pixelsq*1 +  6]
    movhps xm2, [vq + pixelsq*1 +  6]
    vinserti128 m1, m1, xm2, 1
%endif

    pmullw m0, [planar_10_y_mult]
    pmullw m1, [planar_10_uv_mult]

    pshufb m0, [planar_10_y_shuf]
    pshufb m1, [planar_10_uv_shuf]

    por    m0, m1

    movu   [lq], xm0
%if cpuflag(avx2)
    vextracti128 [lq+15], m0, 1
%endif

    add    lq, (15*mmsize)/16
    add    pixelsq, (6*mmsize)/16
    jl .loop

    RET

cglobal planar_to_sdi_10_2, 5, 5, 2+cpuflag(avx2), y, u, v, dst1, dst2, pixels
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq

    neg    pixelsq

    .loop:
        movu   xm0, [yq + pixelsq*2]
        movq   xm1, [uq + pixelsq*1]
        movhps xm1, [vq + pixelsq*1]
%if cpuflag(avx2)
        vinserti128 m0, m0, [yq + pixelsq*2 + 12], 1
        movq   xm2, [uq + pixelsq*1 +  6]
        movhps xm2, [vq + pixelsq*1 +  6]
        vinserti128 m1, m1, xm2, 1
%endif

        pmullw m0, [planar_10_y_mult]
        pmullw m1, [planar_10_uv_mult]

        pshufb m0, [planar_10_y_shuf]
        pshufb m1, [planar_10_uv_shuf]

        por    m0, m1

        movu   [dst1q], xm0
        movu   [dst2q], xm0
%if cpuflag(avx2)
        vextracti128 [dst1q+15], m0, 1
        vextracti128 [dst2q+15], m0, 1
%endif

        add    dst1q, (15*mmsize)/16
        add    dst2q, (15*mmsize)/16
        add    pixelsq, (6*mmsize)/16
    jl .loop
RET

%endmacro

INIT_XMM ssse3
planar_to_sdi_10
INIT_XMM avx
planar_to_sdi_10
INIT_YMM avx2
planar_to_sdi_10
