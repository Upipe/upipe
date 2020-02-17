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

planar_8_y_shuf: times 2 db 0, -1, 1, -1, 2, -1, 3, -1, 4, -1, 5, -1, 6, -1, -1, -1
planar_8_y_mult: times 2 dw 0x40, 0x4, 0x40, 0x4, 0x40, 0x4, 0x40, 0x0
planar_8_y_shuf_after: times 2 db -1, 1, 0, 3, 2, -1, 5, 4, 7, 6, -1, 9, 8, 11, 10, -1

planar_8_u_shuf: times 2 db 0, -1, -1, -1, -1, 1, -1, -1, -1, -1, 2, -1, -1, -1, -1, -1

planar_8_v_shuf: times 2 db 0, -1, 1, -1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
planar_8_v_shuf_after: times 2 db -1, -1, 1, 0, -1, -1, -1, 3, 2, -1, -1, -1, 5, 4, -1, -1

planar_10_y_shift:  times 2 dw 0x10, 0x1, 0x10, 0x1, 0x10, 0x1, 0x10, 0x1
planar_10_uv_shift: times 2 dw 0x40, 0x40, 0x40, 0x40, 0x4, 0x4, 0x4, 0x4

planar_10_y_shuf:  times 2 db -1, 1, 0, 3, 2, -1, 5, 4, 7, 6, -1, 9, 8, 11, 10, -1
planar_10_uv_shuf: times 2 db 1, 0, 9, 8, -1, 3, 2, 11, 10, -1, 5, 4, 13, 12, -1, -1

SECTION .text

%macro planar_to_sdi_8 0

; planar_to_sdi_8(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width)
cglobal planar_to_sdi_8, 5, 5, 3, y, u, v, l, pixels
    shr    pixelsq, 1
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq

    neg    pixelsq

.loop:
    movq   xm0, [yq + pixelsq*2]
    movd   xm1, [uq + pixelsq*1]
    movd   xm2, [vq + pixelsq*1]
%if cpuflag(avx2)
    vinserti128 m0, m0, [yq + pixelsq*2 + 6], 1
    vinserti128 m1, m1, [uq + pixelsq*1 + 3], 1
    vinserti128 m2, m2, [vq + pixelsq*1 + 3], 1
%endif

    pshufb m0, [planar_8_y_shuf]
    pmullw m0, [planar_8_y_mult]
    pshufb m0, [planar_8_y_shuf_after]

    pshufb m1, [planar_8_u_shuf]

    por    m0, m1

    pshufb m2, [planar_8_v_shuf]
    psllw  m2, 4
    pshufb m2, [planar_8_v_shuf_after]

    por    m0, m2

    movu   [lq], xm0
%if cpuflag(avx2)
    vextracti128 [lq+15], m0, 1
%endif

    add    lq, (15*mmsize)/16
    add    pixelsq, (3*mmsize)/16
    jl .loop

    RET

cglobal planar_to_sdi_8_2, 5, 5, 3, y, u, v, dst1, dst2, pixels
    shr    pixelsq, 1
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq

    neg    pixelsq

    .loop:
        movq   xm0, [yq + pixelsq*2]
        movd   xm1, [uq + pixelsq*1]
        movd   xm2, [vq + pixelsq*1]
%if cpuflag(avx2)
        vinserti128 m0, m0, [yq + pixelsq*2 + 6], 1
        vinserti128 m1, m1, [uq + pixelsq*1 + 3], 1
        vinserti128 m2, m2, [vq + pixelsq*1 + 3], 1
%endif

        pshufb m0, [planar_8_y_shuf]
        pmullw m0, [planar_8_y_mult]
        pshufb m0, [planar_8_y_shuf_after]

        pshufb m1, [planar_8_u_shuf]

        por    m0, m1

        pshufb m2, [planar_8_v_shuf]
        psllw  m2, 4
        pshufb m2, [planar_8_v_shuf_after]

        por    m0, m2

        movu   [dst1q], xm0
        movu   [dst2q], xm0
%if cpuflag(avx2)
    vextracti128 [dst1q+15], m0, 1
    vextracti128 [dst2q+15], m0, 1
%endif

        add    dst1q, (15*mmsize)/16
        add    dst2q, (15*mmsize)/16
        add    pixelsq, (3*mmsize)/16
    jl .loop
RET

%endmacro

INIT_XMM ssse3
planar_to_sdi_8
INIT_XMM avx
planar_to_sdi_8
INIT_YMM avx2
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

    pmullw m0, [planar_10_y_shift]
    pmullw m1, [planar_10_uv_shift]

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

        pmullw m0, [planar_10_y_shift]
        pmullw m1, [planar_10_uv_shift]

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
