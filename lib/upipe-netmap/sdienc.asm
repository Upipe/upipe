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

SECTION_RODATA 64

icl_perm_y: ; vpermb does not set bytes to zero when the high bit is set unlike pshufb
%assign i 0
%rep 12
    db -1, i+1, i+0, i+3, i+2
    %assign i i+4
%endrep
times 4 db -1 ; padding to 64 bytes

icl_perm_uv: ; vpermb does not set bytes to zero when the high bit is set unlike pshufb
%assign i 0
%rep 12
    db i+33, i+32, i+1, i+0, -1
    %assign i i+2
%endrep
times 4 db -1 ; padding to 64 bytes

icl_planar10_shift_uv:
    times 16 dw 2 ; shift v left by 2
    times 16 dw 6 ; shift u left by 6
icl_planar8_shift_uv:
    times 16 dw 4 ; shift v left by 4
    times 16 dw 8 ; shift u left by 8

; align 32

planar_8_y_shuf1: times 2 db 1, -1, 0, -1, 3, -1, 2, -1,  5, -1,  4, -1, -1, -1, -1, -1
planar_8_y_shuf2: times 2 db 7, -1, 6, -1, 9, -1, 8, -1, 11, -1, 10, -1, -1, -1, -1, -1
planar_8_y_mult: times 8 dw 4, 64
planar_8_y_shift: times 8 dw 2, 6

planar_8_uv_shuf1: times 2 db -1,  8, -1, -1, -1,  9, -1, -1, -1, 10, -1, -1, 0, 1, 2, -1
planar_8_uv_shuf2: times 2 db -1, 11, -1, -1, -1, 12, -1, -1, -1, 13, -1, -1, 3, 4, 5, -1

planar_8_uv_mult: times 2 dd 16, 16, 16, 1
planar_8_uv_shift: times 2 dd 4, 4, 4, 0

planar_8_shuf_final: times 2 db 12, 3, 2, 1, 0, 13, 7, 6, 5, 4, 14, 11, 10, 9, 8, -1

planar_10_y_mult:  times 2 dw 0x10, 0x1, 0x10, 0x1, 0x10, 0x1, 0x10, 0x1
planar_10_uv_mult: times 2 dw 0x40, 0x40, 0x40, 0x40, 0x4, 0x4, 0x4, 0x4

planar_10_y_shuf:  times 2 db -1, 1, 0, 3, 2, -1, 5, 4, 7, 6, -1, 9, 8, 11, 10, -1
planar_10_uv_shuf: times 2 db 1, 0, 9, 8, -1, 3, 2, 11, 10, -1, 5, 4, 13, 12, -1, -1

icl_perm_y_kmask:  dq 0b11110_11110_11110_11110_11110_11110_11110_11110_11110_11110_11110_11110
icl_perm_uv_kmask: dq 0b01111_01111_01111_01111_01111_01111_01111_01111_01111_01111_01111_01111

icl_planar10_shift_y: dw 4, 0

SECTION .text

%macro planar_to_sdi_8 0-1

; planar_to_sdi_8(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width)
cglobal planar_to_sdi_8%1, 5+%0, 5+%0, 4, y, u, v, dst1, dst2, p
    %if %0 == 1
        %define pixelsq pq
    %else
        %define pixelsq dst2q
    %endif

    shr    pixelsq, 1
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq
    neg    pixelsq

.loop:
    movu   xm0, [yq + pixelsq*2] ; yyyy yyyy yyyy xxxx
    movq   xm1, [uq + pixelsq*1] ; uuuu uuxx
    movhps xm1, [vq + pixelsq*1] ; uuuu uuxx vvvv vvxx
%if cpuflag(avx2)
    vinserti128 m0, m0, [yq + pixelsq*2 + 12], 1 ; yyyy yyyy yyyy xxxx yyyy yyyy yyyy xxxx
    movq   xm2, [uq + pixelsq*1 + 6]
    movhps xm2, [vq + pixelsq*1 + 6]
    vinserti128 m1, m1, xm2, 1 ; uuuu uuxx vvvv vvxx uuuu uuxx vvvv vvxx
%endif

    pshufb    m3, m0, [planar_8_y_shuf2] ; y7 0 y6 0 y9 0 y8 0 y11 0 y10 0 0 0 0 0
    pshufb    m0, [planar_8_y_shuf1]     ; y1 0 y0 0 y3 0 y2 0  y5 0  y4 0 0 0 0 0

%if cpuflag(avx512)
    vpsllvw m3, m3, [planar_8_y_shift]
    vpsllvw m0, m0, [planar_8_y_shift] ; words y1<<2 y0<<6 ...
%else
    pmullw m3, [planar_8_y_mult]
    pmullw m0, [planar_8_y_mult] ; words y1*4 y0*64 ...
%endif

    pshufb m2, m1, [planar_8_uv_shuf2] ; 0 v3 0 0 0 v4 0 0 0 v5 0 0 u3 u4 u5 0
    pshufb m1, [planar_8_uv_shuf1]     ; 0 v0 0 0 0 v1 0 0 0 v2 0 0 u0 u1 u2 0

%if cpuflag(avx512)
    vpsllvd m2, m2, [planar_8_uv_shift]
    vpsllvd m1, m1, [planar_8_uv_shift] ; dwords v0<<4 ...
%else
    pmulld m2, [planar_8_uv_mult]
    pmulld m1, [planar_8_uv_mult] ; dwords v0*16 ...
%endif

    por    m3, m2
    por    m0, m1 ; dwords low y1|v0|y0 high y3|v1|y2 y5|v2|y4 uuu0

    pshufb m3, [planar_8_shuf_final]
    pshufb m0, [planar_8_shuf_final] ; insert u and endian swap dwords

    movu   [dst1q], xm0
    movu   [dst1q+15], xm3
    %if %0 == 1
        movu   [dst2q], xm0
        movu   [dst2q+15], xm3
    %endif

%if cpuflag(avx2)
    vextracti128 [dst1q+((15*mmsize)/16)], m0, 1
    vextracti128 [dst1q+((15*mmsize)/16)+15], m3, 1
    %if %0 == 1
        vextracti128 [dst2q+((15*mmsize)/16)], m0, 1
        vextracti128 [dst2q+((15*mmsize)/16)+15], m3, 1
    %endif
%endif

    add    dst1q, (30*mmsize)/16
    %if %0 == 1
        add    dst2q, (30*mmsize)/16
    %endif
    add    pixelsq, (6*mmsize)/16
    jl .loop
RET

%endmacro

INIT_XMM avx
planar_to_sdi_8
planar_to_sdi_8 _2
INIT_YMM avx2
planar_to_sdi_8
planar_to_sdi_8 _2
INIT_YMM avx512
planar_to_sdi_8
planar_to_sdi_8 _2
INIT_ZMM avx512icl

cglobal planar_to_sdi_8, 5, 5, 6, y, u, v, dst, pixels
    shr    pixelsq, 1
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq
    neg    pixelsq

    vpbroadcastd m2, [planar_8_y_shift+2] ; broadcast and "swap" values
    movu         m3, [icl_planar8_shift_uv]
    movu         m4, [icl_perm_y]
    movu         m5, [icl_perm_uv]
    kmovq        k1, [icl_perm_y_kmask]
    kmovq        k2, [icl_perm_uv_kmask]

    .loop:
        vpmovzxbw    zm0, [yq + pixelsq*2]
        movu         xm1, [vq + pixelsq*1]    ; load v into low end
        vinserti32x4 ym1, [uq + pixelsq*1], 1 ; load u into high end
        pmovzxbw     zm1, ym1

        vpsllvw m0, m2
        vpsllvw m1, m3
        vpermb  m0{k1}{z}, m4, m0 ; endian swap and make space for u where the k-mask sets to zero
        vpermb  m1{k2}{z}, m5, m1 ; move u, endian swap v, and make space for y where the k-mask sets to 0
        por m0, m1

        movu   [dstq], m0
        add     dstq, 60
        add  pixelsq, 12
    jl .loop
RET

%macro planar_to_sdi_10 0-1

; planar_to_sdi_10(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width)
cglobal planar_to_sdi_10%1, 5+%0, 5+%0, 2+cpuflag(avx2), y, u, v, dst1, dst2, p
    %if %0 == 1
        %define pixelsq pq
    %else
        %define pixelsq dst2q
    %endif

    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq
    neg    pixelsq

    .loop:
        movu   xm0, [yq + pixelsq*2]
        movq   xm1, [uq + pixelsq*1]
        movhps xm1, [vq + pixelsq*1]
%if cpuflag(avx2)
        vinserti128 m0, m0, [yq + pixelsq*2 + 12], 1
        movq   xm2, [uq + pixelsq*1 +  6]
        movhps xm2, [vq + pixelsq*1 +  6]
        vinserti128 m1, m1, xm2, 1
%endif

        pmullw m0, [planar_10_y_mult]
        pmullw m1, [planar_10_uv_mult]

        pshufb m0, [planar_10_y_shuf]
        pshufb m1, [planar_10_uv_shuf]

        por    m0, m1

        movu   [dst1q], xm0
        %if %0 == 1
        movu   [dst2q], xm0
        %endif
%if cpuflag(avx2)
        vextracti128 [dst1q+15], m0, 1
        %if %0 == 1
        vextracti128 [dst2q+15], m0, 1
        %endif
%endif

        add    dst1q, (15*mmsize)/16
        %if %0 == 1
        add    dst2q, (15*mmsize)/16
        %endif
        add    pixelsq, (6*mmsize)/16
    jl .loop
RET

%endmacro

INIT_XMM ssse3
planar_to_sdi_10
planar_to_sdi_10 _2
INIT_XMM avx
planar_to_sdi_10
planar_to_sdi_10 _2
INIT_YMM avx2
planar_to_sdi_10
planar_to_sdi_10 _2
INIT_ZMM avx512icl

cglobal planar_to_sdi_10, 5, 5, 6, y, u, v, dst, pixels
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq
    neg    pixelsq

    vpbroadcastd m2, [icl_planar10_shift_y]
    movu         m3, [icl_planar10_shift_uv]
    mova         m4, [icl_perm_y]
    mova         m5, [icl_perm_uv]
    kmovq        k1, [icl_perm_y_kmask]
    kmovq        k2, [icl_perm_uv_kmask]

    .loop:
        movu         zm0, [yq + pixelsq*2]
        movu         ym1, [vq + pixelsq*1]    ; load v into low end
        vinserti32x8 zm1, [uq + pixelsq*1], 1 ; load u into high end

        vpsllvw m0, m2
        vpsllvw m1, m3
        vpermb  m0{k1}{z}, m4, m0 ; endian swap and make space for u where the k-mask sets to zero
        vpermb  m1{k2}{z}, m5, m1 ; endian swap and make space for y where the k-mask sets to zero
        por m0, m1

        movu   [dstq], m0
        add     dstq, 60
        add  pixelsq, 24
    jl .loop
RET
