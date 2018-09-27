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
