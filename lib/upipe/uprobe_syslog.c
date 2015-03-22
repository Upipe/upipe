/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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

/** @file
 * @short probe outputting all log events to syslog
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_syslog.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <stdarg.h>

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_syslog_throw(struct uprobe *uprobe, struct upipe *upipe,
                              int event, va_list args)
{
    struct uprobe_syslog *uprobe_syslog = uprobe_syslog_from_uprobe(uprobe);
    if (event != UPROBE_LOG)
        return uprobe_throw_next(uprobe, upipe, event, args);

    enum uprobe_log_level level = va_arg(args, enum uprobe_log_level);
    if (uprobe_syslog->min_level > level)
        return UBASE_ERR_NONE;
    int priority;
    switch (level) {
        case UPROBE_LOG_VERBOSE: priority = LOG_DEBUG; break;
        case UPROBE_LOG_DEBUG: priority = LOG_DEBUG; break;
        case UPROBE_LOG_NOTICE: priority = LOG_NOTICE; break;
        case UPROBE_LOG_WARNING: priority = LOG_WARNING; break;
        case UPROBE_LOG_ERROR: priority = LOG_ERR; break;
        default: priority = LOG_WARNING; break;
    }

    const char *msg = va_arg(args, const char *);
    syslog(priority, "%s", msg);
    return UBASE_ERR_NONE;
}

/** @This initializes an already allocated uprobe_syslog structure.
 *
 * @param uprobe_syslog pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param ident syslog ident string
 * @param option syslog option (see syslog(3))
 * @param facility syslog facility (see syslog(3))
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_syslog_init(struct uprobe_syslog *uprobe_syslog,
                                  struct uprobe *next, char *ident,
                                  int option, int facility,
                                  enum uprobe_log_level min_level)
{
    assert(uprobe_syslog != NULL);
    struct uprobe *uprobe = uprobe_syslog_to_uprobe(uprobe_syslog);
    uprobe_syslog->ident = strdup(ident);
    if (!uprobe_syslog->ident)
        return NULL;
    uprobe_syslog->min_level = min_level;

    openlog(uprobe_syslog->ident, option, facility);

    uprobe_init(uprobe, uprobe_syslog_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_syslog structure.
 *
 * @param uprobe_syslog structure to clean
 */
void uprobe_syslog_clean(struct uprobe_syslog *uprobe_syslog)
{
    assert(uprobe_syslog != NULL);
    struct uprobe *uprobe = uprobe_syslog_to_uprobe(uprobe_syslog);
    closelog();
    free(uprobe_syslog->ident);
    uprobe_syslog->ident = NULL;
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, char *ident, int option, int facility, enum uprobe_log_level min_level
#define ARGS next, ident, option, facility, min_level
UPROBE_HELPER_ALLOC(uprobe_syslog)
#undef ARGS
#undef ARGS_DECL
