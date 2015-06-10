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

#include <upipe/uuri.h>
#include <upipe/uref_uri.h>

#include <stdlib.h>

int uref_uri_set(struct uref *uref, const struct uuri *uuri)
{
    if (!uref || !uuri)
        return UBASE_ERR_INVALID;

    const struct {
        struct ustring ustring;
        int (*set)(struct uref *, const char *value);
    } values[] = {
        { uuri->scheme, uref_uri_set_scheme },
        { uuri->authority.userinfo, uref_uri_set_userinfo },
        { uuri->authority.host, uref_uri_set_host },
        { uuri->authority.port, uref_uri_set_port },
        { uuri->path, uref_uri_set_path },
        { uuri->query, uref_uri_set_query },
        { uuri->fragment, uref_uri_set_fragment },
    };

    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(values); i++) {
        if (!values[i].ustring.at)
            continue;

        char value[values[i].ustring.len + 1];
        ustring_cpy(values[i].ustring, value, sizeof (value));

        int ret = values[i].set(uref, value);
        if (!ubase_check(ret))
            return ret;
    }

    return UBASE_ERR_NONE;
}

int uref_uri_get(struct uref *uref, struct uuri *uuri)
{
    if (!uref || !uuri)
        return UBASE_ERR_INVALID;

    struct {
        int (*cb)(struct uref *, const char **);
        struct ustring *ustring;
    } fields[] = {
        { uref_uri_get_scheme, &uuri->scheme },
        { uref_uri_get_userinfo, &uuri->authority.userinfo },
        { uref_uri_get_host, &uuri->authority.host },
        { uref_uri_get_port, &uuri->authority.port },
        { uref_uri_get_path, &uuri->path },
        { uref_uri_get_query, &uuri->query },
        { uref_uri_get_fragment, &uuri->fragment },
    };

    *uuri = uuri_null();

    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(fields); i++) {
        const char *value;
        int ret = fields[i].cb(uref, &value);
        if (!ubase_check(ret))
            continue;
        *fields[i].ustring = ustring_from_str(value);
    }

    return UBASE_ERR_NONE;
}

int uref_uri_set_from_str(struct uref *uref, const char *str)
{
    struct uuri uuri = uuri_null();

    int ret = uuri_from_str(&uuri, str);
    if (ret)
        return ret;
    return uref_uri_set(uref, &uuri);
}

int uref_uri_get_to_str(struct uref *uref, char **str_p)
{
    struct uuri uuri = uuri_null();

    int ret = uref_uri_get(uref, &uuri);
    if (ret)
        return ret;
    return uuri_to_str(&uuri, str_p);
}
