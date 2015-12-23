/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe module allowing to transfer other pipes to a remote event loop
 * This is particularly helpful for multithreaded applications.
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulifo.h>
#include <upipe/uqueue.h>
#include <upipe/uprobe.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/uprobe_transfer.h>
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
    /** real refcount management structure */
    struct urefcount urefcount;

    /** public upipe manager structure */
    struct upipe_mgr mgr;

    /** watcher */
    struct upump *upump;
    /** remote upump_mgr */
    struct upump_mgr *upump_mgr;
    /** queue length */
    uint8_t queue_length;
    /** queue of messages */
    struct uqueue uqueue;
    /** pool of @ref upipe_xfer_msg */
    struct ulifo msg_pool;
    /** extra data for the queue and pool structures */
    uint8_t extra[];
};

UBASE_FROM_TO(upipe_xfer_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_xfer_mgr, urefcount, urefcount, urefcount)

/** @This represents types of commands to send to the remote upump_mgr.
 */
enum upipe_xfer_command {
    /** attach upump manager on a pipe */
    UPIPE_XFER_ATTACH_UPUMP_MGR,
    /** set URI on a pipe */
    UPIPE_XFER_SET_URI,
    /** set output of a pipe */
    UPIPE_XFER_SET_OUTPUT,
    /** release pipe */
    UPIPE_XFER_RELEASE,
    /** detach from remote upump_mgr */
    UPIPE_XFER_DETACH
    /* values from @ref uprobe_xfer_event are also allowed (backwards) */
};

/** @This is the optional argument of a message. */
union upipe_xfer_arg {
    /** string */
    char *string;
    /** pipe */
    struct upipe *pipe;
    /** event */
    int event;
};

/** @This is the optional argument of an event. */
union upipe_xfer_event_arg {
    /** unsigned long */
    unsigned long ulong;
    /** uint64_t */
    uint64_t u64;
};

/** @hidden */
static int upipe_xfer_mgr_send(struct upipe_mgr *mgr, int type,
                               struct upipe *upipe_remote,
                               union upipe_xfer_arg arg);

/** @This stores a message to send.
 */
struct upipe_xfer_msg {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** type of command */
    int type;
    /** remote pipe */
    struct upipe *upipe_remote;
    /** optional argument */
    union upipe_xfer_arg arg;
    /** optional event signature */
    uint32_t event_signature;
    /** optional event argument */
    union upipe_xfer_event_arg event_arg;
};

UBASE_FROM_TO(upipe_xfer_msg, uchain, uchain, uchain)

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
}

/** @internal @This is the private context of a xfer pipe. */
struct upipe_xfer {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** pointer to the remote pipe (must not be used directly because
     * it is running in another event loop) */
    struct upipe *upipe_remote;
    /** probe to send events to the main thread */
    struct uprobe uprobe_remote;
    /** refcount of the uprobe remote, used to release upipe_xfer in the main
     * thread */
    struct urefcount urefcount_probe;

    /** public upipe structure */
    struct upipe upipe;

    /** watcher */
    struct upump *upump;
    /** remote upump_mgr */
    struct upump_mgr *upump_mgr;
    /** queue of messages (from remote pipe to main thread) */
    struct uqueue uqueue;
    /** extra data for the queue structure */
    uint8_t extra[];
};

UPIPE_HELPER_UPIPE(upipe_xfer, upipe, UPIPE_XFER_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_xfer, urefcount, upipe_xfer_no_ref)
UPIPE_HELPER_UPUMP_MGR(upipe_xfer, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_xfer, upump, upump_mgr)

UBASE_FROM_TO(upipe_xfer, urefcount, urefcount_real, urefcount_real)
UBASE_FROM_TO(upipe_xfer, urefcount, urefcount_probe, urefcount_probe)

/** @hidden */
static void upipe_xfer_free(struct urefcount *urefcount_real);

/** @internal @This catches events coming from an xfer probe attached to
 * a remote pipe, and attaches them to the bin pipe.
 * Caution: this runs in the remote thread!
 *
 * @param uprobe pointer to the probe in upipe_xfer_alloc
 * @param remote pointer to the remote pipe
 * @param xfer_event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_xfer_probe(struct uprobe *uprobe, struct upipe *remote,
                            int xfer_event, va_list args)
{
    if (xfer_event < UPROBE_LOCAL)
        return uprobe_throw_next(uprobe, remote, xfer_event, args);

    va_list args_copy;
    va_copy(args_copy, args);
    uint32_t signature = va_arg(args_copy, uint32_t);
    va_end(args_copy);
    if (signature != UPROBE_XFER_SIGNATURE)
        return uprobe_throw_next(uprobe, remote, xfer_event, args);
    va_arg(args, uint32_t);

    int event = va_arg(args, int);
    union upipe_xfer_event_arg event_arg;
    switch (xfer_event) {
        case UPROBE_XFER_VOID:
            signature = 0;
            event_arg.ulong = 0;
            break;
        case UPROBE_XFER_UINT64_T:
            signature = 0;
            event_arg.u64 = va_arg(args, uint64_t);
            break;
        case UPROBE_XFER_UNSIGNED_LONG_LOCAL:
            signature = va_arg(args, uint32_t);
            event_arg.ulong = va_arg(args, unsigned long);
            break;
        default:
            return UBASE_ERR_UNHANDLED;
    }

    /* We may only access the manager as the rest is not thread-safe. */
    struct upipe_xfer *upipe_xfer = container_of(uprobe, struct upipe_xfer,
                                                 uprobe_remote);
    struct upipe *upipe = upipe_xfer_to_upipe(upipe_xfer);

    struct upipe_xfer_msg *msg = upipe_xfer_msg_alloc(upipe->mgr);
    if (msg == NULL)
        return UBASE_ERR_ALLOC;

    msg->type = xfer_event;
    msg->upipe_remote = remote;
    msg->arg.event = event;
    msg->event_signature = signature;
    msg->event_arg = event_arg;

    urefcount_use(upipe_xfer_to_urefcount_real(upipe_xfer));
    if (unlikely(!uqueue_push(&upipe_xfer->uqueue, msg))) {
        urefcount_release(upipe_xfer_to_urefcount_real(upipe_xfer));
        upipe_xfer_msg_free(upipe->mgr, msg);
        return UBASE_ERR_EXTERNAL;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This is called when the remote pipe dies, to free to probe
 * and trigger the destruction of the upipe_xfer structure in the main thread.
 * Caution: this runs in the remote thread!
 *
 * @param urefcount_probe pointer to urefcount_probe in @ref upipe_xfer
 */
static void upipe_xfer_probe_free(struct urefcount *urefcount_probe)
{
    /* We may only access the manager as the rest is not thread-safe. */
    struct upipe_xfer *upipe_xfer =
        upipe_xfer_from_urefcount_probe(urefcount_probe);
    struct upipe *upipe = upipe_xfer_to_upipe(upipe_xfer);

    struct upipe_xfer_msg *msg = upipe_xfer_msg_alloc(upipe->mgr);
    if (msg == NULL)
        return;

    msg->type = UPROBE_DEAD;

    urefcount_use(upipe_xfer_to_urefcount_real(upipe_xfer));
    if (unlikely(!uqueue_push(&upipe_xfer->uqueue, msg))) {
        urefcount_release(upipe_xfer_to_urefcount_real(upipe_xfer));
        upipe_xfer_msg_free(upipe->mgr, msg);
    }
}

/** @This allocates and initializes an xfer pipe. An xfer pipe allows to
 * transfer an existing pipe to a remote upump_mgr. The xfer pipe is then
 * used to remotely release the transferred pipe.
 *
 * Please note that upipe_remote is not "used" so its refcount is not
 * incremented. For that reason it shouldn't be "released" afterwards. Only
 * release the xfer pipe.
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
        goto upipe_xfer_alloc_err;
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    struct upipe *upipe_remote = va_arg(args, struct upipe *);
    if (unlikely(upipe_remote == NULL))
        goto upipe_xfer_alloc_err2;

    struct upipe_xfer *upipe_xfer =
        malloc(sizeof(struct upipe_xfer) +
               uqueue_sizeof(xfer_mgr->queue_length));
    if (unlikely(upipe_xfer == NULL))
        goto upipe_xfer_alloc_err2;

    if (unlikely(!uqueue_init(&upipe_xfer->uqueue, xfer_mgr->queue_length,
                              upipe_xfer->extra))) {
        free(upipe_xfer);
        goto upipe_xfer_alloc_err2;
    }

    struct upipe *upipe = upipe_xfer_to_upipe(upipe_xfer);
    upipe_init(upipe, mgr, uprobe);
    upipe_xfer_init_urefcount(upipe);
    urefcount_init(upipe_xfer_to_urefcount_real(upipe_xfer), upipe_xfer_free);
    upipe_xfer_init_upump_mgr(upipe);
    upipe_xfer_init_upump(upipe);
    urefcount_init(upipe_xfer_to_urefcount_probe(upipe_xfer),
                   upipe_xfer_probe_free);
    uprobe_init(&upipe_xfer->uprobe_remote, upipe_xfer_probe, NULL);
    upipe_xfer->uprobe_remote.refcount =
        upipe_xfer_to_urefcount_probe(upipe_xfer);
    upipe_push_probe(upipe_remote, &upipe_xfer->uprobe_remote);
    upipe_xfer->upipe_remote = upipe_remote;
    upipe_throw_ready(upipe);
    return upipe;

upipe_xfer_alloc_err2:
    upipe_release(upipe_remote);
upipe_xfer_alloc_err:
    uprobe_release(uprobe);
    return NULL;
}

/** @This is called by the local upump manager to receive probes from remote.
 *
 * @param upump description structure of the read watcher
 */
static void upipe_xfer_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_xfer *upipe_xfer = upipe_xfer_from_upipe(upipe);
    struct upipe_xfer_msg *msg;
    while ((msg = uqueue_pop(&upipe_xfer->uqueue,
                             struct upipe_xfer_msg *)) != NULL) {
        switch (msg->type) {
            case UPROBE_DEAD:
                urefcount_release(upipe_xfer_to_urefcount_real(upipe_xfer));
                break;
            case UPROBE_XFER_VOID:
                if (upipe_xfer->upipe_remote == msg->upipe_remote)
                    upipe_throw(upipe, msg->arg.event);
                break;
            case UPROBE_XFER_UINT64_T:
                if (upipe_xfer->upipe_remote == msg->upipe_remote)
                    upipe_throw(upipe, msg->arg.event, msg->event_arg.u64);
                break;
            case UPROBE_XFER_UNSIGNED_LONG_LOCAL:
                if (upipe_xfer->upipe_remote == msg->upipe_remote)
                    upipe_throw(upipe, msg->arg.event, msg->event_signature,
                                msg->event_arg.ulong);
                break;
            default:
                /* this should not happen */
                break;
        }

        upipe_xfer_msg_free(upipe->mgr, msg);
        urefcount_release(upipe_xfer_to_urefcount_real(upipe_xfer));
    }
}

/** @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_xfer_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            struct upipe_xfer *upipe_xfer = upipe_xfer_from_upipe(upipe);
            upipe_xfer_set_upump(upipe, NULL);
            UBASE_RETURN(upipe_xfer_attach_upump_mgr(upipe))
            if (upipe_xfer->upump_mgr != NULL) {
                /* prepare a queue to receive probe events */
                upipe_xfer->upump = uqueue_upump_alloc_pop(&upipe_xfer->uqueue,
                        upipe_xfer->upump_mgr, upipe_xfer_worker, upipe,
                        upipe_xfer_to_urefcount_real(upipe_xfer));
                if (unlikely(upipe_xfer->upump == NULL))
                    return UBASE_ERR_UPUMP;
                upump_start(upipe_xfer->upump);
            } else
                upipe_warn(upipe, "unable to allocate upstream queue");
            union upipe_xfer_arg arg = { .pipe = NULL };
            return upipe_xfer_mgr_send(upipe->mgr, UPIPE_XFER_ATTACH_UPUMP_MGR,
                                       upipe_xfer->upipe_remote, arg);
        }
        case UPIPE_SET_URI: {
            struct upipe_xfer *upipe_xfer = upipe_xfer_from_upipe(upipe);
            const char *uri = va_arg(args, const char *);
            char *uri_dup = NULL;
            if (uri != NULL) {
                uri_dup = strdup(uri);
                if (unlikely(uri_dup == NULL))
                    return UBASE_ERR_ALLOC;
            }
            union upipe_xfer_arg arg = { .string = uri_dup };
            return upipe_xfer_mgr_send(upipe->mgr, UPIPE_XFER_SET_URI,
                                       upipe_xfer->upipe_remote, arg);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe_xfer *upipe_xfer = upipe_xfer_from_upipe(upipe);
            struct upipe *output = va_arg(args, struct upipe *);
            union upipe_xfer_arg arg = { .pipe = upipe_use(output) };
            return upipe_xfer_mgr_send(upipe->mgr, UPIPE_XFER_SET_OUTPUT,
                                       upipe_xfer->upipe_remote, arg);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_xfer_free(struct urefcount *urefcount_real)
{
    struct upipe_xfer *upipe_xfer =
        upipe_xfer_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_xfer_to_upipe(upipe_xfer);
    upipe_throw_dead(upipe);
    uqueue_clean(&upipe_xfer->uqueue);
    upipe_xfer_clean_upump(upipe);
    upipe_xfer_clean_upump_mgr(upipe);
    uprobe_clean(&upipe_xfer->uprobe_remote);
    urefcount_clean(urefcount_real);
    urefcount_clean(&upipe_xfer->urefcount_probe);
    upipe_xfer_clean_urefcount(upipe);
    upipe_clean(upipe);
    free(upipe_xfer);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_xfer_no_ref(struct upipe *upipe)
{
    struct upipe_xfer *upipe_xfer = upipe_xfer_from_upipe(upipe);
    union upipe_xfer_arg arg = { .pipe = NULL };
    if (unlikely(!ubase_check(upipe_xfer_mgr_send(upipe->mgr,
                                                  UPIPE_XFER_RELEASE,
                                                  upipe_xfer->upipe_remote,
                                                  arg))))
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
}

/** @This instructs an existing manager to release all structures
 * currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to a upipe manager
 */
static void upipe_xfer_mgr_vacuum(struct upipe_mgr *mgr)
{
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    struct upipe_xfer_msg *msg;

    while ((msg = ulifo_pop(&xfer_mgr->msg_pool,
                            struct upipe_xfer_msg *)) != NULL)
        free(msg);
}

/** @internal @This frees a upipe manager.
 *
 * @param mgr pointer to a upipe manager
 */
static void upipe_xfer_mgr_free(struct upipe_mgr *mgr)
{
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    upump_stop(xfer_mgr->upump);
    upump_free(xfer_mgr->upump);
    upump_mgr_release(xfer_mgr->upump_mgr);
    uqueue_clean(&xfer_mgr->uqueue);
    upipe_xfer_mgr_vacuum(mgr);
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
    struct upipe_xfer_msg *msg;
    while ((msg = uqueue_pop(&xfer_mgr->uqueue,
                             struct upipe_xfer_msg *)) != NULL) {
        switch (msg->type) {
            case UPIPE_XFER_ATTACH_UPUMP_MGR:
                upipe_attach_upump_mgr(msg->upipe_remote);
                break;
            case UPIPE_XFER_SET_URI:
                upipe_set_uri(msg->upipe_remote, msg->arg.string);
                free(msg->arg.string);
                break;
            case UPIPE_XFER_SET_OUTPUT:
                upipe_set_output(msg->upipe_remote, msg->arg.pipe);
                upipe_release(msg->arg.pipe);
                break;
            case UPIPE_XFER_RELEASE:
                upipe_release(msg->upipe_remote);
                break;
            case UPIPE_XFER_DETACH:
                upipe_xfer_msg_free(mgr, msg);
                upipe_xfer_mgr_free(mgr);
                return;
            default:
                /* this should not happen */
                break;
        }

        upipe_xfer_msg_free(mgr, msg);
    }
}

/** @This sends a message to the remote upump manager.
 *
 * @param mgr xfer_mgr structure
 * @param type type of message
 * @param upipe_remote optional remote pipe
 * @paral arg optional argument
 * @return an error code
 */
static int upipe_xfer_mgr_send(struct upipe_mgr *mgr, int type,
                               struct upipe *upipe_remote,
                               union upipe_xfer_arg arg)
{
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    struct upipe_xfer_msg *msg = upipe_xfer_msg_alloc(mgr);
    if (msg == NULL)
        return UBASE_ERR_ALLOC;

    msg->type = type;
    msg->upipe_remote = upipe_remote;
    msg->arg = arg;

    if (unlikely(!uqueue_push(&xfer_mgr->uqueue, msg))) {
        upipe_xfer_msg_free(mgr, msg);
        return UBASE_ERR_EXTERNAL;
    }
    return UBASE_ERR_NONE;
}

/** @This detaches a upipe manager. Real deallocation is only performed after
 * detach. This call is thread-safe and may be performed from any thread.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_xfer_mgr_detach(struct urefcount *urefcount)
{
    struct upipe_xfer_mgr *xfer_mgr =
        upipe_xfer_mgr_from_urefcount(urefcount);
    assert(xfer_mgr->upump_mgr != NULL);
    union upipe_xfer_arg arg = { .pipe = NULL };
    upipe_xfer_mgr_send(upipe_xfer_mgr_to_upipe_mgr(xfer_mgr),
                        UPIPE_XFER_DETACH, NULL, arg);
    urefcount_clean(urefcount);
}

/** @This attaches a upipe_xfer_mgr to a given event loop. The xfer manager
 * will call upump_alloc_XXX and upump_start, so it must be done in a context
 * where it is possible, which generally means that this command is done in
 * the same thread that runs the event loop (upump managers aren't generally
 * thread-safe).
 *
 * Please note that an xfer_mgr must be attached to a upump manager before it
 * can be released.
 *
 * @param mgr xfer_mgr structure
 * @param upump_mgr event loop to attach
 * @return an error code
 */
static int _upipe_xfer_mgr_attach(struct upipe_mgr *mgr,
                                  struct upump_mgr *upump_mgr)
{
    struct upipe_xfer_mgr *xfer_mgr = upipe_xfer_mgr_from_upipe_mgr(mgr);
    if (unlikely(xfer_mgr->upump_mgr != NULL))
        return UBASE_ERR_INVALID;

    xfer_mgr->upump = uqueue_upump_alloc_pop(&xfer_mgr->uqueue,
                                             upump_mgr, upipe_xfer_mgr_worker,
                                             mgr, NULL);
    if (unlikely(xfer_mgr->upump == NULL))
        return UBASE_ERR_UPUMP;

    xfer_mgr->upump_mgr = upump_mgr;
    upump_mgr_use(upump_mgr);
    upump_start(xfer_mgr->upump);
    return UBASE_ERR_NONE;
}

/** @This processes manager control commands.
 *
 * @param mgr xfer_mgr structure
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_xfer_mgr_control(struct upipe_mgr *mgr,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_XFER_MGR_ATTACH: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_XFER_SIGNATURE)
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            return _upipe_xfer_mgr_attach(mgr, upump_mgr);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns a management structure for xfer pipes. You would need one
 * management structure per target event loop (upump manager). The management
 * structure can be allocated in any thread, but must be attached in the
 * same thread as the one running the upump manager.
 *
 * @param queue_length maximum length of the internal queues
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
    xfer_mgr->queue_length = queue_length;
    ulifo_init(&xfer_mgr->msg_pool, msg_pool_depth,
               xfer_mgr->extra + uqueue_sizeof(queue_length));

    struct upipe_mgr *mgr = upipe_xfer_mgr_to_upipe_mgr(xfer_mgr);
    urefcount_init(upipe_xfer_mgr_to_urefcount(xfer_mgr),
                   upipe_xfer_mgr_detach);
    mgr->refcount = upipe_xfer_mgr_to_urefcount(xfer_mgr);
    mgr->signature = UPIPE_XFER_SIGNATURE;
    mgr->upipe_alloc = _upipe_xfer_alloc;
    mgr->upipe_input =  NULL;
    mgr->upipe_control = upipe_xfer_control;
    mgr->upipe_mgr_control = upipe_xfer_mgr_control;
    return mgr;
}
