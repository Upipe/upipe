/*****************************************************************************
 * upipe_file_sink.c: upipe sink module for files
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

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_flows.h>
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
#define SYSTIME_DELAY               (0.01 * UCLOCK_FREQ)
/** expected flow definition on all flows */
#define EXPECTED_FLOW_DEFINITION    "block."

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

    /** list of input flows */
    struct ulist flows;
    /** file descriptor */
    int fd;
    /** file path */
    char *path;
    /** temporary uref storage */
    struct ulist urefs;
    /** true if the sink currently blocks the pipe */
    bool blocked;
    /** true if we have thrown the ready event */
    bool ready;

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
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_fsink_alloc(struct upipe_mgr *mgr)
{
    struct upipe_fsink *upipe_fsink = malloc(sizeof(struct upipe_fsink));
    if (unlikely(upipe_fsink == NULL)) return NULL;
    struct upipe *upipe = upipe_fsink_to_upipe(upipe_fsink);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe_fsink_init_upump_mgr(upipe);
    upipe_fsink_init_uclock(upipe);
    upipe_fsink_init_delay(upipe, SYSTIME_DELAY);
    upipe->signature = UPIPE_FSINK_SIGNATURE;
    upipe_flows_init(&upipe_fsink->flows);
    upipe_fsink->fd = -1;
    upipe_fsink->path = NULL;
    upipe_fsink->blocked = false;
    upipe_fsink->ready = false;
    ulist_init(&upipe_fsink->urefs);
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
        ulog_error(upipe->ulog, "can't create watcher");
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
 * @param uref struct uref structure
 */
static void upipe_fsink_output(struct upipe *upipe, struct uref *uref)
{
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);

    if (unlikely(upipe_fsink->fd == -1)) {
        uref_block_release(uref);
        ulog_warning(upipe->ulog, "received a buffer before opening a file");
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
        ulog_warning(upipe->ulog, "received non-dated buffer");
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
        size_t size;
        uint8_t *buffer = uref_block_buffer(uref, &size);
        ssize_t ret = write(upipe_fsink->fd, buffer, size);

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
            uref_block_release(uref);
            ulog_warning(upipe->ulog, "write error to %s (%s)",
                         upipe_fsink->path, ulog_strerror(upipe->ulog, errno));
            upipe_fsink_set_upump(upipe, NULL);
            upipe_throw_write_end(upipe, upipe_fsink->path);
            return;
        }

        if (likely(ret == size)) {
            uref_block_release(uref);
            break;
        } else
            uref_block_resize(&uref, NULL, size - ret, ret);
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
    ulist_foreach (&urefs, uchain)
        upipe_fsink_output(upipe, uref_from_uchain(uchain));
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_fsink_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
    const char *flow, *def;
    if (unlikely(!uref_flow_get_name(uref, &flow))) {
        ulog_warning(upipe->ulog, "received a buffer outside of a flow");
        uref_release(uref);
        return false;
    }

    if (unlikely(uref_flow_get_definition(uref, &def))) {
        upipe_flows_set(&upipe_fsink->flows, uref);
        ulog_debug(upipe->ulog, "flow definition for %s: %s", flow, def);
        return true;
    }

    if (unlikely(!upipe_flows_get_definition(&upipe_fsink->flows, flow,
                                             &def))) {
        ulog_warning(upipe->ulog, "received a buffer without a flow definition");
        uref_release(uref);
        return false;
    }

    if (unlikely(uref_flow_get_delete(uref))) {
        upipe_flows_delete(&upipe_fsink->flows, flow);
        uref_release(uref);
        return true;
    }

    if (unlikely(strncmp(def, EXPECTED_FLOW_DEFINITION,
                         strlen(EXPECTED_FLOW_DEFINITION)))) {
        ulog_warning(upipe->ulog, "received a buffer with an incompatible flow defintion");
        uref_release(uref);
        return false;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_release(uref);
        return true;
    }

    upipe_fsink_output(upipe, uref);
    return true;
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
            ulog_notice(upipe->ulog, "closing file %s", upipe_fsink->path);
        close(upipe_fsink->fd);
    }
    free(upipe_fsink->path);
    upipe_fsink->path = NULL;
    upipe_fsink_set_upump(upipe, NULL);
    if (upipe_fsink->blocked) {
        upump_mgr_sink_unblock(upipe_fsink->upump_mgr);
        upipe_fsink->blocked = false;
    }

    if (likely(path != NULL)) {
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
                ulog_error(upipe->ulog, "invalid mode %d", mode);
                return false;
        }
        upipe_fsink->fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC | flags,
                               S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (unlikely(upipe_fsink->fd == -1)) {
            ulog_error(upipe->ulog, "can't open file %s (%s)",
                       path, ulog_strerror(upipe->ulog, errno));
            return false;
        }
        if (likely(mode == UPIPE_FSINK_APPEND))
            if (unlikely(lseek(upipe_fsink->fd, 0, SEEK_END)) == -1) {
                ulog_error(upipe->ulog, "can't append to file %s (%s)",
                           path, ulog_strerror(upipe->ulog, errno));
                close(upipe_fsink->fd);
                upipe_fsink->fd = -1;
                return false;
            }

        upipe_fsink->path = strdup(path);
        ulog_notice(upipe->ulog, "opening file %s in %s mode",
                    upipe_fsink->path, mode_desc);
    }
    return true;
}

/** @internal @This processes control commands on a file sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_fsink_control(struct upipe *upipe,
                                 enum upipe_control control, va_list args)
{
    switch (control) {
        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_fsink_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
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
 * @param control type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_fsink_control(struct upipe *upipe, enum upipe_control control,
                                va_list args)
{
    if (likely(control == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        return upipe_fsink_input(upipe, uref);
    }

    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
    bool ret = _upipe_fsink_control(upipe, control, args);

    if (unlikely(upipe_fsink->upump_mgr != NULL)) {
        if (unlikely(!ulist_empty(&upipe_fsink->urefs)))
            upipe_fsink_wait(upipe, 0);
        if (likely(!upipe_fsink->ready)) {
            upipe_throw_ready(upipe);
            upipe_fsink->ready = true;
        }

    } else {
        upipe_fsink->ready = false;
        upipe_throw_need_upump_mgr(upipe);
    }

    return ret;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_fsink_free(struct upipe *upipe)
{
    struct upipe_fsink *upipe_fsink = upipe_fsink_from_upipe(upipe);
    upipe_flows_clean(&upipe_fsink->flows);
    if (likely(upipe_fsink->fd != -1)) {
        if (likely(upipe_fsink->path != NULL))
            ulog_notice(upipe->ulog, "closing file %s", upipe_fsink->path);
        close(upipe_fsink->fd);
    }
    free(upipe_fsink->path);
    upipe_fsink_clean_delay(upipe);
    upipe_fsink_clean_uclock(upipe);
    upipe_fsink_clean_upump_mgr(upipe);
    free(upipe_fsink);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_fsink_mgr = {
    /* no need to initialize refcount as we don't use it */

    .upipe_alloc = upipe_fsink_alloc,
    .upipe_control = upipe_fsink_control,
    .upipe_free = upipe_fsink_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all file sinks
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_fsink_mgr_alloc(void)
{
    return &upipe_fsink_mgr;
}
