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

#ifndef _UPIPE_MODULES_UPROBE_HTTP_REDIRECT_H_
/** @hidden */
# define _UPIPE_MODULES_UPROBE_HTTP_REDIRECT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

struct uprobe_http_redir {
    /** uprobe structure */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_http_redir, uprobe)

/** @This initializes an uprobe_http_redir.
 *
 * @param uprobe_http_redir pointer to the allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_http_redir_init(
    struct uprobe_http_redir *uprobe_http_redir,
    struct uprobe *next);

/** @This cleans a uprobe_http_redir structure.
 *
 * @param uprobe_http_redir structure to clean
 */
void uprobe_http_redir_clean(struct uprobe_http_redir *uprobe_http_redir);

/** @This allocates a new uprobe_http_redir structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_http_redir_alloc(struct uprobe *next);

#ifdef __cplusplus
}
#endif
#endif
