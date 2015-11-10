/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#ifndef _UPIPE_MODULES_UREF_AES_FLOW_H_
# define _UPIPE_MODULES_UREF_AES_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref_attr.h>

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
