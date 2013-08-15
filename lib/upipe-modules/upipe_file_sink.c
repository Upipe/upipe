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
 * @short Upipe sink module for files
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/upump_blocker.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_sink.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_sink_delay.h>
#include <upipe-modules/upipe_file_sink.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>

#ifndef O_CLOEXEC
#   define O_CLOEXEC 0
#endif

/** default delay to use compared to the theorical reception time of the
 * packet */
#define SYSTIME_DELAY        (0.01 * UCLOCK_FREQ)

/** @hidden */
static void upipe_fsink_watcher(struct upump *upump);
/** @hidden */
static bool upipe_fsink_output(struct upipe *upipe, struct uref *uref,
                               struct upump *upump);

/** @internal @This is the private context of a file sink pipe. */
struct upipe_fsink {
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *upump;
    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** delay applied to systime attribute when uclock is provided */
    uint64_t delay;

    /** file descriptor */
    int fd;
    /** file path */
    char *path;
    /** temporary uref storage */
    struct ulist urefs;
    /** list of blockers */
    struct ulist blockers;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_fsink, upipe, UPIPE_FSINK_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_fsink, UPIPE_FSINK_EXPECTED_FLOW_DEF)
UPIPE_HELPER_UPUMP_MGR(upipe_fsink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_fsink, upump, upump_mgr)
UPIPE_HELPER_SINK(upipe_fsink, urefs, blockers, upipe_fsink_output)
UPIPE_HELPER_UCLOCK(upipe_fsink, uclock)
UPIPE_HELPER_SINK_DELAY(upipe_fsink, delay)

/** @internal @This allocates a file sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_fsink_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_fsink_alloc_flow(mgr, uprobe, signature, args,
                                                 NULL);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
    upipe_fsink_init_upump_mgr(upipe);
    upipe_fsink_init_upump(upipe);
    upipe_fsink_init_sink(upipe);
    upipe_fsink_init_uclock(upipe);
    upipe_fsink_init_delay(upipe, SYSTIME_DELAY);
    upipe_fsink->fd = -1;
    upipe_fsink->path = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This starts the watcher waiting for the sink to unblock.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_fsink_poll(struct upipe *upipe)
{
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
    struct upump *watcher = upump_alloc_fd_write(upipe_fsink->upump_mgr,
                                                 upipe_fsink_watcher, upipe,
                                                 upipe_fsink->fd);
    if (unlikely(watcher == NULL)) {
        upipe_err(upipe, "can't create watcher");
        upipe_throw_fatal(upipe, UPROBE_ERR_UPUMP);
    } else {
        upipe_fsink_set_upump(upipe, watcher);
        upump_start(watcher);
    }
}

/** @internal @This outputs data to the file sink.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 * @return true if the uref was processed
 */
static bool upipe_fsink_output(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);

    if (unlikely(upipe_fsink->fd == -1)) {
        uref_free(uref);
        upipe_warn(upipe, "received a buffer before opening a file");
        return true;
    }

    if (likely(upipe_fsink->uclock == NULL))
        goto write_buffer;

    uint64_t systime = 0;
    if (unlikely(!uref_clock_get_systime(uref, &systime))) {
        upipe_warn(upipe, "received non-dated buffer");
        goto write_buffer;
    }

    uint64_t now = uclock_now(upipe_fsink->uclock);
    systime += upipe_fsink->delay;
    if (unlikely(now < systime)) {
        upipe_fsink_wait_upump(upipe, systime - now, upipe_fsink_watcher);
        return false;
    }

write_buffer:
    for ( ; ; ) {
        int iovec_count = uref_block_iovec_count(uref, 0, -1);
        if (unlikely(iovec_count == -1)) {
            uref_free(uref);
            upipe_warn(upipe, "cannot read ubuf buffer");
            break;
        }
        if (unlikely(iovec_count == 0)) {
            uref_free(uref);
            break;
        }

        struct iovec iovecs[iovec_count];
        if (unlikely(!uref_block_iovec_read(uref, 0, -1, iovecs))) {
            uref_free(uref);
            upipe_warn(upipe, "cannot read ubuf buffer");
            break;
        }

        ssize_t ret = writev(upipe_fsink->fd, iovecs, iovec_count);
        uref_block_iovec_unmap(uref, 0, -1, iovecs);

        if (unlikely(ret == -1)) {
            switch (errno) {
                case EINTR:
                    continue;
                case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                case EWOULDBLOCK:
#endif
                    upipe_fsink_poll(upipe);
                    return false;
                case EBADF:
                case EFBIG:
                case EINVAL:
                case EIO:
                case ENOSPC:
                case EPIPE:
                default:
                    break;
            }
            uref_free(uref);
            upipe_warn_va(upipe, "write error to %s (%m)", upipe_fsink->path);
            upipe_fsink_set_upump(upipe, NULL);
            upipe_throw_sink_end(upipe);
            return true;
        }

        uref_free(uref);
        break;
    }
    return true;
}

/** @internal @This is called when the file descriptor can be written again.
 * Unblock the sink and unqueue all queued buffers.
 *
 * @param upump description structure of the watcher
 */
static void upipe_fsink_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_fsink_set_upump(upipe, NULL);
    if (upipe_fsink_output_sink(upipe)) {
        upipe_fsink_unblock_sink(upipe);
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_fsink_input. */
        upipe_release(upipe);
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_fsink_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    if (!upipe_fsink_check_sink(upipe)) {
        upipe_fsink_hold_sink(upipe, uref);
        upipe_fsink_block_sink(upipe, upump);
    } else if (!upipe_fsink_output(upipe, uref, upump)) {
        upipe_fsink_hold_sink(upipe, uref);
        upipe_fsink_block_sink(upipe, upump);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This returns the path of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param path_p filled in with the path of the file
 * @return false in case of error
 */
static bool _upipe_fsink_get_path(struct upipe *upipe, const char **path_p)
{
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
    assert(path_p != NULL);
    *path_p = upipe_fsink->path;
    return true;
}

/** @internal @This asks to open the given file.
 *
 * @param upipe description structure of the pipe
 * @param path relative or absolute path of the file
 * @param mode mode of opening the file
 * @return false in case of error
 */
static bool _upipe_fsink_set_path(struct upipe *upipe, const char *path,
                                  enum upipe_fsink_mode mode)
{
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);

    if (unlikely(upipe_fsink->fd != -1)) {
        if (likely(upipe_fsink->path != NULL))
            upipe_notice_va(upipe, "closing file %s", upipe_fsink->path);
        close(upipe_fsink->fd);
    }
    free(upipe_fsink->path);
    upipe_fsink->path = NULL;
    upipe_fsink_set_upump(upipe, NULL);
    upipe_fsink_unblock_sink(upipe);
    if (!upipe_fsink_check_sink(upipe))
        /* Release the pipe used in @ref upipe_fsink_input. */
        upipe_release(upipe);

    if (unlikely(path == NULL))
        return true;

    if (upipe_fsink->upump_mgr == NULL) {
        upipe_throw_need_upump_mgr(upipe);
        if (unlikely(upipe_fsink->upump_mgr == NULL))
            return false;
    }

    const char *mode_desc = NULL; /* hush gcc */
    int flags;
    switch (mode) {
        case UPIPE_FSINK_NONE:
            flags = 0;
            break;
        case UPIPE_FSINK_APPEND:
            mode_desc = "append";
            flags = O_CREAT;
            break;
        case UPIPE_FSINK_OVERWRITE:
            mode_desc = "overwrite";
            flags = O_CREAT | O_TRUNC;
            break;
        case UPIPE_FSINK_CREATE:
            mode_desc = "create";
            flags = O_CREAT | O_EXCL;
            break;
        default:
            upipe_err_va(upipe, "invalid mode %d", mode);
            return false;
    }
    upipe_fsink->fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC | flags,
                           S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (unlikely(upipe_fsink->fd == -1)) {
        upipe_err_va(upipe, "can't open file %s (%s)", path, mode_desc);
        return false;
    }
    switch (mode) {
        /* O_APPEND seeks on each write, so use this instead */
        case UPIPE_FSINK_APPEND:
            if (unlikely(lseek(upipe_fsink->fd, 0, SEEK_END) == -1)) {
                upipe_err_va(upipe, "can't append to file %s (%s)", path, mode_desc);
                close(upipe_fsink->fd);
                upipe_fsink->fd = -1;
                return false;
            }
            break;
        default:
            break;
    }

    upipe_fsink->path = strdup(path);
    if (unlikely(upipe_fsink->path == NULL)) {
        close(upipe_fsink->fd);
        upipe_fsink->fd = -1;
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }
    if (!upipe_fsink_check_sink(upipe))
        /* Use again the pipe that we previously released. */
        upipe_use(upipe);
    upipe_notice_va(upipe, "opening file %s in %s mode",
                    upipe_fsink->path, mode_desc);
    return true;
}

/** @internal @This flushes all currently held buffers, and unblocks the
 * sources.
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool upipe_fsink_flush(struct upipe *upipe)
{
    if (upipe_fsink_flush_sink(upipe)) {
        upipe_fsink_set_upump(upipe, NULL);
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_fsink_input. */
        upipe_release(upipe);
    }
    return true;
}

/** @internal @This processes control commands on a file sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_fsink_control(struct upipe *upipe,
                                 enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_fsink_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            upipe_fsink_set_upump(upipe, NULL);
            return upipe_fsink_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_fsink_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            upipe_fsink_set_upump(upipe, NULL);
            return upipe_fsink_set_uclock(upipe, uclock);
        }

        case UPIPE_SINK_GET_DELAY: {
            uint64_t *p = va_arg(args, uint64_t *);
            return upipe_fsink_get_delay(upipe, p);
        }
        case UPIPE_SINK_SET_DELAY: {
            uint64_t delay = va_arg(args, uint64_t);
            return upipe_fsink_set_delay(upipe, delay);
        }

        case UPIPE_FSINK_GET_PATH: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_FSINK_SIGNATURE);
            const char **path_p = va_arg(args, const char **);
            return _upipe_fsink_get_path(upipe, path_p);
        }
        case UPIPE_FSINK_SET_PATH: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_FSINK_SIGNATURE);
            const char *path = va_arg(args, const char *);
            enum upipe_fsink_mode mode = va_arg(args, enum upipe_fsink_mode);
            return _upipe_fsink_set_path(upipe, path, mode);
        }
        case UPIPE_SINK_FLUSH:
            return upipe_fsink_flush(upipe);
        default:
            return false;
    }
}

/** @internal @This processes control commands on a file sink pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_fsink_control(struct upipe *upipe, enum upipe_command command,
                                va_list args)
{
    if (unlikely(!_upipe_fsink_control(upipe, command, args)))
        return false;

    if (unlikely(!upipe_fsink_check_sink(upipe)))
        upipe_fsink_poll(upipe);

    return true;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_fsink_free(struct upipe *upipe)
{
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
    if (likely(upipe_fsink->fd != -1)) {
        if (likely(upipe_fsink->path != NULL))
            upipe_notice_va(upipe, "closing file %s", upipe_fsink->path);
        close(upipe_fsink->fd);
    }
    upipe_throw_dead(upipe);

    free(upipe_fsink->path);
    upipe_fsink_clean_delay(upipe);
    upipe_fsink_clean_uclock(upipe);
    upipe_fsink_clean_upump(upipe);
    upipe_fsink_clean_upump_mgr(upipe);
    upipe_fsink_clean_sink(upipe);

    upipe_fsink_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_fsink_mgr = {
    .signature = UPIPE_FSINK_SIGNATURE,

    .upipe_alloc = upipe_fsink_alloc,
    .upipe_input = upipe_fsink_input,
    .upipe_control = upipe_fsink_control,
    .upipe_free = upipe_fsink_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all file sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_fsink_mgr_alloc(void)
{
    return &upipe_fsink_mgr;
}
