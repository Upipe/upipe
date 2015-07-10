/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/uprobe_loglevel.h>

#include <sys/types.h>
#include <regex.h>

struct pattern {
    struct uchain uchain;
    regex_t preq;
    enum uprobe_log_level log_level;
};

UBASE_FROM_TO(pattern, uchain, uchain, uchain)

static int uprobe_loglevel_throw(struct uprobe *uprobe,
                                 struct upipe *upipe,
                                 int event, va_list args)
{
    if (event != UPROBE_LOG)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct uprobe_loglevel *uprobe_loglevel =
        uprobe_loglevel_from_uprobe(uprobe);

    struct ulog *ulog = va_arg(args, struct ulog *);
    if (ulog->level >= uprobe_loglevel->min_level)
        return uprobe_throw(uprobe->next, upipe, UPROBE_LOG, ulog);

    struct uchain *uchain;
    ulist_foreach(&uprobe_loglevel->patterns, uchain) {
        struct pattern *pattern = pattern_from_uchain(uchain);

        if (ulog->level < pattern->log_level)
            continue;

        struct uchain *uchain_sub;
        ulist_foreach(&ulog->prefixes, uchain_sub) {
            struct ulog_pfx *pfx = ulog_pfx_from_uchain(uchain_sub);

            if (!regexec(&pattern->preq, pfx->tag, 0, NULL, 0))
                return uprobe_throw(uprobe->next, upipe, UPROBE_LOG, ulog);
        }
    }

    return UBASE_ERR_NONE;
}

struct uprobe *uprobe_loglevel_init(struct uprobe_loglevel *uprobe_loglevel,
                                    struct uprobe *next,
                                    enum uprobe_log_level min_level)
{
    assert(uprobe_loglevel);
    struct uprobe *uprobe = uprobe_loglevel_to_uprobe(uprobe_loglevel);
    uprobe_init(uprobe, uprobe_loglevel_throw, next);
    ulist_init(&uprobe_loglevel->patterns);
    uprobe_loglevel->min_level = min_level;
    return uprobe;
}

void uprobe_loglevel_clean(struct uprobe_loglevel *uprobe_loglevel)
{
    assert(uprobe_loglevel != NULL);
    struct uprobe *uprobe = uprobe_loglevel_to_uprobe(uprobe_loglevel);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, enum uprobe_log_level min_level
#define ARGS next, min_level
UPROBE_HELPER_ALLOC(uprobe_loglevel)
#undef ARGS
#undef ARGS_DECL

int uprobe_loglevel_set(struct uprobe *uprobe,
                        const char *regex,
                        enum uprobe_log_level log_level)
{
    struct uprobe_loglevel *uprobe_loglevel =
        uprobe_loglevel_from_uprobe(uprobe);

    struct pattern *pattern = malloc(sizeof (*pattern));
    if (unlikely(pattern == NULL))
        return UBASE_ERR_ALLOC;

    if (regcomp(&pattern->preq, regex, REG_NOSUB)) {
        uprobe_err_va(uprobe, NULL, "invalid pattern %s", regex);
        free(pattern);
        return UBASE_ERR_INVALID;
    }
    pattern->log_level = log_level;
    ulist_add(&uprobe_loglevel->patterns, pattern_to_uchain(pattern));

    return UBASE_ERR_NONE;
}
