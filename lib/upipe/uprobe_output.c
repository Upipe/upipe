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
 * @short probe dealing with events having consequences on the output pipe
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_output.h>
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
static enum ubase_err uprobe_output_throw(struct uprobe *uprobe,
                                          struct upipe *upipe,
                                          enum uprobe_event event, va_list args)
{
    if (event != UPROBE_NEW_FLOW_DEF)
        return uprobe_throw_next(uprobe, upipe, event, args);

    va_list args_copy;
    va_copy(args_copy, args);
    struct uref *flow_def = va_arg(args_copy, struct uref *);
    va_end(args_copy);
    struct upipe *output;
    if (likely(upipe_get_output(upipe, &output) && output != NULL)) {
        if (likely(upipe_set_flow_def(output, flow_def)))
            return UBASE_ERR_NONE;
        upipe_set_output(upipe, NULL);
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

/** @This initializes an already allocated uprobe_output structure.
 *
 * @param uprobe_output pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_output_init(struct uprobe_output *uprobe_output,
                                  struct uprobe *next)
{
    assert(uprobe_output != NULL);
    struct uprobe *uprobe = uprobe_output_to_uprobe(uprobe_output);
    uprobe_init(uprobe, uprobe_output_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_output structure.
 *
 * @param uprobe_output structure to clean
 */
void uprobe_output_clean(struct uprobe_output *uprobe_output)
{
    assert(uprobe_output != NULL);
    struct uprobe *uprobe = uprobe_output_to_uprobe(uprobe_output);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_output)
#undef ARGS
#undef ARGS_DECL
