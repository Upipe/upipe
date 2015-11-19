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

#ifndef _UPIPE_UREF_URI_H_
# define _UPIPE_UREF_URI_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref_attr.h>
#include <upipe/uuri.h>

UREF_ATTR_STRING(uri, scheme, "uri.scheme", uri scheme)
UREF_ATTR_STRING(uri, userinfo, "uri.userinfo", uri user info)
UREF_ATTR_STRING(uri, host, "uri.host", uri host)
UREF_ATTR_STRING(uri, port, "uri.port", uri port)
UREF_ATTR_STRING(uri, path, "uri.path", uri path)
UREF_ATTR_STRING(uri, query, "uri.query", uri query)
UREF_ATTR_STRING(uri, fragment, "uri.fragment", uri fragment)

int uref_uri_set(struct uref *uref, const struct uuri *uuri);
int uref_uri_get(struct uref *uref, struct uuri *uuri);
int uref_uri_set_from_str(struct uref *uref, const char *str);
int uref_uri_get_to_str(struct uref *uref, char **str_p);

static inline void uref_uri_delete(struct uref *uref)
{
    uref_uri_delete_scheme(uref);
    uref_uri_delete_userinfo(uref);
    uref_uri_delete_host(uref);
    uref_uri_delete_port(uref);
    uref_uri_delete_path(uref);
    uref_uri_delete_query(uref);
    uref_uri_delete_fragment(uref);
}

static inline int uref_uri_import(struct uref *uref,
                                  struct uref *from)
{
    struct uuri uuri;
    int ret;

    uref_uri_delete(uref);
    if (!from)
        return UBASE_ERR_NONE;
    ret = uref_uri_get(from, &uuri);
    if (!ubase_check(ret))
        return ret;
    return uref_uri_set(uref, &uuri);
}

#ifdef __cplusplus
}
#endif
#endif
