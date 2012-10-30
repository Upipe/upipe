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
 * @short toolbox provided by the application to modules for logging
 */

#ifndef _UPIPE_ULOG_H_
/** @hidden */
#define _UPIPE_ULOG_H_

#include <upipe/ubase.h>

#ifdef HAVE_FEATURES_H
#include <features.h>
#endif
#include <stdarg.h>
#include <string.h>

/** levels of log messages */
enum ulog_level {
    ULOG_DEBUG,
    ULOG_NOTICE,
    ULOG_WARNING,
    ULOG_ERROR
};

/** size of the pipe-specific buffer for temporary storage for strerror_r()
 * calls and the like */
#define ULOG_BUFFER_SIZE 1024

/** @This is a structure passed to a module upon initializing a new pipe. */
struct ulog {
    /** function to print messages to console */
    void (*ulog)(struct ulog *, enum ulog_level, const char *format, va_list);
    /** function to release the structure */
    void (*ulog_free)(struct ulog *);
    /** pipe-specific (therefore thread-specific) buffer for temporary
     * storage for strerror_r() calls and the like */
    char ulog_buffer[ULOG_BUFFER_SIZE];
};

#define ULOG_TEMPLATE(name, NAME)                                           \
/** @This prints name messages to the console.                              \
 *                                                                          \
 * @param ulog utility structure passed to the module                       \
 * @param format printf format string followed by optional parameters       \
 */                                                                         \
static inline void ulog_##name(struct ulog *ulog, const char *format, ...)  \
{                                                                           \
    if (unlikely(ulog == NULL))                                             \
        return;                                                             \
    va_list args;                                                           \
    va_start(args, format);                                                 \
    ulog->ulog(ulog, ULOG_##NAME, format, args);                            \
    va_end(args);                                                           \
}
ULOG_TEMPLATE(error, ERROR)
ULOG_TEMPLATE(warning, WARNING)
ULOG_TEMPLATE(notice, NOTICE)
ULOG_TEMPLATE(debug, DEBUG)
#undef ULOG_TEMPLATE

/** @This prints an allocation error message to the console.
 *
 * @param ulog utility structure passed to the module
 */
#define ulog_aerror(ulog)                                                   \
    ulog_error(ulog, "allocation failure at %s:%d", __FILE__, __LINE__)

/** @This is a wrapper around incompatible strerror_r() implementations,
 * using ulog storage.
 *
 * @param ulog utility structure passed to the module
 * @param errnum errno value
 * @return pointer to a buffer containing a description of the error
 */
static inline const char *ulog_strerror(struct ulog *ulog, int errnum)
{
    if (likely(ulog != NULL)) {
#ifndef STRERROR_R_CHAR_P
        if (likely(strerror_r(errnum, ulog->ulog_buffer,
                              ULOG_BUFFER_SIZE) != -1))
            return ulog->ulog_buffer;
#else
        return strerror_r(errnum, ulog->ulog_buffer, ULOG_BUFFER_SIZE);
#endif
    }
    return "description unavailable";
}

/** @This frees memory occupied by the ulog structure.
 *
 * @param ulog utility structure passed to the module
 */
static inline void ulog_free(struct ulog *ulog)
{
    if (likely(ulog != NULL))
        ulog->ulog_free(ulog);
}

#endif
