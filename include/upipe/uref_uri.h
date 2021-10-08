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

#include "upipe/uref_attr.h"
#include "upipe/uuri.h"

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

static inline int uref_uri_delete(struct uref *uref)
{
    int (*list[])(struct uref *) = {
        uref_uri_delete_scheme,
        uref_uri_delete_userinfo,
        uref_uri_delete_host,
        uref_uri_delete_port,
        uref_uri_delete_path,
        uref_uri_delete_query,
        uref_uri_delete_fragment,
    };
    return uref_attr_delete_list(uref, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_uri_copy(struct uref *uref, struct uref *from)
{
    int (*list[])(struct uref *, struct uref *) = {
        uref_uri_copy_scheme,
        uref_uri_copy_userinfo,
        uref_uri_copy_host,
        uref_uri_copy_port,
        uref_uri_copy_path,
        uref_uri_copy_query,
        uref_uri_copy_fragment,
    };
    return uref_attr_copy_list(uref, from, list, UBASE_ARRAY_SIZE(list));
}

#ifdef __cplusplus
}
#endif
#endif
