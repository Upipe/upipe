/*
 * What license do I need to this mash of code from various sources?
 */

#ifndef _UPIPE_X86_AVX512_H_
/** @hidden */
#define _UPIPE_X86_AVX512_H_

#include <stdbool.h>
#include <stdint.h>

/* GCC bug 85100 says that the AVX512 detection can be incorrect if OS support
 * is disabled (or possibly not present) so just use the built-in function for
 * versions which have that fixed. */
#if __GNUC__ >= 8 \
    || __GNUC__ == 7 && __GNUC_MINOR__ >= 4 \
    || __GNUC__ == 6 && __GNUC_MINOR__ >= 5

static inline bool has_avx512_support(void)
{
    return __builtin_cpu_supports("avx512f")
        && __builtin_cpu_supports("avx512cd")
        && __builtin_cpu_supports("avx512bw")
        && __builtin_cpu_supports("avx512dq")
        && __builtin_cpu_supports("avx512vl")
        && __builtin_cpu_supports("avx512vnni")
        && __builtin_cpu_supports("avx512ifma")
        && __builtin_cpu_supports("avx512vbmi")
        && __builtin_cpu_supports("avx512vbmi2")
        && __builtin_cpu_supports("avx512vpopcntdq")
        && __builtin_cpu_supports("avx512bitalg")
        /* VAES VPCLMULQDQ missing */
        ;
}

#elif defined(__x86_64__)
/* We only support this detection on x86-64. */

/* Copied from GCC 11.1.0 cpuid.h public header because the GCC function
 * __get_cpuid_count is only available from 6.3. */
#define cpuid(level, count, a, b, c, d)				\
  __asm__ __volatile__ ("cpuid\n\t"					\
			: "=a" (a), "=b" (b), "=c" (c), "=d" (d)	\
			: "0" (level), "2" (count))

/* Adapted (slightly) from dav1d's CPU feature detection. */
#define X(reg, mask) (((reg) & (mask)) == (mask))

static inline bool has_avx512_support(void)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, 0, eax, ebx, ecx, edx);
    const unsigned max_leaf = eax;

    if (max_leaf >= 1) {
        cpuid(1, 0, eax, ebx, ecx, edx);
        if (X(ecx, 0x18000000)) /* OSXSAVE/AVX */ {
            uint32_t xcrlow, xcrhigh;
            asm ("xgetbv" : "=a" (xcrlow), "=d" (xcrhigh) : "c" (0));
            if (X(xcrlow, 0x00000006)) /* XMM/YMM */ {
                if (max_leaf >= 7) {
                    cpuid(7, 0, eax, ebx, ecx, edx);
                    if (X(ebx, 0x00000128)) /* BMI1/BMI2/AVX2 */ {
                        if (X(xcrlow, 0x000000e0)) /* ZMM/OPMASK */ {
                            if (X(ebx, 0xd0230000) && X(ecx, 0x00005f42))
                                return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

#undef X

#else

static inline bool has_avx512_support(void)
{
    return false;
}

#endif

#endif /* _UPIPE_X86_AVX512_H_ */
