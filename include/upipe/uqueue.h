/*****************************************************************************
 * uqueue.h: upipe queue of buffers (multiple writers but only ONE reader)
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

#ifndef _UPIPE_UQUEUE_H_
/** @hidden */
#define _UPIPE_UQUEUE_H_

#include <assert.h>

#include <upipe/config.h>
#include <upipe/ubase.h>
#include <upipe/ufifo.h>
#include <upipe/upump.h>

#ifdef HAVE_EVENTFD

#include <unistd.h>
#include <sys/eventfd.h>

/** @This is the implementation of a queue. */
struct uqueue {
    /** FIFO */
    struct ufifo fifo;
    /** maximum number of elements in the FIFO */
    unsigned int max_length;
    /** eventfd triggered when data can be pushed */
    int event_push;
    /** eventfd triggered when data can be popped */
    int event_pop;
};

/** @This initializes a uqueue.
 *
 * @param uqueue pointer to a uqueue structure
 * @param max_length maximum number of elements in the queue
 * @return false in case of failure
 */
static inline bool uqueue_init(struct uqueue *uqueue, unsigned int max_length)
{
    assert(max_length);
    uqueue->event_push = eventfd(1, EFD_NONBLOCK | EFD_CLOEXEC);
    if (unlikely(uqueue->event_push == -1))
        return false;
    uqueue->event_pop = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (unlikely(uqueue->event_pop == -1)) {
        close(uqueue->event_push);
        return false;
    }

    ufifo_init(&uqueue->fifo);
    uqueue->max_length = max_length;
    return true;
}

/** @This allocates a watcher triggering when data is ready to be pushed.
 *
 * @param uqueue pointer to a uqueue structure
 * @param upump_mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *uqueue_upump_alloc_push(struct uqueue *uqueue,
                                                    struct upump_mgr *upump_mgr,
                                                    upump_cb cb, void *opaque)
{
    return upump_alloc_fd_read(upump_mgr, cb, opaque, false,
                               uqueue->event_push);
}

/** @This allocates a watcher triggering when data is ready to be popped.
 *
 * @param uqueue pointer to a uqueue structure
 * @param upump_mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *uqueue_upump_alloc_pop(struct uqueue *uqueue,
                                                   struct upump_mgr *upump_mgr,
                                                   upump_cb cb, void *opaque)
{
    return upump_alloc_fd_read(upump_mgr, cb, opaque, true, uqueue->event_pop);
}

/** @This pushes an element into the queue. It may be called from any thread.
 *
 * @param uqueue pointer to a uqueue structure
 * @param element pointer to element to push
 * @return false if no more element should be pushed afterwards
 */
static inline bool uqueue_push(struct uqueue *uqueue, struct uchain *element)
{
    unsigned int counter_before;
    ufifo_push(&uqueue->fifo, element, &counter_before);
    if (unlikely(counter_before == 0))
        eventfd_write(uqueue->event_pop, 1);

    while (unlikely(ufifo_length(&uqueue->fifo) >= uqueue->max_length)) {
        eventfd_t event;
        eventfd_read(uqueue->event_push, &event);

        /* double-check */
        if (likely(ufifo_length(&uqueue->fifo) >= uqueue->max_length))
            return false;

        /* try again */
        eventfd_write(uqueue->event_push, 1);
    }
    return true;
}

/** @This pops an element from the queue. It may only be called by the thread
 * which "owns" the structure, otherwise there is a race condition.
 *
 * @param uqueue pointer to a uqueue structure
 * @return pointer to element, or NULL if the LIFO is empty
 */
static inline struct uchain *uqueue_pop(struct uqueue *uqueue)
{
    while (unlikely(ufifo_length(&uqueue->fifo) == 0)) {
        eventfd_t event;
        eventfd_read(uqueue->event_pop, &event);

        /* double-check */
        if (likely(ufifo_length(&uqueue->fifo) == 0))
            return NULL;

        /* try again */
        eventfd_write(uqueue->event_pop, 1);
    }

    unsigned int counter_after;
    struct uchain *uchain = ufifo_pop(&uqueue->fifo, &counter_after);
    if (unlikely(counter_after == uqueue->max_length - 1))
        eventfd_write(uqueue->event_push, 1);
    return uchain;
}

/** @This cleans up the queue data structure. Please note that it is the
 * caller's responsibility to empty the queue first.
 *
 * @param uqueue pointer to a uqueue structure
 */
static inline void uqueue_clean(struct uqueue *uqueue)
{
    ufifo_clean(&uqueue->fifo);
    close(uqueue->event_push);
    close(uqueue->event_pop);
}

#elif defined(HAVE_PIPE) /* mkdoc:skip */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifndef FD_CLOEXEC
#   define FD_CLOEXEC 0
#endif

struct uqueue {
    /** FIFO */
    struct ufifo fifo;
    unsigned int max_length;
    int event_push[2];
    int event_pop[2];
};

static inline bool uqueue_init(struct uqueue *uqueue, unsigned int max_length)
{
    long flags;
    assert(max_length);

    if (unlikely(pipe(uqueue->event_push) == -1))
        return false;
    if (unlikely(pipe(uqueue->event_pop) == -1)) {
        close(uqueue->event_push[0]);
        close(uqueue->event_push[1]);
        return false;
    }

    fcntl(uqueue->event_push[0], F_SETFD,
          fcntl(uqueue->event_push[0], F_GETFD) | FD_CLOEXEC);
    fcntl(uqueue->event_push[1], F_SETFD,
          fcntl(uqueue->event_push[1], F_GETFD) | FD_CLOEXEC);
    fcntl(uqueue->event_pop[0], F_SETFD,
          fcntl(uqueue->event_pop[0], F_GETFD) | FD_CLOEXEC);
    fcntl(uqueue->event_pop[1], F_SETFD,
          fcntl(uqueue->event_pop[1], F_GETFD) | FD_CLOEXEC);

    fcntl(uqueue->event_push[0], F_SETFL,
          fcntl(uqueue->event_push[0], F_GETFL) | O_NONBLOCK);
    fcntl(uqueue->event_push[1], F_SETFL,
          fcntl(uqueue->event_push[1], F_GETFL) | O_NONBLOCK);
    fcntl(uqueue->event_pop[0], F_SETFL,
          fcntl(uqueue->event_pop[0], F_GETFL) | O_NONBLOCK);
    fcntl(uqueue->event_pop[1], F_SETFL,
          fcntl(uqueue->event_pop[1], F_GETFL) | O_NONBLOCK);

    ufifo_init(&uqueue->fifo);
    uqueue->max_length = max_length;
    return true;
}

static inline struct upump *uqueue_upump_alloc_push(struct uqueue *uqueue,
                                                    struct upump_mgr *upump_mgr,
                                                    upump_cb cb, void *opaque)
{
    return upump_alloc_fd_read(upump_mgr, cb, opaque, false,
                               uqueue->event_push[0]);
}

static inline struct upump *uqueue_upump_alloc_pop(struct uqueue *uqueue,
                                                   struct upump_mgr *upump_mgr,
                                                   upump_cb cb, void *opaque)
{
    return upump_alloc_fd_read(upump_mgr, cb, opaque, true,
                               uqueue->event_pop[0]);
}

/* not part of the public API */
static inline bool uqueue_read(int fd)
{
    for ( ; ; ) {
        char buf[256];
        ssize_t ret = read(fd, buf, sizeof(buf));
        if (unlikely(ret == 0)) return true;
        if (likely(ret == -1)) {
            switch (errno) {
                case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                case EWOULDBLOCK:
#endif
                    return true;
                case EINTR:
                    break;
                default:
                    return false;
            }
        }
    }
}

/* not part of the public API */
static inline bool uqueue_write(int fd)
{
    for ( ; ; ) {
        char buf[1];
        buf[0] = 0;
        ssize_t ret = write(fd, buf, sizeof(buf));
        if (likely(ret == 1)) return true;
        if (likely(ret == -1)) {
            switch (errno) {
                case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                case EWOULDBLOCK:
#endif
                    return true;
                case EINTR:
                    break;
                default:
                    return false;
            }
        }
    }
}

static inline bool uqueue_push(struct uqueue *uqueue, struct uchain *element)
{
    bool ret;
    unsigned int counter_before;
    ufifo_push(&uqueue->fifo, element, &counter_before);
    if (unlikely(counter_before == 0)) {
        ret = uqueue_write(uqueue->event_pop[1]);
        assert(ret);
    }

    while (unlikely(ufifo_length(&uqueue->fifo) >= uqueue->max_length)) {
        ret = uqueue_read(uqueue->event_push[0]);
        assert(ret);

        /* double-check */
        if (likely(ufifo_length(&uqueue->fifo) >= uqueue->max_length))
            return false;

        /* try again */
        ret = uqueue_write(uqueue->event_push[1]);
        assert(ret);
    }
    return true;
}

static inline struct uchain *uqueue_pop(struct uqueue *uqueue)
{
    bool ret;
    while (unlikely(ufifo_length(&uqueue->fifo) == 0)) {
        ret = uqueue_read(uqueue->event_pop[0]);
        assert(ret);

        /* double-check */
        if (likely(ufifo_length(&uqueue->fifo) == 0))
            return NULL;

        /* try again */
        ret = uqueue_write(uqueue->event_pop[1]);
        assert(ret);
    }

    unsigned int counter_after;
    struct uchain *uchain = ufifo_pop(&uqueue->fifo, &counter_after);
    if (unlikely(counter_after == uqueue->max_length - 1)) {
        ret = uqueue_write(uqueue->event_push[1]);
        assert(ret);
    }
    return uchain;
}

static inline void uqueue_clean(struct uqueue *uqueue)
{
    ufifo_clean(&uqueue->fifo);
    close(uqueue->event_push[0]);
    close(uqueue->event_push[1]);
    close(uqueue->event_pop[0]);
    close(uqueue->event_pop[1]);
}

#else /* mkdoc:skip */

#error no queue implementation

#endif

/** @This returns the number of elements in the queue.
 *
 * @param uqueue pointer to a uqueue structure
 */
static inline unsigned int uqueue_length(struct uqueue *uqueue)
{
    return ufifo_length(&uqueue->fifo);
}

#endif
