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
 * @short Upipe replacement for eventfd calls
 */

#ifndef _UPIPE_UEVENTFD_H_
/** @hidden */
#define _UPIPE_UEVENTFD_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/config.h>
#include <upipe/ubase.h>
#include <upipe/upump.h>

#include <assert.h>
#include <errno.h>

#ifdef UPIPE_HAVE_EVENTFD
#include <unistd.h>
#include <sys/eventfd.h>
#endif

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* Test whether ueventfd can be implemented */
#if !(defined(UPIPE_HAVE_EVENTFD) || defined(UPIPE_HAVE_PIPE))
    #error no ueventfd implementation 
#endif

#ifndef FD_CLOEXEC
#   define FD_CLOEXEC 0
#endif

/** @This is a list of the available allocation modes for the ueventfd. */
enum ueventfd_mode {
    /** uses the eventfd() system call */
    UEVENTFD_MODE_EVENTFD,
    /** uses the pipe() system call */
    UEVENTFD_MODE_PIPE,
};

/** @This allows to wait on a condition in a upump event loop. */
struct ueventfd {
    /** mode of allocation of the ueventfd */
    enum ueventfd_mode mode;
    union {
        /** used when eventfd() is available */
        int event_fd;
        /** used when pipe() is available */
        int pipe_fds[2];
    };
};

/** @This allocates a watcher triggering when the ueventfd is readable.
 *
 * @param fd pointer to a ueventfd
 * @param upump_mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @param refcount pointer to urefcount structure to increment during callback,
 * or NULL
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *ueventfd_upump_alloc(struct ueventfd *fd,
                                                 struct upump_mgr *upump_mgr,
                                                 upump_cb cb, void *opaque,
                                                 struct urefcount *refcount)
{
#ifdef UPIPE_HAVE_EVENTFD
    if (likely(fd->mode == UEVENTFD_MODE_EVENTFD)) {
        return upump_alloc_fd_read(upump_mgr, cb, opaque, refcount,
                                   fd->event_fd);
    } else
#endif
    if (likely(fd->mode == UEVENTFD_MODE_PIPE)) {
        return upump_alloc_fd_read(upump_mgr, cb, opaque, refcount,
                                   (fd->pipe_fds)[0]);
    } else {
        return NULL; // shouldn't happen
    }
}

/** @This reads from a ueventfd and makes it non-readable.
 *
 * @param fd pointer to a ueventfd
 * @return false in case of an unrecoverable error
 */
static inline bool ueventfd_read(struct ueventfd *fd)
{
#ifdef UPIPE_HAVE_EVENTFD
    if (likely(fd->mode == UEVENTFD_MODE_EVENTFD)) {
        for ( ; ; ) {
            eventfd_t event;
            int ret = eventfd_read(fd->event_fd, &event);
            if (likely(ret != -1))
                return true;
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
    } else 
#endif
    if (likely(fd->mode == UEVENTFD_MODE_PIPE)) {
        for ( ; ; ) {
            char buf[256];
            ssize_t ret = read((fd->pipe_fds)[0], buf, sizeof(buf));
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
    } else {
        return false; // shouldn't happen
    }
}

/** @This writes to a ueventfd and make it readable.
 *
 * @param fd pointer to a ueventfd
 * @return false in case of an unrecoverable error
 */
static inline bool ueventfd_write(struct ueventfd *fd)
{
#ifdef UPIPE_HAVE_EVENTFD
    if (likely(fd->mode == UEVENTFD_MODE_EVENTFD)) {
        for ( ; ; ) {
            int ret = eventfd_write(fd->event_fd, 1);
            if (likely(ret != -1))
                return true;
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
    } else
#endif
    if (likely(fd->mode == UEVENTFD_MODE_PIPE)) {
        for ( ; ; ) {
            char buf[1];
            buf[0] = 0;
            ssize_t ret = write((fd->pipe_fds)[1], buf, sizeof(buf));
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
    } else {
        return false; // shouldn't happen
    }
}

/** @This initializes a ueventfd.
 *
 * @param fd pointer to a ueventfd
 * @param readable true if the ueventfd must be initialized as readable
 * @return false in case of failure
 */
static inline bool ueventfd_init(struct ueventfd *fd, bool readable)
{
    int ret;

#ifdef UPIPE_HAVE_EVENTFD
    fd->mode = UEVENTFD_MODE_EVENTFD;
    fd->event_fd = eventfd(readable ? 1 : 0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (unlikely(fd->event_fd == -1)) { // try to eventfd() with no flags (ie. linux < 2.6.27)
        fd->event_fd = eventfd(readable ? 1 : 0, 0);

        ret = fcntl(fd->event_fd, F_GETFD);
        if (unlikely(ret < 0))
            return false;
        ret = fcntl(fd->event_fd, F_SETFD, ret | FD_CLOEXEC);
        if (unlikely(ret < 0))
            return false;

        ret = fcntl(fd->event_fd, F_GETFL);
        if (unlikely(ret < 0))
            return false;
        ret = fcntl(fd->event_fd, F_SETFL | O_NONBLOCK);
        if (unlikely(ret < 0))
            return false;

        if (unlikely(fd->event_fd == -1)) { // eventfd() fails, fallback to pipe()
#endif
            fd->mode = UEVENTFD_MODE_PIPE;
#ifdef UPIPE_HAVE_PIPE
            if (unlikely(pipe(fd->pipe_fds) == -1))
#endif
                return false;

            for (uint8_t i = 0; i < 2; i++) {
                ret = fcntl(fd->pipe_fds[i], F_GETFD);
                if (unlikely(ret < 0))
                    return false;
                ret = fcntl(fd->pipe_fds[i], F_SETFD, ret | FD_CLOEXEC);
                if (unlikely(ret < 0))
                    return false;

                ret = fcntl(fd->pipe_fds[i], F_GETFL);
                if (unlikely(ret < 0))
                    return false;
                ret = fcntl(fd->pipe_fds[i], F_SETFL, ret | O_NONBLOCK);
                if (unlikely(ret < 0))
                    return false;
            }

            if (likely(readable))
                ueventfd_write(fd);
            return true;
#ifdef UPIPE_HAVE_EVENTFD
        }
    }
    return (fd->event_fd != -1);
#endif
}

/** @This releases any data and file descriptors associated with the ueventfd.
 *
 * @param fd pointer to a ueventfd
 */
static inline void ueventfd_clean(struct ueventfd *fd)
{
#ifdef UPIPE_HAVE_EVENTFD
    if (likely(fd->mode == UEVENTFD_MODE_EVENTFD)) {
        close(fd->event_fd);
    } else
#endif
    if (likely(fd->mode == UEVENTFD_MODE_PIPE)) {
        close((fd->pipe_fds)[0]);
        close((fd->pipe_fds)[1]);
    }
}

#ifdef __cplusplus
}
#endif
#endif
