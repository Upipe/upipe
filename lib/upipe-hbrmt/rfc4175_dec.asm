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

sdi_v210_shuf_easy:       times 2 db 1, 0, -1, -1, 4, 3, 7, 6, 8, 7, 11, 10, 12, 11, -1, -1
sdi_v210_rshift_easy:     times 2 dw 0x200, 0x7fff, 0x7fff, 0x7fff, 0x2000, 0x2000, 0x800, 0x7fff
sdi_v210_mask_easy:       times 2 db 0xff, 0x03, 0x00, 0x00, 0xff, 0x03, 0xf0, 0x3f, 0xff, 0x03, 0xf0, 0x3f, 0xff, 0x03, 0x00, 0x00

sdi_v210_shuf_hard_1:     times 2 db 3, 2, 9, 8, 14, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
sdi_v210_mult_hard_1:     times 2 dw 4, 4, 16, 1, 1, 1, 1, 1
sdi_v210_mask_hard_1:     times 2 db 0xf0, 0x3f, 0xfc, 0x0f, 0xf0, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
sdi_v210_shuf_hard_1_end: times 2 db -1, -1, 0, 1, -1, -1, -1, -1, -1, 2, 3, -1, -1, -1, 4, 5

sdi_v210_shuf_hard_2:     times 2 db 2, 1, 6, 5, 13, 12, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1
sdi_v210_rshift_hard_2:   times 2 dw 0x2000, 0x800, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff
sdi_v210_mask_hard_2:     times 2 db 0xfc, 0x0f, 0xfc, 0x0f, 0xfc, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
sdi_v210_shuf_hard_2_end: times 2 db -1, 0, 1, -1, -1, 2, 3, -1, -1, -1, -1, -1, -1, 4, 5, -1

SECTION .text

%macro sdi_v210_unpack 0

; sdi_v210_unpack(const uint8_t *src, uint32_t *dst, int64_t width)
cglobal sdi_v210_unpack, 3, 3, 3+11*ARCH_X86_64, src, dst, size
    add     srcq, sizeq
    neg     sizeq

%if ARCH_X86_64
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
%else
    %define m3  [sdi_v210_shuf_easy]
    %define m4  [sdi_v210_shuf_hard_1]
    %define m5  [sdi_v210_shuf_hard_2]
    %define m6  [sdi_v210_rshift_easy]
    %define m7  [sdi_v210_mult_hard_1]
    %define m8  [sdi_v210_rshift_hard_2]
    %define m9  [sdi_v210_mask_easy]
    %define m10 [sdi_v210_mask_hard_1]
    %define m11 [sdi_v210_mask_hard_2]
    %define m12 [sdi_v210_shuf_hard_1_end]
    %define m13 [sdi_v210_shuf_hard_2_end]
%endif ; ARCH_X86_64

.loop:
    movu     xm0, [srcq+sizeq]
%if cpuflag(avx2)
    vinserti128 m0, m0, [srcq + sizeq + 15], 1
%endif

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
    add      sizeq, (15*mmsize)/16
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
sdi_v210_unpack
INIT_XMM avx
sdi_v210_unpack
INIT_YMM avx2
sdi_v210_unpack
