/*
 * Copyright (C) 2024 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short probe catching http scheme hook for SSL connection
 */

#ifndef _UPIPE_OPENSSL_UPROBE_HTTPS_OPENSSL_H_
#define _UPIPE_OPENSSL_UPROBE_HTTPS_OPENSSL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe.h"

/** @This allocates and initializes a new uprobe_https structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_https_openssl_alloc(struct uprobe *next);

#ifdef __cplusplus
}
#endif
#endif
