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

uyvy_enc_min_10: times 16 dw 0x0004
uyvy_enc_max_10: times 16 dw 0x3fb

uyvy_enc_min_8: times 16 dw 0x0101
uyvy_enc_max_8: times 16 dw 0xFEFE

sdi_blank: times 8 dw 0x200, 0x40

sdi_enc_mult_10: times 4 dw 64, 16, 4, 1
sdi_chroma_shuf_10: times 2 db 1, 0, 5, 4, -1, 9, 8, 13, 12, -1, -1, -1, -1, -1, -1, -1
sdi_luma_shuf_10: times 2 db -1, 3, 2, 7, 6, -1, 11, 10, 15, 14, -1, -1, -1, -1, -1, -1

planar_8_y_shuf: times 2 db 0, -1, 1, -1, 2, -1, 3, -1, 4, -1, 5, -1, 6, -1, -1, -1
planar_8_y_mult: times 2 dw 0x40, 0x4, 0x40, 0x4, 0x40, 0x4, 0x40, 0x0
planar_8_y_shuf_after: times 2 db -1, 1, 0, 3, 2, -1, 5, 4, 7, 6, -1, 9, 8, 11, 10, -1

planar_8_u_shuf: times 2 db 0, -1, -1, -1, -1, 1, -1, -1, -1, -1, 2, -1, -1, -1, -1, -1

planar_8_v_shuf: times 2 db 0, -1, 2, -1, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
planar_8_v_shuf_after: times 2 db -1, -1, 1, 0, -1, -1, -1, 3, 2, -1, -1, -1, 5, 4, -1, -1

planar_10_y_shift:  times 2 dw 0x10, 0x1, 0x10, 0x1, 0x10, 0x1, 0x10, 0x1
planar_10_uv_shift: times 2 dw 0x40, 0x40, 0x40, 0x40, 0x4, 0x4, 0x4, 0x4

planar_10_y_shuf:  times 2 db -1, 1, 0, 3, 2, -1, 5, 4, 7, 6, -1, 9, 8, 11, 10, -1
planar_10_uv_shuf: times 2 db 1, 0, 9, 8, -1, 3, 2, 11, 10, -1, 5, 4, 13, 12, -1, -1

pb_0: times 32 db 0

v210_uyvy_mask1: times 4 db 0xff, 0x03, 0xf0, 0x3f, 0x00, 0xfc, 0x0f, 0xc0
v210_uyvy_mask2: times 4 db 0xf0, 0x3f, 0x00, 0xfc, 0x0f, 0xc0, 0xff, 0x03

v210_uyvy_chroma_shuf1: times 2 db  0, 1,-1,-1, 2, 3,-1,-1, 5, 6,-1,-1, 8, 9,-1,-1
v210_uyvy_luma_shuf1:   times 2 db -1,-1, 1, 2,-1,-1, 4, 5,-1,-1, 6, 7,-1,-1, 9,10
v210_uyvy_chroma_shuf2: times 2 db  0, 1,-1,-1, 3, 4,-1,-1, 6, 7,-1,-1, 8, 9,-1,-1
v210_uyvy_luma_shuf2:   times 2 db -1,-1, 2, 3,-1,-1, 4, 5,-1,-1, 7, 8,-1,-1,10,11
v210_uyvy_chroma_shuf3: times 2 db  5, 6,-1,-1, 8, 9,-1,-1,10,11,-1,-1,13,14,-1,-1
v210_uyvy_luma_shuf3:   times 2 db -1,-1, 6, 7,-1,-1, 9,10,-1,-1,12,13,-1,-1,14,15

v210_uyvy_mult1: times 2 dw 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000
v210_uyvy_mult2: times 2 dw 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff
v210_uyvy_mult3: times 2 dw 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800, 0x7fff, 0x2000, 0x0800

SECTION .text

%macro uyvy_to_sdi 0-1

; sdi_pack_10(uint8_t *dst, const uint8_t *y, int64_t size)
cglobal uyvy_to_sdi%1, 3, 4, 5, dst, y, pixels
    lea     yq, [yq + 4*pixelsq]
    neg     pixelsq
    mova    m2, [sdi_enc_mult_10]
    mova    m3, [sdi_chroma_shuf_10]
    mova    m4, [sdi_luma_shuf_10]

.loop:
%ifidn %1, _unaligned
    movu    m0, [yq+4*pixelsq]
    pmullw  m0, m2
%else
    pmullw  m0, m2, [yq+4*pixelsq]
%endif
    pshufb  m1, m0, m3
    pshufb  m0, m4
    por     m0, m1

    movu    [dstq], xm0
%if cpuflag(avx2)
    vextracti128 [dstq+10], m0, 1
%endif

    add     dstq, (mmsize*5)/8
    add     pixelsq, mmsize/4
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
uyvy_to_sdi _aligned
uyvy_to_sdi _unaligned
INIT_XMM avx
uyvy_to_sdi
INIT_YMM avx2
uyvy_to_sdi

%macro sdi_blank 0

; sdi_blank(uint16_t *dst, int64_t size)
cglobal sdi_blank, 2, 2, 1, dst, pixels
    shl     pixelsq, 2
    add     dstq, pixelsq
    neg     pixelsq

    mova    m0, [sdi_blank]

.loop:
    movu    [dstq+pixelsq], m0

    add     pixelsq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM avx
sdi_blank

%macro planar_to_uyvy_8 1

%ifidn %1, aligned
    %define move mova
%else
    %define move movu
%endif

; planar_to_uyvy_8(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, const int64_t width)
cglobal planar_to_uyvy_8_%1, 5, 5, 8+3*ARCH_X86_64, dst, y, u, v, pixels
    shr       pixelsq, 1
    lea       yq, [yq+2*pixelsq]
    lea       dstq, [dstq+8*pixelsq]
    add       uq, pixelsq
    add       vq, pixelsq
    neg       pixelsq

%if ARCH_X86_64
    pxor      m10, m10

    mova      m8, [uyvy_enc_min_8]
    mova      m9, [uyvy_enc_max_8]
%else
    %define m8  [uyvy_enc_min_8]
    %define m9  [uyvy_enc_max_8]
    %define m10 [pb_0]
%endif ; ARCH_X86_64

.loop:
%if notcpuflag(avx2)
    move      m0, [yq+2*pixelsq]
    move      m1, [yq+2*pixelsq+mmsize]
    move      m2, [uq+pixelsq]
    move      m3, [vq+pixelsq]
%else
    move        xm0, [yq+2*pixelsq]
    move        xm1, [yq+2*pixelsq+16]
    move        xm2, [uq+pixelsq]
    move        xm3, [vq+pixelsq]
    vinserti128  m0, m0, [yq + 2*pixelsq + 32], 1
    vinserti128  m1, m1, [yq + 2*pixelsq + 48], 1
    vinserti128  m2, m2, [uq +   pixelsq + 16], 1
    vinserti128  m3, m3, [vq +   pixelsq + 16], 1
%endif

    CLIPUB    m0, m8, m9
    CLIPUB    m1, m8, m9
    CLIPUB    m2, m8, m9
    CLIPUB    m3, m8, m9

    punpckhbw m5, m2, m3
    punpcklbw m4, m2, m3
    punpckhbw m2, m4, m0
%if cpuflag(avx)
    punpcklbw m0, m4, m0
%else
    punpcklbw m4, m0
    SWAP       0,  4
%endif
    punpckhbw m6, m5, m1
    punpcklbw m4, m5, m1

    punpckhbw m1, m0, m10
    psllw     m1, 2
    punpcklbw m0, m10
    psllw     m0, 2
    punpckhbw m3, m2, m10
    psllw     m3, 2
    punpcklbw m2, m10
    psllw     m2, 2
    punpckhbw m5, m4, m10
    psllw     m5, 2
    punpcklbw m4, m10
    psllw     m4, 2
    punpckhbw m7, m6, m10
    psllw     m7, 2
    punpcklbw m6, m10
    psllw     m6, 2

%if notcpuflag(avx2)
    move      [dstq+8*pixelsq+0*mmsize], m0
    move      [dstq+8*pixelsq+1*mmsize], m1
    move      [dstq+8*pixelsq+2*mmsize], m2
    move      [dstq+8*pixelsq+3*mmsize], m3
    move      [dstq+8*pixelsq+4*mmsize], m4
    move      [dstq+8*pixelsq+5*mmsize], m5
    move      [dstq+8*pixelsq+6*mmsize], m6
    move      [dstq+8*pixelsq+7*mmsize], m7
%else
    move         [dstq + 8*pixelsq +  0*16], xm0
    move         [dstq + 8*pixelsq +  1*16], xm1
    move         [dstq + 8*pixelsq +  2*16], xm2
    move         [dstq + 8*pixelsq +  3*16], xm3
    move         [dstq + 8*pixelsq +  4*16], xm4
    move         [dstq + 8*pixelsq +  5*16], xm5
    move         [dstq + 8*pixelsq +  6*16], xm6
    move         [dstq + 8*pixelsq +  7*16], xm7
    vextracti128 [dstq + 8*pixelsq +  8*16],  m0, 1
    vextracti128 [dstq + 8*pixelsq +  9*16],  m1, 1
    vextracti128 [dstq + 8*pixelsq + 10*16],  m2, 1
    vextracti128 [dstq + 8*pixelsq + 11*16],  m3, 1
    vextracti128 [dstq + 8*pixelsq + 12*16],  m4, 1
    vextracti128 [dstq + 8*pixelsq + 13*16],  m5, 1
    vextracti128 [dstq + 8*pixelsq + 14*16],  m6, 1
    vextracti128 [dstq + 8*pixelsq + 15*16],  m7, 1
%endif

    add       pixelsq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM sse2
planar_to_uyvy_8 aligned
planar_to_uyvy_8 unaligned
INIT_XMM avx
planar_to_uyvy_8 aligned
planar_to_uyvy_8 unaligned
INIT_YMM avx2
planar_to_uyvy_8 aligned
planar_to_uyvy_8 unaligned

%macro planar_to_uyvy_10 1

%ifidn %1, aligned
    %define move mova
%else
    %define move movu
%endif

; planar_to_uyvy_10(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const int64_t width)
cglobal planar_to_uyvy_10_%1, 5, 5, 8+2*ARCH_X86_64, dst, y, u, v, pixels
    lea       yq, [yq+2*pixelsq]
    lea       dstq, [dstq+4*pixelsq]
    add       uq, pixelsq
    add       vq, pixelsq
    neg       pixelsq

%if ARCH_X86_64
    mova      m8, [uyvy_enc_min_10]
    mova      m9, [uyvy_enc_max_10]
%else
    %define m8  [uyvy_enc_min_10]
    %define m9  [uyvy_enc_max_10]
%endif ; ARCH_X86_64

.loop:
%if notcpuflag(avx2)
    move      m0, [yq+2*pixelsq]
    move      m1, [yq+2*pixelsq+mmsize]
    move      m2, [uq+pixelsq]
    move      m3, [vq+pixelsq]
%else
    move        xm0, [yq+2*pixelsq]
    move        xm1, [yq+2*pixelsq+16]
    move        xm2, [uq+pixelsq]
    move        xm3, [vq+pixelsq]
    vinserti128  m0, m0, [yq + 2*pixelsq + 32], 1
    vinserti128  m1, m1, [yq + 2*pixelsq + 48], 1
    vinserti128  m2, m2, [uq +   pixelsq + 16], 1
    vinserti128  m3, m3, [vq +   pixelsq + 16], 1
%endif

    CLIPW     m0, m8, m9
    CLIPW     m1, m8, m9
    CLIPW     m2, m8, m9
    CLIPW     m3, m8, m9

    punpcklwd m4, m2, m3
    punpckhwd m5, m2, m3
    punpcklwd m7, m4, m0
    punpcklwd m6, m5, m1
    punpckhwd m4, m0
    punpckhwd m5, m1

%if notcpuflag(avx2)
    move      [dstq+4*pixelsq+0*mmsize], m7
    move      [dstq+4*pixelsq+1*mmsize], m4
    move      [dstq+4*pixelsq+2*mmsize], m6
    move      [dstq+4*pixelsq+3*mmsize], m5
%else
    move         [dstq + 4*pixelsq +   0], xm7
    move         [dstq + 4*pixelsq +  16], xm4
    move         [dstq + 4*pixelsq +  32], xm6
    move         [dstq + 4*pixelsq +  48], xm5
    vextracti128 [dstq + 4*pixelsq +  64], m7, 1
    vextracti128 [dstq + 4*pixelsq +  80], m4, 1
    vextracti128 [dstq + 4*pixelsq +  96], m6, 1
    vextracti128 [dstq + 4*pixelsq + 112], m5, 1
%endif

    add       pixelsq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM sse2
planar_to_uyvy_10 aligned
planar_to_uyvy_10 unaligned
INIT_XMM avx
planar_to_uyvy_10 aligned
planar_to_uyvy_10 unaligned
INIT_YMM avx2
planar_to_uyvy_10 aligned
planar_to_uyvy_10 unaligned

%macro v210_to_uyvy 1

%ifidn %1, aligned
    %define move mova
%else
    %define move movu
%endif

; v210_uyvy_unpack(const uint32_t *src, uint16_t *uyvy, int64_t width)
cglobal v210_to_uyvy_%1, 3, 3, 8+7*ARCH_X86_64, src, dst, pixels
    shl    pixelsq, 2
    add    dstq,    pixelsq
    neg    pixelsq

    mova  m4, [v210_uyvy_mask1]
    mova  m5, [v210_uyvy_mask2]
    mova  m6, [v210_uyvy_luma_shuf1]
    mova  m7, [v210_uyvy_chroma_shuf1]
%if ARCH_X86_64
    mova  m8, [v210_uyvy_luma_shuf2]
    mova  m9, [v210_uyvy_chroma_shuf2]
    mova m10, [v210_uyvy_luma_shuf3]
    mova m11, [v210_uyvy_chroma_shuf3]
    mova m12, [v210_uyvy_mult1]
    mova m13, [v210_uyvy_mult2]
    mova m14, [v210_uyvy_mult3]
%else
    %define  m8 [v210_uyvy_luma_shuf2]
    %define  m9 [v210_uyvy_chroma_shuf2]
    %define m10 [v210_uyvy_luma_shuf3]
    %define m11 [v210_uyvy_chroma_shuf3]
    %define m12 [v210_uyvy_mult1]
    %define m13 [v210_uyvy_mult2]
    %define m14 [v210_uyvy_mult3]
%endif ; ARCH_X86_64

.loop:
    move   xm0, [srcq]
    move   xm2, [srcq + 16]
%if cpuflag(avx2)
    vinserti128 m0, m0, [srcq + 32], 1
    vinserti128 m2, m2, [srcq + 48], 1
%endif
    palignr  m1, m2, m0, 10

    pandn    m3, m4, m0
    pand     m0, m4
    pshufb   m3, m6
    pshufb   m0, m7
    por      m0, m3
    pmulhrsw m0, m12
    move [dstq + pixelsq], xm0

%if cpuflag(avx2)
    vextracti128 [dstq + pixelsq + 3*16], m0, 1
%endif

    pandn    m3, m5, m1
    pand     m1, m5
    pshufb   m3, m8
    pshufb   m1, m9
    por      m1, m3
    pmulhrsw m1, m13
    move [dstq + pixelsq + 16], xm1

%if cpuflag(avx2)
    vextracti128 [dstq + pixelsq + 4*16], m1, 1
%endif

    pandn    m3, m4, m2
    pand     m2, m4
    pshufb   m3, m10
    pshufb   m2, m11
    por      m2, m3
    pmulhrsw m2, m14
    move [dstq + pixelsq + 2*16], xm2

%if cpuflag(avx2)
    vextracti128 [dstq + pixelsq + 5*16], m2, 1
%endif

    add srcq, 2*mmsize
    add pixelsq, 3*mmsize
    jl  .loop

    REP_RET
%endmacro

INIT_XMM ssse3
v210_to_uyvy unaligned
INIT_XMM avx
v210_to_uyvy unaligned
INIT_YMM avx2
v210_to_uyvy unaligned

INIT_XMM ssse3
v210_to_uyvy aligned
INIT_XMM avx
v210_to_uyvy aligned
INIT_YMM avx2
v210_to_uyvy aligned

%macro planar_to_sdi_8 0

; planar_to_sdi_8(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width)
cglobal planar_to_sdi_8, 5, 5, 3, y, u, v, l, pixels
    shr    pixelsq, 1
    lea    yq, [yq + 2*pixelsq]
    add    uq, pixelsq
    add    vq, pixelsq

    neg    pixelsq

.loop:
    movq   xm0, [yq + pixelsq*2]
    movd   xm1, [uq + pixelsq*1]
    movd   xm2, [vq + pixelsq*1]
%if cpuflag(avx2)
    vinserti128 m0, m0, [yq + pixelsq*2 + 6], 1
    vinserti128 m1, m1, [uq + pixelsq*1 + 3], 1
    vinserti128 m2, m2, [vq + pixelsq*1 + 3], 1
%endif

    pshufb m0, [planar_8_y_shuf]
    pmullw m0, [planar_8_y_mult]
    pshufb m0, [planar_8_y_shuf_after]

    pshufb m1, [planar_8_u_shuf]

    por    m0, m1

    pshufb m2, [planar_8_v_shuf]
    psllw  m2, 4
    pshufb m2, [planar_8_v_shuf_after]

    por    m0, m2

    movu   [lq], xm0
%if cpuflag(avx2)
    vextracti128 [lq+15], m0, 1
%endif

    add    lq, (15*mmsize)/16
    add    pixelsq, (3*mmsize)/16
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
planar_to_sdi_8
INIT_XMM avx
planar_to_sdi_8
INIT_YMM avx2
planar_to_sdi_8

%macro planar_to_sdi_10 0

; planar_to_sdi_10(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width)
cglobal planar_to_sdi_10, 5, 5, 2+cpuflag(avx2), y, u, v, l, pixels, size
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

    pmullw m0, [planar_10_y_shift]
    pmullw m1, [planar_10_uv_shift]

    pshufb m0, [planar_10_y_shuf]
    pshufb m1, [planar_10_uv_shuf]

    por    m0, m1

    movu   [lq], xm0
%if cpuflag(avx2)
    vextracti128 [lq+15], m0, 1
%endif

    add    lq, (15*mmsize)/16
    add    pixelsq, (6*mmsize)/16
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
planar_to_sdi_10
INIT_XMM avx
planar_to_sdi_10
INIT_YMM avx2
planar_to_sdi_10

%macro planar_10_to_planar_8 0

; planar_10_to_planar_8(const uint16_t *y, const uint8_t *y8, const int64_t width)
cglobal planar_10_to_planar_8, 3, 3, 2+cpuflag(avx2), y, y8, samples
    lea      yq, [yq + 2*samplesq]
    add      y8q, samplesq

    neg      samplesq

.loop:
    mova     m0, [yq + samplesq*2 + 0*mmsize]
    mova     m1, [yq + samplesq*2 + 1*mmsize]
%if cpuflag(avx2)
    SBUTTERFLY dqqq, 0, 1, 2
%endif

    psrlw    m0, 2
    psrlw    m1, 2

    packuswb m0, m1

    mova     [y8q + samplesq], m0

    add      samplesq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM sse2
planar_10_to_planar_8
INIT_YMM avx2
planar_10_to_planar_8

%macro planar8_to_planar10 0

; planar_10_to_planar_8(const uint16_t *y, const uint8_t *y8, const int64_t width)
cglobal planar8_to_planar10, 3, 3, 2 + notcpuflag(sse4), y, y8, samples
    lea      yq, [yq + 2*samplesq]
    add      y8q, samplesq

    neg      samplesq

%if notcpuflag(sse4)
    pxor m2, m2
%endif

.loop:
%if cpuflag(sse4)
        pmovzxbw m0, [y8q + samplesq]
%else
        movh m0, [y8q + samplesq]
        punpcklbw m0, m2
%endif

        psllw m1, m0, 2
        psrlw m0, 6
        por m0, m1

        mova [yq + 2*samplesq], m0

        add      samplesq, mmsize/2
    jl .loop
RET

%endmacro

INIT_XMM sse2
planar8_to_planar10
INIT_YMM avx2
planar8_to_planar10
