/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * IN NO WATCHER SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe event loop handling
 */

#ifndef _UPIPE_UPUMP_H_
/** @hidden */
#define _UPIPE_UPUMP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulist.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

/** @hidden */
struct upump_mgr;
/** @hidden */
struct upump;
/** @hidden */
struct upump_blocker;

/** types of pumps */
enum upump_type {
    /** event continuously triggers (no argument) */
    UPUMP_TYPE_IDLER,
    /** event triggers once after a given timeout (arguments = uint64_t,
     * uint64_t) */
    UPUMP_TYPE_TIMER,
    /** event triggers on available data from file descriptor
     * (argument = int) */
    UPUMP_TYPE_FD_READ,
    /** event triggers on available writing space to file descriptor
     * (argument = int) */
    UPUMP_TYPE_FD_WRITE
    /* TODO: Windows objects */
};

/** function called when a pump is triggered */
typedef void (*upump_cb)(struct upump *);

/** @This stores a pump of a given event loop.
 *
 * The structure is not refcounted and shouldn't be used by more than one
 * thread at once.
 */
struct upump {
    /** structure for double-linked lists - for use by the allocating pipe */
    struct uchain uchain;
    /** pointer to the event loop manager */
    struct upump_mgr *mgr;

    /** true if upump_start() was called on the pump */
    bool started;
    /** blockers registered on this pump */
    struct uchain blockers;

    /** function to call back when triggered */
    upump_cb cb;
    /** opaque pointer for the callback */
    void *opaque;
};

UBASE_FROM_TO(upump, uchain, uchain, uchain)

/** @This defines standard commands which upump managers may implement. */
enum upump_mgr_command {
    /** release all buffers kept in pools (void) */
    UPUMP_MGR_VACUUM,

    /** non-standard manager commands implemented by a module type can start
     * from there (first arg = signature) */
    UPUMP_MGR_CONTROL_LOCAL = 0x8000
};

/** @This stores common management parameters for a given event loop. */
struct upump_mgr {
    /** pointer to refcount management structure */
    struct urefcount *refcount;
    /** structure for double-linked lists - for use by the application only */
    struct uchain uchain;
    /** opaque - for use by the application only */
    void *opaque;

    /** function to create a pump */
    struct upump *(*upump_alloc)(struct upump_mgr *,
                                 enum upump_type, va_list);
    /** function to start a pump */
    void (*upump_start)(struct upump *);
    /** function to stop a pump */
    void (*upump_stop)(struct upump *);
    /** function to free the pump */
    void (*upump_free)(struct upump *);

    /** function to create a blocker */
    struct upump_blocker *(*upump_blocker_alloc)(struct upump *);
    /** function to free the blocker */
    void (*upump_blocker_free)(struct upump_blocker *);

    /** control function for standard or local manager commands - all parameters
     * belong to the caller */
    int (*upump_mgr_control)(struct upump_mgr *, int, va_list);
};

UBASE_FROM_TO(upump_mgr, uchain, uchain, uchain)

/** @internal @This allocates and initializes a pump.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the pump triggers
 * @param opaque pointer to the module's internal structure
 * @param event type of event to watch for, followed by optional parameters
 * @return pointer to allocated pump, or NULL in case of failure
 */
static inline struct upump *upump_alloc(struct upump_mgr *mgr,
                                        upump_cb cb, void *opaque,
                                        enum upump_type event, ...)
{
    struct upump *upump;
    va_list args;
    va_start(args, event);
    upump = mgr->upump_alloc(mgr, event, args);
    va_end(args);
    if (unlikely(upump == NULL))
        return NULL;

    uchain_init(&upump->uchain);
    upump->cb = cb;
    upump->opaque = opaque;
    return upump;
}

/** @This allocates and initializes an idler pump.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the pump triggers
 * @param opaque pointer to the module's internal structure
 * @return pointer to allocated pump, or NULL in case of failure
 */
static inline struct upump *upump_alloc_idler(struct upump_mgr *mgr,
                                              upump_cb cb, void *opaque)
{
    return upump_alloc(mgr, cb, opaque, UPUMP_TYPE_IDLER);
}

/** @This allocates and initializes a pump for a timer.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the pump triggers
 * @param opaque pointer to the module's internal structure
 * @param after time after which it triggers, in ticks of a 27 MHz monotonic
 * clock
 * @param repeat pump will trigger again each repeat occurrence, in ticks
 * of a 27 MHz monotonic clock (0 to disable)
 * @return pointer to allocated pump, or NULL in case of failure
 */
static inline struct upump *upump_alloc_timer(struct upump_mgr *mgr,
                                              upump_cb cb, void *opaque,
                                              uint64_t after, uint64_t repeat)
{
    return upump_alloc(mgr, cb, opaque, UPUMP_TYPE_TIMER, after, repeat);
}

/** @This allocates and initializes a pump for a readable file descriptor.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the pump triggers
 * @param opaque pointer to the module's internal structure
 * @param fd file descriptor to watch
 * @return pointer to allocated pump, or NULL in case of failure
 */
static inline struct upump *upump_alloc_fd_read(struct upump_mgr *mgr,
                                                upump_cb cb, void *opaque,
                                                int fd)
{
    return upump_alloc(mgr, cb, opaque, UPUMP_TYPE_FD_READ, fd);
}

/** @This allocates and initializes a pump for a writable file descriptor.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the pump triggers
 * @param opaque pointer to the module's internal structure
 * @param fd file descriptor to watch
 * @return pointer to allocated pump, or NULL in case of failure
 */
static inline struct upump *upump_alloc_fd_write(struct upump_mgr *mgr,
                                                 upump_cb cb, void *opaque,
                                                 int fd)
{
    return upump_alloc(mgr, cb, opaque, UPUMP_TYPE_FD_WRITE, fd);
}

/** @This asks the event loop to start monitoring a pump.
 *
 * @param pump description structure of the pump
 */
static inline void upump_start(struct upump *upump)
{
    upump->mgr->upump_start(upump);
}

/** @This asks the event loop to stop monitoring a pump.
 *
 * @param pump description structure of the pump
 */
static inline void upump_stop(struct upump *upump)
{
    upump->mgr->upump_stop(upump);
}

/** @This frees a struct upump structure.
 * Please note that the pump must be stopped before.
 *
 * @param upump description structure of the pump
 */
static inline void upump_free(struct upump *upump)
{
    if (upump == NULL)
        return;
    upump->mgr->upump_free(upump);
}

/** @This gets the opaque structure with a cast.
 *
 * @param upump description structure of the pump
 * @param type type to cast to
 * return opaque
 */
#define upump_get_opaque(upump, type) (type)(upump)->opaque

/** @This sets the callback parameters of an existing pump.
 *
 * @param upump description structure of the pump
 * @param cb function to call when the pump triggers
 * @param opaque pointer to the module's internal structure
 */
static inline void upump_set_cb(struct upump *upump, upump_cb cb, void *opaque)
{
    upump->cb = cb;
    upump->opaque = opaque;
}

/** @This increments the reference count of a upump manager.
 *
 * @param mgr pointer to upump manager
 * @return same pointer to upump manager
 */
static inline struct upump_mgr *upump_mgr_use(struct upump_mgr *mgr)
{
    if (mgr == NULL)
        return NULL;
    urefcount_use(mgr->refcount);
    return mgr;
}

/** @This decrements the reference count of a upump manager of frees it.
 *
 * @param mgr pointer to upump manager
 */
static inline void upump_mgr_release(struct upump_mgr *mgr)
{
    if (mgr != NULL)
        urefcount_release(mgr->refcount);
}

/** @This gets the opaque member of a upump manager.
 *
 * @param upump_mgr pointer to upump_mgr
 * @param type type to cast to
 * @return opaque
 */
#define upump_mgr_get_opaque(upump_mgr, type) (type)(upump_mgr)->opaque

/** @This sets the opaque member of a upump manager
 *
 * @param upump_mgr pointer to upump_mgr
 * @param opaque opaque
 */
static inline void upump_mgr_set_opaque(struct upump_mgr *upump_mgr,
                                        void *opaque)
{
    upump_mgr->opaque = opaque;
}

/** @internal @This sends a control command to the upump manager. Note that
 * all arguments are owned by the caller.
 *
 * @param mgr pointer to upump manager
 * @param command manager control command to send
 * @param args optional read or write parameters
 * @return an error code
 */
static inline int upump_mgr_control_va(struct upump_mgr *mgr,
                                       int command, va_list args)
{
    assert(mgr != NULL);
    if (mgr->upump_mgr_control == NULL)
        return UBASE_ERR_UNHANDLED;

    return mgr->upump_mgr_control(mgr, command, args);
}

/** @internal @This sends a control command to the pipe manager. Note that
 * thread semantics depend on the pipe manager. Also note that all arguments
 * are owned by the caller.
 *
 * @param mgr pointer to upump manager
 * @param command control manager command to send, followed by optional read
 * or write parameters
 * @return an error code
 */
static inline int upump_mgr_control(struct upump_mgr *mgr, int command, ...)
{
    int err;
    va_list args;
    va_start(args, command);
    err = upump_mgr_control_va(mgr, command, args);
    va_end(args);
    return err;
}

/** @This instructs an existing upump manager to release all structures
 * currently kept in pools. It is inteded as a debug tool only.
 *
 * @param mgr pointer to upump manager
 * @return an error code
 */
static inline int upump_mgr_vacuum(struct upump_mgr *mgr)
{
    return upump_mgr_control(mgr, UPUMP_MGR_VACUUM);
}

#ifdef __cplusplus
}
#endif
#endif
