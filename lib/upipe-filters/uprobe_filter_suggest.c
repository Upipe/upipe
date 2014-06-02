/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short probe dealing with the suggest_flow_def event
 *
 * It catches the suggest_flow_def event, and asks the output pipe to
 * suggest modifications to the proposed flow definition.
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe-filters/uprobe_filter_suggest.h>
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
static int uprobe_filter_suggest_throw(struct uprobe *uprobe,
                                       struct upipe *upipe,
                                       int event, va_list args)
{
    switch (event) {
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);

        case UPROBE_FILTER_SUGGEST_FLOW_DEF: {
            va_list args_copy;
            va_copy(args_copy, args);
            struct uref *flow_def = va_arg(args_copy, struct uref *);
            va_end(args_copy);
            struct upipe *output;
            if (likely(ubase_check(upipe_get_output(upipe, &output)) &&
                       output != NULL)) {
                upipe_suggest_flow_def(output, flow_def);
                upipe_dbg(upipe, "suggested flow def");
                udict_dump(flow_def->udict, upipe->uprobe);
            }
            return uprobe_throw_next(uprobe, upipe, event, args);
        }
    }
}

/** @This initializes an already allocated uprobe_filter_suggest structure.
 *
 * @param uprobe_filter_suggest pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_filter_suggest_init(
        struct uprobe_filter_suggest *uprobe_filter_suggest,
        struct uprobe *next)
{
    assert(uprobe_filter_suggest != NULL);
    struct uprobe *uprobe =
        uprobe_filter_suggest_to_uprobe(uprobe_filter_suggest);
    uprobe_init(uprobe, uprobe_filter_suggest_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_filter_suggest structure.
 *
 * @param uprobe_filter_suggest structure to clean
 */
void uprobe_filter_suggest_clean(
        struct uprobe_filter_suggest *uprobe_filter_suggest)
{
    assert(uprobe_filter_suggest != NULL);
    struct uprobe *uprobe =
        uprobe_filter_suggest_to_uprobe(uprobe_filter_suggest);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_filter_suggest)
#undef ARGS
#undef ARGS_DECL
