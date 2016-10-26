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

sdi_v210_shuf_easy:       db 1, 0, -1, -1, 4, 3, 7, 6, 8, 7, 11, 10, 12, 11, -1, -1
sdi_v210_rshift_easy:     dw 0x200, 0x7fff, 0x7fff, 0x7fff, 0x2000, 0x2000, 0x800, 0x7fff
sdi_v210_mask_easy:       db 0xff, 0x03, 0x00, 0x00, 0xff, 0x03, 0xf0, 0x3f, 0xff, 0x03, 0xf0, 0x3f, 0xff, 0x03, 0x00, 0x00

sdi_v210_shuf_hard_1:     db 3, 2, 9, 8, 14, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
sdi_v210_mult_hard_1:     dw 4, 4, 16, 1, 1, 1, 1, 1
sdi_v210_mask_hard_1:     db 0xf0, 0x3f, 0xfc, 0x0f, 0xf0, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
sdi_v210_shuf_hard_1_end: db -1, -1, 0, 1, -1, -1, -1, -1, -1, 2, 3, -1, -1, -1, 4, 5

sdi_v210_shuf_hard_2:     db 2, 1, 6, 5, 13, 12, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1
sdi_v210_rshift_hard_2:   dw 0x2000, 0x800, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff
sdi_v210_mask_hard_2:     db 0xfc, 0x0f, 0xfc, 0x0f, 0xfc, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
sdi_v210_shuf_hard_2_end: db -1, 0, 1, -1, -1, 2, 3, -1, -1, -1, -1, -1, -1, 4, 5, -1

planar_8_c_shuf: db 0, 5, 10, -1, -1, -1, -1, -1, 3, 2, 8, 7, 13, 12, -1, -1
planar_8_v_shuf_after: db 9, 11, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1

planar_8_y_shuf: db 2, 1, 4, 3, 7, 6, 9, 8, 12, 11, 14, 13, -1, -1, -1, -1
planar_8_y_mult: dw 0x4, 0x40, 0x4, 0x40, 0x4, 0x40, 0x0, 0x0
planar_8_y_shuf_after: db 1, 3, 5, 7, 9, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1

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
    movu     m0, [srcq+sizeq]

    pandn    m1, m2, m0
    pand     m0, m2

    pshufb   m0, m3
    pshufb   m1, m4

    pmulhuw  m0, m5
    pmulhrsw m1, m6

    por      m0, m1

    mova     [yq], m0

    add      yq,    mmsize
    add      sizeq, 10
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
sdi_unpack_10

%macro sdi_v210_unpack 0

; sdi_v210_unpack(const uint8_t *src, uint32_t *dst, int64_t width)
cglobal sdi_v210_unpack, 3, 3, 14, src, dst, size
    add     srcq, sizeq
    neg     sizeq

    mova    m3,  [sdi_v210_shuf_easy]
    mova    m4,  [sdi_v210_shuf_hard_1]
    mova    m5,  [sdi_v210_shuf_hard_2]
    mova    m6,  [sdi_v210_rshift_easy]
    mova    m7,  [sdi_v210_mult_hard_1]
    mova    m8,  [sdi_v210_rshift_hard_2]
    mova    m9,  [sdi_v210_mask_easy]
    mova    m10, [sdi_v210_mask_hard_1]
    mova    m11, [sdi_v210_mask_hard_2]
    mova    m12, [sdi_v210_shuf_hard_1_end]
    mova    m13, [sdi_v210_shuf_hard_2_end]

.loop:
    movu     m0, [srcq+sizeq]

    pshufb   m1, m0, m3
    pshufb   m2, m0, m4
    pshufb   m0, m5

    pmulhrsw m1, m6
    pmullw   m2, m7
    pmulhrsw m0, m8

    pand     m1, m9      ; U1, Y2, Y3, V2, U3, Y5
    pand     m2, m10
    pand     m0, m11

    pshufb   m2, m12     ; V1, Y4, Y6
    pshufb   m0, m13     ; Y1, U2, V3

    por      m1, m2
    por      m1, m0

    mova     [dstq], m1

    add      dstq, mmsize
    add      sizeq, 15
    jl .loop

    RET
%endmacro

INIT_XMM avx
sdi_v210_unpack

%macro sdi_to_planar_8 0

; sdi_to_planar_8(uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t size)
cglobal sdi_to_planar_8, 5, 6, 3, src, y, u, v, size, offset
    xor      offsetq, offsetq
    add      srcq, sizeq
    neg      sizeq

.loop:
    movu     m0, [srcq+sizeq]

    pshufb   m1, m0, [planar_8_c_shuf]

    pshufb   m0, [planar_8_y_shuf]
    pmullw   m0, [planar_8_y_mult]
    pshufb   m0, [planar_8_y_shuf_after]
    movq     [yq + 2*offsetq], m0

    movd     [uq + offsetq], m1
    psllw    m2, m1, 4
    pshufb   m2, [planar_8_v_shuf_after]
    movd     [vq + offsetq], m2

    add      offsetq, 3
    add      sizeq, 15
    jl .loop

    RET
%endmacro

INIT_XMM avx
sdi_to_planar_8
