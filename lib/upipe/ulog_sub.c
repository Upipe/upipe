/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short Upipe API for logging using another (higher-level) ulog
 */

#include <upipe/ubase.h>
#include <upipe/ulog.h>
#include <upipe/ulog_sub.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/** @This is a super-set of the ulog structure with additional local members. */
struct ulog_sub {
    /** higher-level ulog structure */
    struct ulog *up_ulog;
    /** name of the local portion of the pipe (informative) */
    char *name;
    /** minimum level of printed messages */
    enum ulog_level min_level;

    /** structure exported to modules */
    struct ulog ulog;
};

/** @internal @This returns the high-level ulog structure.
 *
 * @param ulog_sub pointer to the ulog_sub structure
 * @return pointer to the ulog structure
 */
static inline struct ulog *ulog_sub_to_ulog(struct ulog_sub *ulog_sub)
{
    return &ulog_sub->ulog;
}

/** @internal @This returns the private ulog_sub structure.
 *
 * @param mgr description structure of the ulog
 * @return pointer to the ulog_sub structure
 */
static inline struct ulog_sub *ulog_sub_from_ulog(struct ulog *ulog)
{
    return container_of(ulog, struct ulog_sub, ulog);
}

/** @internal @This prints messages to the console.
 *
 * @param ulog structure describing the portion of pipe
 * @param level level of interest of the message
 * @param format printf format string
 * @param args optional printf parameters
 */
static void ulog_sub_ulog(struct ulog *ulog, enum ulog_level level,
                          const char *format, va_list args)
{
    struct ulog_sub *ulog_sub = ulog_sub_from_ulog(ulog);
    if (level < ulog_sub->min_level)
        return;

    size_t name_len = likely(ulog_sub->name != NULL) ?
                      strlen(ulog_sub->name) : 0;
    char new_format[strlen(format) + name_len +
                    strlen("[] ") + 1];
    sprintf(new_format, "[%s] %s",
            likely(ulog_sub->name != NULL) ? ulog_sub->name : "unknown",
            format);

    ulog_sub->up_ulog->ulog(ulog_sub->up_ulog, level, new_format, args);
}

/** @This frees a ulog structure.
 *
 * @param ulog structure to free
 */
static void ulog_sub_free(struct ulog *ulog)
{
    struct ulog_sub *ulog_sub = ulog_sub_from_ulog(ulog);
    free(ulog_sub->name);
    free(ulog_sub);
}

/** @This allocates a new ulog structure using an stdio stream.
 *
 * @param up_ulog higher-level ulog to use
 * @param log_level minimum level of messages printed to the console
 * @param name name of this section of pipe (informative)
 * @return pointer to ulog, or NULL in case of error
 */
struct ulog *ulog_sub_alloc(struct ulog *up_ulog, enum ulog_level log_level,
                            const char *name)
{
    struct ulog_sub *ulog_sub = malloc(sizeof(struct ulog_sub));
    if (unlikely(ulog_sub == NULL))
        return NULL;
    ulog_sub->up_ulog = up_ulog;
    if (likely(name != NULL)) {
        ulog_sub->name = strdup(name);
        if (unlikely(ulog_sub->name == NULL))
            ulog_error(up_ulog, "[ulog_sub] couldn't allocate a string");
    } else
        ulog_sub->name = NULL;
    ulog_sub->min_level = log_level;
    ulog_sub->ulog.ulog = ulog_sub_ulog;
    ulog_sub->ulog.ulog_free = ulog_sub_free;
    return ulog_sub_to_ulog(ulog_sub);
}

/** @This allocates a new ulog structure using an stdio stream, with composite
 * name.
 *
 * @param up_ulog higher-level ulog to use
 * @param log_level minimum level of messages printed to the console
 * @param format printf-format string used for this section of pipe, followed
 * by optional arguments
 * @return pointer to ulog, or NULL in case of error
 */
struct ulog *ulog_sub_alloc_va(struct ulog *up_ulog, enum ulog_level log_level,
                               const char *format, ...)
{
    size_t len;
    va_list args;
    va_start(args, format);
    len = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (len > 0) {
        char name[len + 1];
        va_start(args, format);
        vsnprintf(name, len + 1, format, args);
        va_end(args);
        return ulog_sub_alloc(up_ulog, log_level, name);
    }
    return ulog_sub_alloc(up_ulog, log_level, "unknown");
}
