/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
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
