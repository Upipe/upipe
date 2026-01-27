/*
 * Copyright (C) 2013-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short probe prefixing all print events with a given name
 */

#ifndef _UPIPE_UPROBE_PREFIX_H_
/** @hidden */
#define _UPIPE_UPROBE_PREFIX_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe.h"
#include "upipe/uprobe_helper_uprobe.h"

#include <stdbool.h>

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_pfx {
    /** name of the pipe (informative) */
    char *name;
    /** minimum level of messages to pass-through */
    enum uprobe_log_level min_level;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_pfx, uprobe)

/** @This initializes an already allocated uprobe pfx structure.
 *
 * @param uprobe_pfx pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param min_level minimum level of passed-through messages
 * @param name name of the pipe (informative)
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pfx_init(struct uprobe_pfx *uprobe_pfx,
                               struct uprobe *next,
                               enum uprobe_log_level min_level,
                               const char *name);

/** @This cleans a uprobe pfx structure.
 *
 * @param uprobe_pfx structure to clean
 */
void uprobe_pfx_clean(struct uprobe_pfx *uprobe_pfx);

/** @This returns the name of the pipe.
 *
 * @param uprobe pointer to probe
 * @return name of the pipe (informative)
 */
const char *uprobe_pfx_get_name(struct uprobe *uprobe);

/** @This allocates a new uprobe pfx structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param min_level minimum level of passed-through messages
 * @param name name of the pipe (informative)
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pfx_alloc(struct uprobe *next,
                                enum uprobe_log_level min_level,
                                const char *name);

/** @This allocates a new uprobe pfx structure, with printf-style name
 * generation.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param min_level minimum level of passed-through messages
 * @param format printf-style format for the name, followed by optional
 * arguments
 * @return pointer to uprobe, or NULL in case of error
 */
UBASE_FMT_PRINTF(3, 4)
struct uprobe *uprobe_pfx_alloc_va(struct uprobe *next,
                                   enum uprobe_log_level min_level,
                                   const char *format, ...);

#ifdef __cplusplus
}
#endif
#endif
