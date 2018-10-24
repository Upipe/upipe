;******************************************************************************
;* SDI-3G level B assembly
;* Copyright (c) 2018 James Darnley <jdarnley@obe.tv>
;*
;* This file is part of Upipe
;*
;* Upipe is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Upipe is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Upipe; if not, write to the Free Software
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

%macro sdi3g_levelb_unpack 0

cglobal sdi3g_levelb_unpack, 4, 4, 3, src, dst1, dst2, pixels
    lea srcq,  [srcq  + 8*pixelsq]
    lea dst1q, [dst1q + 4*pixelsq]
    lea dst2q, [dst2q + 4*pixelsq]
    neg pixelsq

    ALIGN 16
    .loop:
        movu m0, [srcq + 8*pixelsq]
        pshufd m1, m0, q0020
        pshufd m2, m0, q0031
        movq [dst1q + 4*pixelsq], m1
        movq [dst2q + 4*pixelsq], m2
        add pixelsq, mmsize/8
    jl .loop
RET

%endmacro

INIT_XMM sse2
sdi3g_levelb_unpack

%macro sdi3g_to_uyvy 0

cglobal sdi3g_to_uyvy_2, 4, 4, 15, src, dst1, dst2, pixels
    lea dst1q, [dst1q + 4*pixelsq]
    lea dst2q, [dst2q + 4*pixelsq]
    neg pixelsq

    mova     m2, [sdi_comp_mask_10]
    mova     m3, [sdi_chroma_shuf_10]
    mova     m4, [sdi_luma_shuf_10]
    mova     m5, [sdi_chroma_mult_10]
    mova     m6, [sdi_luma_mult_10]

    .loop:
        movu     xm0, [srcq]
        %if cpuflag(avx2)
            vinserti128 m0, m0, [srcq + 10], 1
        %endif

        pandn    m1, m2, m0
        pand     m0, m2

        pshufb   m0, m3
        pshufb   m1, m4

        pmulhuw  m0, m5
        pmulhrsw m1, m6

        por      m0, m1

        pshufd m0, m0, q3120

        %if cpuflag(avx2)
            vpermq       m0, m0, q3120
            movu         [dst1q + 4*pixelsq], xm0
            vextracti128 [dst2q + 4*pixelsq], m0, 1
        %else
            MOVHL m1, m0
            movq  [dst1q + 4*pixelsq], m0
            movq  [dst2q + 4*pixelsq], m1
        %endif

        add    srcq, (mmsize*5)/8
        add pixelsq, mmsize/8
    jl .loop

RET

%endmacro

INIT_XMM ssse3
sdi3g_to_uyvy
INIT_XMM avx
sdi3g_to_uyvy
INIT_YMM avx2
sdi3g_to_uyvy
