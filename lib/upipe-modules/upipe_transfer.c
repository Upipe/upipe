/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module - acts as a xfer to another module
 * This is particularly helpful for split pipe, where you would need a xfer
 * as an input pipe, to detect end of streams.
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulifo.h>
#include <upipe/uqueue.h>
#include <upipe/uprobe.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe-modules/upipe_transfer.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <assert.h>

/** @internal @This is the private context of a xfer pipe manager. */
struct upipe_xfer_mgr {
    /** watcher */
    struct upump *upump;
    /** remote upump_mgr */
    struct upump_mgr *upump_mgr;

    /** public upipe manager structure */
    struct upipe_mgr mgr;

    /** queue of messages */
    struct uqueue uqueue;
    /** pool of @ref upipe_xfer_msg */
    struct ulifo msg_pool;
    /** extra data for the queue and pool structures */
    uint8_t extra[];
};

/** @internal @This returns the high-level upipe_mgr structure.
 *
 * @param xfer_mgr pointer to the upipe_xfer_mgr structure
 * @return pointer to the upipe_mgr structure
 */
static inline struct upipe_mgr *
    upipe_xfer_mgr_to_upipe_mgr(struct upipe_xfer_mgr *xfer_mgr)
{
    return &xfer_mgr->mgr;
}

/** @internal @This returns the private upipe_xfer_mgr structure.
 *
 * @param mgr description structure of the upipe manager
 * @return pointer to the upipe_xfer_mgr structure
 */
static inline struct upipe_xfer_mgr *
    upipe_xfer_mgr_from_upipe_mgr(struct upipe_mgr *mgr)
{
    return container_of(mgr, struct upipe_xfer_mgr, mgr);
}

/** @This represents types of commands to send to the remote upump_mgr.
 */
enum upipe_xfer_command {
    /** set upump manager on a pipe */
    UPIPE_XFER_SET_UPUMP_MGR,
    /** set URI on a pipe */
    UPIPE_XFER_SET_URI,
    /** release pipe */
    UPIPE_XFER_RELEASE,
    /** detach from remote upump_mgr */
    UPIPE_XFER_DETACH
};

/** @hidden */
static bool upipe_xfer_mgr_send(struct upipe_mgr *mgr,
                                enum upipe_xfer_command type,
                                struct upipe *upipe_remote, char *uri);

/** @This stores a message to send.
 */
struct upipe_xfer_msg {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** type of command */
    enum upipe_xfer_command type;
    /** remote pipe */
    struct upipe *upipe_remote;
    /** optional URI argument */
    char *uri;
};

/** @This returns the high-level upipe_xfer_msg structure.
 *
 * @param uchain pointer to the uchain structure wrapped into the
 * upipe_xfer_msg
 * @return pointer to the upipe_xfer_msg structure
 */
static inline struct upipe_xfer_msg *
    upipe_xfer_msg_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct upipe_xfer_msg, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param upipe_xfer_msg upipe_xfer_msg structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *
    upipe_xfer_msg_to_uchain(struct upipe_xfer_msg *msg)
{
    return &msg->uchain;
}

/** @This allocates and initializes a message structure.
 *
 * @param mgr xfer_mgr structure
 * @return pointer to upipe_xfer_msg or NULL in case of allocation error
 */
static struct upipe_xfer_msg *upipe_xfer_msg_alloc(struct upipe_mgr *mgr)
{
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    struct upipe_xfer_msg *msg = ulifo_pop(&xfer_mgr->msg_pool,
                                           struct upipe_xfer_msg *);
    if (unlikely(msg == NULL))
        msg = malloc(sizeof(struct upipe_xfer_msg));
    if (unlikely(msg == NULL))
        return NULL;
    upipe_mgr_use(mgr);
    return msg;
}

/** @This frees a message structure.
 *
 * @param mgr xfer_mgr structure
 * @param msg message structure to free
 */
static void upipe_xfer_msg_free(struct upipe_mgr *mgr,
                                struct upipe_xfer_msg *msg)
{
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    if (unlikely(!ulifo_push(&xfer_mgr->msg_pool, msg)))
        free(msg);
    upipe_mgr_release(mgr);
}

/** @internal @This is the private context of a xfer pipe. */
struct upipe_xfer {
    /** pointer to the remote pipe (must not be used directly because
     * it is running in another event loop) */
    struct upipe *upipe_remote;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_xfer, upipe);

/** @This allocates and initializes an xfer pipe. An xfer pipe allows to
 * transfer an existing pipe to a remote upump_mgr. The xfer pipe is then
 * used to remotely release the transferred pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_xfer_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    if (signature != UPIPE_XFER_SIGNATURE)
        return NULL;
    struct upipe *upipe_remote = va_arg(args, struct upipe *);
    if (unlikely(upipe_remote == NULL))
        return NULL;

    struct upipe_xfer *upipe_xfer = malloc(sizeof(struct upipe_xfer));
    if (unlikely(upipe_xfer == NULL))
        return NULL;

    if (unlikely(!upipe_xfer_mgr_send(mgr, UPIPE_XFER_SET_UPUMP_MGR,
                                      upipe_remote, NULL))) {
        free(upipe_xfer);
        return NULL;
    }

    struct upipe *upipe = upipe_xfer_to_upipe(upipe_xfer);
    upipe_init(upipe, mgr, uprobe);
    upipe_xfer->upipe_remote = upipe_remote;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_xfer_control(struct upipe *upipe,
                               enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_SET_URI: {
            struct upipe_xfer *upipe_xfer = upipe_xfer_from_upipe(upipe);
            const char *uri = va_arg(args, const char *);
            char *uri_dup = NULL;
            if (uri != NULL) {
                uri_dup = strdup(uri);
                if (unlikely(uri_dup == NULL))
                    return false;
            }
            return upipe_xfer_mgr_send(upipe->mgr, UPIPE_XFER_SET_URI,
                                       upipe_xfer->upipe_remote, uri_dup);
        }
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_xfer_free(struct upipe *upipe)
{
    struct upipe_xfer *upipe_xfer = upipe_xfer_from_upipe(upipe);
    if (unlikely(!upipe_xfer_mgr_send(upipe->mgr, UPIPE_XFER_RELEASE,
                                      upipe_xfer->upipe_remote, NULL)))
        upipe_throw_fatal(upipe, UPROBE_ERR_UPUMP);

    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(upipe_xfer);
}

/** @This instructs an existing manager to release all structures
 * currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to a upipe manager
 */
void upipe_xfer_mgr_vacuum(struct upipe_mgr *mgr)
{
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    struct upipe_xfer_msg *msg;

    while ((msg = ulifo_pop(&xfer_mgr->msg_pool,
                            struct upipe_xfer_msg *)) != NULL)
        free(msg);
}

/** @This frees a upipe manager.
 *
 * @param mgr pointer to a upipe manager
 */
static void upipe_xfer_mgr_free(struct upipe_mgr *mgr)
{
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    /* being here implies that the upump_mgr has already been released, and
     * the queue emptied */
    uqueue_clean(&xfer_mgr->uqueue);
    upipe_xfer_mgr_vacuum(mgr);
    urefcount_clean(&xfer_mgr->mgr.refcount);
    free(xfer_mgr);
}

/** @This is called by the remote upump manager to receive messages.
 *
 * @param upump description structure of the read watcher
 */
static void upipe_xfer_mgr_worker(struct upump *upump)
{
    struct upipe_mgr *mgr = upump_get_opaque(upump, struct upipe_mgr *);
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    struct uchain *uchain = uqueue_pop(&xfer_mgr->uqueue);
    if (unlikely(uchain == NULL))
        return;

    struct upipe_xfer_msg *msg = upipe_xfer_msg_from_uchain(uchain);
    switch (msg->type) {
        case UPIPE_XFER_SET_UPUMP_MGR:
            upipe_set_upump_mgr(msg->upipe_remote, xfer_mgr->upump_mgr);
            break;
        case UPIPE_XFER_SET_URI:
            upipe_set_uri(msg->upipe_remote, msg->uri);
            free(msg->uri);
            break;
        case UPIPE_XFER_RELEASE:
            upipe_release(msg->upipe_remote);
            break;
        case UPIPE_XFER_DETACH:
            upump_stop(xfer_mgr->upump);
            upump_free(xfer_mgr->upump);
            upump_mgr_release(xfer_mgr->upump_mgr);
            xfer_mgr->upump = NULL;
            xfer_mgr->upump_mgr = NULL;
            upipe_mgr_release(mgr);
            break;
    }

    upipe_xfer_msg_free(mgr, msg);
}

/** @This sends a message to the remote upump manager.
 *
 * @param mgr xfer_mgr structure
 * @param type type of message
 * @param upipe_remote optional remote pipe
 * @paral uri optional URI
 * @return false in case of error
 */
static bool upipe_xfer_mgr_send(struct upipe_mgr *mgr,
                                enum upipe_xfer_command type,
                                struct upipe *upipe_remote, char *uri)
{
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    struct upipe_xfer_msg *msg = upipe_xfer_msg_alloc(mgr);
    if (msg == NULL)
        return false;

    msg->type = type;
    msg->upipe_remote = upipe_remote;
    msg->uri = uri;

    if (unlikely(!uqueue_push(&xfer_mgr->uqueue,
                              upipe_xfer_msg_to_uchain(msg)))) {
        upipe_xfer_msg_free(mgr, msg);
        return false;
    }
    return true;
}

/** @This returns a management structure for xfer pipes. You would need one
 * management structure per target event loop (upump manager). The management
 * structure can be allocated in any thread, but must be attached in the
 * same thread as the one running the upump manager.
 *
 * @param queue_length maximum length of the internal queue of commands
 * @param msg_pool_depth maximum number of messages in the pool
 * @return pointer to manager
 */
struct upipe_mgr *upipe_xfer_mgr_alloc(uint8_t queue_length,
                                       uint16_t msg_pool_depth)
{
    assert(queue_length);
    struct upipe_xfer_mgr *xfer_mgr = malloc(sizeof(struct upipe_xfer_mgr) +
                                             uqueue_sizeof(queue_length) +
                                             ulifo_sizeof(msg_pool_depth));
    if (unlikely(xfer_mgr == NULL))
        return NULL;

    if (unlikely(!uqueue_init(&xfer_mgr->uqueue, queue_length,
                              xfer_mgr->extra))) {
        free(xfer_mgr);
        return NULL;
    }
    xfer_mgr->upump = NULL;
    xfer_mgr->upump_mgr = NULL;
    ulifo_init(&xfer_mgr->msg_pool, msg_pool_depth,
               xfer_mgr->extra + uqueue_sizeof(queue_length));

    struct upipe_mgr *mgr = upipe_xfer_mgr_to_upipe_mgr(xfer_mgr);
    urefcount_init(&mgr->refcount);
    mgr->signature = UPIPE_XFER_SIGNATURE;
    mgr->upipe_alloc = _upipe_xfer_alloc;
    mgr->upipe_input =  NULL;
    mgr->upipe_control = upipe_xfer_control;
    mgr->upipe_free = upipe_xfer_free;
    mgr->upipe_mgr_free = upipe_xfer_mgr_free;
    return mgr;
}

/** @This attaches a upipe_xfer_mgr to a given event loop. The xfer manager
 * will call upump_alloc_XXX and upump_start, so it must be done in a context
 * where it is possible, which generally means that this command is done in
 * the same thread that runs the event loop (upump managers aren't generally
 * thread-safe).
 *
 * @param mgr xfer_mgr structure
 * @param upump_mgr event loop to attach
 * @return false in case of error
 */
bool upipe_xfer_mgr_attach(struct upipe_mgr *mgr, struct upump_mgr *upump_mgr)
{
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    if (unlikely(xfer_mgr->upump_mgr != NULL))
        return false;

    xfer_mgr->upump = uqueue_upump_alloc_pop(&xfer_mgr->uqueue,
                                             upump_mgr, upipe_xfer_mgr_worker,
                                             mgr);
    if (unlikely(xfer_mgr->upump == NULL))
        return false;

    xfer_mgr->upump_mgr = upump_mgr;
    upump_mgr_use(upump_mgr);
    upump_start(xfer_mgr->upump);

    /* also increment the local refcount to avoid being released before all
     * messages have been received */
    upipe_mgr_use(mgr);
    return true;
}

/** @This detaches a upipe_xfer_mgr from an event loop. This call is thread-safe
 * and may be performed from any thread. The manager cannot be reattached
 * to another thread afterwards.
 *
 * @param mgr xfer_mgr structure
 * @return false in case of error
 */
bool upipe_xfer_mgr_detach(struct upipe_mgr *mgr)
{
    return upipe_xfer_mgr_send(mgr, UPIPE_XFER_DETACH, NULL, NULL);
}
