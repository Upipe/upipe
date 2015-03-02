/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
#include <upipe/urequest.h>
#include <upipe/udict_dump.h>

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
    /** sends a request to attach a uref manager (void) */
    UPIPE_ATTACH_UREF_MGR,
    /** sends a request to attach a upump manager (void) */
    UPIPE_ATTACH_UPUMP_MGR,
    /** sends a request to attach a uclock (void) */
    UPIPE_ATTACH_UCLOCK,
    /** gets uniform resource identifier (const char **) */
    UPIPE_GET_URI,
    /** sets uniform resource identifier (const char *) */
    UPIPE_SET_URI,
    /** gets a string option (const char *, const char **) */
    UPIPE_GET_OPTION,
    /** sets a string option (const char *, const char *) */
    UPIPE_SET_OPTION,

    /*
     * Input-related commands, normally called by the upstream pipe
     */
    /** registers a request (struct urequest *) */
    UPIPE_REGISTER_REQUEST,
    /** unregisters a request (struct urequest *) */
    UPIPE_UNREGISTER_REQUEST,
    /** sets input flow definition (struct uref *) */
    UPIPE_SET_FLOW_DEF,
    /** gets the length of the internal queue (unsigned int *) */
    UPIPE_GET_MAX_LENGTH,
    /** sets the length of the internal queue (unsigned int) */
    UPIPE_SET_MAX_LENGTH,
    /** flushes all currently held buffers and unblock the sources (void) */
    UPIPE_FLUSH,

    /*
     * Output-related commands
     */
    /** gets output (struct upipe **) */
    UPIPE_GET_OUTPUT,
    /** sets output (struct upipe *) */
    UPIPE_SET_OUTPUT,
    /** sends a request to attach a ubuf manager (void) */
    UPIPE_ATTACH_UBUF_MGR,
    /** gets output flow definition (struct uref **) */
    UPIPE_GET_FLOW_DEF,
    /** gets output packet size (unsigned int *) */
    UPIPE_GET_OUTPUT_SIZE,
    /** sets output packet size (unsigned int) */
    UPIPE_SET_OUTPUT_SIZE,

    /*
     * Split elements commands
     */
    /** iterates over the flows (struct uref **) */
    UPIPE_SPLIT_ITERATE,

    /*
     * Sub/super pipes commands
     */
    /** returns the sub manager associated with a super-pipe
     * (struct upipe_mgr **) */
    UPIPE_GET_SUB_MGR,
    /** iterates over subpipes (struct upipe **) */
    UPIPE_ITERATE_SUB,
    /** returns the super-pipe associated with a subpipe (struct upipe **) */
    UPIPE_SUB_GET_SUPER,

    /** non-standard commands implemented by a module type can start from
     * there (first arg = signature) */
    UPIPE_CONTROL_LOCAL = 0x8000
};

/** @This stores common parameters for upipe structures. */
struct upipe {
    /** pointer to refcount management structure */
    struct urefcount *refcount;
    /** structure for double-linked lists - for use by the application only */
    struct uchain uchain;
    /** opaque - for use by the application only */
    void *opaque;

    /** pointer to the uprobe hierarchy passed on initialization */
    struct uprobe *uprobe;
    /** pointer to the manager for this pipe type */
    struct upipe_mgr *mgr;
};

UBASE_FROM_TO(upipe, uchain, uchain, uchain)

/** @This defines standard commands which upipe managers may implement. */
enum upipe_mgr_command {
    /** release all buffers kept in pools (void) */
    UPIPE_MGR_VACUUM,

    /** non-standard manager commands implemented by a module type can start
     * from there (first arg = signature) */
    UPIPE_MGR_CONTROL_LOCAL = 0x8000
};

/** @This stores common management parameters for a pipe type. */
struct upipe_mgr {
    /** pointer to refcount management structure */
    struct urefcount *refcount;
    /** signature of the pipe allocator */
    unsigned int signature;

    /** function to create a pipe - uprobe belongs to the callee */
    struct upipe *(*upipe_alloc)(struct upipe_mgr *, struct uprobe *,
                                 uint32_t, va_list);
    /** function to send a uref to an input - the uref then belongs to the
     * callee */
    void (*upipe_input)(struct upipe *, struct uref *, struct upump **);
    /** control function for standard or local commands - all parameters
     * belong to the caller */
    int (*upipe_control)(struct upipe *, int, va_list);

    /** control function for standard or local manager commands - all parameters
     * belong to the caller */
    int (*upipe_mgr_control)(struct upipe_mgr *, int, va_list);
};

/** @This increments the reference count of a upipe manager.
 *
 * @param mgr pointer to upipe manager
 * @return same pointer to upipe manager
 */
static inline struct upipe_mgr *upipe_mgr_use(struct upipe_mgr *mgr)
{
    if (mgr == NULL)
        return NULL;
    urefcount_use(mgr->refcount);
    return mgr;
}

/** @This decrements the reference count of a upipe manager or frees it.
 *
 * @param mgr pointer to upipe manager
 */
static inline void upipe_mgr_release(struct upipe_mgr *mgr)
{
    if (mgr != NULL)
        urefcount_release(mgr->refcount);
}

/** @internal @This sends a control command to the pipe manager. Note that
 * thread semantics depend on the pipe manager. Also note that all arguments
 * are owned by the caller.
 *
 * @param mgr pointer to upipe manager
 * @param command manager control command to send
 * @param args optional read or write parameters
 * @return an error code
 */
static inline int upipe_mgr_control_va(struct upipe_mgr *mgr,
                                       int command, va_list args)
{
    assert(mgr != NULL);
    if (mgr->upipe_mgr_control == NULL)
        return UBASE_ERR_UNHANDLED;

    return mgr->upipe_mgr_control(mgr, command, args);
}

/** @internal @This sends a control command to the pipe manager. Note that
 * thread semantics depend on the pipe manager. Also note that all arguments
 * are owned by the caller.
 *
 * @param mgr pointer to upipe manager
 * @param command control manager command to send, followed by optional read
 * or write parameters
 * @return an error code
 */
static inline int upipe_mgr_control(struct upipe_mgr *mgr, int command, ...)
{
    int err;
    va_list args;
    va_start(args, command);
    err = upipe_mgr_control_va(mgr, command, args);
    va_end(args);
    return err;
}

/** @This instructs an existing upipe manager to release all structures
 * currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to upipe manager
 * @return an error code
 */
static inline int upipe_mgr_vacuum(struct upipe_mgr *mgr)
{
    return upipe_mgr_control(mgr, UPIPE_MGR_VACUUM);
}

/** @internal @This allocates and initializes a pipe.
 *
 * Please note that this function does not _use() the probe, so if you want
 * to reuse an existing probe, you have to use it first.
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
    return mgr->upipe_alloc(mgr, uprobe, signature, args);
}

/** @internal @This allocates and initializes a pipe with a variable list of
 * arguments.
 *
 * Please note that this function does not _use() the probe, so if you want
 * to reuse an existing probe, you have to use it first.
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

/** @This initializes the public members of a pipe.
 *
 * Please note that this function does not _use() the probe, so if you want
 * to reuse an existing probe, you have to use it first.
 *
 * @param upipe description structure of the pipe
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 */
static inline void upipe_init(struct upipe *upipe, struct upipe_mgr *mgr,
                              struct uprobe *uprobe)
{
    assert(upipe != NULL);
    uchain_init(&upipe->uchain);
    upipe->opaque = NULL;
    upipe->uprobe = uprobe;
    upipe->refcount = NULL;
    upipe->mgr = mgr;
    upipe_mgr_use(mgr);
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe pointer to upipe
 * @return same pointer to upipe
 */
static inline struct upipe *upipe_use(struct upipe *upipe)
{
    if (upipe == NULL)
        return NULL;
    urefcount_use(upipe->refcount);
    return upipe;
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe pointer to upipe
 */
static inline void upipe_release(struct upipe *upipe)
{
    if (upipe != NULL)
        urefcount_release(upipe->refcount);
}

/** @This checks if the pipe has more than one reference.
 *
 * @param upipe pointer to upipe
 * @return true if there is only one reference to the pipe
 */
static inline bool upipe_single(struct upipe *upipe)
{
    assert(upipe != NULL);
    return urefcount_single(upipe->refcount);
}

/** @This checks if the pipe has no more references.
 *
 * @param upipe pointer to upipe
 * @return true if there is no reference to the pipe
 */
static inline bool upipe_dead(struct upipe *upipe)
{
    assert(upipe != NULL);
    return urefcount_dead(upipe->refcount);
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

/** @This adds the given probe to the LIFO of probes associated with a pipe.
 * The new probe will be executed first.
 *
 * Please note that this function does not _use() the probe, so if you want
 * to reuse an existing probe, you have to use it first.
 *
 * @param upipe description structure of the pipe
 * @param uprobe pointer to probe
 */
static inline void upipe_push_probe(struct upipe *upipe, struct uprobe *uprobe)
{
    uprobe->next = upipe->uprobe;
    upipe->uprobe = uprobe;
}

/** @This deletes the first probe from the LIFO of probes associated with a
 * pipe, and returns it so it can be released.
 *
 * Please note that this function does not _release() the popped probe, so it
 * must be done by the caller.
 *
 * @param upipe description structure of the pipe
 * @return uprobe pointer to popped probe
 */
static inline struct uprobe *upipe_pop_probe(struct upipe *upipe)
{
    struct uprobe *uprobe = upipe->uprobe;
    if (uprobe != NULL)
        upipe->uprobe = uprobe->next;
    return uprobe;
}

/** @This should be called by the module writer before it disposes of its
 * upipe structure.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_clean(struct upipe *upipe)
{
    assert(upipe != NULL);
    uprobe_release(upipe->uprobe);
    upipe_mgr_release(upipe->mgr);
}

/** @internal @This throws generic events with optional arguments.
 *
 * @param upipe description structure of the pipe
 * @param event event to throw
 * @param args arguments
 * @return an error code
 */
static inline int upipe_throw_va(struct upipe *upipe, int event, va_list args)
{
    return uprobe_throw_va(upipe->uprobe, upipe, event, args);
}

/** @internal @This throws generic events with optional arguments.
 *
 * @param upipe description structure of the pipe
 * @param event event to throw, followed by arguments
 * @return an error code
 */
static inline int upipe_throw(struct upipe *upipe, int event, ...)
{
    va_list args;
    va_start(args, event);
    int err = upipe_throw_va(upipe, event, args);
    va_end(args);
    return err;
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
 * @return an error code
 */
#define upipe_throw_fatal(upipe, errcode)                                   \
    uprobe_throw_fatal((upipe)->uprobe, upipe, errcode)

/** @This throws an error event.
 *
 * @param upipe description structure of the pipe
 * @param errcode error code
 * @return an error code
 */
#define upipe_throw_error(upipe, errcode)                                   \
    uprobe_throw_error((upipe)->uprobe, upipe, errcode)

/** @This throws a ready event. This event is thrown whenever a
 * pipe is ready to accept input or respond to control commands.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_throw_ready(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw ready event");
    return upipe_throw(upipe, UPROBE_READY);
}

/** @This throws a dead event. This event is thrown whenever a
 * pipe is about to be destroyed and will no longer accept input and
 * control commands.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_throw_dead(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw dead event");
    return upipe_throw(upipe, UPROBE_DEAD);
}

/** @This throws a source end event. This event is thrown when a pipe is unable
 * to read from an input because the end of file was reached, or because an
 * error occurred.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_throw_source_end(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw source end");
    return upipe_throw(upipe, UPROBE_SOURCE_END);
}

/** @This throws a sink end event. This event is thrown when a pipe is unable
 * to write to an output because the disk is full, or another error occurred.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_throw_sink_end(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw sink end");
    return upipe_throw(upipe, UPROBE_SINK_END);
}

/** @This throws an event asking for an output, either because no output
 * pipe has been defined, or because the output pipe rejected the flow
 * definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def output flow definition
 * @return an error code
 */
static inline int upipe_throw_need_output(struct upipe *upipe,
                                          struct uref *flow_def)
{
    if (flow_def == NULL || flow_def->udict == NULL)
        upipe_dbg(upipe, "throw need output (NULL)");
    else {
        upipe_dbg(upipe, "throw need output");
        udict_dump(flow_def->udict, upipe->uprobe);
    }
    return upipe_throw(upipe, UPROBE_NEED_OUTPUT, flow_def);
}

/** @This throws an event asking to provide a urequest. It is thrown by a
 * pipe when it has no output, or when it would make no sense to forward the
 * request to the output (for instance a request for a ubuf manager when the
 * pipe is a decoder or encoder).
 *
 * @param upipe description structure of the pipe
 * @param urequest request to provide
 * @return an error code
 */
static inline int upipe_throw_provide_request(struct upipe *upipe,
                                              struct urequest *urequest)
{
    upipe_dbg_va(upipe, "throw provide request type %d", urequest->type);
    return upipe_throw(upipe, UPROBE_PROVIDE_REQUEST, urequest);
}

/** @This throws an event asking for a upump manager. Note that all parameters
 * belong to the caller, so there is no need to @ref upump_mgr_use the given
 * manager.
 *
 * @param upipe description structure of the pipe
 * @param upump_mgr_p filled in with a pointer to the upump manager
 * @return an error code
 */
static inline int upipe_throw_need_upump_mgr(struct upipe *upipe,
        struct upump_mgr **upump_mgr_p)
{
    upipe_dbg(upipe, "throw need upump mgr");
    int err = upipe_throw(upipe, UPROBE_NEED_UPUMP_MGR, upump_mgr_p);
    upipe_dbg_va(upipe, "got upump_mgr %p with error code 0x%x",
                 *upump_mgr_p, err);
    return err;
}

/** @This throws an event asking to freeze the upump manager of the current
 * thread. This allows to prepare pipes that will be deported later.
 * @see upipe_throw_thaw_upump_mgr
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_throw_freeze_upump_mgr(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw freeze upump mgr");
    return upipe_throw(upipe, UPROBE_FREEZE_UPUMP_MGR);
}

/** @This throws an event asking to thaw the upump manager of the current
 * thread. This allows to prepare pipes that will be deported later.
 * @see upipe_throw_freeze_upump_mgr
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_throw_thaw_upump_mgr(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw thaw upump mgr");
    return upipe_throw(upipe, UPROBE_THAW_UPUMP_MGR);
}

/** @This throws an event declaring a new flow definition on the output.
 *
 * @param upipe description structure of the pipe
 * @param flow_def definition for this flow
 * @return an error code
 */
static inline int upipe_throw_new_flow_def(struct upipe *upipe,
                                           struct uref *flow_def)
{
    if (flow_def == NULL || flow_def->udict == NULL)
        upipe_dbg(upipe, "throw new flow def (NULL)");
    else {
        upipe_dbg(upipe, "throw new flow def");
        udict_dump(flow_def->udict, upipe->uprobe);
    }
    return upipe_throw(upipe, UPROBE_NEW_FLOW_DEF, flow_def);
}

/** @This throws an event declaring a new random access point in the input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref containing the random access point
 * @return an error code
 */
static inline int upipe_throw_new_rap(struct upipe *upipe, struct uref *uref)
{
    return upipe_throw(upipe, UPROBE_NEW_RAP, uref);
}

/** @This throws an update event. This event is thrown whenever a split pipe
 * declares a new output flow list.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_split_throw_update(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw split update");
    return upipe_throw(upipe, UPROBE_SPLIT_UPDATE);
}

/** @This throws an event telling that a pipe synchronized on its input.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_throw_sync_acquired(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw sync acquired");
    return upipe_throw(upipe, UPROBE_SYNC_ACQUIRED);
}

/** @This throws an event telling that a pipe lost synchronization with its
 * input.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_throw_sync_lost(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw sync lost");
    return upipe_throw(upipe, UPROBE_SYNC_LOST);
}

/** @This throws an event telling that the given uref carries a clock reference.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying a clock reference
 * @param clock_ref clock reference, in 27 MHz scale
 * @param discontinuity 1 if there is a suspicion of discontinuity
 * @return an error code
 */
static inline int upipe_throw_clock_ref(struct upipe *upipe, struct uref *uref,
                                        uint64_t clock_ref, int discontinuity)
{
    return upipe_throw(upipe, UPROBE_CLOCK_REF, uref, clock_ref, discontinuity);
}

/** @This throws an event telling that the given uref carries a presentation
 * and/or a decoding timestamp. The uref must at least have k.dts.orig set.
 * Depending on the module documentation, k.dts may
 * also be set. A probe is entitled to adding new attributes such as k.pts.sys
 * and/or k.dts.sys.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying a presentation and/or a decoding timestamp
 * @return an error code
 */
static inline int upipe_throw_clock_ts(struct upipe *upipe, struct uref *uref)
{
    return upipe_throw(upipe, UPROBE_CLOCK_TS, uref);
}

/** @This catches an event coming from an inner pipe, and rethrows is as if
 * it were sent by the outermost pipe.
 *
 * @param upipe pointer to outermost pipe
 * @param inner pointer to inner pipe
 * @param event event thrown
 * @param args optional arguments of the event
 * @return an error code
 */
static inline int upipe_throw_proxy(struct upipe *upipe, struct upipe *inner,
                                    int event, va_list args)
{
    if (event != UPROBE_READY && event != UPROBE_DEAD)
        return upipe_throw_va(upipe, event, args);
    return UBASE_ERR_NONE;
}

/** @This sends an input buffer into a pipe. Note that all inputs and control
 * commands must be executed from the same thread - no reentrancy or locking
 * is required from the pipe. Also note that uref is then owned by the callee
 * and shouldn't be used any longer.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure to send
 * @param upump_p reference to the pump that generated the buffer
 */
static inline void upipe_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    assert(upipe != NULL);
    assert(upipe->mgr->upipe_input != NULL);
    upipe_use(upipe);
    upipe->mgr->upipe_input(upipe, uref, upump_p);
    upipe_release(upipe);
}

/** @internal @This sends a control command to the pipe. Note that all control
 * commands must be executed from the same thread - no reentrancy or locking
 * is required from the pipe. Also note that all arguments are owned by the
 * caller.
 *
 * This version doesn't print debug messages to avoid overflowing the console.
 *
 * @param upipe description structure of the pipe
 * @param command control command to send
 * @param args optional read or write parameters
 * @return an error code
 */
static inline int upipe_control_nodbg_va(struct upipe *upipe,
                                         int command, va_list args)
{
    assert(upipe != NULL);
    if (upipe->mgr->upipe_control == NULL)
        return UBASE_ERR_UNHANDLED;

    int err;
    upipe_use(upipe);
    err = upipe->mgr->upipe_control(upipe, command, args);
    upipe_release(upipe);
    return err;
}

/** @internal @This sends a control command to the pipe. Note that all control
 * commands must be executed from the same thread - no reentrancy or locking
 * is required from the pipe. Also note that all arguments are owned by the
 * caller.
 *
 * @param upipe description structure of the pipe
 * @param command control command to send
 * @param args optional read or write parameters
 * @return an error code
 */
static inline int upipe_control_va(struct upipe *upipe,
                                   int command, va_list args)
{
    int err = upipe_control_nodbg_va(upipe, command, args);
    if (unlikely(!ubase_check(err)))
        upipe_dbg_va(upipe, "returned error 0x%x to command 0x%x", err,
                     command);
    return err;
}

/** @internal @This sends a control command to the pipe. Note that all control
 * commands must be executed from the same thread - no reentrancy or locking
 * is required from the pipe. Also note that all arguments are owned by the
 * caller.
 *
 * This version doesn't print debug messages to avoid overflowing the console.
 *
 * @param upipe description structure of the pipe
 * @param command control command to send, followed by optional read or write
 * parameters
 * @return an error code
 */
static inline int upipe_control_nodbg(struct upipe *upipe, int command, ...)
{
    int err;
    va_list args;
    va_start(args, command);
    err = upipe_control_nodbg_va(upipe, command, args);
    va_end(args);
    return err;
}

/** @internal @This sends a control command to the pipe. Note that all control
 * commands must be executed from the same thread - no reentrancy or locking
 * is required from the pipe. Also note that all arguments are owned by the
 * caller.
 *
 * @param upipe description structure of the pipe
 * @param command control command to send, followed by optional read or write
 * parameters
 * @return an error code
 */
static inline int upipe_control(struct upipe *upipe, int command, ...)
{
    int err;
    va_list args;
    va_start(args, command);
    err = upipe_control_va(upipe, command, args);
    va_end(args);
    return err;
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
 * @return an error code                                                    \
 */                                                                         \
static inline int group##_get_##name(struct upipe *upipe, type *p)          \
{                                                                           \
    return upipe_control(upipe, GROUP##_GET_##NAME, p);                     \
}                                                                           \
/** @This sets the desc.                                                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param s new value                                                       \
 * @return an error code                                                    \
 */                                                                         \
static inline int group##_set_##name(struct upipe *upipe, type s)           \
{                                                                           \
    return upipe_control(upipe, GROUP##_SET_##NAME, s);                     \
}

UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, uri, URI,
                       const char *, uniform resource identifier)

UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, flow_def, FLOW_DEF, struct uref *,
                       flow definition of the output)
UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, output, OUTPUT, struct upipe *,
                       pipe acting as output (unsafe, use only internally))
UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, max_length, MAX_LENGTH, unsigned int,
                       max length of the internal queue)
UPIPE_CONTROL_TEMPLATE(upipe, UPIPE, output_size, OUTPUT_SIZE,
                       unsigned int, packet size of the output)
#undef UPIPE_CONTROL_TEMPLATE

/** @This gets a string option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param value_p filled with the value of the option
 * @return an error code
 */
static inline int upipe_get_option(struct upipe *upipe, const char *option,
                                   const char **value_p)
{
    return upipe_control(upipe, UPIPE_GET_OPTION, option, value_p);
}

/** @This sets a string option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param value value of the option
 * @return an error code
 */
static inline int upipe_set_option(struct upipe *upipe, const char *option,
                                   const char *value)
{
    return upipe_control(upipe, UPIPE_SET_OPTION, option, value);
}

/** @This sends a request to attach a uref manager.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_attach_uref_mgr(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_ATTACH_UREF_MGR);
}

/** @This sends a request to attach a upump manager.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_attach_upump_mgr(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_ATTACH_UPUMP_MGR);
}

/** @This sends a request to attach a uclock.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_attach_uclock(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_ATTACH_UCLOCK);
}

/** @This sends a request to attach a ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_attach_ubuf_mgr(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_ATTACH_UBUF_MGR);
}

/** @This registers a request.
 *
 * @param upipe description structure of the pipe
 * @param urequest description structure of the request
 * @return an error code
 */
static inline int upipe_register_request(struct upipe *upipe,
                                         struct urequest *urequest)
{
    return upipe_control(upipe, UPIPE_REGISTER_REQUEST, urequest);
}

/** @This unregisters a request.
 *
 * @param upipe description structure of the pipe
 * @param urequest description structure of the request
 * @return an error code
 */
static inline int upipe_unregister_request(struct upipe *upipe,
                                           struct urequest *urequest)
{
    return upipe_control(upipe, UPIPE_UNREGISTER_REQUEST, urequest);
}

/** @This flushes all currently held buffers, and unblocks the sources.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_flush(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_FLUSH);
}

/** @deprecated @see upipe_flush */
static inline UBASE_DEPRECATED int upipe_sink_flush(struct upipe *upipe)
{
    return upipe_flush(upipe);
}

/** @This iterates over the list of possible output flow definitions.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the next flow def, initialize at NULL
 * @return an error code
 */
static inline int upipe_split_iterate(struct upipe *upipe, struct uref **p)
{
    return upipe_control(upipe, UPIPE_SPLIT_ITERATE, p);
}

/** @This returns the subpipe manager of a super-pipe.
 *
 * @param upipe description structure of the super-pipe
 * @param p filled in with a pointer to the subpipe manager
 * @return an error code
 */
static inline int upipe_get_sub_mgr(struct upipe *upipe, struct upipe_mgr **p)
{
    return upipe_control(upipe, UPIPE_GET_SUB_MGR, p);
}

/** @This iterates over the subpipes of a super-pipe.
 *
 * @param upipe description structure of the super-pipe
 * @param p filled in with a pointer to the next subpipe, initialize at NULL
 * @return an error code
 */
static inline int upipe_iterate_sub(struct upipe *upipe, struct upipe **p)
{
    return upipe_control(upipe, UPIPE_ITERATE_SUB, p);
}

/** @This returns the super-pipe of a subpipe.
 *
 * @param upipe description structure of the subpipe
 * @param p filled in with a pointer to the super-pipe
 * @return an error code
 */
static inline int upipe_sub_get_super(struct upipe *upipe, struct upipe **p)
{
    return upipe_control(upipe, UPIPE_SUB_GET_SUPER, p);
}

/** @This declares ten functions to allocate pipes with a certain pipe
 * allocator.
 *
 * Supposing the name of the allocator is upipe_foo, it declares:
 * @list
 * @item @code
 *  struct upipe *upipe_foo_alloc(struct upipe_mgr *upipe_mgr,
 *                                struct uprobe *uprobe, ...)
 * @end code
 * The basic pipe allocator.
 *
 * @item @code
 *  struct upipe *upipe_foo_alloc_output(struct upipe *upipe,
 *                                       struct upipe_mgr *upipe_mgr,
 *                                       struct uprobe *uprobe, ...)
 * @end code
 * A wrapper to upipe_foo_alloc() which additionally sets the allocated
 * pipe as the output of the upipe argument.
 *
 * @item @code
 *  struct upipe *upipe_foo_chain_output(struct upipe *upipe,
 *                                       struct upipe_mgr *upipe_mgr,
 *                                       struct uprobe *uprobe, ...)
 * @end code
 * A wrapper to upipe_foo_alloc_output() which additionally releases the
 * upipe argument.
 *
 * @item @code
 *  struct upipe *upipe_foo_alloc_input(struct upipe *upipe,
 *                                      struct upipe_mgr *upipe_mgr,
 *                                      struct uprobe *uprobe, ...)
 * @end code
 * A wrapper to upipe_foo_alloc() which additionally sets the upipe argument
 * as the output of the allocated pipe.
 *
 * @item @code
 *  struct upipe *upipe_foo_chain_input(struct upipe *upipe,
 *                                      struct upipe_mgr *upipe_mgr,
 *                                      struct uprobe *uprobe, ...)
 * @end code
 * A wrapper to upipe_foo_alloc_input() which additionally releases the
 * upipe argument.
 *
 * @item @code
 *  struct upipe *upipe_foo_alloc_sub(struct upipe *super_pipe,
 *                                    struct uprobe *uprobe, ...)
 * @end code
 * A wrapper to upipe_foo_alloc() which retrieves the subpipe manager from the
 * given super-pipe.
 *
 * @item @code
 *  struct upipe *upipe_foo_alloc_output_sub(struct upipe *upipe,
 *                                           struct upipe *super_pipe,
 *                                           struct uprobe *uprobe, ...)
 * @end code
 * A wrapper to upipe_foo_alloc_sub() which additionally sets the allocated
 * pipe as the output of the upipe argument.
 *
 * @item @code
 *  struct upipe *upipe_foo_chain_output_sub(struct upipe *upipe,
 *                                           struct upipe *super_pipe,
 *                                           struct uprobe *uprobe, ...)
 * @end code
 * A wrapper to upipe_foo_alloc_output_sub() which additionally releases the
 * upipe argument.
 *
 * @item @code
 *  struct upipe *upipe_foo_alloc_input_sub(struct upipe *upipe,
 *                                          struct upipe *super_pipe,
 *                                          struct uprobe *uprobe, ...)
 * @end code
 * A wrapper to upipe_foo_alloc_sub() which additionally sets the upipe argument
 * as the output of the allocated pipe.
 *
 * @item @code
 *  struct upipe *upipe_foo_chain_input_sub(struct upipe *upipe,
 *                                          struct upipe *super_pipe,
 *                                          struct uprobe *uprobe, ...)
 * @end code
 * A wrapper to upipe_foo_alloc_input() which additionally releases the
 * upipe argument.
 * @end list
 *
 * Please note that you must declare upipe_foo_alloc before this helper,
 * and the macros ARGS_DECL and ARGS must be filled respectively with the
 * declaration of arguments of upipe_foo_alloc, and the use of them in the
 * call to uprobe_foo_alloc (arguments after uprobe).
 *
 * @param GROUP name of the allocator
 */
#define UPIPE_HELPER_ALLOC(GROUP, SIGNATURE)                                \
/** @This allocates and initializes a pipe from the given manager.          \
 *                                                                          \
 * Please note that this function does not _use() the probe, so if you want \
 * to reuse an existing probe, you have to use it first.                    \
 *                                                                          \
 * @param mgr management structure for this pipe type                       \
 * @param uprobe structure used to raise events (belongs to the callee),    \
 * followed by arguments for the allocator (@see upipe_##GROUP##_alloc)     \
 * @return pointer to allocated pipe, or NULL in case of failure            \
 */                                                                         \
static inline struct upipe *                                                \
    upipe_##GROUP##_alloc(struct upipe_mgr *mgr,                            \
                          struct uprobe *uprobe  ARGS_DECL)                 \
{                                                                           \
    return upipe_alloc(mgr, uprobe, SIGNATURE  ARGS);                       \
}                                                                           \
/** @This allocates a new pipe from the given manager, and sets it as the   \
 * output of the given pipe.                                                \
 *                                                                          \
 * Please note that this function does not _use() the probe, so if you want \
 * to reuse an existing probe, you have to use it first.                    \
 *                                                                          \
 * @param upipe description structure of the existing pipe                  \
 * @param upipe_mgr manager for the output pipe                             \
 * @param uprobe structure used to raise events (belongs to the callee),    \
 * followed by arguments for the allocator (@see upipe_##GROUP##_alloc)     \
 * @return pointer to allocated output pipe (which must be stored or        \
 * released), or NULL in case of failure                                    \
 */                                                                         \
static inline struct upipe *                                                \
    upipe_##GROUP##_alloc_output(struct upipe *upipe,                       \
                                 struct upipe_mgr *upipe_mgr,               \
                                 struct uprobe *uprobe  ARGS_DECL)          \
{                                                                           \
    struct upipe *output = upipe_##GROUP##_alloc(upipe_mgr, uprobe  ARGS);  \
    if (unlikely(output == NULL)) {                                         \
        uprobe_release(uprobe);                                             \
        return NULL;                                                        \
    }                                                                       \
    if (unlikely(!ubase_check(upipe_set_output(upipe, output)))) {          \
        upipe_release(output);                                              \
        return NULL;                                                        \
    }                                                                       \
    return output;                                                          \
}                                                                           \
/** @This allocates a new pipe from the given manager, sets it as the       \
 * output of the given pipe, and releases the latter.                       \
 *                                                                          \
 * Please note that this function does not _use() the probe, so if you want \
 * to reuse an existing probe, you have to use it first.                    \
 *                                                                          \
 * @param upipe description structure of the pipe (belongs to the callee)   \
 * @param upipe_mgr manager for the output pipe                             \
 * @param uprobe structure used to raise events (belongs to the callee),    \
 * followed by arguments for the allocator (@see upipe_##GROUP##_alloc)     \
 * @return pointer to allocated output pipe (which must be stored or        \
 * released), or NULL in case of failure                                    \
 */                                                                         \
static inline struct upipe *                                                \
    upipe_##GROUP##_chain_output(struct upipe *upipe,                       \
                                 struct upipe_mgr *upipe_mgr,               \
                                 struct uprobe *uprobe  ARGS_DECL)          \
{                                                                           \
    if (unlikely(upipe == NULL))                                            \
        return NULL;                                                        \
    struct upipe *output = upipe_##GROUP##_alloc_output(upipe, upipe_mgr,   \
                                                        uprobe  ARGS);      \
    upipe_release(upipe);                                                   \
    return output;                                                          \
}                                                                           \
/** @This allocates a new pipe from the given manager, and sets it as the   \
 * input of the given pipe.                                                 \
 *                                                                          \
 * Please note that this function does not _use() the probe, so if you want \
 * to reuse an existing probe, you have to use it first.                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param upipe_mgr manager for the input pipe                              \
 * @param uprobe structure used to raise events (belongs to the callee),    \
 * followed by arguments for the allocator (@see upipe_##GROUP##_alloc)     \
 * @return pointer to allocated input pipe (which must be stored or         \
 * released), or NULL in case of failure                                    \
 */                                                                         \
static inline struct upipe *                                                \
    upipe_##GROUP##_alloc_input(struct upipe *upipe,                        \
                                struct upipe_mgr *upipe_mgr,                \
                                struct uprobe *uprobe  ARGS_DECL)           \
{                                                                           \
    struct upipe *input = upipe_##GROUP##_alloc(upipe_mgr, uprobe  ARGS);   \
    if (unlikely(input == NULL)) {                                          \
        uprobe_release(uprobe);                                             \
        return NULL;                                                        \
    }                                                                       \
    if (unlikely(!ubase_check(upipe_set_output(input, upipe)))) {           \
        upipe_release(input);                                               \
        return NULL;                                                        \
    }                                                                       \
    return input;                                                           \
}                                                                           \
/** @This allocates a new pipe from the given manager, sets it as the       \
 * input of the given pipe, and releases it.                                \
 *                                                                          \
 * Please note that this function does not _use() the probe, so if you want \
 * to reuse an existing probe, you have to use it first.                    \
 *                                                                          \
 * @param upipe description structure of the pipe (belongs to the callee)   \
 * @param upipe_mgr manager for the input pipe                              \
 * @param uprobe structure used to raise events (belongs to the callee),    \
 * followed by arguments for the allocator (@see upipe_##GROUP##_alloc)     \
 * @return pointer to allocated input pipe (which must be stored or         \
 * released), or NULL in case of failure                                    \
 */                                                                         \
static inline struct upipe *                                                \
    upipe_##GROUP##_chain_input(struct upipe *upipe,                        \
                                struct upipe_mgr *upipe_mgr,                \
                                struct uprobe *uprobe  ARGS_DECL)           \
{                                                                           \
    if (unlikely(upipe == NULL))                                            \
        return NULL;                                                        \
    struct upipe *input = upipe_##GROUP##_alloc_input(upipe, upipe_mgr,     \
                                                      uprobe  ARGS);        \
    upipe_release(upipe);                                                   \
    return input;                                                           \
}                                                                           \
/** @This allocates and initializes a subpipe from the given super-pipe.    \
 *                                                                          \
 * Please note that this function does not _use() the probe, so if you want \
 * to reuse an existing probe, you have to use it first.                    \
 *                                                                          \
 * @param super_upipe description structure of the super-pipe               \
 * @param uprobe structure used to raise events (belongs to the callee)     \
 * followed by arguments for the allocator (@see upipe_##GROUP##_alloc)     \
 * @return pointer to allocated subpipe, or NULL in case of failure         \
 */                                                                         \
static inline struct upipe *                                                \
    upipe_##GROUP##_alloc_sub(struct upipe *super_pipe,                     \
                              struct uprobe *uprobe  ARGS_DECL)             \
{                                                                           \
    struct upipe_mgr *sub_mgr;                                              \
    if (unlikely(!ubase_check(upipe_get_sub_mgr(super_pipe, &sub_mgr)))) {  \
        uprobe_release(uprobe);                                             \
        return NULL;                                                        \
    }                                                                       \
    return upipe_##GROUP##_alloc(sub_mgr, uprobe  ARGS);                    \
}                                                                           \
/** @This allocates a subpipe from the given super-pipe, and sets it as the \
 * output of the given pipe.                                                \
 *                                                                          \
 * Please note that this function does not _use() the probe, so if you want \
 * to reuse an existing probe, you have to use it first.                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param super_pipe description structure of the super-pipe                \
 * @param uprobe structure used to raise events (belongs to the callee)     \
 * followed by arguments for the allocator (@see upipe_##GROUP##_alloc)     \
 * @return pointer to allocated output subpipe (which must be stored or     \
 * released), or NULL in case of failure                                    \
 */                                                                         \
static inline struct upipe *                                                \
    upipe_##GROUP##_alloc_output_sub(struct upipe *upipe,                   \
                                     struct upipe *super_pipe,              \
                                     struct uprobe *uprobe  ARGS_DECL)      \
{                                                                           \
    struct upipe_mgr *sub_mgr;                                              \
    if (!ubase_check(upipe_get_sub_mgr(super_pipe, &sub_mgr))) {            \
        uprobe_release(uprobe);                                             \
        return NULL;                                                        \
    }                                                                       \
    return upipe_##GROUP##_alloc_output(upipe, sub_mgr, uprobe  ARGS);      \
}                                                                           \
/** @This allocates a subpipe from the given super-pipe, sets it as the     \
 * output of the given pipe, and releases it.                               \
 *                                                                          \
 * Please note that this function does not _use() the probe, so if you want \
 * to reuse an existing probe, you have to use it first.                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param super_pipe description structure of the super-pipe                \
 * @param uprobe structure used to raise events (belongs to the callee)     \
 * followed by arguments for the allocator (@see upipe_##GROUP##_alloc)     \
 * @return pointer to allocated output subpipe (which must be stored or     \
 * released), or NULL in case of failure                                    \
 */                                                                         \
static inline struct upipe *                                                \
    upipe_##GROUP##_chain_output_sub(struct upipe *upipe,                   \
                                     struct upipe *super_pipe,              \
                                     struct uprobe *uprobe  ARGS_DECL)      \
{                                                                           \
    if (unlikely(upipe == NULL))                                            \
        return NULL;                                                        \
    struct upipe *output = upipe_##GROUP##_alloc_output_sub(upipe,          \
            super_pipe, uprobe  ARGS);                                      \
    upipe_release(upipe);                                                   \
    return output;                                                          \
}                                                                           \
/** @This allocates a subpipe from the given super-pipe, and sets it as the \
 * input of the given pipe.                                                 \
 *                                                                          \
 * Please note that this function does not _use() the probe, so if you want \
 * to reuse an existing probe, you have to use it first.                    \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param super_pipe description structure of the super-pipe                \
 * @param uprobe structure used to raise events (belongs to the callee),    \
 * followed by arguments for the allocator (@see upipe_##GROUP##_alloc)     \
 * @return pointer to allocated input pipe (which must be stored or         \
 * released), or NULL in case of failure                                    \
 */                                                                         \
static inline struct upipe *                                                \
    upipe_##GROUP##_alloc_input_sub(struct upipe *upipe,                    \
                                    struct upipe *super_pipe,               \
                                    struct uprobe *uprobe  ARGS_DECL)       \
{                                                                           \
    struct upipe_mgr *sub_mgr;                                              \
    if (!ubase_check(upipe_get_sub_mgr(super_pipe, &sub_mgr))) {            \
        uprobe_release(uprobe);                                             \
        return NULL;                                                        \
    }                                                                       \
    return upipe_##GROUP##_alloc_input(upipe, sub_mgr, uprobe  ARGS);       \
}                                                                           \
/** @This allocates a new pipe from the given manager, sets it as the       \
 * input of the given pipe, and releases it.                                \
 *                                                                          \
 * Please note that this function does not _use() the probe, so if you want \
 * to reuse an existing probe, you have to use it first.                    \
 *                                                                          \
 * @param upipe description structure of the pipe (belongs to the callee)   \
 * @param super_pipe description structure of the super-pipe                \
 * @param uprobe structure used to raise events (belongs to the callee),    \
 * followed by arguments for the allocator (@see upipe_##GROUP##_alloc)     \
 * @return pointer to allocated input pipe (which must be stored or         \
 * released), or NULL in case of failure                                    \
 */                                                                         \
static inline struct upipe *                                                \
    upipe_##GROUP##_chain_input_sub(struct upipe *upipe,                    \
                                    struct upipe *super_pipe,               \
                                    struct uprobe *uprobe  ARGS_DECL)       \
{                                                                           \
    if (unlikely(upipe == NULL))                                            \
        return NULL;                                                        \
    struct upipe *input = upipe_##GROUP##_alloc_input_sub(upipe,            \
            super_pipe, uprobe  ARGS);                                      \
    upipe_release(upipe);                                                   \
    return input;                                                           \
}

/** @hidden */
#define ARGS_DECL
/** @hidden */
#define ARGS
UPIPE_HELPER_ALLOC(void, UPIPE_VOID_SIGNATURE)
#undef ARGS
#undef ARGS_DECL

/** @hidden */
#define ARGS_DECL , struct uref *flow_def
/** @hidden */
#define ARGS , flow_def
UPIPE_HELPER_ALLOC(flow, UPIPE_FLOW_SIGNATURE)
#undef ARGS
#undef ARGS_DECL

#ifdef __cplusplus
}
#endif
#endif
