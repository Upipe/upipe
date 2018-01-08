;******************************************************************************
;* V210 SIMD pack
;* Copyright (c) 2014 Kieran Kunhya <kierank@obe.tv>
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

SECTION_RODATA 64

v210_enc_min_10: times 16 dw 0x0004
v210_enc_max_10: times 16 dw 0x3fb

v210_enc_luma_mult_10: times 2 dw 4,1,16,4,1,16,0,0
v210_enc_luma_shuf_10: times 2 db -1,0,1,-1,2,3,4,5,-1,6,7,-1,8,9,10,11

v210_enc_chroma_mult_10: times 2 dw 1,4,16,0,16,1,4,0
v210_enc_chroma_shuf_10: times 2 db 0,1,8,9,-1,2,3,-1,10,11,4,5,-1,12,13,-1

v210_enc_min_8: times 16 dw 0x0101
v210_enc_max_8: times 16 dw 0xFEFE

v210_enc_luma_shuf_8: times 2 db 6,-1,7,-1,8,-1,9,-1,10,-1,11,-1,-1,-1,-1,-1
v210_enc_luma_mult_8: times 2 dw 16,4,64,16,4,64,0,0

v210_enc_chroma_shuf1_8: times 2 db 0,-1,1,-1,2,-1,3,-1,8,-1,9,-1,10,-1,11,-1
v210_enc_chroma_shuf2_8: times 2 db 3,-1,4,-1,5,-1,7,-1,11,-1,12,-1,13,-1,15,-1

v210_enc_chroma_mult_8: times 2 dw 4,16,64,0,64,4,16,0

v210_enc_uyvy_chroma_shift_10: times 2 dw 1, 0, 16, 0, 4, 0, 0, 0
v210_enc_uyvy_chroma_shuff_10: times 2 db 0, 1, 4, 5, -1, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1

v210_enc_uyvy_luma_shift_10: times 2 dw 0, 4, 0, 1, 0, 16, 0, 0
v210_enc_uyvy_luma_shuft_10: times 2 db -1, 2, 3, -1, 6, 7, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1

SECTION .text

%macro planar_to_v210_10 0

; planar_to_v210_10(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst, ptrdiff_t width)
cglobal planar_to_v210_10, 5, 5, 4+cpuflag(avx2), y, u, v, dst, width
    lea     r0, [yq+2*widthq]
    add     uq, widthq
    add     vq, widthq
    neg     widthq

    mova    m2, [v210_enc_min_10]
    mova    m3, [v210_enc_max_10]

.loop:
    movu        xm0, [yq+2*widthq]
%if cpuflag(avx2)
    vinserti128 m0,   m0, [yq+widthq*2+12], 1
%endif
    CLIPW   m0, m2, m3

    movq         xm1, [uq+widthq]
    movhps       xm1, [vq+widthq]
%if cpuflag(avx2)
    movq         xm4, [uq+widthq+6]
    movhps       xm4, [vq+widthq+6]
    vinserti128  m1,   m1, xm4, 1
%endif
    CLIPW   m1, m2, m3

    pmullw  m0, [v210_enc_luma_mult_10]
    pshufb  m0, [v210_enc_luma_shuf_10]

    pmullw  m1, [v210_enc_chroma_mult_10]
    pshufb  m1, [v210_enc_chroma_shuf_10]

    por     m0, m1

    movu    [dstq], m0

    add     dstq, mmsize
    add     widthq, (mmsize*3)/8
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
planar_to_v210_10

INIT_YMM avx2
planar_to_v210_10

%macro v210_uyvy_pack_10 0

; v210_uyvy_pack_10(const uint16_t *y, uint8_t *dst, ptrdiff_t width)
cglobal v210_uyvy_pack_10, 3, 6, 6, y, dst, width
    shl     widthq, 2
    add     yq, widthq
    neg     widthq

    mova    m4, [v210_enc_min_10]
    mova    m5, [v210_enc_max_10]

.loop:
    movu    m0, [yq+widthq+ 0]
    movu    m1, [yq+widthq+12]

    CLIPW   m0, m4, m5
    CLIPW   m1, m4, m5

    pmullw  m2, m0, [v210_enc_uyvy_luma_shift_10]
    pmullw  m3, m1, [v210_enc_uyvy_luma_shift_10]

    pmullw  m0, [v210_enc_uyvy_chroma_shift_10]
    pshufb  m0, [v210_enc_uyvy_chroma_shuff_10]

    pmullw  m1, [v210_enc_uyvy_chroma_shift_10]
    pshufb  m1, [v210_enc_uyvy_chroma_shuff_10]

    pshufb  m2, [v210_enc_uyvy_luma_shuft_10]
    pshufb  m3, [v210_enc_uyvy_luma_shuft_10]

    por     m0, m2
    por     m1, m3

    movu    [dstq+0], m0
    movq    [dstq+8], m1

    add     dstq, mmsize
    add     widthq, 24
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
v210_uyvy_pack_10

%macro planar_to_v210_8 0

; planar_to_v210_8(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dst, ptrdiff_t width)
cglobal planar_to_v210_8, 5, 5, 7, y, u, v, dst, width
    add     yq, widthq
    shr     widthq, 1
    add     uq, widthq
    add     vq, widthq
    neg     widthq

    mova    m4, [v210_enc_min_8]
    mova    m5, [v210_enc_max_8]
    pxor    m6, m6

.loop:
    movu        xm1, [yq+widthq*2]
%if cpuflag(avx2)
    vinserti128 m1,   m1, [yq+widthq*2+12], 1
%endif
    CLIPUB  m1, m4, m5

    punpcklbw m0, m1, m6
    ; can't unpack high bytes in the same way because we process
    ; only six bytes at a time
    pshufb  m1, [v210_enc_luma_shuf_8]

    pmullw  m0, [v210_enc_luma_mult_8]
    pmullw  m1, [v210_enc_luma_mult_8]
    pshufb  m0, [v210_enc_luma_shuf_10]
    pshufb  m1, [v210_enc_luma_shuf_10]

    movq         xm3, [uq+widthq]
    movhps       xm3, [vq+widthq]
%if cpuflag(avx2)
    movq         xm2, [uq+widthq+6]
    movhps       xm2, [vq+widthq+6]
    vinserti128  m3,   m3, xm2, 1
%endif
    CLIPUB  m3, m4, m5

    ; shuffle and multiply to get the same packing as in 10-bit
    pshufb  m2, m3, [v210_enc_chroma_shuf1_8]
    pshufb  m3, [v210_enc_chroma_shuf2_8]

    pmullw  m2, [v210_enc_chroma_mult_8]
    pmullw  m3, [v210_enc_chroma_mult_8]
    pshufb  m2, [v210_enc_chroma_shuf_10]
    pshufb  m3, [v210_enc_chroma_shuf_10]

    por     m0, m2
    por     m1, m3

    movu         [dstq],    xm0
    movu         [dstq+16], xm1
%if cpuflag(avx2)
    vextracti128 [dstq+32], m0, 1
    vextracti128 [dstq+48], m1, 1
%endif

    add     dstq, 2*mmsize
    add     widthq, (mmsize*3)/8
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
planar_to_v210_8
INIT_XMM avx
planar_to_v210_8
INIT_YMM avx2
planar_to_v210_8
