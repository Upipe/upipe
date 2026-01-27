/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPROBE_HTTP_REDIRECT_H_
/** @hidden */
# define _UPIPE_MODULES_UPROBE_HTTP_REDIRECT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe.h"
#include "upipe/uprobe_helper_uprobe.h"

struct uprobe_http_redir {
    /** uprobe structure */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_http_redir, uprobe)

/** @This initializes an uprobe_http_redir.
 *
 * @param uprobe_http_redir pointer to the allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_http_redir_init(
    struct uprobe_http_redir *uprobe_http_redir,
    struct uprobe *next);

/** @This cleans a uprobe_http_redir structure.
 *
 * @param uprobe_http_redir structure to clean
 */
void uprobe_http_redir_clean(struct uprobe_http_redir *uprobe_http_redir);

/** @This allocates a new uprobe_http_redir structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_http_redir_alloc(struct uprobe *next);

#ifdef __cplusplus
}
#endif
#endif
