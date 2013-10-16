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
 * @short probe outputing all print events with a given name
 */

#ifndef _UPIPE_UPROBE_OUTPUT_H_
/** @hidden */
#define _UPIPE_UPROBE_OUTPUT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

#include <stdbool.h>

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_output {
    /** true if we are in ad-hoc mode */
    bool adhoc;
    /** in ad-hoc mode, pointer to the pipe we're attached to */
    struct upipe *adhoc_pipe;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_output, uprobe)

/** @This initializes an already allocated uprobe output structure.
 *
 * @param uprobe_output pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_output_init(struct uprobe_output *uprobe_output,
                                  struct uprobe *next);

/** @This cleans a uprobe output structure.
 *
 * @param uprobe structure to clean
 */
void uprobe_output_clean(struct uprobe_output *uprobe_output);

/** @This allocates a new uprobe output structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_output_alloc(struct uprobe *next);

/** @This allocates a new uprobe output structure in ad-hoc mode (will be
 * deallocated when the pipe dies).
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_output_adhoc_alloc(struct uprobe *next);

/** @This frees a uprobe output structure.
 *
 * @param uprobe uprobe structure to free
 * @return next probe
 */
struct uprobe *uprobe_output_free(struct uprobe *uprobe);

#ifdef __cplusplus
}
#endif
#endif
