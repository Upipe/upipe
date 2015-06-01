/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short probe outputting all log events to stdio
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdio.h>
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
static int uprobe_stdio_throw(struct uprobe *uprobe, struct upipe *upipe,
                              int event, va_list args)
{
    struct uprobe_stdio *uprobe_stdio = uprobe_stdio_from_uprobe(uprobe);
    if (event != UPROBE_LOG)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct ulog *ulog = va_arg(args, struct ulog *);

    if (uprobe_stdio->min_level > ulog->level)
        return UBASE_ERR_NONE;
    const char *level_name;
    switch (ulog->level) {
        case UPROBE_LOG_VERBOSE: level_name = "verbose"; break;
        case UPROBE_LOG_DEBUG: level_name = "debug"; break;
        case UPROBE_LOG_NOTICE: level_name = "notice"; break;
        case UPROBE_LOG_WARNING: level_name = "warning"; break;
        case UPROBE_LOG_ERROR: level_name = "error"; break;
        default: level_name = "unknown"; break;
    }

    size_t len = 0;
    struct uchain *uchain;
    ulist_foreach_reverse(&ulog->prefixes, uchain) {
        struct ulog_pfx *ulog_pfx = ulog_pfx_from_uchain(uchain);
        len += strlen(ulog_pfx->tag) + 3;
    }

    char buffer[len + 1];
    memset(buffer, 0, sizeof (buffer));
    char *tmp = buffer;
    ulist_foreach_reverse(&ulog->prefixes, uchain) {
        struct ulog_pfx *ulog_pfx = ulog_pfx_from_uchain(uchain);
        tmp += sprintf(tmp, "[%s] ", ulog_pfx->tag);
    }

    fprintf(uprobe_stdio->stream, "%s: %s%s\n", level_name, buffer, ulog->msg);
    return UBASE_ERR_NONE;
}

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
                                 enum uprobe_log_level min_level)
{
    assert(uprobe_stdio != NULL);
    struct uprobe *uprobe = uprobe_stdio_to_uprobe(uprobe_stdio);
    uprobe_stdio->stream = stream;
    uprobe_stdio->min_level = min_level;
    uprobe_init(uprobe, uprobe_stdio_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_stdio structure.
 *
 * @param uprobe_stdio structure to clean
 */
void uprobe_stdio_clean(struct uprobe_stdio *uprobe_stdio)
{
    assert(uprobe_stdio != NULL);
    struct uprobe *uprobe = uprobe_stdio_to_uprobe(uprobe_stdio);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, FILE *stream, enum uprobe_log_level min_level
#define ARGS next, stream, min_level
UPROBE_HELPER_ALLOC(uprobe_stdio)
#undef ARGS
#undef ARGS_DECL
