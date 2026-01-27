/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_UPROBE_LOGLEVEL_H_
/** @hidden */
# define _UPIPE_UPROBE_LOGLEVEL_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ulist.h"
#include "upipe/uprobe.h"
#include "upipe/uprobe_helper_uprobe.h"

struct uprobe_loglevel {
    /** minimum level of printed messages */
    enum uprobe_log_level min_level;
    /** uprobe structure */
    struct uprobe uprobe;
    /** list of pattern */
    struct uchain patterns;
};

UPROBE_HELPER_UPROBE(uprobe_loglevel, uprobe)

/** @This initializes an already allocated uprobe_loglevel structure.
 *
 * @param uprobe_loglevel pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_loglevel_init(
    struct uprobe_loglevel *uprobe_loglevel,
    struct uprobe *next,
    enum uprobe_log_level min_level);

/** @This cleans a uprobe_loglevel structure.
 *
 * @param uprobe_loglevel structure to clean
 */
void uprobe_loglevel_clean(struct uprobe_loglevel *uprobe_loglevel);

/** @This allocates a new uprobe_loglevel structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_loglevel_alloc(struct uprobe *next,
                                     enum uprobe_log_level level);

int uprobe_loglevel_set(struct uprobe *uprobe,
                        const char *regex,
                        enum uprobe_log_level log_level);

#ifdef __cplusplus
}
#endif
#endif
