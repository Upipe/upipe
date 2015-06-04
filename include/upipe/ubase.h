/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/config.h>

#ifdef UPIPE_HAVE_FEATURES_H
#include <features.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#ifdef __GNUC__

#ifndef likely
/** @This should be used in a if conditional when it will be true most of the
 * time. */
#   define likely(x)       __builtin_expect(!!(x),1)
#endif
#ifndef unlikely
/** @This should be used in a if conditional when it will be false most of the
 * time. */
#   define unlikely(x)     __builtin_expect(!!(x),0)
#endif

/** @This marks a function or variable as possibly unused (suppresses compiler
 * warnings). */
#define UBASE_UNUSED __attribute__ ((unused))
/** @This marks a function or variable as deprecated (forces compiler
 * warnings). */
#define UBASE_DEPRECATED __attribute__ ((deprecated))

#else /* mkdoc:skip */
#define likely(x)       !!(x)
#define unlikely(x)     !!(x)

#define UBASE_UNUSED
#define UBASE_DEPRECATED
#endif

#ifndef container_of
/** @This is used to retrieve the private portion of a structure. */
#   define container_of(ptr, type, member) ({                               \
        const typeof( ((type *)0)->member ) *_mptr = (ptr);                 \
        (type *)( (char *)_mptr - offsetof(type,member) );})
#endif

#ifndef UBASE_ARRAY_SIZE
/** @This is used to retrieve the number of items of an array. */
#   define UBASE_ARRAY_SIZE(a)        (sizeof (a) / sizeof ((a)[0]))
#endif

/** @This declares two functions dealing with substructures included into a
 * larger structure.
 *
 * @param STRUCTURE name of the larger structure
 * @param SUBSTRUCT name of the smaller substructure
 * @param SUBNAME name to use for the functions
 * (STRUCTURE##_{to,from}_##SUBNAME)
 * @param SUB name of the @tt{struct SUBSTRUCT} field of @tt{struct STRUCTURE}
 */
#define UBASE_FROM_TO(STRUCTURE, SUBSTRUCT, SUBNAME, SUB)                   \
/** @internal @This returns a pointer to SUBNAME.                           \
 *                                                                          \
 * @param STRUCTURE pointer to struct STRUCTURE                             \
 * @return pointer to struct SUBSTRUCT                                      \
 */                                                                         \
static UBASE_UNUSED inline struct SUBSTRUCT *                               \
    STRUCTURE##_to_##SUBNAME(struct STRUCTURE *s)                           \
{                                                                           \
    return &s->SUB;                                                         \
}                                                                           \
/** @internal @This returns a pointer to SUBNAME.                           \
 *                                                                          \
 * @param sub pointer to struct SUBSTRUCT                                   \
 * @return pointer to struct STRUCTURE                                      \
 */                                                                         \
static UBASE_UNUSED inline struct STRUCTURE *                               \
    STRUCTURE##_from_##SUBNAME(struct SUBSTRUCT *sub)                       \
{                                                                           \
    return container_of(sub, struct STRUCTURE, SUB);                        \
}

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

#ifdef UPIPE_WORDS_BIGENDIAN
/** @This allows to define a 32-bit unsigned integer with 4 letters. */     \
#   define UBASE_FOURCC(a, b, c, d)                                         \
        (((uint32_t)d) | (((uint32_t)c) << 8) | (((uint32_t)b) << 16) |     \
         (((uint32_t)a) << 24))

#else /* mkdoc:skip */
#   define UBASE_FOURCC(a, b, c, d)                                         \
        (((uint32_t)a) | (((uint32_t)b) << 8) | (((uint32_t)c) << 16) |     \
         (((uint32_t)d) << 24))

#endif

/** @This defines the standard error codes. */
enum ubase_err {
    /** no error */
    UBASE_ERR_NONE = 0,
    /** unknown error */
    UBASE_ERR_UNKNOWN,
    /** allocation error */
    UBASE_ERR_ALLOC,
    /** unable to allocate a upump */
    UBASE_ERR_UPUMP,
    /** unhandled command or event */
    UBASE_ERR_UNHANDLED,
    /** invalid argument */
    UBASE_ERR_INVALID,
    /** error in external library */
    UBASE_ERR_EXTERNAL,
    /** failure to get an exclusive resource */
    UBASE_ERR_BUSY,

    /** non-standard error codes implemented by a module type can start from
     * there */
    UBASE_ERR_LOCAL = 0x8000
};

/** @This return the corresponding error string.
 *
 * @param err the error value
 * @return the error string
 */
static inline const char *ubase_err_str(int err)
{
    switch (err) {
    case UBASE_ERR_NONE: return "UBASE_ERR_NONE";
    case UBASE_ERR_UNKNOWN: return "UBASE_ERR_UNKNOWN";
    case UBASE_ERR_ALLOC: return "UBASE_ERR_ALLOC";
    case UBASE_ERR_UPUMP: return "UBASE_ERR_UPUMP";
    case UBASE_ERR_UNHANDLED: return "UBASE_ERR_UNHANDLED";
    case UBASE_ERR_INVALID: return "UBASE_ERR_INVALID";
    case UBASE_ERR_EXTERNAL: return "UBASE_ERR_EXTERNAL";
    case UBASE_ERR_BUSY: return "UBASE_ERR_BUSY";
    case UBASE_ERR_LOCAL: break;
    }
    return NULL;
}

/** @This returns true if no error happened in an error code.
 *
 * @param err error code
 * @return true if no error happened
 */
static inline bool ubase_check(int err)
{
    return err == UBASE_ERR_NONE;
}

/** @This runs the given function and returns an error in case of failure.
 *
 * @param command command whose return code is to be checked
 */
#define UBASE_RETURN(command)                                               \
do {                                                                        \
    int ubase_err_tmp = command;                                            \
    if (unlikely(!ubase_check(ubase_err_tmp)))                              \
        return ubase_err_tmp;                                               \
} while (0);

/** @This runs the given function and throws a fatal error in case of failure.
 *
 * @param command command whose return code is to be checked
 */
#define UBASE_FATAL(upipe, command)                                         \
do {                                                                        \
    int ubase_err_tmp = command;                                            \
    if (unlikely(!ubase_check(ubase_err_tmp)))                              \
        upipe_throw_fatal(upipe, ubase_err_tmp);                            \
} while (0);

/** @This runs the given function, throws a fatal error and returns in case of
 * failure.
 *
 * @param command command whose return code is to be checked
 */
#define UBASE_FATAL_RETURN(upipe, command)                                  \
do {                                                                        \
    int ubase_err_tmp = command;                                            \
    if (unlikely(!ubase_check(ubase_err_tmp))) {                            \
        upipe_throw_fatal(upipe, ubase_err_tmp);                            \
        return;                                                             \
    }                                                                       \
} while (0);

/** @This runs the given function and throws an error in case of failure.
 *
 * @param command command whose return code is to be checked
 */
#define UBASE_ERROR(upipe, command)                                         \
do {                                                                        \
    int ubase_err_tmp = command;                                            \
    if (unlikely(!ubase_check(ubase_err_tmp)))                              \
        upipe_throw_error(upipe, ubase_err_tmp);                            \
} while (0);

/** @This asserts if the given command (returning an @ref ubase_err) failed.
 *
 * @param command command whose return code is to be checked
 */
#define ubase_assert(command)                                               \
    assert(ubase_check(command))

/** @This asserts if the given command (returning an @ref ubase_err) succeeded.
 *
 * @param command command whose return code is to be checked
 */
#define ubase_nassert(command)                                              \
    assert(!ubase_check(command))

/** @This checks that the first argument is equal to the given signature.
 *
 * @param args va_list of arguments
 * @param signature unsigned int representing the signature of a module
 */
#define UBASE_SIGNATURE_CHECK(args, signature)                              \
{                                                                           \
    if (va_arg(args, unsigned int) != signature)                            \
        return UBASE_ERR_UNHANDLED;                                         \
}

/** @This returns #UBASE_ERR_ALLOC if the variable is NULL.
 *
 * @param var variable to check
 */
#define UBASE_ALLOC_RETURN(var)                                             \
    if (unlikely(var == NULL))                                              \
        return UBASE_ERR_ALLOC;

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

/** @This adds two rationals.
 *
 * @param urational1 pointer to rational 1
 * @param urational2 pointer to rational 2
 * @return a rational
 */
static inline struct urational urational_add(const struct urational *urational1,
                                             const struct urational *urational2)
{
    struct urational sum;
    sum.num = urational1->num * (int64_t)urational2->den +
              urational2->num * (int64_t)urational1->den;
    sum.den = urational1->den * (int64_t)urational2->den;
    urational_simplify(&sum);
    return sum;
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

#ifdef __cplusplus
}
#endif
#endif
