%use smartalign
ALIGNMODE p6

%include "x86inc.asm"

SECTION_RODATA 16

blit10_threshold_splat: times 8 db 0, 1
blit10_alpha_shuffle: db 0,1, 4,5, 8,9, 12,13
    times 8 db -1
blit8_alpha_shuffle: db 0, 2, 4, 6, 8, 10, 12, 14
    times 8 db -1

SECTION .text

; Every kernel computes dst = alpha > threshold ? src : bg.  The background is a
; separate pointer so that dst can be a freshly allocated picture; callers
; blending in place pass bg == dst, which makes the write of the background
; samples a no-op.

%macro SCALAR_TAIL_B 3-4 1 ; loop counter, temp1, temp2, optional subsampling
    .loop_scalar:

        movzx %2d, byte [alpha_q + %4 * %1q] ; load alpha sample
        movzx %3d, byte [bg_q + %1q]         ; load background sample
        cmp %2d, threshold_d                 ; compare with threshold
        jle .no_copy                         ; jump if less than or equal
            movzx %3d, byte [src_q + %1q]    ; load source sample instead
        .no_copy:
        mov [dst_q + %1q], %3b               ; write to destination
        add %1q, 1                           ; increment
        cmp hsize_q, %1q                     ; compare
    jne .loop_scalar                         ; jump if not equal
%endmacro

%macro SCALAR_TAIL_W 3-4 1 ; loop counter, temp1, temp2, optional subsampling
    .loop_scalar:
        movzx %2d, word [alpha_q + %4 * 2*%1q]
        movzx %3d, word [bg_q + 2*%1q]
        cmp %2d, threshold_d
        jle .no_copy
            movzx %3d, word [src_q + 2*%1q]
        .no_copy:
        mov [dst_q + 2*%1q], %3w
        add %1q, 1
        cmp hsize_q, %1q
    jne .loop_scalar
%endmacro

INIT_XMM ssse3

%define xor_reg m0
%define threshold m1
%define bg m2
%define src m3
%define alp m4

cglobal blit8_threshold_hsub1, 6, 9, 6, dst_, bg_, src_, alpha_, hsize_, threshold_
    mov r6d, 0x01010101 * 0x80 ; Splat 0x80 to every byte
    pxor m5, m5
    movd threshold, threshold_d
    movd xor_reg, r6d
    pshufb threshold, m5 ; Splat threshold to every byte
    pshufd xor_reg, xor_reg, 0 ; Splat 0x80 to every byte
    mov r6d, mmsize
    pxor threshold, xor_reg ; Packed compare bytes is for signed bytes so need to do a trick
    ALIGN 16
    .loop_simd:
        movu bg, [bg_q + r6q - mmsize]
        movu src, [src_q + r6q - mmsize]
        movu alp, [alpha_q + r6q - mmsize]
        mov r7q, r6q
        pxor alp, xor_reg ; Packed compare bytes is for signed bytes so need to do a trick
        pcmpgtb alp, threshold
        mova m5, alp
        pand src, alp
        pandn m5, bg
        por m5, src
        movu [dst_q + r6q - mmsize], m5
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_B r7, r6, r8
    .ret:
RET

%define xor_reg m0
%define threshold m1
%define shuffle m2
%define bg m3
%define src m4
%define alp m5

cglobal blit8_threshold_hsub2, 6, 9, 7, dst_, bg_, src_, alpha_, hsize_, threshold_
    mov r6d, 0x01010101 * 0x80 ; Splat 0x80 to every byte
    pxor m5, m5
    movd threshold, threshold_d
    movd xor_reg, r6d
    movq shuffle, [blit8_alpha_shuffle]
    pshufb threshold, m5 ; Splat threshold to every byte
    pshufd xor_reg, xor_reg, 0 ; Splat 0x80 to every byte
    mov r6d, mmsize
    pxor threshold, xor_reg ; Packed compare bytes is for signed bytes so need to do a trick
    ALIGN 16
    .loop_simd:
        movu bg, [bg_q + r6q - mmsize]
        movu src, [src_q + r6q - mmsize]
        movu m5, [alpha_q + 2*r6q - 2*mmsize]
        movu m6, [alpha_q + 2*r6q - 1*mmsize]
        ; Need to compress every second byte
        pshufb m5, shuffle
        pshufb m6, shuffle
        punpcklqdq alp, m5, m6
        mov r7q, r6q
        pxor alp, xor_reg ; Packed compare bytes is for signed bytes so need to do a trick
        pcmpgtb alp, threshold
        mova m6, alp
        pand src, alp
        pandn m6, bg
        por m6, src
        movu [dst_q + r6q - mmsize], m6
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_B r7, r6, r8, 2
    .ret:
RET

%define threshold m0
%define bg m1
%define src m2
%define alp m3

cglobal blit10_threshold_hsub1, 6, 9, 6, dst_, bg_, src_, alpha_, hsize_, threshold_
    movd threshold, threshold_d
    mov r6d, mmsize/2
    pshufb threshold, [blit10_threshold_splat] ; Splat threshold to every word
    ALIGN 16
    .loop_simd:
        movu bg, [bg_q + 2*r6q - mmsize]
        movu src, [src_q + 2*r6q - mmsize]
        movu alp, [alpha_q + 2*r6q - mmsize]
        mov r7q, r6q
        pcmpgtw alp, threshold
        mova m5, alp
        pand src, alp
        pandn m5, bg
        por m5, src
        movu [dst_q + 2*r6q - mmsize], m5
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_W r7, r6, r8
    .ret:
RET

%define shuffle m0
%define threshold m1
%define bg m2
%define src m3
%define alp m4

cglobal blit10_threshold_hsub2, 6, 9, 6, dst_, bg_, src_, alpha_, hsize_, threshold_
    movd threshold, threshold_d
    mov r6d, mmsize/2
    pshufb threshold, [blit10_threshold_splat] ; Splat threshold to every word
    movq shuffle, [blit10_alpha_shuffle] ; Load shuffle constants to compress words
    ALIGN 16
    .loop_simd:
        movu bg, [bg_q + 2*r6q - mmsize]
        movu src, [src_q + 2*r6q - mmsize]
        movu m4, [alpha_q + 4*r6q - 2*mmsize]
        movu m5, [alpha_q + 4*r6q - 1*mmsize]
        mov r7q, r6q
        ; Need to compress every second word
        pshufb m4, shuffle
        pshufb m5, shuffle
        punpcklqdq alp, m4, m5
        pcmpgtw alp, threshold
        mova m5, alp
        pand src, alp
        pandn m5, bg
        por m5, src
        movu [dst_q + 2*r6q - mmsize], m5
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_W r7, r6, r8, 2
    .ret:
RET

INIT_YMM avx2

%define xor_reg m0
%define threshold m1
%define bg m2
%define src m3
%define alp m4

cglobal blit8_threshold_hsub1, 6, 9, 5, dst_, bg_, src_, alpha_, hsize_, threshold_
    mov  r6d, 0x01010101 * 0x80 ; Splat 0x80 to every byte
    movd xmm %+ threshold, threshold_d
    movd xmm %+ xor_reg, r6d
    vpbroadcastb threshold, xmm %+ threshold ; Broadcast threshold to every byte
    vpbroadcastd xor_reg, xmm %+ xor_reg ; Broadcast 0x80 to every byte
    mov r6d, mmsize
    pxor threshold, xor_reg ; Packed compare bytes is for signed bytes so need to do a trick
    ALIGN 16
    .loop_simd:
        pxor alp, xor_reg, [alpha_q + r6q - mmsize] ; Packed compare bytes is for signed bytes so need to do a trick
        mov r7q, r6q
        pcmpgtb alp, threshold
        pand src, alp, [src_q + r6q - mmsize]
        pandn bg, alp, [bg_q + r6q - mmsize]
        por bg, src
        movu [dst_q + r6q - mmsize], bg
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_B r7, r6, r8
    .ret:
RET

%define xor_reg m0
%define threshold m1
%define shuffle m2
%define bg m3
%define src m4
%define alp m5

cglobal blit8_threshold_hsub2, 6, 9, 7, dst_, bg_, src_, alpha_, hsize_, threshold_
    mov r6d, 0x01010101 * 0x80 ; Splat 0x80 to every byte
    movd xmm %+ threshold, threshold_d
    VBROADCASTI128 shuffle, [blit8_alpha_shuffle]
    movd xmm %+ xor_reg, r6d
    vpbroadcastb threshold, xmm %+ threshold ; Broadcast threshold to every byte
    vpbroadcastd xor_reg, xmm %+ xor_reg ; Broadcast 0x80 to every byte
    mov r6d, mmsize
    pxor threshold, xor_reg ; Packed compare bytes is for signed bytes so need to do a trick
    ALIGN 16
    .loop_simd:
        movu m5, [alpha_q + 2*r6q - 2*mmsize]
        movu m6, [alpha_q + 2*r6q - 1*mmsize]
        mov r7q, r6q
        ; Need to compress every second byte
        pshufb m5, shuffle
        pshufb m6, shuffle
        punpcklqdq alp, m5, m6
        vpermq alp, alp, q3120 ; Permute to get correct order of qwords
        pxor alp, xor_reg ; Packed compare bytes is for signed bytes so need to do a trick
        pcmpgtb alp, threshold
        pand src, alp, [src_q + r6q - mmsize]
        pandn bg, alp, [bg_q + r6q - mmsize]
        por bg, src
        movu [dst_q + r6q - mmsize], bg
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_B r7, r6, r8, 2
    .ret:
RET

%define threshold m0
%define bg m1
%define src m2
%define alp m3

cglobal blit10_threshold_hsub1, 6, 9, 4, dst_, bg_, src_, alpha_, hsize_, threshold_
    movd xmm %+ threshold, threshold_d
    mov r6d, mmsize/2
    vpbroadcastw threshold, xmm %+ threshold ; Broadcast threshold to every word
    ALIGN 16
    .loop_simd:
        movu alp, [alpha_q + 2*r6q - mmsize]
        mov r7q, r6q
        pcmpgtw alp, threshold
        pand src, alp, [src_q + 2*r6q - mmsize]
        pandn bg, alp, [bg_q + 2*r6q - mmsize]
        por bg, src
        movu [dst_q + 2*r6q - mmsize], bg
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_W r7, r6, r8
    .ret:
RET

%define shuffle m0
%define threshold m1
%define bg m2
%define src m3
%define alp m4

cglobal blit10_threshold_hsub2, 6, 9, 6, dst_, bg_, src_, alpha_, hsize_, threshold_
    movd xmm %+ threshold, threshold_d
    VBROADCASTI128 shuffle, [blit10_alpha_shuffle] ; Load shuffle constants to compress words
    mov r6d, mmsize/2
    vpbroadcastw threshold, xmm %+ threshold ; Broadcast threshold to every word
    ALIGN 16
    .loop_simd:
        movu m4, [alpha_q + 4*r6q - 2*mmsize]
        movu m5, [alpha_q + 4*r6q - 1*mmsize]
        mov r7q, r6q
        ; Need to compress every second word
        pshufb m4, shuffle
        pshufb m5, shuffle
        punpcklqdq alp, m4, m5
        vpermq alp, alp, q3120 ; Permute to get correct order of qwords
        pcmpgtw alp, threshold
        pand src, alp, [src_q + 2*r6q - mmsize]
        pandn bg, alp, [bg_q + 2*r6q - mmsize]
        por bg, src
        movu [dst_q + 2*r6q - mmsize], bg
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_W r7, r6, r8, 2
    .ret:
RET
