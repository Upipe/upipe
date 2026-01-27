/*
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short OpenSSL HTTPS hooks for SSL data read/write.
 */

#ifndef _UPIPE_MODULES_HTTPS_SOURCE_HOOK_OPENSSL_H_
#define _UPIPE_MODULES_HTTPS_SOURCE_HOOK_OPENSSL_H_

#include "upipe/uref.h"
#include "upipe-modules/upipe_http_source.h"

/** @This allocates and initializes an OpenSSL context.
 *
 * @param flow_def connection attributes
 * @return the public hook description
 */
struct upipe_http_src_hook *https_src_hook_openssl_alloc(struct uref *flow_def);

#endif
