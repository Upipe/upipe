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

#ifndef _UPIPE_UPROBE_LOGLEVEL_H_
/** @hidden */
# define _UPIPE_UPROBE_LOGLEVEL_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

struct uprobe_loglevel {
    /** minimum level of printed messages */
    enum uprobe_log_level min_level;
    /** uprobe structure */
    struct uprobe uprobe;
    /** list of pattern */
    struct uchain patterns;
};

UPROBE_HELPER_UPROBE(uprobe_loglevel, uprobe)

/** @This initializes an already allocated uprobe_loglevel structure.
 *
 * @param uprobe_loglevel pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_loglevel_init(
    struct uprobe_loglevel *uprobe_loglevel,
    struct uprobe *next,
    enum uprobe_log_level min_level);

/** @This cleans a uprobe_loglevel structure.
 *
 * @param uprobe_loglevel structure to clean
 */
void uprobe_loglevel_clean(struct uprobe_loglevel *uprobe_loglevel);

/** @This allocates a new uprobe_loglevel structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_loglevel_alloc(struct uprobe *next,
                                     enum uprobe_log_level level);

int uprobe_loglevel_set(struct uprobe *uprobe,
                        const char *regex,
                        enum uprobe_log_level log_level);

#ifdef __cplusplus
}
#endif
#endif
