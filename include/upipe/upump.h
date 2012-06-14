/*****************************************************************************
 * upump.h: upipe event loop handling
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
 * IN NO WATCHER SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef _UPIPE_UPUMP_H_
/** @hidden */
#define _UPIPE_UPUMP_H_

#include <upipe/ubase.h>
#include <upipe/urefcount.h>

#include <stdint.h>
#include <stdarg.h>

/** @hidden */
struct upump_mgr;
/** @hidden */
struct upump;

/** types of upump watchers */
enum upump_watcher {
    /** event continuously triggers (no argument) */
    UPUMP_WATCHER_IDLER,
    /** event triggers after a given timeout (arguments = uint64_t, uint64_t) */
    UPUMP_WATCHER_TIMER,
    /** event triggers on available data from file descriptor
     * (argument = int) */
    UPUMP_WATCHER_FD_READ,
    /** event triggers on available writing space to file descriptor
     * (argument = int) */
    UPUMP_WATCHER_FD_WRITE
    /* TODO: Windows objects */
};

/** function called when a watcher is triggered */
typedef void (*upump_cb)(struct upump *);

/** @This stores an event watcher of a given event loop.
 *
 * The structure is not refcounted and shouldn't be used by more than one
 * thread at once.
 */
struct upump {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** pointer to the event loop manager */
    struct upump_mgr *mgr;

    /** function to call back when triggered */
    upump_cb cb;
    /** opaque pointer for the callback */
    void *opaque;
};

/** @This stores common management parameters for a given event loop. */
struct upump_mgr {
    /** refcount management structure
     * NOTE: the atomicity is probably unneeded here */
    urefcount refcount;

    /** number of blocked sinks */
    unsigned int nb_blocked_sinks;

    /** function to create a watcher */
    struct upump *(*upump_alloc)(struct upump_mgr *, bool,
                                 enum upump_watcher, va_list);
    /** function to start a watcher */
    bool (*upump_start)(struct upump *, bool);
    /** function to stop a watcher */
    bool (*upump_stop)(struct upump *, bool);
    /** function to free the watcher */
    void (*upump_free)(struct upump *);

    /** function to free the struct upump_mgr structure */
    void (*upump_mgr_free)(struct upump_mgr *);
};

/** @internal @This allocates and initializes a watcher.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @param source true if this watcher is a source and should block when sinks
 * are blocked
 * @param event type of event to watch for, followed by optional parameters
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *upump_alloc(struct upump_mgr *mgr, upump_cb cb,
                                        void *opaque, bool source,
                                        enum upump_watcher event, ...)
{
    struct upump *upump;
    va_list args;
    va_start(args, event);
    upump = mgr->upump_alloc(mgr, source, event, args);
    va_end(args);
    if (unlikely(upump == NULL)) return NULL;

    uchain_init(&upump->uchain);
    upump->mgr = mgr;
    upump->cb = cb;
    upump->opaque = opaque;
    return upump;
}

/** @This allocates and initializes an idler watcher.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @param source true if this watcher is a source and should block when sinks
 * are blocked
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *upump_alloc_idler(struct upump_mgr *mgr,
                                              upump_cb cb, void *opaque,
                                              bool source)
{
    return upump_alloc(mgr, cb, opaque, source, UPUMP_WATCHER_IDLER);
}

/** @This allocates and initializes a watcher for a timer.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @param source true if this watcher is a source and should block when sinks
 * are blocked
 * @param after time after which it triggers, in ticks of a 27 MHz monotonic
 * clock
 * @param repeat watcher will trigger again each repeat occurrence, in ticks
 * of a 27 MHz monotonic clock
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *upump_alloc_timer(struct upump_mgr *mgr,
                                              upump_cb cb, void *opaque,
                                              bool source,
                                              uint64_t after, uint64_t repeat)
{
    return upump_alloc(mgr, cb, opaque, source, UPUMP_WATCHER_TIMER,
                       after, repeat);
}

/** @This allocates and initializes a watcher for a readable file descriptor.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @param source true if this watcher is a source and should block when sinks
 * are blocked
 * @param fd file descriptor to watch
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *upump_alloc_fd_read(struct upump_mgr *mgr,
                                                upump_cb cb, void *opaque,
                                                bool source, int fd)
{
    return upump_alloc(mgr, cb, opaque, source, UPUMP_WATCHER_FD_READ, fd);
}

/** @This allocates and initializes a watcher for a writable file descriptor.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @param source true if this watcher is a source and should block when sinks
 * are blocked
 * @param fd file descriptor to watch
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *upump_alloc_fd_write(struct upump_mgr *mgr,
                                                 upump_cb cb, void *opaque,
                                                 bool source, int fd)
{
    return upump_alloc(mgr, cb, opaque, source, UPUMP_WATCHER_FD_WRITE, fd);
}

/** @This asks the event loop to start monitoring a watcher.
 *
 * @param watcher description structure of the watcher
 * @return false in case of failure
 */
static inline bool upump_start(struct upump *upump)
{
    return upump->mgr->upump_start(upump, false);
}

/** @internal @This forces starting a watcher.
 * It is used internally to enable sources after an iteration of the loop.
 *
 * @param watcher description structure of the watcher
 * @return false in case of failure
 */
static inline bool upump_start_source(struct upump *upump)
{
    return upump->mgr->upump_start(upump, true);
}

/** @This asks the event loop to stop monitoring a watcher.
 *
 * @param watcher description structure of the watcher
 * @return false in case of failure
 */
static inline bool upump_stop(struct upump *upump)
{
    return upump->mgr->upump_stop(upump, false);
}

/** @This frees a struct upump structure.
 * Please note that the watcher must be stopped before.
 *
 * @param upump description structure of the watcher
 */
static inline void upump_free(struct upump *upump)
{
    upump->mgr->upump_free(upump);
}

/** @internal @This forces stopping a watcher.
 * It is used internally to disable sources after an iteration of the loop.
 *
 * @param watcher description structure of the watcher
 * @return false in case of failure
 */
static inline bool upump_stop_source(struct upump *upump)
{
    return upump->mgr->upump_stop(upump, true);
}

/** @This gets the opaque structure with a cast.
 */
#define upump_get_opaque(upump, type) (type)(upump)->opaque

/** @This sets the callback parameters of an existing watcher.
 *
 * @param upump description structure of the watcher
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 */
static inline void upump_set_cb(struct upump *upump, upump_cb cb, void *opaque)
{
    upump->cb = cb;
    upump->opaque = opaque;
}

/** @This increments the reference count of a upump_mgr.
 *
 * @param mgr pointer to upump_mgr
 */
static inline void upump_mgr_use(struct upump_mgr *mgr)
{
    urefcount_use(&mgr->refcount);
}

/** @This decrements the reference count of a upump_mgr, and frees it when
 * it gets down to 0.
 *
 * @param mgr pointer to upump_mgr
 */
static inline void upump_mgr_release(struct upump_mgr *mgr)
{
    if (unlikely(urefcount_release(&mgr->refcount)))
        mgr->upump_mgr_free(mgr);
}

/** @This increments the number of blocked sinks.
 *
 * @param mgr pointer to upump_mgr
 */
static inline void upump_mgr_sink_block(struct upump_mgr *mgr)
{
    mgr->nb_blocked_sinks++;
}

/** @This decrements the number of blocked sinks.
 *
 * @param mgr pointer to upump_mgr
 */
static inline void upump_mgr_sink_unblock(struct upump_mgr *mgr)
{
    mgr->nb_blocked_sinks--;
}

#endif
