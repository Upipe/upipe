%use smartalign
ALIGNMODE p6

%include "x86inc.asm"

SECTION_RODATA 16

blit10_alpha_shuffle: db 0,1, 4,5, 8,9, 12,13
    times 8 db -1
blit8_alpha_shuffle: db 0, 2, 4, 6, 8, 10, 12, 14
    times 8 db -1
; vpermt2w indices selecting the even words of two concatenated zmm registers
blit10_alpha_permute:
    dw  0,  2,  4,  6,  8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30
    dw 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62
blit8_blend_bias: dw 0x7f81
blit8_blend_101: dw 0x101
blit8_blend_80: db 0x80

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

; In place, bg == dst: a vector with no sample over the threshold is neither
; loaded nor stored, which leaves the cache lines of transparent areas clean.
; The scalar tails read the background from the destination.

cglobal blit8_threshold_inplace_hsub1, 5, 9, 5, dst_, src_, alpha_, hsize_, threshold_
    %define bg_q dst_q
    mov r6d, 0x01010101 * 0x80
    movd xm1, threshold_d
    movd xm0, r6d
    vpbroadcastb m1, xm1
    vpbroadcastd m0, xm0
    mov r6d, mmsize
    pxor m1, m0              ; signed compare, as above
    ALIGN 16
    .loop_simd:
        pxor m4, m0, [alpha_q + r6q - mmsize]
        mov r7q, r6q
        pcmpgtb m4, m1
        ptest m4, m4
        jz .next
        pand m3, m4, [src_q + r6q - mmsize]
        pandn m2, m4, [dst_q + r6q - mmsize]
        por m2, m3
        movu [dst_q + r6q - mmsize], m2
    .next:
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_B r7, r6, r8
    .ret:
RET

cglobal blit8_threshold_inplace_hsub2, 5, 9, 7, dst_, src_, alpha_, hsize_, threshold_
    %define bg_q dst_q
    mov r6d, 0x01010101 * 0x80
    movd xm1, threshold_d
    VBROADCASTI128 m5, [blit8_alpha_shuffle]
    movd xm0, r6d
    vpbroadcastb m1, xm1
    vpbroadcastd m0, xm0
    mov r6d, mmsize
    pxor m1, m0
    ALIGN 16
    .loop_simd:
        movu m4, [alpha_q + 2*r6q - 2*mmsize]
        movu m6, [alpha_q + 2*r6q - 1*mmsize]
        mov r7q, r6q
        pshufb m4, m5
        pshufb m6, m5
        punpcklqdq m4, m4, m6
        vpermq m4, m4, q3120
        pxor m4, m0
        pcmpgtb m4, m1
        ptest m4, m4
        jz .next
        pand m3, m4, [src_q + r6q - mmsize]
        pandn m2, m4, [dst_q + r6q - mmsize]
        por m2, m3
        movu [dst_q + r6q - mmsize], m2
    .next:
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_B r7, r6, r8, 2
    .ret:
RET

cglobal blit10_threshold_inplace_hsub1, 5, 9, 4, dst_, src_, alpha_, hsize_, threshold_
    %define bg_q dst_q
    movd xm0, threshold_d
    mov r6d, mmsize/2
    vpbroadcastw m0, xm0
    ALIGN 16
    .loop_simd:
        movu m3, [alpha_q + 2*r6q - mmsize]
        mov r7q, r6q
        pcmpgtw m3, m0
        ptest m3, m3
        jz .next
        pand m2, m3, [src_q + 2*r6q - mmsize]
        pandn m1, m3, [dst_q + 2*r6q - mmsize]
        por m1, m2
        movu [dst_q + 2*r6q - mmsize], m1
    .next:
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_W r7, r6, r8
    .ret:
RET

cglobal blit10_threshold_inplace_hsub2, 5, 9, 6, dst_, src_, alpha_, hsize_, threshold_
    %define bg_q dst_q
    movd xm0, threshold_d
    VBROADCASTI128 m5, [blit10_alpha_shuffle]
    mov r6d, mmsize/2
    vpbroadcastw m0, xm0
    ALIGN 16
    .loop_simd:
        movu m3, [alpha_q + 4*r6q - 2*mmsize]
        movu m4, [alpha_q + 4*r6q - 1*mmsize]
        mov r7q, r6q
        pshufb m3, m5
        pshufb m4, m5
        punpcklqdq m3, m3, m4
        vpermq m3, m3, q3120
        pcmpgtw m3, m0
        ptest m3, m3
        jz .next
        pand m2, m3, [src_q + 2*r6q - mmsize]
        pandn m1, m3, [dst_q + 2*r6q - mmsize]
        por m1, m2
        movu [dst_q + 2*r6q - mmsize], m1
    .next:
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_W r7, r6, r8, 2
    .ret:
RET

%undef bg_q

; The 8 bit blend kernels compute dst = (bg * (0xff - a) + src * a) / 0xff,
; matching the C bit for bit, with one multiply-add per pair of samples.
;
; pmaddubsw multiplies unsigned bytes by signed bytes and sums adjacent pairs:
; with the weights (a, 0xff - a) unsigned and the samples flipped to signed,
; (src - 0x80, bg - 0x80), it gives N - 0x7f80 for the numerator N, which
; lies between -0x7f80 and 0x7e81, so the sum never saturates.  Adding 0x7f81
; leaves N + 1 as an unsigned word, and the division is then one pmulhuw:
; (N + 1) * 0x101 >> 16 == N / 0xff for every N up to 0xff * 0xff (checked
; over all of them).

%define pb_80   m0
%define pw_bias m1
%define pw_101  m2
%define pb_ff   m10

%macro BLEND8_CONSTANTS 0
    vpbroadcastb pb_80, [blit8_blend_80]
    vpbroadcastw pw_bias, [blit8_blend_bias]
    vpbroadcastw pw_101, [blit8_blend_101]
    pcmpeqb pb_ff, pb_ff
%endmacro

; Takes alpha in m3, loads src and the background (at %1, bg_q by default) at
; r6q, leaves the finished samples in m3 and clobbers m4 to m8.
%macro BLEND8 0-1 bg_q
    pxor      m4, m3, pb_ff     ; 0xff - a
    punpckhbw m5, m3, m4        ; (a, 0xff - a) pairs, high half
    punpcklbw m3, m4            ; and low half
    movu      m6, [src_q + r6q - mmsize]
    movu      m7, [%1 + r6q - mmsize]
    pxor      m6, pb_80         ; src - 0x80
    pxor      m7, pb_80         ; bg - 0x80
    punpckhbw m8, m6, m7        ; the matching (src, bg) pairs
    punpcklbw m6, m7
    pmaddubsw m3, m6            ; N - 0x7f80
    pmaddubsw m5, m8
    paddw     m3, pw_bias       ; N + 1
    paddw     m5, pw_bias
    pmulhuw   m3, pw_101        ; / 0xff
    pmulhuw   m5, pw_101
    packuswb  m3, m5
%endmacro

cglobal blit8_blend_hsub1, 5, 10, 11, dst_, bg_, src_, alpha_, hsize_
    BLEND8_CONSTANTS
    mov r6d, mmsize
    ALIGN 16
    .loop_simd:
        movu m3, [alpha_q + r6q - mmsize]
        mov r7q, r6q
        BLEND8
        movu [dst_q + r6q - mmsize], m3
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
    BLEND8_CONSTANTS
    VBROADCASTI128 shuffle, [blit8_alpha_shuffle]
    mov r6d, mmsize
    ALIGN 16
    .loop_simd:
        ; Load alpha and compress every second byte
        movu m5, [alpha_q + 2*r6q - 2*mmsize]
        movu m6, [alpha_q + 2*r6q - 1*mmsize]
        pshufb m5, shuffle
        pshufb m6, shuffle
        punpcklqdq m3, m5, m6
        vpermq m3, m3, q3120 ; Permute to get correct order of qwords
        mov r7q, r6q
        BLEND8
        movu [dst_q + r6q - mmsize], m3
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_BLEND_B r7, r8, r9, r6, 2
    .ret:
RET

; In place, bg == dst: a vector whose alpha is all 0 keeps the background and is
; neither loaded nor stored, and one whose alpha is all 0xff takes the source
; without the blend, which gives the same samples.

%macro BLEND8_INPLACE 0 ; alpha in m3
    ptest m3, m3
    jz .next
    pcmpeqb m4, m3, pb_ff
    pmovmskb r8d, m4
    cmp r8d, -1
    je .opaque
    BLEND8 dst_q
    movu [dst_q + r6q - mmsize], m3
    jmp .next
.opaque:
    movu m4, [src_q + r6q - mmsize]
    movu [dst_q + r6q - mmsize], m4
.next:
%endmacro

cglobal blit8_blend_inplace_hsub1, 4, 10, 11, dst_, src_, alpha_, hsize_
    %define bg_q dst_q
    BLEND8_CONSTANTS
    mov r6d, mmsize
    ALIGN 16
    .loop_simd:
        movu m3, [alpha_q + r6q - mmsize]
        mov r7q, r6q
        BLEND8_INPLACE
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_BLEND_B r7, r8, r9, r6
    .ret:
RET

cglobal blit8_blend_inplace_hsub2, 4, 10, 12, dst_, src_, alpha_, hsize_
    %define bg_q dst_q
    BLEND8_CONSTANTS
    VBROADCASTI128 shuffle, [blit8_alpha_shuffle]
    mov r6d, mmsize
    ALIGN 16
    .loop_simd:
        movu m5, [alpha_q + 2*r6q - 2*mmsize]
        movu m6, [alpha_q + 2*r6q - 1*mmsize]
        pshufb m5, shuffle
        pshufb m6, shuffle
        punpcklqdq m3, m5, m6
        vpermq m3, m3, q3120
        mov r7q, r6q
        BLEND8_INPLACE
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_BLEND_B r7, r8, r9, r6, 2
    .ret:
RET

%undef bg_q

%undef pb_80
%undef pw_bias
%undef pw_101
%undef pb_ff
%undef shuffle

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

; In place, as the 8 bit ones: all 0 alpha skipped, all 0x3ff alpha copied.

%macro BLEND10_INPLACE 0 ; alpha in m4
    ptest m4, m4
    jz .next
    pcmpeqw m5, m4, m0
    pmovmskb r8d, m5
    cmp r8d, -1
    je .opaque
    movu m2, [dst_q + 2*r6q - mmsize]
    movu m3, [src_q + 2*r6q - mmsize]
    BLEND10
    movu [dst_q + 2*r6q - mmsize], m6
    jmp .next
.opaque:
    movu m3, [src_q + 2*r6q - mmsize]
    movu [dst_q + 2*r6q - mmsize], m3
.next:
%endmacro

cglobal blit10_blend_inplace_hsub1, 4, 11, 8, dst_, src_, alpha_, hsize_
    %define bg_q dst_q
    BLEND10_CONSTANTS
    mov r6d, mmsize/2
    ALIGN 16
    .loop_simd:
        movu m4, [alpha_q + 2*r6q - mmsize]
        mov r7q, r6q
        BLEND10_INPLACE
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_BLEND_W r7, r8, r9, r10
    .ret:
RET

cglobal blit10_blend_inplace_hsub2, 4, 11, 9, dst_, src_, alpha_, hsize_
    %define bg_q dst_q
    BLEND10_CONSTANTS
    VBROADCASTI128 m8, [blit10_alpha_shuffle]
    mov r6d, mmsize/2
    ALIGN 16
    .loop_simd:
        movu m4, [alpha_q + 4*r6q - 2*mmsize]
        movu m5, [alpha_q + 4*r6q - 1*mmsize]
        mov r7q, r6q
        pshufb m4, m8
        pshufb m5, m8
        punpcklqdq m4, m4, m5
        vpermq m4, m4, q3120
        BLEND10_INPLACE
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    SCALAR_TAIL_BLEND_W r7, r8, r9, r10, 2
    .ret:
RET

%undef bg_q

; The avx512 kernels replace the +0x80 bias and the pand/pandn/por select with an
; unsigned compare into a mask register and a merge-masked load, and predicate the
; trailing samples instead of falling back to scalar code.  Masked loads suppress
; faults on masked-out samples, so the tail never reads past the end of the row
; and any hsize is accepted.

; Build k2, the predicate for the trailing hsize_q samples, and leave -1 in r6q
; for the alpha predicates below.  %1 is the mask width: q for byte samples,
; d for word samples.
%macro TAIL_MASK 1
    mov r6q, -1
    bzhi r7q, r6q, hsize_q
    kmov%1 k2, r7 %+ %1
%endmacro

; Build k3 and k4, the predicates for the two alpha vectors feeding the trailing
; samples of a subsampled kernel.  %2 is the number of alpha samples per vector.
%macro TAIL_MASK_ALPHA 2
    add hsize_q, hsize_q   ; alpha samples for the trailing destination samples
    bzhi r7q, r6q, hsize_q ; bzhi leaves every bit set when the count exceeds the width
    kmov%1 k3, r7 %+ %1
    sub hsize_q, %2
    jle %%lo_only
        bzhi r7q, r6q, hsize_q
        kmov%1 k4, r7 %+ %1
        jmp %%done
    %%lo_only:
        kxor%1 k4, k4, k4
    %%done:
%endmacro

INIT_ZMM avx512

%define threshold m0
%define blend m1
%define alp m2
%define alp2 m3

cglobal blit8_threshold_hsub2, 6, 8, 4, dst_, bg_, src_, alpha_, hsize_, threshold_
    vpbroadcastb threshold, threshold_d
    mov r6q, hsize_q
    and r6q, -mmsize
    add dst_q, r6q
    add bg_q, r6q
    add src_q, r6q
    lea alpha_q, [alpha_q + 2*r6q]
    neg r6q
    jz .tail
    ALIGN 16
    .loop_simd:
        movu alp, [alpha_q + 2*r6q]
        movu alp2, [alpha_q + 2*r6q + mmsize]
        ; vpmovwb keeps the low byte of every word, i.e. every second alpha sample
        vpmovwb ymm %+ alp, alp
        vpmovwb ymm %+ alp2, alp2
        vinserti64x4 alp, alp, ymm %+ alp2, 1
        vpcmpub k1, threshold, alp, 1
        movu blend, [bg_q + r6q]
        vmovdqu8 blend{k1}, [src_q + r6q]
        movu [dst_q + r6q], blend
        add r6q, mmsize
    jl .loop_simd
    .tail:
    and hsize_q, mmsize-1
    jz .ret
    TAIL_MASK q
    TAIL_MASK_ALPHA q, mmsize
    vmovdqu8 alp{k3}{z}, [alpha_q]
    vmovdqu8 alp2{k4}{z}, [alpha_q + mmsize]
    vpmovwb ymm %+ alp, alp
    vpmovwb ymm %+ alp2, alp2
    vinserti64x4 alp, alp, ymm %+ alp2, 1
    vpcmpub k1, threshold, alp, 1
    kandq k1, k1, k2
    vmovdqu8 blend{k2}{z}, [bg_q]
    vmovdqu8 blend{k1}, [src_q]
    vmovdqu8 [dst_q]{k2}, blend
    .ret:
RET

%define threshold m0
%define blend m1
%define alp m2
%define alp2 m3
%define alp_idx m4

cglobal blit10_threshold_hsub2, 6, 8, 5, dst_, bg_, src_, alpha_, hsize_, threshold_
    vpbroadcastw threshold, threshold_d
    movu alp_idx, [blit10_alpha_permute]
    mov r6q, hsize_q
    and r6q, -(mmsize/2)
    lea dst_q, [dst_q + 2*r6q]
    lea bg_q, [bg_q + 2*r6q]
    lea src_q, [src_q + 2*r6q]
    lea alpha_q, [alpha_q + 4*r6q]
    neg r6q
    jz .tail
    ALIGN 16
    .loop_simd:
        movu alp, [alpha_q + 4*r6q]
        ; gather the even words of both alpha vectors in one pass
        vpermt2w alp, alp_idx, [alpha_q + 4*r6q + mmsize]
        vpcmpuw k1, threshold, alp, 1
        movu blend, [bg_q + 2*r6q]
        vmovdqu16 blend{k1}, [src_q + 2*r6q]
        movu [dst_q + 2*r6q], blend
        add r6q, mmsize/2
    jl .loop_simd
    .tail:
    and hsize_q, mmsize/2-1
    jz .ret
    TAIL_MASK d
    TAIL_MASK_ALPHA d, mmsize/2
    vmovdqu16 alp{k3}{z}, [alpha_q]
    vmovdqu16 alp2{k4}{z}, [alpha_q + mmsize]
    vpermt2w alp, alp_idx, alp2
    vpcmpuw k1, threshold, alp, 1
    kandd k1, k1, k2
    vmovdqu16 blend{k2}{z}, [bg_q]
    vmovdqu16 blend{k1}, [src_q]
    vmovdqu16 [dst_q]{k2}, blend
    .ret:
RET

; In place, bg == dst: the selected source samples are stored under the compare
; mask and the others are not touched, so the background is never loaded, and a
; vector with no selected sample is neither loaded nor stored, which leaves the
; cache lines of transparent areas clean.

%macro INPLACE_STORE 3 ; mask width q or d, sample bits 8 or 16, offset
        kortest%1 k1, k1
        jz .next
        movu m1, [src_q + %3]
        vmovdqu%2 [dst_q + %3]{k1}, m1
%endmacro

cglobal blit8_threshold_inplace_hsub1, 5, 8, 2, dst_, src_, alpha_, hsize_, threshold_
    vpbroadcastb m0, threshold_d
    mov r5q, hsize_q
    and r5q, -mmsize
    add dst_q, r5q
    add src_q, r5q
    add alpha_q, r5q
    neg r5q
    jz .tail
    ALIGN 16
    .loop_simd:
        vpcmpub k1, m0, [alpha_q + r5q], 1 ; threshold < alpha
        INPLACE_STORE q, 8, r5q
    .next:
        add r5q, mmsize
    jl .loop_simd
    .tail:
    and hsize_q, mmsize-1
    jz .ret
    TAIL_MASK q
    vmovdqu8 m1{k2}{z}, [alpha_q]
    vpcmpub k1, m0, m1, 1
    kandq k1, k1, k2
    vmovdqu8 m1{k1}{z}, [src_q]
    vmovdqu8 [dst_q]{k1}, m1
    .ret:
RET

cglobal blit8_threshold_inplace_hsub2, 5, 8, 3, dst_, src_, alpha_, hsize_, threshold_
    vpbroadcastb m0, threshold_d
    mov r5q, hsize_q
    and r5q, -mmsize
    add dst_q, r5q
    add src_q, r5q
    lea alpha_q, [alpha_q + 2*r5q]
    neg r5q
    jz .tail
    ALIGN 16
    .loop_simd:
        movu m1, [alpha_q + 2*r5q]
        movu m2, [alpha_q + 2*r5q + mmsize]
        vpmovwb ym1, m1
        vpmovwb ym2, m2
        vinserti64x4 m1, m1, ym2, 1
        vpcmpub k1, m0, m1, 1
        INPLACE_STORE q, 8, r5q
    .next:
        add r5q, mmsize
    jl .loop_simd
    .tail:
    and hsize_q, mmsize-1
    jz .ret
    TAIL_MASK q
    TAIL_MASK_ALPHA q, mmsize
    vmovdqu8 m1{k3}{z}, [alpha_q]
    vmovdqu8 m2{k4}{z}, [alpha_q + mmsize]
    vpmovwb ym1, m1
    vpmovwb ym2, m2
    vinserti64x4 m1, m1, ym2, 1
    vpcmpub k1, m0, m1, 1
    kandq k1, k1, k2
    vmovdqu8 m1{k1}{z}, [src_q]
    vmovdqu8 [dst_q]{k1}, m1
    .ret:
RET

cglobal blit10_threshold_inplace_hsub1, 5, 8, 2, dst_, src_, alpha_, hsize_, threshold_
    vpbroadcastw m0, threshold_d
    mov r5q, hsize_q
    and r5q, -(mmsize/2)
    lea dst_q, [dst_q + 2*r5q]
    lea src_q, [src_q + 2*r5q]
    lea alpha_q, [alpha_q + 2*r5q]
    neg r5q
    jz .tail
    ALIGN 16
    .loop_simd:
        vpcmpuw k1, m0, [alpha_q + 2*r5q], 1
        INPLACE_STORE d, 16, 2*r5q
    .next:
        add r5q, mmsize/2
    jl .loop_simd
    .tail:
    and hsize_q, mmsize/2-1
    jz .ret
    TAIL_MASK d
    vmovdqu16 m1{k2}{z}, [alpha_q]
    vpcmpuw k1, m0, m1, 1
    kandd k1, k1, k2
    vmovdqu16 m1{k1}{z}, [src_q]
    vmovdqu16 [dst_q]{k1}, m1
    .ret:
RET

cglobal blit10_threshold_inplace_hsub2, 5, 8, 4, dst_, src_, alpha_, hsize_, threshold_
    vpbroadcastw m0, threshold_d
    movu m3, [blit10_alpha_permute]
    mov r5q, hsize_q
    and r5q, -(mmsize/2)
    lea dst_q, [dst_q + 2*r5q]
    lea src_q, [src_q + 2*r5q]
    lea alpha_q, [alpha_q + 4*r5q]
    neg r5q
    jz .tail
    ALIGN 16
    .loop_simd:
        movu m1, [alpha_q + 4*r5q]
        vpermt2w m1, m3, [alpha_q + 4*r5q + mmsize]
        vpcmpuw k1, m0, m1, 1
        INPLACE_STORE d, 16, 2*r5q
    .next:
        add r5q, mmsize/2
    jl .loop_simd
    .tail:
    and hsize_q, mmsize/2-1
    jz .ret
    TAIL_MASK d
    TAIL_MASK_ALPHA d, mmsize/2
    vmovdqu16 m1{k3}{z}, [alpha_q]
    vmovdqu16 m2{k4}{z}, [alpha_q + mmsize]
    vpermt2w m1, m3, m2
    vpcmpuw k1, m0, m1, 1
    kandd k1, k1, k2
    vmovdqu16 m1{k1}{z}, [src_q]
    vmovdqu16 [dst_q]{k1}, m1
    .ret:
RET


; The fused hsub1 kernels at 256 bits: once the planes overflow the cache, zmm
; loses about 8% to avx2 on Zen 4 (masked load or register blend alike) while
; ymm stays level with it, and is still faster from L1.
;
; %1 8 or 10, %2 mask width, %3 b or w, %4 bytes per sample, %5 bits of the
; vmovdqu element
%macro THRESHOLD_HSUB1_FUSED 5
cglobal blit%1_threshold_hsub1, 6, 8, 3, dst_, bg_, src_, alpha_, hsize_, threshold_
    vpbroadcast%3 m0, threshold_d
    mov r6q, hsize_q
    and r6q, -(mmsize/%4)
    lea dst_q, [dst_q + %4*r6q]
    lea bg_q, [bg_q + %4*r6q]
    lea src_q, [src_q + %4*r6q]
    lea alpha_q, [alpha_q + %4*r6q]
    neg r6q
    jz .tail
    ALIGN 16
    .loop_simd:
        vpcmpu%3 k1, m0, [alpha_q + %4*r6q], 1
        movu m1, [bg_q + %4*r6q]
        vmovdqu%5 m1{k1}, [src_q + %4*r6q]
        movu [dst_q + %4*r6q], m1
        add r6q, mmsize/%4
    jl .loop_simd
    .tail:
    and hsize_q, mmsize/%4-1
    jz .ret
    TAIL_MASK %2
    vmovdqu%5 m2{k2}{z}, [alpha_q]
    vpcmpu%3 k1, m0, m2, 1
    kand%2 k1, k1, k2 ; a zero threshold would otherwise select samples past the row
    vmovdqu%5 m1{k2}{z}, [bg_q]
    vmovdqu%5 m1{k1}, [src_q]
    vmovdqu%5 [dst_q]{k2}, m1
    .ret:
RET
%endmacro

INIT_YMM avx512
THRESHOLD_HSUB1_FUSED 8, d, b, 1, 8
THRESHOLD_HSUB1_FUSED 10, w, w, 2, 16

; The blend in place at 512 bits: the same arithmetic, with the all 0 and all
; maximum alpha tests on mask registers.  The tail is scalar, and a row shorter
; than one vector, which the avx2 minimum allows, is all tail.

INIT_ZMM avx512

%define pb_80   m0
%define pw_bias m1
%define pw_101  m2
%define pb_ff   m10

%macro BLEND8_INPLACE_ZMM 0 ; alpha in m3
    vptestmb k1, m3, m3
    kortestq k1, k1
    jz .next
    vpcmpeqb k1, m3, pb_ff
    kortestq k1, k1
    jc .opaque
    BLEND8 dst_q
    movu [dst_q + r6q - mmsize], m3
    jmp .next
.opaque:
    movu m4, [src_q + r6q - mmsize]
    movu [dst_q + r6q - mmsize], m4
.next:
%endmacro

%macro BLEND8_CONSTANTS_ZMM 0
    vpbroadcastb pb_80, [blit8_blend_80]
    vpbroadcastw pw_bias, [blit8_blend_bias]
    vpbroadcastw pw_101, [blit8_blend_101]
    vpternlogd pb_ff, pb_ff, pb_ff, 0xff
%endmacro

cglobal blit8_blend_inplace_hsub1, 4, 10, 11, dst_, src_, alpha_, hsize_
    %define bg_q dst_q
    BLEND8_CONSTANTS_ZMM
    mov r6d, mmsize
    xor r7d, r7d
    cmp hsize_q, r6q
    jb .tail
    ALIGN 16
    .loop_simd:
        movu m3, [alpha_q + r6q - mmsize]
        mov r7q, r6q
        BLEND8_INPLACE_ZMM
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    .tail:
    SCALAR_TAIL_BLEND_B r7, r8, r9, r6
    .ret:
RET

cglobal blit8_blend_inplace_hsub2, 4, 10, 11, dst_, src_, alpha_, hsize_
    %define bg_q dst_q
    BLEND8_CONSTANTS_ZMM
    mov r6d, mmsize
    xor r7d, r7d
    cmp hsize_q, r6q
    jb .tail
    ALIGN 16
    .loop_simd:
        movu m5, [alpha_q + 2*r6q - 2*mmsize]
        movu m6, [alpha_q + 2*r6q - 1*mmsize]
        vpmovwb ym3, m5      ; the low byte of every word
        vpmovwb ym6, m6
        vinserti64x4 m3, m3, ym6, 1
        mov r7q, r6q
        BLEND8_INPLACE_ZMM
        lea r6q, [r6q + mmsize]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    .tail:
    SCALAR_TAIL_BLEND_B r7, r8, r9, r6, 2
    .ret:
RET

%undef pb_80
%undef pw_bias
%undef pw_101
%undef pb_ff

%macro BLEND10_CONSTANTS_ZMM 0
    vpternlogd m0, m0, m0, 0xff
    psrlw m0, 6             ; 0x3ff in every word
    vpternlogd m1, m1, m1, 0xff
    psrld m1, 31            ; 1 in every dword
%endmacro

%macro BLEND10_INPLACE_ZMM 0 ; alpha in m4
    vptestmw k1, m4, m4
    kortestd k1, k1
    jz .next
    vpcmpeqw k1, m4, m0
    kortestd k1, k1
    jc .opaque
    movu m2, [dst_q + 2*r6q - mmsize]
    movu m3, [src_q + 2*r6q - mmsize]
    BLEND10
    movu [dst_q + 2*r6q - mmsize], m6
    jmp .next
.opaque:
    movu m3, [src_q + 2*r6q - mmsize]
    movu [dst_q + 2*r6q - mmsize], m3
.next:
%endmacro

cglobal blit10_blend_inplace_hsub1, 4, 11, 8, dst_, src_, alpha_, hsize_
    %define bg_q dst_q
    BLEND10_CONSTANTS_ZMM
    mov r6d, mmsize/2
    xor r7d, r7d
    cmp hsize_q, r6q
    jb .tail
    ALIGN 16
    .loop_simd:
        movu m4, [alpha_q + 2*r6q - mmsize]
        mov r7q, r6q
        BLEND10_INPLACE_ZMM
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    .tail:
    SCALAR_TAIL_BLEND_W r7, r8, r9, r10
    .ret:
RET

cglobal blit10_blend_inplace_hsub2, 4, 11, 9, dst_, src_, alpha_, hsize_
    %define bg_q dst_q
    BLEND10_CONSTANTS_ZMM
    movu m8, [blit10_alpha_permute]
    mov r6d, mmsize/2
    xor r7d, r7d
    cmp hsize_q, r6q
    jb .tail
    ALIGN 16
    .loop_simd:
        movu m4, [alpha_q + 4*r6q - 2*mmsize]
        vpermt2w m4, m8, [alpha_q + 4*r6q - mmsize]
        mov r7q, r6q
        BLEND10_INPLACE_ZMM
        lea r6q, [r6q + mmsize/2]
        cmp hsize_q, r6q
    jae .loop_simd
    cmp r7q, hsize_q
    jae .ret
    ALIGN 16
    .tail:
    SCALAR_TAIL_BLEND_W r7, r8, r9, r10, 2
    .ret:
RET

%undef bg_q
