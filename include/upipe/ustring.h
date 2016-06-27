/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe sub string manipulation
 */

#ifndef _UPIPE_USTRING_H_
/** @hidden */
# define _UPIPE_USTRING_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

#define USTRING_ALPHA_LOWER \
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', \
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', \
    'u', 'v', 'w', 'x', 'y', 'z'
#define USTRING_ALPHA_UPPER \
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', \
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', \
    'U', 'V', 'W', 'X', 'Y', 'Z'
#define USTRING_ALPHA USTRING_ALPHA_LOWER, USTRING_ALPHA_UPPER
#define USTRING_DIGIT '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
#define USTRING_HEXDIGIT USTRING_DIGIT, 'a', 'b', 'c', 'd', 'e', 'f', \
                                  'A', 'B', 'C', 'D', 'E', 'F'
/** @This stores a portion of a string. */
struct ustring {
    /** pointer to the first character */
    char *at;
    /** length from the first character */
    size_t len;
};

/** @This returns an initialized ustring.
 *
 * @return an ustring
 */
static inline struct ustring ustring_null(void)
{
    struct ustring ustring;
    ustring.at = NULL;
    ustring.len = 0;
    return ustring;
}

/** @This makes an ustring from a string.
 *
 * @param str the string
 * @return an initialized ustring structure
 */
static inline struct ustring ustring_from_str(const char *str)
{
    struct ustring ustring = ustring_null();
    ustring.at = (char *)str;
    ustring.len = str != NULL ? strlen(str) : 0;
    return ustring;
}

/** @This returns true is the ustring is null.
 *
 * @param sub an ustring
 * @return true if the ustring is null.
 */
static inline bool ustring_is_null(const struct ustring sub)
{
    return sub.at == NULL;
}

/** @This returns true is the ustring is null or empty.
 *
 * @param sub an ustring
 * @return true if the ustring is null or empty
 */
static inline bool ustring_is_empty(const struct ustring sub)
{
    return ustring_is_null(sub) || sub.len == 0;
}

/** @This allocated a string from an ustring.
 *
 * @param sub an ustring
 * @param str_p a pointer to a string
 * @return an error code
 */
static inline int ustring_to_str(const struct ustring sub, char **str_p)
{
    if (ustring_is_null(sub)) {
        if (str_p)
            *str_p = NULL;
    }
    else {
        if (str_p) {
            *str_p = strndup(sub.at, sub.len);
            if (!*str_p)
                return UBASE_ERR_ALLOC;
        }
    }
    return UBASE_ERR_NONE;
}

/** @This copies an ustring to a buffer.
 *
 * @param sub an ustring
 * @param buffer the destination buffer
 * @param len the size of the buffer
 * @return an error code
 */
static inline int ustring_cpy(const struct ustring sub,
                              char *buffer, size_t len)
{
    memset(buffer, 0, len);
    memcpy(buffer, sub.at, sub.len >= len ? len - 1 : sub.len);
    return sub.len < len ? UBASE_ERR_NONE : UBASE_ERR_NOSPC;
}

/** @This makes an ustring from another.
 *
 * @param sub an ustring
 * @param offset offset in the ustring sub
 * @param length length of the ustring sub to get
 * @return an ustring
 */
static inline struct ustring ustring_sub(struct ustring sub,
                                         size_t offset, size_t length)
{
    sub.at = offset > sub.len ? NULL : sub.at + offset;
    sub.len = sub.at ?
        (sub.len - offset > length ? length : sub.len - offset) : 0;
    return sub;
}

/** @This returns a shifted ustring.
 *
 * @param sub an ustring
 * @param offset offset in the ustring
 * @return the shifted ustring
 */
static inline struct ustring ustring_shift(const struct ustring sub,
                                           size_t offset)
{
    return ustring_sub(sub, offset, sub.len);
}

/** @This returns a truncated ustring.
 *
 * @param sub an ustring
 * @param length to get from the ustring
 * @return the truncated ustring
 */
static inline struct ustring ustring_truncate(const struct ustring sub,
                                              size_t length)
{
    return ustring_sub(sub, 0, length);
}

/** @This returns the beginning of an ustring containing only characters
 * from set.
 *
 * @param sub an ustring
 * @param set set of allowed characters
 * @return an ustring containing the beginning of sub
 */
static inline struct ustring ustring_while(const struct ustring sub,
                                           const char *set)
{
    for (size_t i = 0; i < sub.len; i++)
        for (size_t j = 0; set[j] != sub.at[i]; j++)
            if (!set[j])
                return ustring_truncate(sub, i);
    return sub;
}

/** @This returns the beginning of an ustring containing only characters
 * absent from set.
 *
 * @param sub an ustring
 * @param set set of rejected characters
 * @return an ustring containing the beginning of sub
 */
static inline struct ustring ustring_until(const struct ustring sub,
                                           const char *set)
{
    for (size_t i = 0; i < sub.len; i++)
        for (size_t j = 0; set[j]; j++)
            if (set[j] == sub.at[i])
                return ustring_truncate(sub, i);
    return sub;
}

/** @This returns a shifted ustring while characters are present in set.
 *
 * @param sub an ustring
 * @param set set of allowed characters
 * @return a shifted ustring
 */
static inline struct ustring ustring_shift_while(const struct ustring sub,
                                                 const char *set)
{
    struct ustring tmp = ustring_while(sub, set);
    return ustring_shift(sub, tmp.len);
}

/** @This returns a shifted ustring while characters are absent from set.
 *
 * @param sub an ustring
 * @param set set of rejected characters
 * @return a shifted ustring
 */
static inline struct ustring ustring_shift_until(const struct ustring sub,
                                                 const char *set)
{
    struct ustring tmp = ustring_until(sub, set);
    return ustring_shift(sub, tmp.len);
}

/** @This compares at most len characters from two ustrings.
 *
 * @param sub1 the first ustring to compare
 * @param sub2 the second ustring to compare
 * @return an integer less than, equal to, or greater than zero if the first
 * len bytes of sub1, respectively, to be less than, to match,
 * or to be grearter than the first len bytes of sub2
 */
static inline int ustring_ncmp(const struct ustring sub1,
                               const struct ustring sub2,
                               size_t len)
{
    size_t cmplen = len > sub1.len ? sub1.len : len;
    cmplen = cmplen > sub2.len ? sub2.len : cmplen;
    int ret = cmplen ? strncmp(sub1.at, sub2.at, cmplen) : 0;
    if (ret != 0 || cmplen == len || sub1.len == sub2.len)
        return ret;
    return sub1.len < sub2.len ? -1 : 1;
}

/** @This compares at most len characters from two ustrings ignoring
 * the case.
 *
 * @param sub1 the first ustring to compare
 * @param sub2 the second ustring to compare
 * @return an integer less than, equal to, or greater than zero if the first
 * len bytes of sub1, respectively, to be less than, to match,
 * or to be grearter than the first len bytes of sub2
 */
static inline int ustring_ncasecmp(const struct ustring sub1,
                                   const struct ustring sub2,
                                   size_t len)
{
    size_t cmplen = len > sub1.len ? sub1.len : len;
    cmplen = cmplen > sub2.len ? sub2.len : cmplen;
    int ret = cmplen ? strncasecmp(sub1.at, sub2.at, cmplen) : 0;
    if (ret != 0 || cmplen == len || sub1.len == sub2.len)
        return ret;
    return sub1.len < sub2.len ? -1 : 1;
}

/** @This compares two ustrings.
 *
 * @param sub1 the first ustring to compare
 * @param sub2 the second ustring to compare
 * @return an integer less than, equal to, or greater than zero if sub1,
 * respectively, to be less than, to match, or to be grearter than sub2
 */
static inline int ustring_cmp(const struct ustring sub1,
                              const struct ustring sub2)
{
    return ustring_ncmp(sub1, sub2,
                        sub1.len >= sub2.len ?
                        sub1.len : sub2.len);
}

/** @This compares an ustring an a string.
 *
 * @param sub the ustring to compare
 * @param str the string to compare
 * @return an integer less than, equal to, or greater than zero if sub,
 * respectively, to be less than, to match, or to be grearter than str
 */
static inline int ustring_cmp_str(const struct ustring sub,
                                  const char *str)
{
    return ustring_cmp(sub, ustring_from_str(str));
}

/** @This compares two ustrings ignoring the case.
 *
 * @param sub1 the first ustring to compare
 * @param sub2 the second ustring to compare
 * @return an integer less than, equal to, or greater than zero if sub1,
 * respectively, to be less than, to match, or to be grearter than sub2
 */
static inline int ustring_casecmp(const struct ustring sub1,
                                  const struct ustring sub2)
{
    return ustring_ncasecmp(sub1, sub2,
                            sub1.len >= sub2.len ?
                            sub1.len : sub2.len);
}

/** @This compares an ustring an a string ignoring the case.
 *
 * @param sub the ustring to compare
 * @param str the string to compare
 * @return an integer less than, equal to, or greater than zero if sub,
 * respectively, to be less than, to match, or to be grearter than str
 */
static inline int ustring_casecmp_str(const struct ustring sub,
                                      const char *str)
{
    return ustring_casecmp(sub, ustring_from_str(str));
}

/** @This returns true if the ustring sub start with ustring prefix.
 *
 * @param sub the ustring to test
 * @param prefix the prefix to match
 * @return a boolean
 */
static inline bool ustring_match(const struct ustring sub,
                                 const struct ustring prefix)
{
    return !ustring_ncmp(sub, prefix, prefix.len);
}

/** @This returns true if the ustring sub start with ustring prefix.
 *
 * @param sub the ustring to test
 * @param prefix the prefix to match
 * @return a boolean
 */
static inline bool ustring_match_str(const struct ustring sub,
                                     const char *prefix)
{
    struct ustring ustring_prefix = ustring_from_str(prefix);
    return !ustring_ncmp(sub, ustring_prefix, ustring_prefix.len);
}

/** @This returns true if the ustring sub start with ustring prefix
 * ignoring the case.
 *
 * @param sub the ustring to test
 * @param prefix the prefix to match
 * @return a boolean
 */
static inline bool ustring_casematch(const struct ustring sub,
                                     const struct ustring prefix)
{
    return !ustring_ncasecmp(sub, prefix, prefix.len);
}

/** @This returns true if the ustring sub end with ustring suffix.
 *
 * @param sub the ustring to test
 * @param suffix the suffix to match
 * @return a boolean
 */
static inline bool ustring_match_sfx(const struct ustring sub,
                                     const struct ustring suffix)
{
    return suffix.len > sub.len ? false :
        ustring_cmp(ustring_shift(sub, sub.len - suffix.len), suffix) == 0;
}

/** @This returns true if the ustring sub end with ustring suffix
 * ignoring the case.
 *
 * @param sub the ustring to test
 * @param suffix the suffix to match
 * @return a boolean
 */
static inline bool ustring_casematch_sfx(const struct ustring sub,
                                         const struct ustring suffix)
{
    return suffix.len > sub.len ? false :
        ustring_casecmp(ustring_shift(sub, sub.len - suffix.len), suffix) == 0;
}

/** @This returns the beginning of ustring sub containing only characters
 * present in set. And shift sub accordingly.
 *
 * @param sub an ustring
 * @param set set of allowed characters
 * @return an ustring
 */
static inline struct ustring ustring_split_while(struct ustring *sub,
                                                 const char *set)
{
    struct ustring tmp = ustring_while(*sub, set);
    *sub = ustring_shift(*sub, tmp.len);
    return tmp;
}

/** @This returns the beginning of ustring sub containing only characters
 * absent from set. And shift sub accordingly.
 *
 * @param sub an ustring
 * @param set set of allowed characters
 * @return an ustring
 */
static inline struct ustring ustring_split_until(struct ustring *sub,
                                                 const char *set)
{
    struct ustring tmp = ustring_until(*sub, set);
    *sub = ustring_shift(*sub, tmp.len);
    return tmp;
}

/** @This splits an ustring into two ustring at the first separator found.
 *
 * @param sub a pointer to an ustring
 * @param separators set of separators
 * @return the sub ustring before the separator
 */
static inline struct ustring ustring_split_sep(struct ustring *sub,
                                               const char *separators)
{
    struct ustring left = ustring_until(*sub, separators);
    struct ustring right = ustring_shift(*sub, left.len);
    *sub = right.len ? ustring_shift(right, 1) : ustring_null();
    return left;
}

/** @This returns the matched prefix or a null ustring and shift ustring sub.
 *
 * @param sub the ustring to test and shift
 * @param prefix the prefix to match
 * @return the matched prefix of a null ustring
 */
static inline struct ustring ustring_split_match(struct ustring *sub,
                                                 const struct ustring prefix)
{
    if (ustring_match(*sub, prefix)) {
        struct ustring tmp = *sub;
        *sub = ustring_shift(*sub, prefix.len);
        return tmp;
    }
    return ustring_null();
}

/** @This returns the matched prefix or a null ustring and shift ustring sub.
 *
 * @param sub the ustring to test and shift
 * @param prefix the prefix to match
 * @return the matched prefix of a null ustring
 */
static inline struct ustring ustring_split_match_str(struct ustring *sub,
                                                     const char *prefix)
{
    struct ustring ustring_prefix = ustring_from_str(prefix);
    if (ustring_match(*sub, ustring_prefix)) {
        struct ustring tmp = *sub;
        *sub = ustring_shift(*sub, ustring_prefix.len);
        return tmp;
    }
    return ustring_null();
}

/** @This returns the matched prefix ignoring case or a null ustring and shift
 * ustring sub.
 *
 * @param sub the ustring to test and shift
 * @param prefix the prefix to match
 * @return the matched prefix of a null ustring
 */
static inline struct ustring
ustring_split_casematch(struct ustring *sub, const struct ustring prefix)
{
    if (ustring_casematch(*sub, prefix)) {
        struct ustring tmp = *sub;
        *sub = ustring_shift(*sub, prefix.len);
        return tmp;
    }
    return ustring_null();
}

/** @This returns the matched prefix ignoring case or a null ustring and shift
 * ustring sub.
 *
 * @param sub the ustring to test and shift
 * @param prefix the prefix to match
 * @return the matched prefix of a null ustring
 */
static inline struct ustring
ustring_split_casematch_str(struct ustring *sub, const char *prefix)
{
    struct ustring ustring_prefix = ustring_from_str(prefix);
    if (ustring_casematch(*sub, ustring_prefix)) {
        struct ustring tmp = *sub;
        *sub = ustring_shift(*sub, ustring_prefix.len);
        return tmp;
    }
    return ustring_null();
}

static inline struct ustring
ustring_unframe(const struct ustring ustring, char c)
{
    if (ustring.len >= 2 &&
        ustring.at[0] == ustring.at[ustring.len - 1] &&
        ustring.at[0] == c) {
        return ustring_sub(ustring, 1, ustring.len - 2);
    }
    return ustring;
}

struct ustring_uint64 {
    struct ustring str;
    uint64_t value;
};

struct ustring_uint64 ustring_to_uint64(const struct ustring str, int base);

static inline struct ustring_uint64 ustring_to_uint64_str(const char *str,
                                                          int base)
{
    return ustring_to_uint64(ustring_from_str(str), base);
}

struct ustring_time {
    struct ustring str;
    uint64_t value;
};

struct ustring_time ustring_to_time(const struct ustring str);

static inline struct ustring_time ustring_to_time_str(const char *str)
{
    return ustring_to_time(ustring_from_str(str));
}

struct ustring_size {
    struct ustring str;
    uint64_t value;
};

struct ustring_size ustring_to_size(const struct ustring str);

#ifdef __cplusplus
}
#endif
#endif
