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
#include <upipe/uprobe_helper_adhoc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

/** @hidden */
struct uprobe *uprobe_output_free(struct uprobe *uprobe);

UPROBE_HELPER_ADHOC(uprobe_output, adhoc_pipe)

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true for log events
 */
static bool uprobe_output_throw(struct uprobe *uprobe, struct upipe *upipe,
                                enum uprobe_event event, va_list args)
{
    struct uprobe_output *uprobe_output = uprobe_output_from_uprobe(uprobe);
    if (event != UPROBE_NEW_FLOW_DEF) {
        if (uprobe_output->adhoc)
            return uprobe_output_throw_adhoc(uprobe, upipe, event, args);
        return false;
    }

    struct uref *flow_def = va_arg(args, struct uref *);
    struct upipe *output;
    if (unlikely(!upipe_get_output(upipe, &output))) {
        if (uprobe_output->adhoc) {
            upipe_delete_probe(upipe, uprobe);
            uprobe_output_free(uprobe);
        }
        return false;
    }

    if (unlikely(output == NULL))
        return false;

    if (unlikely(!upipe_set_flow_def(output, flow_def))) {
        upipe_set_output(upipe, NULL);
        if (uprobe_output->adhoc) {
            upipe_delete_probe(upipe, uprobe);
            uprobe_output_free(uprobe);
        }
        return false;
    }
    return true;
}

/** @This initializes an already allocated uprobe output structure.
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
    uprobe_output->adhoc = false;
    uprobe_init(uprobe, uprobe_output_throw, next);
    return uprobe;
}

/** @This cleans a uprobe output structure.
 *
 * @param uprobe structure to clean
 */
void uprobe_output_clean(struct uprobe_output *uprobe_output)
{
    if (uprobe_output->adhoc)
        uprobe_output_clean_adhoc(&uprobe_output->uprobe);
}

/** @This allocates a new uprobe output structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_output_alloc(struct uprobe *next)
{
    struct uprobe_output *uprobe_output = malloc(sizeof(struct uprobe_output));
    if (unlikely(uprobe_output == NULL))
        return NULL;
    return uprobe_output_init(uprobe_output, next);
}

/** @This allocates a new uprobe output structure in ad-hoc mode (will be
 * deallocated when the pipe dies).
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_output_adhoc_alloc(struct uprobe *next)
{
    struct uprobe *uprobe = uprobe_output_alloc(next);
    if (unlikely(uprobe == NULL)) {
        uprobe_throw_fatal(next, NULL, UPROBE_ERR_ALLOC);
        /* we still return the next probe so that the pipe still has a
         * probe hierarchy */
        return next;
    }
    struct uprobe_output *uprobe_output = uprobe_output_from_uprobe(uprobe);
    uprobe_output->adhoc = true;
    uprobe_output_init_adhoc(uprobe);
    return uprobe;
}

/** @This frees a uprobe output structure.
 *
 * @param uprobe structure to free
 * @return next probe
 */
struct uprobe *uprobe_output_free(struct uprobe *uprobe)
{
    struct uprobe *next = uprobe->next;
    struct uprobe_output *uprobe_output = uprobe_output_from_uprobe(uprobe);
    uprobe_output_clean(uprobe_output);
    free(uprobe_output);
    return next;
}
