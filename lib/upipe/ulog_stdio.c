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
 * @short Upipe API for logging using stdio
 */

#include <upipe/ubase.h>
#include <upipe/ulog.h>
#include <upipe/ulog_stdio.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/** @This is a super-set of the ulog structure with additional local members. */
struct ulog_stdio {
    /** file stream to write to */
    FILE *stream;
    /** name of the local portion of the pipe (informative) */
    char *name;
    /** minimum level of printed messages */
    enum ulog_level min_level;

    /** structure exported to modules */
    struct ulog ulog;
};

/** @internal @This returns the high-level ulog structure.
 *
 * @param ulog_stdio pointer to the ulog_stdio structure
 * @return pointer to the ulog structure
 */
static inline struct ulog *ulog_stdio_to_ulog(struct ulog_stdio *ulog_stdio)
{
    return &ulog_stdio->ulog;
}

/** @internal @This returns the private ulog_stdio structure.
 *
 * @param mgr description structure of the ulog
 * @return pointer to the ulog_stdio structure
 */
static inline struct ulog_stdio *ulog_stdio_from_ulog(struct ulog *ulog)
{
    return container_of(ulog, struct ulog_stdio, ulog);
}

/** @internal @This prints messages to the console.
 *
 * @param ulog structure describing the portion of pipe
 * @param level level of interest of the message
 * @param format printf format string
 * @param args optional printf parameters
 */
static void ulog_stdio_ulog(struct ulog *ulog, enum ulog_level level,
                            const char *format, va_list args)
{
    struct ulog_stdio *ulog_stdio = ulog_stdio_from_ulog(ulog);
    if (level < ulog_stdio->min_level)
        return;

    size_t name_len = likely(ulog_stdio->name != NULL) ?
                      strlen(ulog_stdio->name) : 0;
    const char *level_name;
    switch (level) {
        case ULOG_DEBUG: level_name = "debug"; break;
        case ULOG_NOTICE: level_name = "notice"; break;
        case ULOG_WARNING: level_name = "warning"; break;
        case ULOG_ERROR: level_name = "error"; break;
        default: level_name = "unknown"; break;
    }
    char new_format[strlen(format) + strlen(level_name) + name_len +
                    strlen(": [] \n") + 1];
    sprintf(new_format, "%s: [%s] %s\n", level_name,
            likely(ulog_stdio->name != NULL) ? ulog_stdio->name : "unknown",
            format);

    vfprintf(ulog_stdio->stream, new_format, args);
}

/** @This frees a ulog structure.
 *
 * @param ulog structure to free
 */
static void ulog_stdio_free(struct ulog *ulog)
{
    struct ulog_stdio *ulog_stdio = ulog_stdio_from_ulog(ulog);
    free(ulog_stdio->name);
    free(ulog_stdio);
}

/** @This allocates a new ulog structure using an stdio stream.
 *
 * @param stream file stream to write to (eg. stderr)
 * @param log_level minimum level of messages printed to the console
 * @param name name of this section of pipe (informative)
 * @return pointer to ulog, or NULL in case of error
 */
struct ulog *ulog_stdio_alloc(FILE *stream, enum ulog_level log_level,
                              const char *name)
{
    struct ulog_stdio *ulog_stdio = malloc(sizeof(struct ulog_stdio));
    if (unlikely(ulog_stdio == NULL))
        return NULL;
    ulog_stdio->stream = stream;
    if (likely(name != NULL)) {
        ulog_stdio->name = strdup(name);
        if (unlikely(ulog_stdio->name == NULL))
            fprintf(stream, "error: [ulog_stdio] couldn't allocate a string\n");
    } else
        ulog_stdio->name = NULL;
    ulog_stdio->min_level = log_level;
    ulog_stdio->ulog.ulog = ulog_stdio_ulog;
    ulog_stdio->ulog.ulog_free = ulog_stdio_free;
    return ulog_stdio_to_ulog(ulog_stdio);
}

/** @This allocates a new ulog structure using an stdio stream, with composite
 * name.
 *
 * @param stream file stream to write to (eg. stderr)
 * @param log_level minimum level of messages printed to the console
 * @param format printf-format string used for this section of pipe, followed
 * by optional arguments
 * @return pointer to ulog, or NULL in case of error
 */
struct ulog *ulog_stdio_alloc_va(FILE *stream, enum ulog_level log_level,
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
        return ulog_stdio_alloc(stream, log_level, name);
    }
    return ulog_stdio_alloc(stream, log_level, "unknown");
}
