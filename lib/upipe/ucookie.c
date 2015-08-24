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

#include <upipe/ucookie.h>

#define UCOOKIE_ALPHA_LOWER \
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', \
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', \
    'u', 'v', 'w', 'x', 'y', 'z'
#define UCOOKIE_ALPHA_UPPER \
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', \
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', \
    'U', 'V', 'W', 'X', 'Y', 'Z'
#define UCOOKIE_ALPHA UCOOKIE_ALPHA_LOWER, UCOOKIE_ALPHA_UPPER
#define UCOOKIE_DIGIT \
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
#define UCOOKIE_TOKEN UCOOKIE_ALPHA, UCOOKIE_DIGIT, \
        '*', '!', '+', '^', '|', '#', '-', '_', '$', \
        '.', '~', '%', '&', '\'', '`'

#define UCOOKIE_VALUE UCOOKIE_ALPHA, UCOOKIE_DIGIT, \
    '!', '#', '$', '%', '&', '\'', '(', ')', '*', '+', '-', '.', '/', ':', \
    '<', '=', '>', '?', '@', '[', ']', '^', '_', '`', '{', '|', '}', '~'

static int ucookie_parse_cookie_pair(struct ucookie *ucookie,
                                     struct ustring *ustring)
{
    const char token_set[] = { UCOOKIE_TOKEN, 0 };
    struct ustring sub = *ustring;
    struct ustring name = ustring_split_while(&sub, token_set);
    if (!name.len)
        return UBASE_ERR_INVALID;
    if (ustring_is_null(ustring_split_match_str(&sub, "=")))
        return UBASE_ERR_INVALID;
    sub = ustring_shift_while(sub, " \t");

    bool dquote = false;
    if (!ustring_is_null(ustring_split_match_str(&sub, "\"")))
        dquote = true;

    const char value_set[] = { UCOOKIE_VALUE, 0 };
    struct ustring value = ustring_split_while(&sub, value_set);

    if (dquote &&
        ustring_is_null(ustring_split_match_str(&sub, "\"")))
        return UBASE_ERR_INVALID;

    ucookie->name = name;
    ucookie->value = value;
    *ustring = sub;
    return UBASE_ERR_NONE;
}

static int ucookie_parse_cookie_av(struct ucookie *ucookie,
                                   struct ustring *ustring)
{
    struct ustring sub = *ustring;
    sub = ustring_shift_while(sub, " \t");

    struct {
        const char *name;
        struct ustring *value;
    } avs[] = {
        { "Expires=", &ucookie->expires },
        { "Max-Age=", &ucookie->max_age },
        { "Domain=", &ucookie->domain },
        { "Path=", &ucookie->path },
    };
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(avs); i++) {
        if (ustring_is_null(ustring_split_casematch_str(&sub, avs[i].name)))
            continue;

        *avs[i].value = ustring_split_until(&sub, ";");
        *ustring = sub;
        return UBASE_ERR_NONE;
    }

    struct {
        const char *name;
        bool *value;
    } avs_b[] = {
        { "Secure", &ucookie->secure },
        { "HttpOnly", &ucookie->http_only },
    };
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(avs_b); i++) {
        if (ustring_is_null(ustring_split_casematch_str(&sub, avs_b[i].name)))
            continue;

        *avs_b[i].value = true;
        *ustring = sub;
        return UBASE_ERR_NONE;
    }

    sub = ustring_shift_until(sub, ";");
    *ustring = sub;
    return UBASE_ERR_NONE;
}

static int ucookie_parse_cookie_string(struct ucookie *ucookie,
                                       struct ustring *ustring)
{
    struct ustring sub = *ustring;

    int ret = ucookie_parse_cookie_pair(ucookie, &sub);
    if (!ubase_check(ret))
        return ret;

    do  {
        sub = ustring_shift_while(sub, " \t");
        if (!sub.len)
            break;

        if (ustring_is_null(ustring_split_match_str(&sub, ";")))
            return UBASE_ERR_INVALID;

        ret = ucookie_parse_cookie_av(ucookie, &sub);
        if (!ubase_check(ret))
            return ret;
    } while (1);

    return UBASE_ERR_NONE;
}

int ucookie_parse(struct ucookie *ucookie, struct ustring *ustring)
{
    struct ustring sub = *ustring;
    sub = ustring_shift_while(sub, " \t");
    return ucookie_parse_cookie_string(ucookie, &sub);
}

int ucookie_from_str(struct ucookie *ucookie, const char *str)
{
    struct ustring ustring = ustring_from_str(str);
    return ucookie_parse(ucookie, &ustring);
}
