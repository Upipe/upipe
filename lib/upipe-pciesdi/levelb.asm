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

sdi_shuf_10:         times 2 db 1,0, 3,2, 6,5, 8,7, 2,1, 4,3, 7,6, 9,8

sdi_mask_10:         times 2 dw 0xffc0, 0x0ffc, 0xffc0, 0x0ffc, 0x3ff0, 0x03ff, 0x3ff0, 0x03ff

sdi_chroma_mult_10:  times 2 dw 0x400, 0x4000, 0x400, 0x4000, 0, 0, 0, 0
sdi_luma_mult_10:    times 2 dw 0, 0, 0, 0, 0x800, 0x7fff, 0x800, 0x7fff

sdi_shift_10:        times 2 dw 6, 2, 6, 2, 4, 0, 4, 0

SECTION .text

%macro levelb_to_uyvy 0

cglobal levelb_to_uyvy, 4, 4, 6-cpuflag(avx512), src, dst1, dst2, pixels
    lea dst1q, [dst1q + 4*pixelsq]
    lea dst2q, [dst2q + 4*pixelsq]
    neg pixelsq

    mova     m2, [sdi_shuf_10]
    mova     m3, [sdi_mask_10]
    %if cpuflag(avx512)
        mova m4, [sdi_shift_10]
    %else
        mova m4, [sdi_chroma_mult_10]
        mova m5, [sdi_luma_mult_10]
    %endif

    .loop:
        movu     xm0, [srcq]
        %if cpuflag(avx2)
            vinserti128 m0, m0, [srcq + 10], 1
        %endif

        pshufb m0, m2 ; spread into words and byte swap
        pand   m0, m3 ; mask out bits

        %if cpuflag(avx512)
            vpsrlvw  m0, m4
        %else
            pmulhuw  m1, m0, m4
            pmulhrsw m0, m5
            por      m0, m1
        %endif

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
