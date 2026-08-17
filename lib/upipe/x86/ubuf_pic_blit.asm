%use smartalign
ALIGNMODE p6

%include "x86inc.asm"

SECTION_RODATA 16

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

%macro SCALAR_TAIL_BLEND_B 4-5 1 ; counter, temp1, temp2, temp3, optional subsampling
    .loop_scalar:
        movzx %2d, byte [alpha_q + %5 * %1q] ; load alpha sample
        movzx %3d, byte [bg_q + %1q]         ; load background sample
        movzx %4d, byte [src_q + %1q]        ; load source sample
        imul %4d, %2d                        ; src * a
        neg %2d
        add %2d, 0xff                        ; 0xff - a
        imul %3d, %2d                        ; bg * (0xff - a)
        add %4d, %3d                         ; the numerator
        mov %2d, %4d
        shr %2d, 8
        add %4d, %2d                         ; N + (N >> 8)
        add %4d, 1
        shr %4d, 8                          ; / 0xff
        mov [dst_q + %1q], %4b
        add %1q, 1
        cmp hsize_q, %1q
    jne .loop_scalar
%endmacro

; The blend kernels compute, per 10 bit sample,
;
;     dst = (bg * (0x3ff - a) + src * a) / 0x3ff
;
; matching the C reference bit for bit.  Two things make that cheap.
;
; The numerator is a dot product of (bg, src) with (0x3ff - a, a), which is what
; pmaddwd does: it multiplies signed 16 bit pairs and sums each adjacent pair
; into 32 bits.  Every input is below 0x400, so the products and their sum stay
; well inside a signed 32 bit lane, and the results are below 0x400 again, so
; the pack back to words never saturates.
;
; The division has an exact shift form over the range the numerator can take,
; 0 to 0x3ff * 0x3ff.  Writing N = 1023q + r with r <= 1022,
;
;     (N + (N >> 10) + 1) >> 10 == q
;
; because N >> 10 is at least q - 1, so the sum reaches 1024q, and at most
; q + r/1024, so it stays below 1024(q + 1); the tightest case is r = 1022,
; which leaves 1023.998.  It truncates, like the C does.

%macro SCALAR_TAIL_BLEND_W 4-5 1 ; counter, temp1, temp2, temp3, optional subsampling
    .loop_scalar:
        movzx %2d, word [alpha_q + %5 * 2*%1q] ; load alpha sample
        movzx %3d, word [bg_q + 2*%1q]         ; load background sample
        movzx %4d, word [src_q + 2*%1q]        ; load source sample
        imul %4d, %2d                          ; src * a
        neg %2d
        add %2d, 0x3ff                         ; 0x3ff - a
        imul %3d, %2d                          ; bg * (0x3ff - a)
        add %4d, %3d                           ; the numerator
        mov %2d, %4d
        shr %2d, 10
        add %4d, %2d                           ; N + (N >> 10)
        add %4d, 1
        shr %4d, 10                            ; / 0x3ff
        mov [dst_q + 2*%1q], %4w
        add %1q, 1
        cmp hsize_q, %1q
    jne .loop_scalar
%endmacro

%macro BLEND10_CONSTANTS 0
    pcmpeqw m0, m0
    psrlw m0, 6             ; 0x3ff in every word
    pcmpeqd m1, m1
    psrld m1, 31            ; 1 in every dword
%endmacro

; Takes bg in m2, src in m3 and the already expanded alpha in m4, leaves the
; finished samples in m6 and clobbers m2, m5 and m7.
%macro BLEND10 0
    psubw m5, m0, m4        ; 0x3ff - a
    punpcklwd m6, m2, m3    ; (bg, src) pairs, low half
    punpckhwd m2, m2, m3    ; and high half
    punpcklwd m7, m5, m4    ; the matching (0x3ff - a, a) pairs
    punpckhwd m5, m5, m4
    pmaddwd m6, m7          ; bg * (0x3ff - a) + src * a
    pmaddwd m2, m5
    psrld m7, m6, 10
    paddd m6, m7
    paddd m6, m1
    psrld m6, 10            ; / 0x3ff
    psrld m5, m2, 10
    paddd m2, m5
    paddd m2, m1
    psrld m2, 10
    packssdw m6, m2         ; back to words
%endmacro

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

%define zero m0
%define pw1 m1
%define pw255 m2

cglobal blit8_blend_hsub1, 5, 10, 11, dst_, bg_, src_, alpha_, hsize_
    pxor zero, zero
    pcmpeqw pw255, pw255
    mov r6d, mmsize
    psrlw pw1, pw255, 15
    psrlw pw255, 8
    ALIGN 16
    .loop_simd:
        ; Load and unpack alpha
        movu m3, [alpha_q + r6q - mmsize]
        punpckhbw m4, m3, zero
        punpcklbw m3, zero
        mov r7q, r6q
        ; Load and unpack src
        movu m5, [src_q + r6q - mmsize]
        punpckhbw m6, m5, zero
        punpcklbw m5, zero
        ; 255-a
        psubw m8, pw255, m4
        psubw m7, pw255, m3
        ; Load and unpack bg
        movu m9, [bg_q + r6q - mmsize]
        punpckhbw m10, m9, zero
        punpcklbw m9, zero
        ; src*a
        pmullw m6, m4
        pmullw m5, m3
        ; bg*(255-a)
        pmullw m10, m8
        pmullw m9, m7
        ; bg*(255-a) + src*a
        paddw m4, m10, m6
        paddw m3, m9, m5
        ; /255 -> (t + 1 + (t>>8)) >> 8
        paddw m6, m4, pw1
        paddw m5, m3, pw1
        psrlw m4, 8
        psrlw m3, 8
        paddw m6, m4
        paddw m5, m3
        psrlw m6, 8
        psrlw m5, 8
        packuswb m5, m6
        movu [dst_q + r6q - mmsize], m5
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_BLEND_B r7, r8, r9, r6
    .ret:
RET

%define shuffle m11

cglobal blit8_blend_hsub2, 5, 10, 12, dst_, bg_, src_, alpha_, hsize_
    pxor zero, zero
    pcmpeqw pw255, pw255
    mov r6d, mmsize
    psrlw pw1, pw255, 15
    psrlw pw255, 8
    VBROADCASTI128 shuffle, [blit8_alpha_shuffle]
    ALIGN 16
    .loop_simd:
        ; Load and unpack alpha
        movu m5, [alpha_q + 2*r6q - 2*mmsize]
        movu m6, [alpha_q + 2*r6q - 1*mmsize]
        ; Need to compress every second byte
        pshufb m5, shuffle
        pshufb m6, shuffle
        punpcklqdq m3, m5, m6
        vpermq m3, m3, q3120 ; Permute to get correct order of qwords
        punpckhbw m4, m3, zero
        punpcklbw m3, zero
        mov r7q, r6q
        ; Load and unpack src
        movu m5, [src_q + r6q - mmsize]
        punpckhbw m6, m5, zero
        punpcklbw m5, zero
        ; 255-a
        psubw m8, pw255, m4
        psubw m7, pw255, m3
        ; Load and unpack bg
        movu m9, [bg_q + r6q - mmsize]
        punpckhbw m10, m9, zero
        punpcklbw m9, zero
        ; src*a
        pmullw m6, m4
        pmullw m5, m3
        ; bg*(255-a)
        pmullw m10, m8
        pmullw m9, m7
        ; bg*(255-a) + src*a
        paddw m4, m10, m6
        paddw m3, m9, m5
        ; /255 -> (t + 1 + (t>>8)) >> 8
        paddw m6, m4, pw1
        paddw m5, m3, pw1
        psrlw m4, 8
        psrlw m3, 8
        paddw m6, m4
        paddw m5, m3
        psrlw m6, 8
        psrlw m5, 8
        packuswb m5, m6
        movu [dst_q + r6q - mmsize], m5
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_BLEND_B r7, r8, r9, r6, 2
    .ret:
RET

cglobal blit10_blend_hsub1, 5, 11, 8, dst_, bg_, src_, alpha_, hsize_
    BLEND10_CONSTANTS
    mov r6d, mmsize/2
    ALIGN 16
    .loop_simd:
        movu m2, [bg_q + 2*r6q - mmsize]
        movu m3, [src_q + 2*r6q - mmsize]
        movu m4, [alpha_q + 2*r6q - mmsize]
        mov r7q, r6q
        BLEND10
        movu [dst_q + 2*r6q - mmsize], m6
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_BLEND_W r7, r8, r9, r10
    .ret:
RET

cglobal blit10_blend_hsub2, 5, 11, 9, dst_, bg_, src_, alpha_, hsize_
    BLEND10_CONSTANTS
    VBROADCASTI128 m8, [blit10_alpha_shuffle] ; Load shuffle constants to compress words
    mov r6d, mmsize/2
    ALIGN 16
    .loop_simd:
        movu m2, [bg_q + 2*r6q - mmsize]
        movu m3, [src_q + 2*r6q - mmsize]
        movu m4, [alpha_q + 4*r6q - 2*mmsize]
        movu m5, [alpha_q + 4*r6q - 1*mmsize]
        mov r7q, r6q
        ; Need to compress every second word
        pshufb m4, m8
        pshufb m5, m8
        punpcklqdq m4, m4, m5
        vpermq m4, m4, q3120 ; Permute to get correct order of qwords
        BLEND10
        movu [dst_q + 2*r6q - mmsize], m6
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_BLEND_W r7, r8, r9, r10, 2
    .ret:
RET
