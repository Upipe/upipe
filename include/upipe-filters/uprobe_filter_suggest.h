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

#ifndef _UPIPE_FILTER_UPROBE_FILTER_SUGGEST_H_
/** @hidden */
#define _UPIPE_FILTER_UPROBE_FILTER_SUGGEST_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

#include <stdbool.h>

/** @This is a super-set of the uprobe structure with additional local
 * members (or not). */
struct uprobe_filter_suggest {
    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_filter_suggest, uprobe)

/** @This initializes an already allocated uprobe_filter_suggest structure.
 *
 * @param uprobe_filter_suggest pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
UBASE_DEPRECATED struct uprobe *uprobe_filter_suggest_init(
        struct uprobe_filter_suggest *uprobe_filter_suggest,
        struct uprobe *next);

/** @This cleans a uprobe_filter_suggest structure.
 *
 * @param uprobe_filter_suggest structure to clean
 */
UBASE_DEPRECATED void uprobe_filter_suggest_clean(
        struct uprobe_filter_suggest *uprobe_filter_suggest);

/** @This allocates a new uprobe_filter_suggest structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
UBASE_DEPRECATED struct uprobe *uprobe_filter_suggest_alloc(struct uprobe *next);

#ifdef __cplusplus
}
#endif
#endif
