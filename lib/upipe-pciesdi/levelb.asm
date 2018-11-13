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
