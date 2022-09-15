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

sdi_shuf_10:         times 2 db 1, 0, 2, 1, 3, 2, 4, 3, 6, 5, 7, 6, 8, 7, 9, 8

sdi_mask_10:         times 2 db 0xc0, 0xff, 0xf0, 0x3f, 0xfc, 0x0f, 0xff, 0x03, 0xc0, 0xff, 0xf0, 0x3f, 0xfc, 0x0f, 0xff, 0x03

sdi_chroma_mult_10:  times 4 dw 0x400, 0x0, 0x4000, 0x0
sdi_luma_mult_10:    times 4 dw 0x0, 0x800, 0x0, 0x7fff

sdi_shift_10:        times 4 dw 0x6, 0x4, 0x2, 0x0

levelb_shuf: times 2 db 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15

SECTION .text

%macro levelb_to_uyvy 0

cglobal levelb_to_uyvy, 4, 4, 7-cpuflag(avx512), src, dst1, dst2, pixels
    lea dst1q, [dst1q + 4*pixelsq]
    lea dst2q, [dst2q + 4*pixelsq]
    neg pixelsq

    mova     m2, [sdi_shuf_10]
    mova     m3, [sdi_mask_10]
    mova     m4, [levelb_shuf]
%if notcpuflag(avx512)
    mova     m5, [sdi_chroma_mult_10]
    mova     m6, [sdi_luma_mult_10]
%else
    mova     m5, [sdi_shift_10]
%endif

    .loop:
        movu     xm0, [srcq]
        %if cpuflag(avx2)
            vinserti128 m0, m0, [srcq + 10], 1
        %endif

        pshufb   m0, m2
        pand     m0, m3

%if cpuflag(avx512)
        vpsrlvw m0, m5
%else
        pmulhuw  m1, m0, m5
        pmulhrsw m0, m6

        por      m0, m1
%endif

        pshufb   m0, m4 ; FIXME merge this shuffle with the above

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
levelb_to_uyvy
INIT_XMM avx
levelb_to_uyvy
INIT_YMM avx2
levelb_to_uyvy
INIT_YMM avx512
levelb_to_uyvy