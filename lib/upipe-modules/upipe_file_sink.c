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
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_upump_mgr.h>
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
/** expected flow definition on all flows */
#define EXPECTED_FLOW_DEF    "block."

static void upipe_fsink_watcher(struct upump *upump);

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
    /** true if the sink currently blocks the pipe */
    bool blocked;
    /** true if we have received a compatible flow definition */
    bool flow_def_ok;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_fsink, upipe)
UPIPE_HELPER_UPUMP_MGR(upipe_fsink, upump_mgr, upump)
UPIPE_HELPER_UCLOCK(upipe_fsink, uclock)
UPIPE_HELPER_SINK_DELAY(upipe_fsink, delay)

/** @internal @This allocates a file sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_fsink_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe)
{
    struct upipe_fsink *upipe_fsink = malloc(sizeof(struct upipe_fsink));
    if (unlikely(upipe_fsink == NULL))
        return NULL;
    struct upipe *upipe = upipe_fsink_to_upipe(upipe_fsink);
    upipe_init(upipe, mgr, uprobe);
    upipe_fsink_init_upump_mgr(upipe);
    upipe_fsink_init_uclock(upipe);
    upipe_fsink_init_delay(upipe, SYSTIME_DELAY);
    upipe_fsink->fd = -1;
    upipe_fsink->path = NULL;
    upipe_fsink->blocked = false;
    upipe_fsink->flow_def_ok = false;
    ulist_init(&upipe_fsink->urefs);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This marks the sink as blocked and starts the watcher.
 *
 * @param upipe description structure of the pipe
 * @param timeout time to wait before waking; if 0, wait for the
 * file descriptor to unblock
 */
static void upipe_fsink_wait(struct upipe *upipe, uint64_t timeout)
{
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
    struct upump *upump;
    if (unlikely(timeout != 0))
        upump = upump_alloc_timer(upipe_fsink->upump_mgr, upipe_fsink_watcher,
                                  upipe, false, timeout, 0);
    else
        upump = upump_alloc_fd_write(upipe_fsink->upump_mgr,
                                     upipe_fsink_watcher, upipe, false,
                                     upipe_fsink->fd);
    if (unlikely(upump == NULL)) {
        upipe_err_va(upipe, "can't create watcher");
        upipe_throw_upump_error(upipe);
        return;
    }

    upipe_fsink_set_upump(upipe, upump);
    upump_start(upump);
    if (!upipe_fsink->blocked) {
        upump_mgr_sink_block(upipe_fsink->upump_mgr);
        upipe_fsink->blocked = true;
    }
}

/** @internal @This outputs data to the file sink.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_fsink_output(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);

    if (unlikely(upipe_fsink->fd == -1)) {
        uref_free(uref);
        upipe_warn(upipe, "received a buffer before opening a file");
        return;
    }

    if (unlikely(!ulist_empty(&upipe_fsink->urefs))) {
        ulist_add(&upipe_fsink->urefs, uref_to_uchain(uref));
        return;
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
        ulist_add(&upipe_fsink->urefs, uref_to_uchain(uref));
        upipe_fsink_wait(upipe, systime - now);
        return;
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
                    ulist_add(&upipe_fsink->urefs, uref_to_uchain(uref));
                    upipe_fsink_wait(upipe, 0);
                    return;
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
            upipe_throw_write_end(upipe, upipe_fsink->path);
            return;
        }

        uref_free(uref);
        break;
    }
}

/** @internal @This is called when the file descriptor can be written again.
 * Unblock the sink and unqueue all queued buffers.
 *
 * @param upump description structure of the watcher
 */
static void upipe_fsink_watcher(struct upump *upump)
{
    struct upump_mgr *mgr = upump->mgr;
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
    struct ulist urefs = upipe_fsink->urefs;

    ulist_init(&upipe_fsink->urefs);
    upipe_fsink_set_upump(upipe, NULL);
    upump_mgr_sink_unblock(mgr);
    upipe_fsink->blocked = false;

    struct uchain *uchain;
    ulist_delete_foreach (&urefs, uchain) {
        ulist_delete(&urefs, uchain);
        upipe_fsink_output(upipe, uref_from_uchain(uchain), NULL);
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
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(ubase_ncmp(def, EXPECTED_FLOW_DEF))) {
            upipe_fsink->flow_def_ok = false;
            upipe_throw_flow_def_error(upipe, uref);
            uref_free(uref);
            return;
        }

        upipe_fsink->flow_def_ok = true;
        upipe_dbg_va(upipe, "flow definition %s", def);
        uref_free(uref);
        return;
    }

    if (unlikely(uref_flow_get_end(uref))) {
        uref_free(uref);
        upipe_throw_need_input(upipe);
        return;
    }

    if (unlikely(!upipe_fsink->flow_def_ok)) {
        uref_free(uref);
        upipe_throw_flow_def_error(upipe, uref);
        return;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    upipe_fsink_output(upipe, uref, upump);
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
    if (upipe_fsink->blocked) {
        upump_mgr_sink_unblock(upipe_fsink->upump_mgr);
        upipe_fsink->blocked = false;
    }

    if (unlikely(path == NULL))
        return true;

    if (upipe_fsink->upump_mgr == NULL) {
        upipe_throw_need_upump_mgr(upipe);
        if (unlikely(upipe_fsink->upump_mgr == NULL))
            return false;
    }

    const char *mode_desc;
    int flags;
    switch (mode) {
        case UPIPE_FSINK_APPEND:
            mode_desc = "append";
            flags = O_CREAT;
            break;
        case UPIPE_FSINK_OVERWRITE:
            mode_desc = "overwrite";
            flags = O_CREAT;
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
        upipe_err_va(upipe, "can't open file %s (%m)", path);
        return false;
    }
    if (likely(mode == UPIPE_FSINK_APPEND))
        if (unlikely(lseek(upipe_fsink->fd, 0, SEEK_END)) == -1) {
            upipe_err_va(upipe, "can't append to file %s (%m)", path);
            close(upipe_fsink->fd);
            upipe_fsink->fd = -1;
            return false;
        }

    upipe_fsink->path = strdup(path);
    if (unlikely(upipe_fsink->path == NULL)) {
        close(upipe_fsink->fd);
        upipe_fsink->fd = -1;
        upipe_throw_aerror(upipe);
        return false;
    }
    upipe_notice_va(upipe, "opening file %s in %s mode",
                    upipe_fsink->path, mode_desc);
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
            struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
            if (upipe_fsink->upump != NULL)
                upipe_fsink_set_upump(upipe, NULL);
            return upipe_fsink_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_fsink_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
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

    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
    if (unlikely(!ulist_empty(&upipe_fsink->urefs)))
        upipe_fsink_wait(upipe, 0);

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
    upipe_fsink_clean_upump_mgr(upipe);

    struct uchain *uchain;
    ulist_delete_foreach (&upipe_fsink->urefs, uchain) {
        ulist_delete(&upipe_fsink->urefs, uchain);
        uref_free(uref_from_uchain(uchain));
    }

    upipe_clean(upipe);
    free(upipe_fsink);
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
