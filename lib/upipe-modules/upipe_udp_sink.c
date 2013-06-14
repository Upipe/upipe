/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *          Benjamin Cohen
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
 * @short Upipe sink module for udp
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
#include <upipe-modules/upipe_udp_sink.h>
#include "upipe_udp.h"

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

/** default delay to use compared to the theorical reception time of the
 * packet */
#define SYSTIME_DELAY        (0.01 * UCLOCK_FREQ)
/** expected flow definition on all flows */
#define EXPECTED_FLOW_DEF    "block."

#define UDP_DEFAULT_TTL 0
#define UDP_DEFAULT_PORT 1234

static void upipe_udpsink_watcher(struct upump *upump);

/** @internal @This is the private context of a file sink pipe. */
struct upipe_udpsink {
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
    /** file uri */
    char *uri;
    /** temporary uref storage */
    struct ulist urefs;
    /** true if the sink currently blocks the pipe */
    bool blocked;
    /** true if we have received a compatible flow definition */
    bool flow_def_ok;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_udpsink, upipe)
UPIPE_HELPER_UPUMP_MGR(upipe_udpsink, upump_mgr, upump)
UPIPE_HELPER_UCLOCK(upipe_udpsink, uclock)
UPIPE_HELPER_SINK_DELAY(upipe_udpsink, delay)

/** @internal @This allocates a file sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_udpsink_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe)
{
    struct upipe_udpsink *upipe_udpsink = malloc(sizeof(struct upipe_udpsink));
    if (unlikely(upipe_udpsink == NULL))
        return NULL;
    struct upipe *upipe = upipe_udpsink_to_upipe(upipe_udpsink);
    upipe_init(upipe, mgr, uprobe);
    upipe_udpsink_init_upump_mgr(upipe);
    upipe_udpsink_init_uclock(upipe);
    upipe_udpsink_init_delay(upipe, SYSTIME_DELAY);
    upipe_udpsink->fd = -1;
    upipe_udpsink->uri = NULL;
    upipe_udpsink->blocked = false;
    upipe_udpsink->flow_def_ok = false;
    ulist_init(&upipe_udpsink->urefs);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This marks the sink as blocked and starts the watcher.
 *
 * @param upipe description structure of the pipe
 * @param timeout time to wait before waking; if 0, wait for the
 * file descriptor to unblock
 */
static void upipe_udpsink_wait(struct upipe *upipe, uint64_t timeout)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    struct upump *upump;
    if (unlikely(timeout != 0))
        upump = upump_alloc_timer(upipe_udpsink->upump_mgr, upipe_udpsink_watcher,
                                  upipe, false, timeout, 0);
    else
        upump = upump_alloc_fd_write(upipe_udpsink->upump_mgr,
                                     upipe_udpsink_watcher, upipe, false,
                                     upipe_udpsink->fd);
    if (unlikely(upump == NULL)) {
        upipe_err_va(upipe, "can't create watcher");
        upipe_throw_upump_error(upipe);
        return;
    }

    upipe_udpsink_set_upump(upipe, upump);
    upump_start(upump);
    if (!upipe_udpsink->blocked) {
        upump_mgr_sink_block(upipe_udpsink->upump_mgr);
        upipe_udpsink->blocked = true;
    }
}

/** @internal @This outputs data to the file sink.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_udpsink_output(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);

    if (unlikely(upipe_udpsink->fd == -1)) {
        uref_free(uref);
        upipe_warn(upipe, "received a buffer before opening a file");
        return;
    }

    if (unlikely(!ulist_empty(&upipe_udpsink->urefs))) {
        ulist_add(&upipe_udpsink->urefs, uref_to_uchain(uref));
        return;
    }

    if (likely(upipe_udpsink->uclock == NULL))
        goto write_buffer;

    uint64_t systime = 0;
    if (unlikely(!uref_clock_get_systime(uref, &systime))) {
        upipe_warn(upipe, "received non-dated buffer");
        goto write_buffer;
    }

    uint64_t now = uclock_now(upipe_udpsink->uclock);
    systime += upipe_udpsink->delay;
    if (unlikely(now < systime)) {
        ulist_add(&upipe_udpsink->urefs, uref_to_uchain(uref));
        upipe_udpsink_wait(upipe, systime - now);
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

        ssize_t ret = writev(upipe_udpsink->fd, iovecs, iovec_count);
        uref_block_iovec_unmap(uref, 0, -1, iovecs);

        if (unlikely(ret == -1)) {
            switch (errno) {
                case EINTR:
                    continue;
                case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                case EWOULDBLOCK:
#endif
                    ulist_add(&upipe_udpsink->urefs, uref_to_uchain(uref));
                    upipe_udpsink_wait(upipe, 0);
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
            upipe_warn_va(upipe, "write error to %s (%m)", upipe_udpsink->uri);
            upipe_udpsink_set_upump(upipe, NULL);
            upipe_throw_write_end(upipe, upipe_udpsink->uri);
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
static void upipe_udpsink_watcher(struct upump *upump)
{
    struct upump_mgr *mgr = upump->mgr;
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    struct ulist urefs = upipe_udpsink->urefs;

    ulist_init(&upipe_udpsink->urefs);
    upipe_udpsink_set_upump(upipe, NULL);
    upump_mgr_sink_unblock(mgr);
    upipe_udpsink->blocked = false;

    struct uchain *uchain;
    ulist_delete_foreach (&urefs, uchain) {
        ulist_delete(&urefs, uchain);
        upipe_udpsink_output(upipe, uref_from_uchain(uchain), NULL);
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_udpsink_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(ubase_ncmp(def, EXPECTED_FLOW_DEF))) {
            upipe_udpsink->flow_def_ok = false;
            upipe_throw_flow_def_error(upipe, uref);
            uref_free(uref);
            return;
        }

        upipe_udpsink->flow_def_ok = true;
        upipe_dbg_va(upipe, "flow definition %s", def);
        uref_free(uref);
        return;
    }

    if (unlikely(uref_flow_get_end(uref))) {
        upipe_udpsink->flow_def_ok = false;
        uref_free(uref);
        upipe_throw_need_input(upipe);
        return;
    }

    if (unlikely(!upipe_udpsink->flow_def_ok)) {
        uref_free(uref);
        upipe_throw_flow_def_error(upipe, uref);
        return;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    upipe_udpsink_output(upipe, uref, upump);
}

/** @internal @This returns the uri of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the uri of the file
 * @return false in case of error
 */
static bool _upipe_udpsink_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_udpsink->uri;
    return true;
}

/** @internal @This asks to open the given file.
 *
 * @param upipe description structure of the pipe
 * @param uri relative or absolute uri of the file
 * @param mode mode of opening the file
 * @return false in case of error
 */
static bool _upipe_udpsink_set_uri(struct upipe *upipe, const char *uri,
                                  enum upipe_udpsink_mode mode)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    bool use_tcp = false;

    if (unlikely(upipe_udpsink->fd != -1)) {
        if (likely(upipe_udpsink->uri != NULL))
            upipe_notice_va(upipe, "closing file %s", upipe_udpsink->uri);
        close(upipe_udpsink->fd);
    }
    free(upipe_udpsink->uri);
    upipe_udpsink->uri = NULL;
    upipe_udpsink_set_upump(upipe, NULL);
    if (upipe_udpsink->blocked) {
        upump_mgr_sink_unblock(upipe_udpsink->upump_mgr);
        upipe_udpsink->blocked = false;
    }

    if (unlikely(uri == NULL))
        return true;

    if (upipe_udpsink->upump_mgr == NULL) {
        upipe_throw_need_upump_mgr(upipe);
        if (unlikely(upipe_udpsink->upump_mgr == NULL))
            return false;
    }

    const char *mode_desc = NULL; /* hush gcc */
    switch (mode) {
        case UPIPE_UDPSINK_NONE:
            mode_desc = "none";
            break;
        default:
            upipe_err_va(upipe, "invalid mode %d", mode);
            return false;
    }
    upipe_udpsink->fd = upipe_udp_open_socket(upipe, uri,
            UDP_DEFAULT_TTL, UDP_DEFAULT_PORT, 0, NULL, &use_tcp);
    if (unlikely(upipe_udpsink->fd == -1)) {
        upipe_err_va(upipe, "can't open file %s (%s)", uri, mode_desc);
        return false;
    }

    upipe_udpsink->uri = strdup(uri);
    if (unlikely(upipe_udpsink->uri == NULL)) {
        close(upipe_udpsink->fd);
        upipe_udpsink->fd = -1;
        upipe_throw_aerror(upipe);
        return false;
    }
    upipe_notice_va(upipe, "opening uri %s in %s mode",
                    upipe_udpsink->uri, mode_desc);
    return true;
}

/** @internal @This processes control commands on a file sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_udpsink_control(struct upipe *upipe,
                                 enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_udpsink_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
            if (upipe_udpsink->upump != NULL)
                upipe_udpsink_set_upump(upipe, NULL);
            return upipe_udpsink_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_udpsink_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            return upipe_udpsink_set_uclock(upipe, uclock);
        }

        case UPIPE_SINK_GET_DELAY: {
            uint64_t *p = va_arg(args, uint64_t *);
            return upipe_udpsink_get_delay(upipe, p);
        }
        case UPIPE_SINK_SET_DELAY: {
            uint64_t delay = va_arg(args, uint64_t);
            return upipe_udpsink_set_delay(upipe, delay);
        }

        case UPIPE_UDPSINK_GET_URI: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_UDPSINK_SIGNATURE);
            const char **uri_p = va_arg(args, const char **);
            return _upipe_udpsink_get_uri(upipe, uri_p);
        }
        case UPIPE_UDPSINK_SET_URI: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_UDPSINK_SIGNATURE);
            const char *uri = va_arg(args, const char *);
            enum upipe_udpsink_mode mode = va_arg(args, enum upipe_udpsink_mode);
            return _upipe_udpsink_set_uri(upipe, uri, mode);
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
static bool upipe_udpsink_control(struct upipe *upipe, enum upipe_command command,
                                va_list args)
{
    if (unlikely(!_upipe_udpsink_control(upipe, command, args)))
        return false;

    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    if (unlikely(!ulist_empty(&upipe_udpsink->urefs)))
        upipe_udpsink_wait(upipe, 0);

    return true;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_udpsink_free(struct upipe *upipe)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    if (likely(upipe_udpsink->fd != -1)) {
        if (likely(upipe_udpsink->uri != NULL))
            upipe_notice_va(upipe, "closing file %s", upipe_udpsink->uri);
        close(upipe_udpsink->fd);
    }
    upipe_throw_dead(upipe);

    free(upipe_udpsink->uri);
    upipe_udpsink_clean_delay(upipe);
    upipe_udpsink_clean_uclock(upipe);
    upipe_udpsink_clean_upump_mgr(upipe);

    struct uchain *uchain;
    ulist_delete_foreach (&upipe_udpsink->urefs, uchain) {
        ulist_delete(&upipe_udpsink->urefs, uchain);
        uref_free(uref_from_uchain(uchain));
    }

    upipe_clean(upipe);
    free(upipe_udpsink);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_udpsink_mgr = {
    .signature = UPIPE_UDPSINK_SIGNATURE,

    .upipe_alloc = upipe_udpsink_alloc,
    .upipe_input = upipe_udpsink_input,
    .upipe_control = upipe_udpsink_control,
    .upipe_free = upipe_udpsink_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all file sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_udpsink_mgr_alloc(void)
{
    return &upipe_udpsink_mgr;
}
