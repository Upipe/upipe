/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UREF_HTTP_FLOW_H_
# define _UPIPE_MODULES_UREF_HTTP_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref_attr.h"

UREF_ATTR_STRING(http, content_type, "http.content_type", http content type);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UREF_HTTP_FLOW_H_ */
