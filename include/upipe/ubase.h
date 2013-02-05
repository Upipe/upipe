/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe common definitions
 */

#ifndef _UPIPE_UBASE_H_
/** @hidden */
#define _UPIPE_UBASE_H_

#include <upipe/config.h>

#ifdef HAVE_FEATURES_H
#include <features.h>
#endif
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#ifdef __GNUC__

/** @This should be used in a if conditional when it will be true most of the
 * time. */
#define likely(x)       __builtin_expect(!!(x),1)
/** @This should be used in a if conditional when it will be false most of the
 * time. */
#define unlikely(x)     __builtin_expect(!!(x),0)

#else /* mkdoc:skip */
#define likely(x)       !!(x)
#define unlikely(x)     !!(x)

#endif

#ifndef container_of
/** @This is used to retrieve the private portion of a structure. */
#   define container_of(ptr, type, member) ({                               \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);                \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#endif

/** @internal @This is a helper to simplify printf-style functions. */
#define UBASE_VARARG(command)                                               \
    size_t len;                                                             \
    va_list args;                                                           \
    va_start(args, format);                                                 \
    len = vsnprintf(NULL, 0, format, args);                                 \
    va_end(args);                                                           \
    if (len > 0) {                                                          \
        char string[len + 1];                                               \
        va_start(args, format);                                             \
        vsnprintf(string, len + 1, format, args);                           \
        va_end(args);                                                       \
        return command;                                                     \
    } else {                                                                \
        char *string = NULL;                                                \
        return command;                                                     \
    }

/** @This is designed to chain uref and ubuf in a list. */
struct uchain {
    /** pointer to next element */
    struct uchain *next;
    /** pointer to previous element */
    struct uchain *prev;
};

/** @This initializes a uchain.
 *
 * @param uchain pointer to a uchain structure
 */
static inline void uchain_init(struct uchain *uchain)
{
    uchain->next = uchain->prev = NULL;
}

#ifdef WORDS_BIGENDIAN
/** @This allows to define a 32-bit unsigned integer with 4 letters. */     \
#   define UBASE_FOURCC(a, b, c, d)                                         \
        (((uint32_t)d) | (((uint32_t)c) << 8) | (((uint32_t)b) << 16) |     \
         (((uint32_t)a) << 24))

#else /* mkdoc:skip */
#   define UBASE_FOURCC(a, b, c, d)                                         \
        (((uint32_t)a) | (((uint32_t)b) << 8) | (((uint32_t)c) << 16) |     \
         (((uint32_t)d) << 24))

#endif

/** @This returns the greatest common denominator between two positive integers.
 *
 * @param a first integer (not null)
 * @param b second integer
 * @return GCD of the two integers
 */
static inline uint64_t ubase_gcd(uint64_t a, uint64_t b)
{
    while (likely(b != 0)) {
        uint64_t c = a % b;
        a = b;
        b = c;
    }
    return a;
}

/** @This defines the rational type. */
struct urational {
    /** numerator */
    int64_t num;
    /** denominator */
    uint64_t den;
};

/** @This simplifies a rational.
 *
 * @param urational pointer to rational
 */
static inline void urational_simplify(struct urational *urational)
{
    uint64_t gcd;
    if (urational->num >= 0)
        gcd = ubase_gcd(urational->num, urational->den);
    else
        gcd = ubase_gcd(-urational->num, urational->den);
    urational->num /= gcd;
    urational->den /= gcd;
}

/** @This checks if a prefix matches a string.
 *
 * @param string large string
 * @param prefix prefix to check
 * @return 0 if the prefix matches
 */
static inline int ubase_ncmp(const char *string, const char *prefix)
{
    return strncmp(string, prefix, strlen(prefix));
}

#endif
