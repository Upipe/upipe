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
 * @short Upipe functions to parse HTTP cookies
 */

#ifndef _UPIPE_UCOOKIE_H_
/** @hidden */
# define _UPIPE_UCOOKIE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ustring.h>

/** @This stores a parsed cookie.  */
struct ucookie {
    /** cookie name */
    struct ustring name;
    /** cookie value */
    struct ustring value;
    /** cookie expiration */
    struct ustring expires;
    /** cookie max age */
    struct ustring max_age;
    /** cookie domain */
    struct ustring domain;
    /** cookie path */
    struct ustring path;
    /** cookie secure */
    bool secure;
    /** cookie http only */
    bool http_only;
};

/** @This returns an initialized ucookie structure.
 *
 * @return an initialized ucookie structure
 */
static inline struct ucookie ucookie_null(void)
{
    struct ucookie ucookie;
    ucookie.name = ustring_null();
    ucookie.value = ustring_null();
    ucookie.expires = ustring_null();
    ucookie.max_age = ustring_null();
    ucookie.domain = ustring_null();
    ucookie.path = ustring_null();
    ucookie.secure = false;
    ucookie.http_only = false;
    return ucookie;
}

/** @This makes an ucookie structure from a string.
 *
 * @param ucookie a pointer to an ucookie structure
 * @param str the string to parse
 * @return an error code
 */
int ucookie_from_str(struct ucookie *ucookie, const char *str);

#ifdef __cplusplus
}
#endif
#endif
