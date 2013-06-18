/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short probe prefixing all log events with a given name
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_helper_adhoc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

/** @hidden */
struct uprobe *uprobe_pfx_free(struct uprobe *uprobe);

UPROBE_HELPER_ADHOC(uprobe_pfx, adhoc_pipe)

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true for log events
 */
static bool uprobe_pfx_throw(struct uprobe *uprobe, struct upipe *upipe,
                             enum uprobe_event event, va_list args)
{
    struct uprobe_pfx *uprobe_pfx = uprobe_pfx_from_uprobe(uprobe);
    switch (event) {
        case UPROBE_LOG: {
            enum uprobe_log_level level = va_arg(args, enum uprobe_log_level);
            if (uprobe->next == NULL || uprobe_pfx->min_level > level)
                return true;

            const char *msg = va_arg(args, const char *);
            const char *name = likely(uprobe_pfx->name != NULL) ?
                               uprobe_pfx->name : "unknown";
            char new_msg[strlen(msg) + +strlen(name) + strlen("[] ") + 1];
            sprintf(new_msg, "[%s] %s", name, msg);
            uprobe_throw(uprobe->next, upipe, event, level, new_msg);
            return true;
        }
        default:
            if (uprobe_pfx->adhoc)
                return uprobe_pfx_throw_adhoc(uprobe, upipe, event, args);
            else
                return false;
    }
}

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
                               const char *name)
{
    assert(uprobe_pfx != NULL);
    struct uprobe *uprobe = uprobe_pfx_to_uprobe(uprobe_pfx);
    if (likely(name != NULL)) {
        uprobe_pfx->name = strdup(name);
        if (unlikely(uprobe_pfx->name == NULL)) {
            uprobe_throw(next, NULL, UPROBE_LOG, UPROBE_LOG_ERROR,
                         "allocation error in uprobe_pfx_init");
            uprobe_throw(next, NULL, UPROBE_AERROR);
        }
    } else
        uprobe_pfx->name = NULL;
    uprobe_pfx->min_level = min_level;
    uprobe_pfx->adhoc = false;
    uprobe_init(uprobe, uprobe_pfx_throw, next);
    return uprobe;
}

/** @This cleans a uprobe pfx structure.
 *
 * @param uprobe structure to clean
 */
void uprobe_pfx_clean(struct uprobe_pfx *uprobe_pfx)
{
    free(uprobe_pfx->name);
    if (uprobe_pfx->adhoc)
        uprobe_pfx_clean_adhoc(&uprobe_pfx->uprobe);
}

/** @This allocates a new uprobe pfx structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param min_level minimum level of passed-through messages
 * @param name name of the pipe (informative)
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pfx_alloc(struct uprobe *next,
                                enum uprobe_log_level min_level,
                                const char *name)
{
    struct uprobe_pfx *uprobe_pfx = malloc(sizeof(struct uprobe_pfx));
    if (unlikely(uprobe_pfx == NULL))
        return NULL;
    return uprobe_pfx_init(uprobe_pfx, next, min_level, name);
}

/** @This allocates a new uprobe pfx structure, with printf-style name
 * generation.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param min_level minimum level of passed-through messages
 * @param format printf-style format for the name, followed by optional
 * arguments
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pfx_alloc_va(struct uprobe *next,
                                   enum uprobe_log_level min_level,
                                   const char *format, ...)
{
    UBASE_VARARG(uprobe_pfx_alloc(next, min_level, string))
}

/** @This allocates a new uprobe pfx structure in ad-hoc mode (will be
 * deallocated when the pipe dies).
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param min_level minimum level of passed-through messages
 * @param name name of the pipe (informative)
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pfx_adhoc_alloc(struct uprobe *next,
                                      enum uprobe_log_level min_level,
                                      const char *name)
{
    struct uprobe *uprobe = uprobe_pfx_alloc(next, min_level, name);
    if (unlikely(uprobe == NULL)) {
        uprobe_throw_aerror(next, NULL);
        /* we still return the next probe so that the pipe still has a
         * probe hierarchy */
        return next;
    }
    struct uprobe_pfx *uprobe_pfx = uprobe_pfx_from_uprobe(uprobe);
    uprobe_pfx->adhoc = true;
    uprobe_pfx_init_adhoc(uprobe);
    return uprobe;
}

/** @This allocates a new uprobe pfx structure in ad-hoc mode (will be
 * deallocated when the pipe dies), with printf-style name generation.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param min_level minimum level of passed-through messages
 * @param format printf-style format for the name, followed by optional
 * arguments
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pfx_adhoc_alloc_va(struct uprobe *next,
                                         enum uprobe_log_level min_level,
                                         const char *format, ...)
{
    UBASE_VARARG(uprobe_pfx_adhoc_alloc(next, min_level, string))
}

/** @This frees a uprobe pfx structure.
 *
 * @param uprobe structure to free
 * @return next probe
 */
struct uprobe *uprobe_pfx_free(struct uprobe *uprobe)
{
    struct uprobe *next = uprobe->next;
    struct uprobe_pfx *uprobe_pfx = uprobe_pfx_from_uprobe(uprobe);
    uprobe_pfx_clean(uprobe_pfx);
    free(uprobe_pfx);
    return next;
}
