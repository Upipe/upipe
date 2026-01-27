/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_UREF_M3U_H_
/** @hidden */
#define _UPIPE_UREF_M3U_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref_attr.h"

UREF_ATTR_STRING(m3u, uri, "m3u.uri", uri)

UREF_ATTR_STRING(m3u_variable, name, "m3u.variable.name", variable name);
UREF_ATTR_STRING(m3u_variable, value, "m3u.variable.value", variable value);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UREF_M3U_H_ */
