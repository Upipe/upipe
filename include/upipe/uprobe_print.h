/*****************************************************************************
 * uprobe_print.h: declarations for the standard toolbox for logging
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#ifndef _UPIPE_UPROBE_PRINT_H_
/** @hidden */
#define _UPIPE_UPROBE_PRINT_H_

#include <upipe/uprobe.h>

#include <stdio.h>

/** @This frees a uprobe print structure.
 *
 * @param uprobe print structure to free
 */
void uprobe_print_free(struct uprobe *uprobe);

/** @This allocates a new uprobe print structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param stream file stream to write to (eg. stderr)
 * @param name prefix appended to all messages by this probe (informative)
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_print_alloc(struct uprobe *next, FILE *stream,
                                  const char *name);

/** @This allocates a new uprobe print structure, with composite name.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param stream file stream to write to (eg. stderr)
 * @param format printf-format string used for the prefix appended to all
 * messages by this probe, followed by optional arguments
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_print_alloc_va(struct uprobe *next, FILE *stream,
                                     const char *format, ...);

#endif
