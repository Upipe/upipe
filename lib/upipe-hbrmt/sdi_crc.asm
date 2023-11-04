%include "x86util.asm"

SECTION_RODATA 64

mult:
    dw 16,  0, 64,  0, 1, 0, 4, 0 ; chroma
    dw  0, 16,  0, 64, 0, 1, 0, 4 ; luma

shuf_lo_even:
times 8 db -1
db 0,1, -1, 4,5, 8,9, -1

shuf_lo_odd:
times 8 db -1
db -1, 2,3, -1, 6,7, 10,11

shuf_hi_even:
db 0,1, 4,5, -1, 8,9, -1
times 8 db -1

shuf_hi_odd:
db -1, 2,3, 6,7, -1, 10,11
times 8 db -1

sdi_crc_k3_k4: dq 0x46840000, 0x95450000
sdi_crc_k5_k6: dd 0x00000000, 0x14980000, 0x00000000, 0x40000 ;  x^14+64-1 mod P, x^14-1 mod P
sdi_crc_mu dd 0x80040000, 0x5e405011, 0x14980559, 0x4c9bb5d5
sdi_crc_p  dq 0x46001, 0

SECTION .text

INIT_YMM avx2

; a, b, constant, tmp
%macro REDUCE128 4
    pclmulqdq %4, %1, %3, 0x00
    pclmulqdq     %1, %3, 0x11
    pxor      %1, %2
    pxor      %1, %4
%endmacro

%macro END_1 3 ; acc, temp, const
    pclmulqdq %2, %1, %3, 0x00
    pclmulqdq %1, %1, %3, 0x11
    pxor      %1, %2
%endmacro

%macro END_2 4 ; acc, temp1, temp2, const
    pclmulqdq %2, %1, %4, 0x01
    pclmulqdq %3, %1, %4, 0x10
    pxor      %2, %3
%endmacro

%macro END_3 3 ; acc, temp, const
    psrldq    %2, 8
    pclmulqdq %1, %3, 0x11
    pxor      %1, %2
%endmacro

cglobal compute_sdi_crc, 4, 5, 12, crcc, crcy, src, len, offset
    shl lenq, 2
    xor offsetd, offsetd
    mova           m4, [mult]
    VBROADCASTI128 m5, [shuf_lo_even]
    VBROADCASTI128 m6, [shuf_hi_even]
    VBROADCASTI128 m7, [shuf_lo_odd]
    VBROADCASTI128 m8, [shuf_hi_odd]

    mova xm9, [sdi_crc_k3_k4]
    pxor xm10, xm10 ; crc "accumulator"
    pxor xm11, xm11

    .loop:
        VBROADCASTI128 m0, [srcq+offsetq]    ; broadcast into hi and lo dqwords
        VBROADCASTI128 m1, [srcq+offsetq+16]
        VBROADCASTI128 m2, [srcq+offsetq+32]

        pmaddwd m0, m4 ; chroma | luma
        pmaddwd m1, m4 ; chroma | luma
        pmaddwd m2, m4 ; chroma | luma

        packusdw m0, m1
        packusdw m2, m2
        palignr  m2, m2, m0, 12 ; prepend last 4 bytes of m0 onto m2

        pshufb m3, m0, m5
        pshufb     m0, m7
        pshufb m1, m2, m6
        pshufb     m2, m8

        por m0, m3
        por m1, m2

        palignr m0, m1, m0, 8 ; data is in center 120 bits

        vextracti128 xm1, ym0, 1
        REDUCE128 xm10, xm0, xm9, xm2
        REDUCE128 xm11, xm1, xm9, xm3

        add offsetq, 48
        cmp offsetq, lenq
    jl .loop

    mova xm4, [sdi_crc_k5_k6]
    mova xm5, [sdi_crc_mu]
    mova xm6, [sdi_crc_p]

    END_1 xm10, xm0, xm4
    END_1 xm11, xm2, xm4

    END_2 xm10, xm0, xm1, xm5
    END_2 xm11, xm2, xm3, xm5
    END_3 xm10, xm0, xm5
    END_3 xm11, xm2, xm5

    pclmulqdq  xm10, xm6, 0x00
    pclmulqdq  xm11, xm6, 0x00

    movd      [crccq], xm10
    movd      [crcyq], xm11
RET
