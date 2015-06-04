/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_pfx_throw(struct uprobe *uprobe, struct upipe *upipe,
                            int event, va_list args)
{
    struct uprobe_pfx *uprobe_pfx = uprobe_pfx_from_uprobe(uprobe);
    if (event != UPROBE_LOG)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct ulog *ulog = va_arg(args, struct ulog *);
    enum uprobe_log_level level = ulog->level;
    if (uprobe->next == NULL || uprobe_pfx->min_level > level)
        return UBASE_ERR_NONE;

    struct ulog_pfx ulog_pfx;
    ulog_pfx.tag = likely(uprobe_pfx->name != NULL) ?
        uprobe_pfx->name : "unknown";
    ulist_add(&ulog->prefixes, ulog_pfx_to_uchain(&ulog_pfx));

    return uprobe_throw(uprobe->next, upipe, event, ulog);
}

/** @This initializes an already allocated uprobe_pfx structure.
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
            uprobe_err(next, NULL, "allocation error in uprobe_pfx_init");
            uprobe_throw(next, NULL, UPROBE_FATAL, UBASE_ERR_ALLOC);
        }
    } else
        uprobe_pfx->name = NULL;
    uprobe_pfx->min_level = min_level;
    uprobe_init(uprobe, uprobe_pfx_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_pfx structure.
 *
 * @param uprobe_pfx structure to clean
 */
void uprobe_pfx_clean(struct uprobe_pfx *uprobe_pfx)
{
    assert(uprobe_pfx != NULL);
    struct uprobe *uprobe = uprobe_pfx_to_uprobe(uprobe_pfx);
    free(uprobe_pfx->name);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, enum uprobe_log_level min_level, const char *name
#define ARGS next, min_level, name
UPROBE_HELPER_ALLOC(uprobe_pfx)
#undef ARGS
#undef ARGS_DECL

/** @This allocates a new uprobe_pfx structure, with printf-style name
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
