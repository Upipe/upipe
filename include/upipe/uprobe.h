/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short Upipe structure used to raise events from pipes
 */

#ifndef _UPIPE_UPROBE_H_
/** @hidden */
#define _UPIPE_UPROBE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uref_flow.h>

#include <stdbool.h>
#include <stdarg.h>

/** @hidden */
struct uprobe;
/** @hidden */
struct upipe;

/** common types of events */
enum uprobe_event {
    /** a pipe is ready to accept input and respond to control commands
     * (void) */
    UPROBE_READY,
    /** a pipe is about to be destroyed and will no longer accept input
     * and control commands (void) */
    UPROBE_DEAD,
    /** something occurred, and the pipe send a textual message
     * (enum uprobe_log_level, const char *) */
    UPROBE_LOG,
    /** a fatal error occurred, data may be lost (enum uprobe_error_code);
     * from now on the behaviour of the pipe is undefined, except
     * @ref upipe_release */
    UPROBE_FATAL,
    /** an error occurred, data may be lost (enum uprobe_error_code); the
     * module probably needs to be reinitialized */
    UPROBE_ERROR,
    /** unable to read from a source because the end of file was reached, or
     * the component disappeared, or because of an error (void) */
    UPROBE_SOURCE_END,
    /** unable to write to an output because the disk is full or another error
     * occurred (const char *) */
    UPROBE_SINK_END,
    /** a uref manager is necessary to operate (void) */
    UPROBE_NEED_UREF_MGR,
    /** a upump manager is necessary to operate (void) */
    UPROBE_NEED_UPUMP_MGR,
    /** a uclock is necessary to operate (void) */
    UPROBE_NEED_UCLOCK,
    /** a new flow definition is available on the output (struct uref *) */
    UPROBE_NEW_FLOW_DEF,
    /** a ubuf manager is necessary to operate (struct uref *) */
    UPROBE_NEED_UBUF_MGR,
    /** a split pipe declares a new possible output flow (uint64_t,
     * struct uref *) */
    UPROBE_SPLIT_ADD_FLOW,
    /** a split pipe declares an output flow is no longer possible
     * (uint64_t) */
    UPROBE_SPLIT_DEL_FLOW,
    /** a pipe got synchronized with its input (void) */
    UPROBE_SYNC_ACQUIRED,
    /** a pipe lost synchronization with its input (void) */
    UPROBE_SYNC_LOST,
    /** a pipe signals that a uref carries a new clock reference, and
     * potentially a clock discontinuity * (struct uref *, uint64_t, int) */
    UPROBE_CLOCK_REF,
    /** a pipe signals that a uref carries a presentation and/or a
     * decoding timestamp (struct uref *) */
    UPROBE_CLOCK_TS,

    /** non-standard events implemented by a module type can start from
     * there (first arg = signature) */
    UPROBE_LOCAL = 0x8000
};

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

/** @This defines the standard error codes. */
enum uprobe_error_code {
    /** allocation error */
    UPROBE_ERR_ALLOC,
    /** unable to allocate a upump */
    UPROBE_ERR_UPUMP,
    /** invalid argument */
    UPROBE_ERR_INVALID,
    /** error in external library */
    UPROBE_ERR_EXTERNAL,

    /** non-standard error codes implemented by a module type can start from
     * there (first arg = signature) */
    UPROBE_ERR_LOCAL = 0x8000
};

/** @This is the call-back type for uprobe events; it returns true if the
 * event was handled. */
typedef bool (*uprobe_throw_func)(struct uprobe *, struct upipe *,
                                  enum uprobe_event, va_list);

/** @This is a structure passed to a module upon initializing a new pipe. */
struct uprobe {
    /** function to throw events */
    uprobe_throw_func uthrow;
    /** next probe to test if this one doesn't catch the event */
    struct uprobe *next;
};

/** @This initializes a uprobe structure. It is typically called by the
 * application or a pipe creating sub-pipes (on a structure already
 * allocated by the master object).
 *
 * @param uprobe pointer to probe
 * @param uthrow function which will be called when an event is thrown
 * @param next next probe to test if this one doesn't catch the event
 */
static inline void uprobe_init(struct uprobe *uprobe, uprobe_throw_func uthrow,
                               struct uprobe *next)
{
    uprobe->uthrow = uthrow;
    uprobe->next = next;
}

/** @internal @This throws generic events with optional arguments.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param event event to throw
 * @param args list of arguments
 */
static inline void uprobe_throw_va(struct uprobe *uprobe, struct upipe *upipe,
                                   enum uprobe_event event, va_list args)
{
    while (likely(uprobe != NULL)) {
        va_list args_copy;
        va_copy(args_copy, args);
        /* in case our probe deletes itself */
        struct uprobe *next = uprobe->next;
        bool ret = uprobe->uthrow(uprobe, upipe, event, args_copy);
        va_end(args_copy);
        if (ret)
            break;
        uprobe = next;
    }
}

/** @internal @This throws generic events with optional arguments.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param event event to throw, followed with optional arguments
 */
static inline void uprobe_throw(struct uprobe *uprobe, struct upipe *upipe,
                                enum uprobe_event event, ...)
{
    va_list args;
    va_start(args, event);
    uprobe_throw_va(uprobe, upipe, event, args);
    va_end(args);
}

/** @internal @This throws a log event. This event is thrown whenever a pipe
 * wants to send a textual message.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param level level of importance of the message
 * @param msg textual message
 */
static inline void uprobe_log(struct uprobe *uprobe, struct upipe *upipe,
                              enum uprobe_log_level level, const char *msg)
{
    uprobe_throw(uprobe, upipe, UPROBE_LOG, level, msg);
}

/** @internal @This throws a log event, with printf-style message generation.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param level level of importance of the message
 * @param format format of the textual message, followed by optional arguments
 */
static inline void uprobe_log_va(struct uprobe *uprobe, struct upipe *upipe,
                                enum uprobe_log_level level,
                                const char *format, ...)
{
    UBASE_VARARG(uprobe_log(uprobe, upipe, level, string))
}

/** @This throws an error event. This event is thrown whenever a pipe wants
 * to send a textual message.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param msg textual message
 */
#define uprobe_err(uprobe, upipe, msg)                                      \
    uprobe_log(uprobe, upipe, UPROBE_LOG_ERROR, msg)

/** @This throws an error event, with printf-style message generation.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param format format of the textual message, followed by optional arguments
 */
static inline void uprobe_err_va(struct uprobe *uprobe, struct upipe *upipe,
                                 const char *format, ...)
{
    UBASE_VARARG(uprobe_err(uprobe, upipe, string))
}

/** @This throws a warning event. This event is thrown whenever a pipe wants
 * to send a textual message.
 *
 * @param uprobe description structure of the pipe
 * @param upipe description structure of the pipe
 * @param msg textual message
 */
#define uprobe_warn(uprobe, upipe, msg)                                     \
    uprobe_log(uprobe, upipe, UPROBE_LOG_WARNING, msg)

/** @This throws a warning event, with printf-style message generation.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param format format of the textual message, followed by optional arguments
 */
static inline void uprobe_warn_va(struct uprobe *uprobe, struct upipe *upipe,
                                  const char *format, ...)
{
    UBASE_VARARG(uprobe_warn(uprobe, upipe, string))
}

/** @This throws a notice statement event. This event is thrown whenever a pipe
 * wants to send a textual message.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param msg textual message
 */
#define uprobe_notice(uprobe, upipe, msg)                                   \
    uprobe_log(uprobe, upipe, UPROBE_LOG_NOTICE, msg)

/** @This throws a notice statement event, with printf-style message generation.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param format format of the textual message, followed by optional arguments
 */
static inline void uprobe_notice_va(struct uprobe *uprobe, struct upipe *upipe,
                                    const char *format, ...)
{
    UBASE_VARARG(uprobe_notice(uprobe, upipe, string))
}

/** @This throws a debug statement event. This event is thrown whenever a pipe
 * wants to send a textual message.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param msg textual message
 */
#define uprobe_dbg(uprobe, upipe, msg)                                      \
    uprobe_log(uprobe, upipe, UPROBE_LOG_DEBUG, msg)

/** @This throws a debug statement event, with printf-style message generation.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param format format of the textual message, followed by optional arguments
 */
static inline void uprobe_dbg_va(struct uprobe *uprobe, struct upipe *upipe,
                                 const char *format, ...)
{
    UBASE_VARARG(uprobe_dbg(uprobe, upipe, string))
}

/** @This throws a verbose statement event. This event is thrown whenever a
 * pipe wants to send a textual message.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param msg textual message
 */
#define uprobe_verbose(uprobe, upipe, msg)                                      \
    uprobe_log(uprobe, upipe, UPROBE_LOG_VERBOSE, msg)

/** @This throws a verbose statement event, with printf-style message
 * generation.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param format format of the textual message, followed by optional arguments
 */
static inline void uprobe_verbose_va(struct uprobe *uprobe, struct upipe *upipe,
                                 const char *format, ...)
{
    UBASE_VARARG(uprobe_verbose(uprobe, upipe, string))
}

/** @This throws a fatal error event. After this event, the behaviour
 * of a pipe is undefined, except for calls to @ref upipe_release.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param errcode error code
 */
#define uprobe_throw_fatal(uprobe, upipe, errcode)                          \
    do {                                                                    \
        uprobe_err_va(uprobe, upipe, "fatal error at %s:%d (0x%x)",         \
                     __FILE__, __LINE__, errcode);                          \
        uprobe_throw(uprobe, upipe, UPROBE_FATAL, errcode);                 \
    } while (0)

/** @This throws an error event.
 *
 * @param uprobe pointer to probe hierarchy
 * @param upipe description structure of the pipe
 * @param errcode error code
 */
#define uprobe_throw_error(uprobe, upipe, errcode)                          \
    do {                                                                    \
        uprobe_err_va(uprobe, upipe, "error at %s:%d (0x%x)",               \
                     __FILE__, __LINE__, errcode);                          \
        uprobe_throw(uprobe, upipe, UPROBE_ERROR, errcode);                 \
    } while (0)

/** @This implements the common parts of a plumber probe (catching the
 * new_flow_def event).
 *
 * @param uprobe pointer to the probe
 * @param upipe pointer to the pipe
 * @param event event triggered by the pipe
 * @param args arguments of the event
 * @param flow_def_p filled in with the flow definition uref passed with the
 * event
 * @param def_p filled in with the flow definition
 * @return false if the event cannot be handled by a plumber
 */
static inline bool uprobe_plumber(struct uprobe *uprobe, struct upipe *upipe,
                                  enum uprobe_event event, va_list args,
                                  struct uref **flow_def_p, const char **def_p)
{
    if (event != UPROBE_NEW_FLOW_DEF)
        return false;

    va_list args_copy;
    va_copy(args_copy, args);
    *flow_def_p = va_arg(args, struct uref *);
    va_end(args_copy);

    if (unlikely(!uref_flow_get_def(*flow_def_p, def_p)))
        return false;

    return true;
}

#ifdef __cplusplus
}
#endif
#endif
