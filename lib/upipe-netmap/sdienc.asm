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

planar_8_y_shuf: db 0, -1, 1, -1, 2, -1, 3, -1, 4, -1, 5, -1, 6, -1, -1, -1
planar_8_y_mult: dw 0x40, 0x4, 0x40, 0x4, 0x40, 0x4, 0x40, 0x0
planar_8_y_shuf_after: db -1, 1, 0, 3, 2, -1, 5, 4, 7, 6, -1, 9, 8, 11, 10, -1

planar_8_u_shuf: db 0, -1, -1, -1, -1, 1, -1, -1, -1, -1, 2, -1, -1, -1, -1, -1

planar_8_v_shuf: db 0, -1, 1, -1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
planar_8_v_shuf_after: db -1, -1, 1, 0, -1, -1, -1, 3, 2, -1, -1, -1, 5, 4, -1, -1

planar_10_y_shift:  dw 0x10, 0x1, 0x10, 0x1, 0x10, 0x1, 0x10, 0x1
planar_10_uv_shift: dw 0x40, 0x40, 0x40, 0x40, 0x4, 0x4, 0x4, 0x4

planar_10_y_shuf:  db -1, 1, 0, 3, 2, -1, 5, 4, 7, 6, -1, 9, 8, 11, 10, -1
planar_10_uv_shuf: db 1, 0, 9, 8, -1, 3, 2, 11, 10, -1, 5, 4, 13, 12, -1, -1

SECTION .text

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
