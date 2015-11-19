/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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
 * @short Upipe logging structure
 */

#ifndef _UPIPE_ULOG_H_
/** @hidden */
# define _UPIPE_ULOG_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>

/** @This defines the levels of log messages. */
enum uprobe_log_level {
    /** verbose messages, on a uref basis */
    UPROBE_LOG_VERBOSE,
    /** debug messages, not necessarily meaningful */
    UPROBE_LOG_DEBUG,
    /** notice messages, only informative */
    UPROBE_LOG_NOTICE,
    /** warning messages, the processing continues but may have unexpected
     * results */
    UPROBE_LOG_WARNING,
    /** error messages, the processing cannot continue */
    UPROBE_LOG_ERROR
};

/** @This describe a prefix tag for a log message. */
struct ulog_pfx {
    /** uchain to attach in @ref ulog prefixes field */
    struct uchain uchain;
    /** the prefix string */
    const char *tag;
};

UBASE_FROM_TO(ulog_pfx, uchain, uchain, uchain);

/** @This describe a log message. */
struct ulog {
    /** log level of the message */
    enum uprobe_log_level level;
    /** the message to be logged */
    const char *msg;
    /** list of prefix tags */
    struct uchain prefixes;
};

/** @This initializes an ulog structure.
 *
 * @param ulog pointer to the ulog structure to initialize
 * @param level the level of the log
 * @param msg the message to log
 */
static inline void ulog_init(struct ulog *ulog,
                             enum uprobe_log_level level,
                             const char *msg)
{
    ulog->level = level;
    ulog->msg = msg;
    ulist_init(&ulog->prefixes);
}

#ifdef __cplusplus
}
#endif
#endif
