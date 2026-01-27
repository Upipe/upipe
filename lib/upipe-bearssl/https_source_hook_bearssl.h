/*
 * Copyright (C) 2020-2024 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short BearSSL HTTPS hooks for SSL data read/write.
 */

#ifndef _UPIPE_BEARSSL_HTTPS_SOURCE_HOOK_BEARSSL_H_
#define _UPIPE_BEARSSL_HTTPS_SOURCE_HOOK_BEARSSL_H_

#include "upipe/uref.h"
#include "upipe-modules/upipe_http_source.h"

/** @This allocates and initializes a BearSSL context.
 *
 * @param flow_def connection attributes
 * @return the public hook description
 */
struct upipe_http_src_hook *https_src_hook_bearssl_alloc(struct uref *flow_def);

#endif
