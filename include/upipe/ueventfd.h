/*****************************************************************************
 * ueventfd.h: upipe replacement for eventfd calls
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

#ifndef _UPIPE_UEVENTFD_H_
/** @hidden */
#define _UPIPE_UEVENTFD_H_

#include <upipe/config.h>
#include <upipe/ubase.h>
#include <upipe/upump.h>

#include <assert.h>
#include <errno.h>

#ifdef HAVE_EVENTFD

#include <unistd.h>
#include <sys/eventfd.h>

/** object allowing to wait on a condition */
typedef int ueventfd;

/** @This initializes a ueventfd.
 *
 * @param fd pointer to a ueventfd
 * @param readable true if the ueventfd must be initialized as readable
 * @return false in case of failure
 */
static inline bool ueventfd_init(ueventfd *fd, bool readable)
{
    *fd = eventfd(readable ? 1 : 0, EFD_NONBLOCK | EFD_CLOEXEC);
    return *fd != -1;
}

/** @This allocates a watcher triggering when the ueventfd is readable.
 *
 * @param fd pointer to a ueventfd
 * @param upump_mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @param source true if this watcher is a source and should block when sinks
 * are blocked
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *ueventfd_upump_alloc(ueventfd *fd,
                                                 struct upump_mgr *upump_mgr,
                                                 upump_cb cb, void *opaque,
                                                 bool source)
{
    return upump_alloc_fd_read(upump_mgr, cb, opaque, source, *fd);
}

/** @This reads from a ueventfd and makes it non-readable.
 *
 * @param fd pointer to a ueventfd
 * @return false in case of an unrecoverable error
 */
static inline bool ueventfd_read(ueventfd *fd)
{
    for ( ; ; ) {
        eventfd_t event;
        int ret = eventfd_read(*fd, &event);
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
}

/** @This writes to a ueventfd and make it readable.
 *
 * @param fd pointer to a ueventfd
 * @return false in case of an unrecoverable error
 */
static inline bool ueventfd_write(ueventfd *fd)
{
    for ( ; ; ) {
        int ret = eventfd_write(*fd, 1);
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
}

/** @This released any data and file descriptors associated with the ueventfd.
 *
 * @param fd pointer to a ueventfd
 */
static inline void ueventfd_clean(ueventfd *fd)
{
    close(*fd);
}


#elif defined(HAVE_PIPE) /* mkdoc:skip */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifndef FD_CLOEXEC
#   define FD_CLOEXEC 0
#endif

typedef int ueventfd[2];

static inline struct upump *ueventfd_upump_alloc(ueventfd *fds,
                                                 struct upump_mgr *upump_mgr,
                                                 upump_cb cb, void *opaque,
                                                 bool source)
{
    return upump_alloc_fd_read(upump_mgr, cb, opaque, source, (*fds)[0]);
}

static inline bool ueventfd_read(ueventfd *fds)
{
    for ( ; ; ) {
        char buf[256];
        ssize_t ret = read((*fds)[0], buf, sizeof(buf));
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

static inline bool ueventfd_write(ueventfd *fds)
{
    for ( ; ; ) {
        char buf[1];
        buf[0] = 0;
        ssize_t ret = write((*fds)[1], buf, sizeof(buf));
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

static inline bool ueventfd_init(ueventfd *fds, bool readable)
{
    if (unlikely(pipe(*fds) == -1))
        return false;

    fcntl((*fds)[0], F_SETFD, fcntl((*fds)[0], F_GETFD) | FD_CLOEXEC);
    fcntl((*fds)[1], F_SETFD, fcntl((*fds)[1], F_GETFD) | FD_CLOEXEC);

    fcntl((*fds)[0], F_SETFL, fcntl((*fds)[0], F_GETFL) | O_NONBLOCK);
    fcntl((*fds)[1], F_SETFL, fcntl((*fds)[1], F_GETFL) | O_NONBLOCK);

    if (likely(readable))
        ueventfd_write(fds);
    return true;
}

static inline void ueventfd_clean(ueventfd *fds)
{
    close((*fds)[0]);
    close((*fds)[1]);
}

#else /* mkdoc:skip */

#error no queue implementation

#endif

#endif
