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

SECTION .text

%macro sdi_to_uyvy 0

; sdi_to_uyvy(const uint8_t *src, uint16_t *y, int64_t size)
cglobal sdi_to_uyvy, 3, 3, 7, src, y, size
    add      srcq, sizeq
    neg      sizeq

    mova     m2, [sdi_comp_mask_10]
    mova     m3, [sdi_chroma_shuf_10]
    mova     m4, [sdi_luma_shuf_10]
    mova     m5, [sdi_chroma_mult_10]
    mova     m6, [sdi_luma_mult_10]

.loop:
    movu     xm0, [srcq+sizeq]
%if cpuflag(avx2)
    vinserti128 m0, m0, [srcq+sizeq+10], 1
%endif

    pandn    m1, m2, m0
    pand     m0, m2

    pshufb   m0, m3
    pshufb   m1, m4

    pmulhuw  m0, m5
    pmulhrsw m1, m6

    por      m0, m1

    mova     [yq], m0

    add      yq,    mmsize
    add      sizeq, (mmsize*5)/8
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
sdi_to_uyvy
INIT_YMM avx2
sdi_to_uyvy
