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
 * @short probe prefixing all print events with a given name
 */

#ifndef _UPIPE_UPROBE_PREFIX_H_
/** @hidden */
#define _UPIPE_UPROBE_PREFIX_H_

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

#include <stdbool.h>

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_pfx {
    /** name of the pipe (informative) */
    char *name;
    /** minimum level of messages to pass-through */
    enum uprobe_log_level min_level;

    /** true if we are in ad-hoc mode */
    bool adhoc;
    /** in ad-hoc mode, pointer to the pipe we're attached to */
    struct upipe *adhoc_pipe;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_pfx, uprobe)

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
                               const char *name);

/** @This cleans a uprobe pfx structure.
 *
 * @param uprobe structure to clean
 */
void uprobe_pfx_clean(struct uprobe_pfx *uprobe_pfx);

/** @This allocates a new uprobe pfx structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param min_level minimum level of passed-through messages
 * @param name name of the pipe (informative)
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pfx_alloc(struct uprobe *next,
                                enum uprobe_log_level min_level,
                                const char *name);

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
                                   const char *format, ...);

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
                                      const char *name);

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
                                         const char *format, ...);

/** @This frees a uprobe pfx structure.
 *
 * @param uprobe uprobe structure to free
 */
void uprobe_pfx_free(struct uprobe *uprobe);

#endif
