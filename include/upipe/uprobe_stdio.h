/*
 * Copyright (C) 2013-2017 OpenHeadend S.A.R.L.
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Christophe Massiot
 *          Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short probe outputting all log events to stdio
 */

#ifndef _UPIPE_UPROBE_STDIO_H_
/** @hidden */
#define _UPIPE_UPROBE_STDIO_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe.h"
#include "upipe/uprobe_helper_uprobe.h"

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_stdio {
    /** file stream to write to */
    FILE *stream;
    /** minimum level of printed messages */
    enum uprobe_log_level min_level;
    /** colored output enabled? */
    bool colored;
    /** timing output format or NULL */
    char *time_format;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_stdio, uprobe)

/** @This initializes an already allocated uprobe_stdio structure.
 *
 * @param uprobe_stdio pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param stream stdio stream to which to log the messages
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_stdio_init(struct uprobe_stdio *uprobe_stdio,
                                 struct uprobe *next, FILE *stream,
                                 enum uprobe_log_level min_level);

/** @This cleans a uprobe_stdio structure.
 *
 * @param uprobe_stdio structure to clean
 */
void uprobe_stdio_clean(struct uprobe_stdio *uprobe_stdio);

/** @This allocates a new uprobe stdio structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param stream stdio stream to which to log the messages
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_stdio_alloc(struct uprobe *next, FILE *stream,
                                  enum uprobe_log_level min_level);

/** @This enables or disables colored output.
 *
 * @param uprobe pointer to probe
 * @param enabled enable (or disable) colored output
 */
void uprobe_stdio_set_color(struct uprobe *uprobe, bool enabled);

/** @This sets the output time format or disables it.
 *
 * @param uprobe pointer to probe
 * @param format strftime format or NULL to disable
 * @return an error code
 */
int uprobe_stdio_set_time_format(struct uprobe *uprobe, const char *format);

#ifdef __cplusplus
}
#endif
#endif
