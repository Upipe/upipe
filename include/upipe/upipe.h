/*****************************************************************************
 * upipe.h: upipe module-level interface (typically implemented by a module)
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

#ifndef _UPIPE_UPIPE_H_
/** @hidden */
#define _UPIPE_UPIPE_H_

#include <upipe/urefcount.h>
#include <upipe/ulog.h>
#include <upipe/uprobe.h>

#include <stdint.h>
#include <stdarg.h>

/** @hidden */
struct uref;
/** @hidden */
struct ulog;
/** @hidden */
struct uclock;
/** @hidden */
struct uref_mgr;
/** @hidden */
struct upump_mgr;
/** @hidden */
struct ubuf_mgr;
/** @hidden */
struct upipe_mgr;

/** @This defines standard commands which upipe modules may implement. */
enum upipe_control {
    /*
     * Global commands
     */
    /** send input uref (struct uref *) */
    UPIPE_INPUT,
    /** gets uclock structure (struct uclock **) */
    UPIPE_GET_UCLOCK,
    /** sets uclock structure (struct uclock *) */
    UPIPE_SET_UCLOCK,
    /** gets uref manager (struct uref_mgr **) */
    UPIPE_GET_UREF_MGR,
    /** sets uref manager (struct uref_mgr *) */
    UPIPE_SET_UREF_MGR,
    /** gets upump manager (struct upump_mgr **) */
    UPIPE_GET_UPUMP_MGR,
    /** sets upump manager (struct upump_mgr *) */
    UPIPE_SET_UPUMP_MGR,

    /*
     * Linear elements commands
     */
    /** gets output (struct upipe **) */
    UPIPE_LINEAR_GET_OUTPUT,
    /** sets output (struct upipe *) */
    UPIPE_LINEAR_SET_OUTPUT,
    /** gets ubuf manager (struct ubuf_mgr **) */
    UPIPE_LINEAR_GET_UBUF_MGR,
    /** sets ubuf manager (struct ubuf_mgr *) */
    UPIPE_LINEAR_SET_UBUF_MGR,

    /*
     * Split elements commands
     */
    /** gets output for given flow suffix (struct upipe **, const char *) */
    UPIPE_SPLIT_GET_OUTPUT,
    /** sets output for given flow suffix (struct upipe *, const char *) */
    UPIPE_SPLIT_SET_OUTPUT,
    /** gets ubuf manager for given flow suffix (struct ubuf_mgr **,
     * const char *) */
    UPIPE_SPLIT_GET_UBUF_MGR,
    /** sets ubuf manager for given flow suffix (struct ubuf_mgr *,
     * const char *) */
    UPIPE_SPLIT_SET_UBUF_MGR,

    /*
     * Source elements commands
     */
    /** gets name of the source flow (const char **) */
    UPIPE_SOURCE_GET_FLOW_NAME,
    /** sets name of the source flow (const char *) */
    UPIPE_SOURCE_SET_FLOW_NAME,
    /** gets read buffer size (unsigned int *) */
    UPIPE_SOURCE_GET_READ_SIZE,
    /** sets read buffer size (unsigned int) */
    UPIPE_SOURCE_SET_READ_SIZE,

    /*
     * Sink elements commands
     */
    /** gets delay applied to systime attribute (uint64_t *) */
    UPIPE_SINK_GET_DELAY,
    /** sets delay applied to systime attribute (uint64_t) */
    UPIPE_SINK_SET_DELAY,

    /** non-standard commands implemented by a module type can start from
     * there (first arg = signature) */
    UPIPE_CONTROL_LOCAL = 0x8000
};

/** @This stores common parameters for upipe structures. */
struct upipe {
    /** refcount management structure (number of pipes using this pipe as
     * output - atomicity is not really needed) */
    urefcount refcount;
    /** signature of the pipe allocator */
    unsigned int signature;

    /** pointer to the uprobe structure passed on initialization */
    struct uprobe *uprobe;
    /** pointer to the ulog structure passed on initialization */
    struct ulog *ulog;
    /** pointer to the manager for this pipe type */
    struct upipe_mgr *mgr;
};

/** @This stores common management parameters for a pipe type. */
struct upipe_mgr {
    /** refcount management structure */
    urefcount refcount;

    /** function to create a pipe */
    struct upipe *(*upipe_alloc)(struct upipe_mgr *);
    /** control function for standard or local commands */
    bool (*upipe_control)(struct upipe *, enum upipe_control, va_list);
    /** function to free a pipe structure */
    void (*upipe_free)(struct upipe *);

    /** function to free the upipe_mgr structure */
    void (*upipe_mgr_free)(struct upipe_mgr *);
};

/** @This allocates and initializes a pipe.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 * @param ulog structure used to output logs
 * @return pointer to allocated pipe, or NULL in case of failure
 */
static inline struct upipe *upipe_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        struct ulog *ulog)
{
    struct upipe *upipe = mgr->upipe_alloc(mgr);
    if (unlikely(upipe == NULL)) return NULL;
    urefcount_init(&upipe->refcount);
    upipe->uprobe = uprobe;
    upipe->ulog = ulog;
    return upipe;
}

/** @internal @This sends a control command to the pipe. Note that all control
 * commands (including input) must be executed from the same thread - no
 * reentrancy or locking is required from the pipe.
 *
 * @param upipe description structure of the pipe
 * @param control control command to send, followed by optional read or write
 * parameters
 * @return false in case of error
 */
static inline bool upipe_control(struct upipe *upipe,
                                 enum upipe_control control, ...)
{
    bool ret;
    va_list args;
    va_start(args, control);
    ret = upipe->mgr->upipe_control(upipe, control, args);
    va_end(args);
    return ret;
}

/** @This sends an input buffer into a pipe.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure to send
 */
static inline bool upipe_input(struct upipe *upipe, struct uref *uref)
{
    return upipe_control(upipe, UPIPE_INPUT, uref);
}

/** @internal @This allows to easily define accessors for control commands.
 *
 * @param group group of commands in lower-case
 * @param GROUP group of commands in upper-case
 * @param name name of the command in lower-case
 * @param NAME name of the command in upper-case
 * @param type C-type representation of the parameter
 * @param desc description for auto-generated documentation
 */
#define UPIPE_CONTROL_TEMPLATE(group, GROUP, name, NAME, type, desc)        \
/** @This gets a pointer to the desc.                                       \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p reference to a value, will be modified                          \
 * @return false in case of error                                           \
 */                                                                         \
static inline bool group##_get_##name(struct upipe *upipe, type *p)         \
{                                                                           \
    return upipe_control(upipe, GROUP##_GET_##NAME, p);                     \
}                                                                           \
/** @This sets the desc.                                                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param s new value                                                       \
 * @return false in case of error                                           \
 */                                                                         \
static inline bool group##_set_##name(struct upipe *upipe, type s)          \
{                                                                           \
    return upipe_control(upipe, GROUP##_SET_##NAME, s);                     \
}

UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, uclock, UCLOCK, struct uclock *,
                       uclock structure)
UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, uref_mgr, UREF_MGR, struct uref_mgr *,
                       uref manager)
UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, upump_mgr, UPUMP_MGR, struct upump_mgr *,
                       upump manager)

UPIPE_CONTROL_TEMPLATE(upipe_linear, UPIPE_LINEAR, output, OUTPUT,
                       struct upipe *, pipe acting as output)
UPIPE_CONTROL_TEMPLATE(upipe_linear, UPIPE_LINEAR, ubuf_mgr, UBUF_MGR,
                       struct ubuf_mgr *, ubuf manager)

UPIPE_CONTROL_TEMPLATE(upipe_source, UPIPE_SOURCE, flow_name, FLOW_NAME,
                       const char *, flow name of the source)
UPIPE_CONTROL_TEMPLATE(upipe_source, UPIPE_SOURCE, read_size, READ_SIZE,
                       unsigned int, read size of the source)

UPIPE_CONTROL_TEMPLATE(upipe_sink, UPIPE_SINK, delay, DELAY, uint64_t,
                       delay applied to systime attribute)
#undef UPIPE_CONTROL_TEMPLATE

/** @internal @This allows to easily define accessors for split control
 * commands.
 *
 * @param name name of the command in lower-case
 * @param NAME name of the command in upper-case
 * @param type C-type representation of the parameter
 * @param desc description for auto-generated documentation
 */
#define UPIPE_SPLIT_CONTROL_TEMPLATE(name, NAME, type, desc)                \
/** @This gets a pointer to the desc for the given flow suffix.             \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p reference to a value, will be modified                          \
 * @param flow_suffix flow suffix                                           \
 * @return false in case of error                                           \
 */                                                                         \
static inline bool upipe_split_get_##name(struct upipe *upipe, type *p,     \
                                          const char *flow_suffix)          \
{                                                                           \
    return upipe_control(upipe, UPIPE_SPLIT_GET_##NAME, p, flow_suffix);    \
}                                                                           \
/** @This gets a pointer to the desc for the given flow suffix with         \
 * printf-style name generation.                                            \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p reference to a value, will be modified                          \
 * @param format printf-style format of the flow suffix, followed by a      \
 * variable list of arguments                                               \
 * @return false in case of error                                           \
 */                                                                         \
static inline bool upipe_split_get_##name##_va(struct upipe *upipe, type *p,\
                                               const char *format, ...)     \
                   __attribute__ ((format(printf, 3, 4)));                  \
/** @hidden */                                                              \
static inline bool upipe_split_get_##name##_va(struct upipe *upipe, type *p,\
                                               const char *format, ...)     \
{                                                                           \
    UBASE_VARARG(upipe_split_get_##name(upipe, p, string))                  \
}                                                                           \
/** @This sets the desc for the given flow suffix.                          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param s new value                                                       \
 * @param flow_suffix flow suffix                                           \
 * @return false in case of error                                           \
 */                                                                         \
static inline bool upipe_split_set_##name(struct upipe *upipe, type s,      \
                                          const char *flow_suffix)          \
{                                                                           \
    return upipe_control(upipe, UPIPE_SPLIT_SET_##NAME, s, flow_suffix);    \
}                                                                           \
/** @This sets the desc for the given flow suffix with printf-style name    \
 * generation.                                                              \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param s new value                                                       \
 * @param format printf-style format of the flow suffix, followed by a      \
 * variable list of arguments                                               \
 * @return false in case of error                                           \
 */                                                                         \
static inline bool upipe_split_set_##name##_va(struct upipe *upipe, type s, \
                                               const char *format, ...)     \
                   __attribute__ ((format(printf, 3, 4)));                  \
/** @hidden */                                                              \
static inline bool upipe_split_set_##name##_va(struct upipe *upipe, type s, \
                                               const char *format, ...)     \
{                                                                           \
    UBASE_VARARG(upipe_split_set_##name(upipe, s, string))                  \
}

UPIPE_SPLIT_CONTROL_TEMPLATE(output, OUTPUT, struct upipe *,
                             pipe acting as output)
UPIPE_SPLIT_CONTROL_TEMPLATE(ubuf_mgr, UBUF_MGR, struct ubuf_mgr *,
                             ubuf manager)
#undef UPIPE_SPLIT_CONTROL_TEMPLATE

/** @This increments the reference count of a upipe.
 *
 * @param upipe pointer to struct upipe
 */
static inline void upipe_use(struct upipe *upipe)
{
    urefcount_use(&upipe->refcount);
}

/** @This decrements the reference count of a upipe, and frees it when
 * it gets down to 0. Please note that this function may not be called from a
 * uprobe call-back, when the probe has been triggered by event on the same
 * pipe.
 *
 * @param upipe pointer to struct upipe
 */
static inline void upipe_release(struct upipe *upipe)
{
    if (unlikely(urefcount_release(&upipe->refcount))) {
        struct ulog *ulog = upipe->ulog;
        urefcount_clean(&upipe->refcount);
        upipe->mgr->upipe_free(upipe);
        ulog_free(ulog);
    }
}

/** @This checks if we are the single owner of the upipe.
 *
 * @param upipe pointer to upipe
 * @return false if other pipes reference this one
 */
static inline bool upipe_single(struct upipe *upipe)
{
    return urefcount_single(&upipe->refcount);
}

/** @internal @This throws generic events with optional arguments.
 *
 * @param upipe description structure of the pipe
 * @param event event to throw
 */
static inline void upipe_throw(struct upipe *upipe,
                               enum uprobe_event event, ...)
{
    struct uprobe *uprobe = upipe->uprobe;
    while (likely(uprobe != NULL)) {
        bool ret;
        va_list args;
        va_start(args, event);
        ret = uprobe->uthrow(uprobe, upipe, event, args);
        va_end(args);
        if (ret) break;
        uprobe = uprobe->next;
    }
}

/** @This throws a ready event. This event is thrown whenever a
 * pipe is ready to process data or respond to control commands asking
 * for information about the processing. If a packet is input into a pipe
 * before the ready event was thrown, the behaviour of the pipe is undefined,
 * except for calls to @ref upipe_release.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_ready(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_READY);
}

/** @This throws an allocation error event. This event is thrown whenever a
 * pipe is unable to allocate required data. After this event, the behaviour
 * of a pipe is undefined, except for calls to @ref upipe_release.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_aerror(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_AERROR);
}

/** @This throws a upump error event. This event is thrown whenever a pipe
 * is unable to allocate a watcher. After this event, the behaviour of a
 * pipe is undefined, except for calls to @ref upipe_release.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_upump_error(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_UPUMP_ERROR);
}

/** @This throws a read end event. This event is thrown when a pipe is unable
 * to read from an input because the end of file was reached, or because an
 * error occurred.
 *
 * @param upipe description structure of the pipe
 * @param location string describing the location of the input (like file path)
 */
static inline void upipe_throw_read_end(struct upipe *upipe,
                                        const char *location)
{
    upipe_throw(upipe, UPROBE_READ_END, location);
}

/** @This throws a write end event. This event is thrown when a pipe is unable
 * to write to an output because the disk is full, or another error occurred.
 *
 * @param upipe description structure of the pipe
 * @param location string describing the location of the output (like file path)
 */
static inline void upipe_throw_write_end(struct upipe *upipe,
                                         const char *location)
{
    upipe_throw(upipe, UPROBE_WRITE_END, location);
}

/** @This throws a new flow event. This event is thrown whenever a pipe
 * declares a new output flow.
 *
 * @param upipe description structure of the pipe
 * @param flow_suffix suffix appended to the flow name for this output, or NULL
 * in case of a linear pipe
 * @param flow_def_head head of list of flow definitions supported by the output
 */
static inline void upipe_throw_new_flow(struct upipe *upipe,
                                        const char *flow_suffix,
                                        struct uref *flow_def_head)
{
    upipe_throw(upipe, UPROBE_NEW_FLOW, flow_suffix, flow_def_head);
}

/** @This throws an event asking for a uref manager.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_need_uref_mgr(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_NEED_UREF_MGR);
}

/** @This throws an event asking for a upump manager.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_need_upump_mgr(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_NEED_UPUMP_MGR);
}

/** @This throws an event asking for a ubuf manager.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_linear_need_ubuf_mgr(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_LINEAR_NEED_UBUF_MGR);
}

/** @This throws an event asking for a source flow name.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_source_need_flow_name(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_SOURCE_NEED_FLOW_NAME);
}

/** @This increments the reference count of a upipe_mgr.
 *
 * @param mgr pointer to struct upipe_mgr
 */
static inline void upipe_mgr_use(struct upipe_mgr *mgr)
{
    if (unlikely(mgr->upipe_mgr_free != NULL))
        urefcount_use(&mgr->refcount);
}

/** @This decrements the reference count of a upipe_mgr, and frees it when
 * it gets down to 0.
 *
 * @param mgr pointer to struct upipe_mgr
 */
static inline void upipe_mgr_release(struct upipe_mgr *mgr)
{
    if (unlikely(mgr->upipe_mgr_free != NULL &&
        urefcount_release(&mgr->refcount)))
        mgr->upipe_mgr_free(mgr);
}

#endif
