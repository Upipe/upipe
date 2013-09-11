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
 * @short Upipe module-level interface, typically implemented by a module
 */

#ifndef _UPIPE_UPIPE_H_
/** @hidden */
#define _UPIPE_UPIPE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uprobe.h>

#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

#define UPIPE_VOID_SIGNATURE UBASE_FOURCC('v','o','i','d')
#define UPIPE_FLOW_SIGNATURE UBASE_FOURCC('f','l','o','w')

/** @hidden */
struct uref;
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
/** @hidden */
struct upump;

/** @This defines standard commands which upipe modules may implement. */
enum upipe_command {
    /*
     * Global commands
     */
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
    /** gets uniform resource identifier (const char **) */
    UPIPE_GET_URI,
    /** sets uniform resource identifier (const char *) */
    UPIPE_SET_URI,

    /*
     * Output-related commands
     */
    /** gets output (struct upipe **) */
    UPIPE_GET_OUTPUT,
    /** sets output (struct upipe *) */
    UPIPE_SET_OUTPUT,
    /** gets ubuf manager (struct ubuf_mgr **) */
    UPIPE_GET_UBUF_MGR,
    /** sets ubuf manager (struct ubuf_mgr *) */
    UPIPE_SET_UBUF_MGR,
    /** gets output flow definition (struct uref **) */
    UPIPE_GET_FLOW_DEF,
    /** sets output flow definition (struct uref *) */
    UPIPE_SET_FLOW_DEF,

    /*
     * Source elements commands
     */
    /** gets read buffer size (unsigned int *) */
    UPIPE_SOURCE_GET_READ_SIZE,
    /** sets read buffer size (unsigned int) */
    UPIPE_SOURCE_SET_READ_SIZE,

    /*
     * Sink elements commands
     */
    /** gets the length of the internal queue (unsigned int *) */
    UPIPE_SINK_GET_MAX_LENGTH,
    /** sets the length of the internal queue (unsigned int) */
    UPIPE_SINK_SET_MAX_LENGTH,
    /** flushes all currently held buffers and unblock the sources (void) */
    UPIPE_SINK_FLUSH,
    /** gets delay applied to systime attribute (uint64_t *) */
    UPIPE_SINK_GET_DELAY,
    /** sets delay applied to systime attribute (uint64_t) */
    UPIPE_SINK_SET_DELAY,

    /*
     * Sub/super pipes commands
     */
    /** returns the sub manager associated with a super-pipe
     * (struct upipe_mgr **) */
    UPIPE_GET_SUB_MGR,
    /** returns the super-pipe associated with a subpipe (struct upipe **) */
    UPIPE_SUB_GET_SUPER,

    /** non-standard commands implemented by a module type can start from
     * there (first arg = signature) */
    UPIPE_CONTROL_LOCAL = 0x8000
};

/** @This stores common parameters for upipe structures. */
struct upipe {
    /** refcount management structure */
    urefcount refcount;
    /** structure for double-linked lists - for use by the application only */
    struct uchain uchain;
    /** opaque - for use by the application only */
    void *opaque;

    /** pointer to the uprobe hierarchy passed on initialization */
    struct uprobe *uprobe;
    /** pointer to the manager for this pipe type */
    struct upipe_mgr *mgr;
};

/** @This stores common management parameters for a pipe type. */
struct upipe_mgr {
    /** refcount management structure */
    urefcount refcount;
    /** signature of the pipe allocator */
    unsigned int signature;

    /** function to create a pipe */
    struct upipe *(*upipe_alloc)(struct upipe_mgr *, struct uprobe *,
                                 uint32_t, va_list);
    /** function to send a uref to an input - the uref then belongs to the
     * callee */
    void (*upipe_input)(struct upipe *, struct uref *, struct upump *);
    /** control function for standard or local commands - all parameters
     * belong to the caller */
    bool (*upipe_control)(struct upipe *, enum upipe_command, va_list);
    /** function to free the pipe */
    void (*upipe_free)(struct upipe *);

    /** function to free the upipe manager */
    void (*upipe_mgr_free)(struct upipe_mgr *);
};

/** @This increments the reference count of a upipe manager.
 *
 * @param mgr pointer to upipe manager
 */
static inline void upipe_mgr_use(struct upipe_mgr *mgr)
{
    if (mgr->upipe_mgr_free != NULL)
        urefcount_use(&mgr->refcount);
}

/** @This decrements the reference count of a upipe manager or frees it.
 *
 * @param mgr pointer to upipe manager.
 */
static inline void upipe_mgr_release(struct upipe_mgr *mgr)
{
    if (mgr->upipe_mgr_free != NULL && urefcount_release(&mgr->refcount))
        mgr->upipe_mgr_free(mgr);
}

/** @internal @This allocates and initializes a pipe.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events (belongs to the caller and
 * must be kept alive for all the duration of the pipe)
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to allocated pipe, or NULL in case of failure
 */
static inline struct upipe *upipe_alloc_va(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature, va_list args)
{
    struct upipe *upipe = mgr->upipe_alloc(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        /* notify ad-hoc probes that something went wrong so they can
         * deallocate */
        uprobe_throw_fatal(uprobe, NULL, UPROBE_ERR_ALLOC);
    return upipe;
}

/** @internal @This allocates and initializes a pipe with a variable list of
 * arguments.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events (belongs to the caller and
 * must be kept alive for all the duration of the pipe)
 * @param signature signature of the pipe allocator, followed by optional
 * arguments
 * @return pointer to allocated pipe, or NULL in case of failure
 */
static inline struct upipe *upipe_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, ...)
{
    va_list args;
    va_start(args, signature);
    struct upipe *upipe = upipe_alloc_va(mgr, uprobe, signature, args);
    va_end(args);
    return upipe;
}

/** @This allocates and initializes a pipe which is designed to accept no
 * argument.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events (belongs to the caller and
 * must be kept alive for all the duration of the pipe)
 * @return pointer to allocated pipe, or NULL in case of failure
 */
static inline struct upipe *upipe_void_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe)
{
    return upipe_alloc(mgr, uprobe, UPIPE_VOID_SIGNATURE);
}

/** @This allocates and initializes a pipe which is designed to accept a
 * flow definition.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events (belongs to the caller and
 * must be kept alive for all the duration of the pipe)
 * @param flow_def flow definition of the input
 * @return pointer to allocated pipe, or NULL in case of failure
 */
static inline struct upipe *upipe_flow_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             struct uref *flow_def)
{
    return upipe_alloc(mgr, uprobe, UPIPE_FLOW_SIGNATURE, flow_def);
}

/** @This initializes the public members of a pipe.
 *
 * @param upipe description structure of the pipe
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 */
static inline void upipe_init(struct upipe *upipe, struct upipe_mgr *mgr,
                              struct uprobe *uprobe)
{
    assert(upipe != NULL);
    urefcount_init(&upipe->refcount);
    uchain_init(&upipe->uchain);
    upipe->opaque = NULL;
    upipe->uprobe = uprobe;
    upipe->mgr = mgr;
    upipe_mgr_use(mgr);
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe pointer to upipe
 */
static inline void upipe_use(struct upipe *upipe)
{
    if (upipe->mgr->upipe_free != NULL)
        urefcount_use(&upipe->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe pointer to upipe
 */
static inline void upipe_release(struct upipe *upipe)
{
    if (upipe->mgr->upipe_free != NULL && urefcount_release(&upipe->refcount))
        upipe->mgr->upipe_free(upipe);
}

/** @This checks if the pipe has more than one reference.
 *
 * @param upipe pointer to upipe
 * @return true if there is only one reference to the pipe
 */
static inline bool upipe_single(struct upipe *upipe)
{
    return upipe->mgr->upipe_free != NULL && urefcount_single(&upipe->refcount);
}

/** @This gets the opaque member of a pipe.
 *
 * @param upipe pointer to upipe
 * @param type type to cast to
 * @return opaque
 */
#define upipe_get_opaque(upipe, type) (type)(upipe)->opaque

/** @This sets the opaque member of a pipe.
 *
 * @param upipe pointer to upipe
 * @param opaque opaque
 */
static inline void upipe_set_opaque(struct upipe *upipe, void *opaque)
{
    upipe->opaque = opaque;
}

/** @This sends an input buffer into a pipe. Note that all inputs and control
 * commands must be executed from the same thread - no reentrancy or locking
 * is required from the pipe. Also note that uref is then owned by the callee
 * and shouldn't be used any longer.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure to send
 * @param upump pump that generated the buffer
 */
static inline void upipe_input(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    assert(upipe->mgr->upipe_input != NULL);
    upipe_use(upipe);
    upipe->mgr->upipe_input(upipe, uref, upump);
    upipe_release(upipe);
}

/** @internal @This sends a control command to the pipe. Note that all control
 * commands must be executed from the same thread - no reentrancy or locking
 * is required from the pipe. Also note that all arguments are owned by the
 * caller.
 *
 * @param upipe description structure of the pipe
 * @param command control command to send
 * @param optional read or write parameters
 * @return false in case of error
 */
static inline bool upipe_control_va(struct upipe *upipe,
                                    enum upipe_command command, va_list args)
{
    if (upipe->mgr->upipe_control == NULL)
        return false;

    bool ret;
    upipe_use(upipe);
    ret = upipe->mgr->upipe_control(upipe, command, args);
    upipe_release(upipe);
    return ret;
}

/** @internal @This sends a control command to the pipe. Note that all control
 * commands must be executed from the same thread - no reentrancy or locking
 * is required from the pipe. Also note that all arguments are owned by the
 * caller.
 *
 * @param upipe description structure of the pipe
 * @param command control command to send, followed by optional read or write
 * parameters
 * @return false in case of error
 */
static inline bool upipe_control(struct upipe *upipe,
                                 enum upipe_command command, ...)
{
    bool ret;
    va_list args;
    va_start(args, command);
    ret = upipe_control_va(upipe, command, args);
    va_end(args);
    return ret;
}

/** @This should be called by the module writer before it disposes of its
 * upipe structure.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_clean(struct upipe *upipe)
{
    assert(upipe != NULL);
    urefcount_clean(&upipe->refcount);
    upipe_mgr_release(upipe->mgr);
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
UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, uri, URI,
                       const char *, uniform resource identifier)

UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, output, OUTPUT, struct upipe *,
                       pipe acting as output)
UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, ubuf_mgr, UBUF_MGR, struct ubuf_mgr *,
                       ubuf manager)
UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, flow_def, FLOW_DEF, struct uref *,
                       flow definition of the output)

UPIPE_CONTROL_TEMPLATE(upipe_source, UPIPE_SOURCE, read_size, READ_SIZE,
                       unsigned int, read size of the source)

UPIPE_CONTROL_TEMPLATE(upipe_sink, UPIPE_SINK, max_length, MAX_LENGTH,
                       unsigned int,
                       max length of the internal queue)
UPIPE_CONTROL_TEMPLATE(upipe_sink, UPIPE_SINK, delay, DELAY, uint64_t,
                       delay applied to systime attribute)
#undef UPIPE_CONTROL_TEMPLATE

/** @This flushes all currently held buffers, and unblocks the sources.
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static inline bool upipe_sink_flush(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_SINK_FLUSH);
}

/** @This returns the super-pipe of a subpipe.
 *
 * @param upipe description structure of the subpipe
 * @param p filled in with a pointer to the super-pipe
 * @return false in case of error
 */
static inline bool upipe_sub_get_super(struct upipe *upipe, struct upipe **p)
{
    return upipe_control(upipe, UPIPE_SUB_GET_SUPER, p);
}

/** @This returns the subpipe manager of a super-pipe.
 *
 * @param upipe description structure of the super-pipe
 * @param p filled in with a pointer to the subpipe manager
 * @return false in case of error
 */
static inline bool upipe_get_sub_mgr(struct upipe *upipe, struct upipe_mgr **p)
{
    return upipe_control(upipe, UPIPE_GET_SUB_MGR, p);
}

/** @This allocates and initializes a subpipe which is designed to accept no
 * argument.
 *
 * @param upipe structure of the super-pipe
 * @param uprobe structure used to raise events (belongs to the caller and
 * must be kept alive for all the duration of the pipe)
 * @return pointer to allocated subpipe, or NULL in case of failure
 */
static inline struct upipe *upipe_void_alloc_sub(struct upipe *upipe,
                                                 struct uprobe *uprobe)
{
    struct upipe_mgr *sub_mgr;
    if (!upipe_get_sub_mgr(upipe, &sub_mgr)) {
        /* notify ad-hoc probes that something went wrong so they can
         * deallocate */
        uprobe_throw_fatal(uprobe, NULL, UPROBE_ERR_ALLOC);
        return NULL;
    }
    return upipe_void_alloc(sub_mgr, uprobe);
}

/** @This allocates and initializes a subpipe which is designed to accept a
 * flow definition.
 *
 * @param upipe structure of the super-pipe
 * @param uprobe structure used to raise events (belongs to the caller and
 * must be kept alive for all the duration of the pipe)
 * @param flow_def flow definition of the input
 * @return pointer to allocated subpipe, or NULL in case of failure
 */
static inline struct upipe *upipe_flow_alloc_sub(struct upipe *upipe,
                                                 struct uprobe *uprobe,
                                                 struct uref *flow_def)
{
    struct upipe_mgr *sub_mgr;
    if (!upipe_get_sub_mgr(upipe, &sub_mgr)) {
        /* notify ad-hoc probes that something went wrong so they can
         * deallocate */
        uprobe_throw_fatal(uprobe, NULL, UPROBE_ERR_ALLOC);
        return NULL;
    }
    return upipe_flow_alloc(sub_mgr, uprobe, flow_def);
}

/** @internal @This throws generic events with optional arguments.
 *
 * @param upipe description structure of the pipe
 * @param event event to throw
 * @param args arguments
 */
static inline void upipe_throw_va(struct upipe *upipe,
                                  enum uprobe_event event, va_list args)
{
    uprobe_throw_va(upipe->uprobe, upipe, event, args);
}

/** @internal @This throws generic events with optional arguments.
 *
 * @param upipe description structure of the pipe
 * @param event event to throw, followed by arguments
 */
static inline void upipe_throw(struct upipe *upipe,
                               enum uprobe_event event, ...)
{
    va_list args;
    va_start(args, event);
    upipe_throw_va(upipe, event, args);
    va_end(args);
}

/** @This throws a ready event. This event is thrown whenever a
 * pipe is ready to accept input or respond to control commands.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_ready(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_READY);
}

/** @This throws a dead event. This event is thrown whenever a
 * pipe is about to be destroyed and will no longer accept input and
 * control commands.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_dead(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_DEAD);
}

/** @internal @This throws a log event. This event is thrown whenever a pipe
 * wants to send a textual message.
 *
 * @param upipe description structure of the pipe
 * @param level level of importance of the message
 * @param msg textual message
 */
#define upipe_log(upipe, level, msg)                                        \
    uprobe_log((upipe)->uprobe, upipe, level, msg)

/** @internal @This throws a log event, with printf-style message generation.
 *
 * @param upipe description structure of the pipe
 * @param level level of importance of the message
 * @param format format of the textual message, followed by optional arguments
 */
static inline void upipe_log_va(struct upipe *upipe,
                                enum uprobe_log_level level,
                                const char *format, ...)
{
    UBASE_VARARG(upipe_log(upipe, level, string))
}

/** @This throws an error event. This event is thrown whenever a pipe wants
 * to send a textual message.
 *
 * @param upipe description structure of the pipe
 * @param msg textual message
 */
#define upipe_err(upipe, msg) upipe_log(upipe, UPROBE_LOG_ERROR, msg)

/** @This throws an error event, with printf-style message generation.
 *
 * @param upipe description structure of the pipe
 * @param format format of the textual message, followed by optional arguments
 */
static inline void upipe_err_va(struct upipe *upipe, const char *format, ...)
{
    UBASE_VARARG(upipe_err(upipe, string))
}

/** @This throws a warning event. This event is thrown whenever a pipe wants
 * to send a textual message.
 *
 * @param upipe description structure of the pipe
 * @param msg textual message
 */
#define upipe_warn(upipe, msg) upipe_log(upipe, UPROBE_LOG_WARNING, msg)

/** @This throws a warning event, with printf-style message generation.
 *
 * @param upipe description structure of the pipe
 * @param format format of the textual message, followed by optional arguments
 */
static inline void upipe_warn_va(struct upipe *upipe, const char *format, ...)
{
    UBASE_VARARG(upipe_warn(upipe, string))
}

/** @This throws a notice statement event. This event is thrown whenever a pipe
 * wants to send a textual message.
 *
 * @param upipe description structure of the pipe
 * @param msg textual message
 */
#define upipe_notice(upipe, msg) upipe_log(upipe, UPROBE_LOG_NOTICE, msg)

/** @This throws a notice statement event, with printf-style message generation.
 *
 * @param upipe description structure of the pipe
 * @param format format of the textual message, followed by optional arguments
 */
static inline void upipe_notice_va(struct upipe *upipe, const char *format, ...)
{
    UBASE_VARARG(upipe_notice(upipe, string))
}

/** @This throws a debug statement event. This event is thrown whenever a pipe
 * wants to send a textual message.
 *
 * @param upipe description structure of the pipe
 * @param msg textual message
 */
#define upipe_dbg(upipe, msg) upipe_log(upipe, UPROBE_LOG_DEBUG, msg)

/** @This throws a debug statement event, with printf-style message generation.
 *
 * @param upipe description structure of the pipe
 * @param format format of the textual message, followed by optional arguments
 */
static inline void upipe_dbg_va(struct upipe *upipe, const char *format, ...)
{
    UBASE_VARARG(upipe_dbg(upipe, string))
}

/** @This throws a verbose statement event. This event is thrown whenever a pipe
 * wants to send a textual message.
 *
 * @param upipe description structure of the pipe
 * @param msg textual message
 */
#define upipe_verbose(upipe, msg) upipe_log(upipe, UPROBE_LOG_VERBOSE, msg)

/** @This throws a verbose statement event, with printf-style message
 * generation.
 *
 * @param upipe description structure of the pipe
 * @param format format of the textual message, followed by optional arguments
 */
static inline void upipe_verbose_va(struct upipe *upipe,
                                    const char *format, ...)
{
    UBASE_VARARG(upipe_verbose(upipe, string))
}

/** @This throws a fatal error event. After this event, the behaviour
 * of a pipe is undefined, except for calls to @ref upipe_release.
 *
 * @param upipe description structure of the pipe
 * @param errcode error code
 */
#define upipe_throw_fatal(upipe, errcode)                                   \
    uprobe_throw_fatal((upipe)->uprobe, upipe, errcode)

/** @This throws an error event.
 *
 * @param upipe description structure of the pipe
 * @param errcode error code
 */
#define upipe_throw_error(upipe, errcode)                                   \
    uprobe_throw_error((upipe)->uprobe, upipe, errcode)

/** @This throws a source end event. This event is thrown when a pipe is unable
 * to read from an input because the end of file was reached, or because an
 * error occurred.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_source_end(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_SOURCE_END);
}

/** @This throws a sink end event. This event is thrown when a pipe is unable
 * to write to an output because the disk is full, or another error occurred.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_sink_end(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_SINK_END);
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

/** @This throws an event asking for a uclock.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_need_uclock(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_NEED_UCLOCK);
}

/** @This throws an event declaring a new flow definition on the output.
 *
 * @param upipe description structure of the pipe
 * @param flow_def definition for this flow
 */
static inline void upipe_throw_new_flow_def(struct upipe *upipe,
                                            struct uref *flow_def)
{
    upipe_throw(upipe, UPROBE_NEW_FLOW_DEF, flow_def);
}

/** @This throws an event asking for a ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_def definition for this flow
 */
static inline void upipe_throw_need_ubuf_mgr(struct upipe *upipe,
                                             struct uref *flow_def)
{
    upipe_throw(upipe, UPROBE_NEED_UBUF_MGR, flow_def);
}

/** @This throws an add flow event. This event is thrown whenever a split pipe
 * declares a new possible output flow.
 *
 * @param upipe description structure of the pipe
 * @param flow_id unique ID for the split pipe
 * @param flow_def definition for this flow
 */
static inline void upipe_split_throw_add_flow(struct upipe *upipe,
                                              uint64_t flow_id,
                                              struct uref *flow_def)
{
    upipe_throw(upipe, UPROBE_SPLIT_ADD_FLOW, flow_id, flow_def);
}

/** @This throws a del flow event. This event is thrown whenever a split pipe
 * declares that a given flow is no longer possible. If there is currently
 * an output subpipe on this flow, it will afterwards throw a read_end event.
 *
 * @param upipe description structure of the pipe
 * @param flow_id unique ID for the split pipe
 */
static inline void upipe_split_throw_del_flow(struct upipe *upipe,
                                              uint64_t flow_id)
{
    upipe_throw(upipe, UPROBE_SPLIT_DEL_FLOW, flow_id);
}

/** @This throws an event telling that a pipe synchronized on its input.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_sync_acquired(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_SYNC_ACQUIRED);
}

/** @This throws an event telling that a pipe lost synchronization with its
 * input.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_throw_sync_lost(struct upipe *upipe)
{
    upipe_throw(upipe, UPROBE_SYNC_LOST);
}

/** @This throws an event telling that the given uref carries a clock reference.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying a clock reference
 * @param clock_ref clock reference, in 27 MHz scale
 * @param discontinuity 1 if there is a suspicion of discontinuity
 */
static inline void upipe_throw_clock_ref(struct upipe *upipe, struct uref *uref,
                                         uint64_t clock_ref, int discontinuity)
{
    upipe_throw(upipe, UPROBE_CLOCK_REF, uref, clock_ref, discontinuity);
}

/** @This throws an event telling that the given uref carries a presentation
 * and/or a decoding timestamp. The uref must at least have k.pts.orig and/or
 * k.dts.orig set. Depending on the module documentation, k.pts and k.dts may
 * also be set. A probe is entitled to adding new attributes such as k.pts.sys
 * and/or k.dts.sys.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying a presentation and/or a decoding timestamp
 */
static inline void upipe_throw_clock_ts(struct upipe *upipe, struct uref *uref)
{
    upipe_throw(upipe, UPROBE_CLOCK_TS, uref);
}

#ifdef __cplusplus
}
#endif
#endif
