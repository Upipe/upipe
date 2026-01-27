/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UREF_AES_FLOW_H_
# define _UPIPE_MODULES_UREF_AES_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref_attr.h"

UREF_ATTR_STRING(aes, method, "aes.method", aes method);
UREF_ATTR_OPAQUE(aes, key, "aes.key", aes key);
UREF_ATTR_OPAQUE(aes, iv, "aes.iv", aes initialization vector);

static inline int uref_aes_delete(struct uref *uref)
{
    int (*list[])(struct uref *) = {
        uref_aes_delete_method,
        uref_aes_delete_key,
        uref_aes_delete_iv,
    };

    return uref_attr_delete_list(uref, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_aes_copy(struct uref *uref, struct uref *uref_src)
{
    int (*list[])(struct uref *, struct uref *) = {
        uref_aes_copy_method,
        uref_aes_copy_key,
        uref_aes_copy_iv,
    };
    return uref_attr_copy_list(uref, uref_src, list, UBASE_ARRAY_SIZE(list));
}

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UREF_AES_FLOW_H_ */
