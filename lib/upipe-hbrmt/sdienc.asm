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

sdi_enc_mult_10: times 4 dw 64, 16, 4, 1
sdi_chroma_shuf_10: times 2 db 1, 0, 5, 4, -1, 9, 8, 13, 12, -1, -1, -1, -1, -1, -1, -1
sdi_luma_shuf_10: times 2 db -1, 3, 2, 7, 6, -1, 11, 10, 15, 14, -1, -1, -1, -1, -1, -1

SECTION .text

%macro uyvy_to_sdi 0

; uyvy_to_sdi(uint8_t *dst, const uint8_t *y, int64_t size)
cglobal uyvy_to_sdi, 3, 4, 3, dst, y, size
    add     sizeq, sizeq
    add     yq, sizeq
    neg     sizeq
    mova    m2, [sdi_enc_mult_10]

.loop:
    pmullw  m0, m2, [yq+sizeq]
    pshufb  m1, m0, [sdi_chroma_shuf_10]
    pshufb  m0, [sdi_luma_shuf_10]
    por     m0, m1

    movu    [dstq], xm0
%if cpuflag(avx2)
    vextracti128 [dstq+10], m0, 1
%endif

    add     dstq, (mmsize*5)/8
    add     sizeq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
uyvy_to_sdi
INIT_XMM avx
uyvy_to_sdi
INIT_YMM avx2
uyvy_to_sdi
