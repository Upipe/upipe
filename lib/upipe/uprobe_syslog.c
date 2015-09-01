/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Kieran Kunhya
 *          Christophe Massiot
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

    struct ulog *ulog = va_arg(args, struct ulog *);

    if (uprobe_syslog->min_level > ulog->level)
        return UBASE_ERR_NONE;
    int priority;
    switch (ulog->level) {
        case UPROBE_LOG_VERBOSE: priority = LOG_DEBUG; break;
        case UPROBE_LOG_DEBUG: priority = LOG_DEBUG; break;
        case UPROBE_LOG_NOTICE: priority = LOG_NOTICE; break;
        case UPROBE_LOG_WARNING: priority = LOG_WARNING; break;
        case UPROBE_LOG_ERROR: priority = LOG_ERR; break;
        default: priority = LOG_WARNING; break;
    }

    size_t len = 0;
    if (!uprobe_syslog->inited) {
        priority |= uprobe_syslog->facility;
        if (uprobe_syslog->ident != NULL)
            len += strlen(uprobe_syslog->ident) + 2;
    }

    struct uchain *uchain;
    ulist_foreach_reverse(&ulog->prefixes, uchain) {
        struct ulog_pfx *ulog_pfx = ulog_pfx_from_uchain(uchain);
        len += strlen(ulog_pfx->tag) + 3;
    }

    char buffer[len + 1];
    memset(buffer, 0, sizeof (buffer));
    char *tmp = buffer;
    if (!uprobe_syslog->inited && uprobe_syslog->ident != NULL)
        tmp += sprintf(tmp, "%s: ", uprobe_syslog->ident);

    ulist_foreach_reverse(&ulog->prefixes, uchain) {
        struct ulog_pfx *ulog_pfx = ulog_pfx_from_uchain(uchain);
        tmp += sprintf(tmp, "[%s] ", ulog_pfx->tag);
    }

    syslog(priority, "%s%s", buffer, ulog->msg);
    return UBASE_ERR_NONE;
}

/** @This initializes an already allocated uprobe_syslog structure.
 *
 * @param uprobe_syslog pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param ident syslog ident string (may be NULL)
 * @param option syslog option (see syslog(3)), or -1 to not call openlog
 * @param facility syslog facility (see syslog(3))
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_syslog_init(struct uprobe_syslog *uprobe_syslog,
                                  struct uprobe *next, const char *ident,
                                  int option, int facility,
                                  enum uprobe_log_level min_level)
{
    assert(uprobe_syslog != NULL);
    struct uprobe *uprobe = uprobe_syslog_to_uprobe(uprobe_syslog);

    if (ident != NULL) {
        uprobe_syslog->ident = strdup(ident);
        if (!uprobe_syslog->ident)
            return NULL;
    } else
        uprobe_syslog->ident = NULL;

    uprobe_syslog->min_level = min_level;
    uprobe_syslog->facility = facility;
    uprobe_syslog->inited = option != -1;

    if (uprobe_syslog->inited)
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
    if (uprobe_syslog->inited)
        closelog();
    ubase_clean_str(&uprobe_syslog->ident);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, const char *ident, int option, int facility, enum uprobe_log_level min_level
#define ARGS next, ident, option, facility, min_level
UPROBE_HELPER_ALLOC(uprobe_syslog)
#undef ARGS
#undef ARGS_DECL
