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

#include "upipe/ubase.h"
#include "upipe/ulist.h"

/** @This defines the levels of log messages. */
enum uprobe_log_level {
    /** verbose messages, on a uref basis */
    UPROBE_LOG_VERBOSE,
    /** debug messages, not necessarily meaningful */
    UPROBE_LOG_DEBUG,
    /** informational messages */
    UPROBE_LOG_INFO,
    /** notice messages, normal but significant */
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
    /** format string for the message */
    const char *format;
    /** arguments for the message format string */
    va_list *args;
    /** list of prefix tags */
    struct uchain prefixes;
};

/** @This initializes an ulog structure.
 *
 * @param ulog pointer to the ulog structure to initialize
 * @param level the level of the log
 * @param format format of the log message
 * @param args arguments for the format string
 */
static inline void ulog_init(struct ulog *ulog,
                             enum uprobe_log_level level,
                             const char *format,
                             va_list *args)
{
    ulog->level = level;
    ulog->format = format;
    ulog->args = args;
    ulist_init(&ulog->prefixes);
}

/* ignore clang format-nonliteral warning on vsnprintf */
UBASE_PRAGMA_CLANG(diagnostic push)
UBASE_PRAGMA_CLANG(diagnostic ignored "-Wformat-nonliteral")

/** @This print the ulog message in the provided buffer.
 *
 * @param ulog pointer to the ulog structure
 * @param buf the destination buffer
 * @param size the size of the buffer
 * @return the message length excluding zero terminator
 */
static inline int ulog_msg_print(struct ulog *ulog, char *buf, size_t size)
{
    va_list args;

    va_copy(args, *ulog->args);
    int ret = vsnprintf(buf, size, ulog->format, args);
    va_end(args);
    return ret;
}

UBASE_PRAGMA_CLANG(diagnostic pop)

/** @This returns the ulog message length.
 *
 * @param ulog pointer to the ulog structure
 * @return the message length excluding zero terminator
 */
static inline int ulog_msg_len(struct ulog *ulog)
{
    return ulog_msg_print(ulog, NULL, 0);
}

#ifdef __cplusplus
}
#endif
#endif
