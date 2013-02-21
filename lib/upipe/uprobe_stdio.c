/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short probe outputting all print events to stdio
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_stdio {
    /** file stream to write to */
    FILE *stream;
    /** minimum level of printed messages */
    enum uprobe_log_level min_level;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_stdio, uprobe)

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return always false
 */
static bool uprobe_stdio_throw(struct uprobe *uprobe, struct upipe *upipe,
                               enum uprobe_event event, va_list args)
{
    struct uprobe_stdio *uprobe_stdio = uprobe_stdio_from_uprobe(uprobe);

    switch (event) {
        case UPROBE_LOG: {
            enum uprobe_log_level level = va_arg(args, enum uprobe_log_level);
            if (uprobe_stdio->min_level > level)
                return true;
            const char *level_name;
            switch (level) {
                case UPROBE_LOG_DEBUG: level_name = "debug"; break;
                case UPROBE_LOG_NOTICE: level_name = "notice"; break;
                case UPROBE_LOG_WARNING: level_name = "warning"; break;
                case UPROBE_LOG_ERROR: level_name = "error"; break;
                default: level_name = "unknown"; break;
            }

            const char *msg = va_arg(args, const char *);
            fprintf(uprobe_stdio->stream, "%s: %s\n", level_name, msg);
            return true;
        }
        default:
            return false;
    }
}

/** @This allocates a new uprobe stdio structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_stdio_alloc(struct uprobe *next, FILE *stream,
                                  enum uprobe_log_level min_level)
{
    struct uprobe_stdio *uprobe_stdio = malloc(sizeof(struct uprobe_stdio));
    if (unlikely(uprobe_stdio == NULL))
        return NULL;
    struct uprobe *uprobe = uprobe_stdio_to_uprobe(uprobe_stdio);
    uprobe_stdio->stream = stream;
    uprobe_stdio->min_level = min_level;
    uprobe_init(uprobe, uprobe_stdio_throw, next);
    return uprobe;
}

/** @This frees a uprobe stdio structure.
 *
 * @param uprobe structure to free
 * @return next probe
 */
struct uprobe *uprobe_stdio_free(struct uprobe *uprobe)
{
    struct uprobe *next = uprobe->next;
    struct uprobe_stdio *uprobe_stdio = uprobe_stdio_from_uprobe(uprobe);
    free(uprobe_stdio);
    return next;
}
