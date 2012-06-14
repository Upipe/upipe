/*****************************************************************************
 * upipe_file_source.c: upipe source module for files
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

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_linear.h>
#include <upipe/upipe_source.h>
#include <upipe-modules/upipe_file_source.h>

#ifndef O_CLOEXEC
#   define O_CLOEXEC 0
#endif

/** default size of buffers when unspecified */
#define UBUF_DEFAULT_SIZE       4096

/** super-set of the upipe_source structure with additional local members */
struct upipe_fsrc {
    /** file descriptor */
    int fd;
    /** file path */
    char *path;

    /** members common to source pipes */
    struct upipe_source upipe_source;
};

/** @internal @This returns the high-level upipe structure.
 *
 * @param upipe_fsrc pointer to the upipe_fsrc structure
 * @return pointer to the upipe_t structure
 */
static inline struct upipe *upipe_fsrc_to_upipe(struct upipe_fsrc *upipe_fsrc)
{
    return upipe_source_to_upipe(&upipe_fsrc->upipe_source);
}

/** @internal @This returns the private upipe_fsrc structure.
 *
 * @param upipe description structure of the pipe
 * @return pointer to the upipe_fsrc structure
 */
static inline struct upipe_fsrc *upipe_fsrc_from_upipe(struct upipe *upipe)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    return container_of(upipe_source, struct upipe_fsrc, upipe_source);
}

/** @This checks if the file source pipe is ready to process data.
 *
 * @param upipe description structure of the pipe
 */
static bool upipe_fsrc_ready(struct upipe *upipe)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    return upipe_source_ready(upipe) && upipe_fsrc->fd != -1;
}

/** @internal @This allocates a file source pipe.
 *
 * @param mgr common management structure
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_fsrc_alloc(struct upipe_mgr *mgr)
{
    struct upipe_fsrc *upipe_fsrc = malloc(sizeof(struct upipe_fsrc));
    if (unlikely(upipe_fsrc == NULL)) return NULL;
    struct upipe *upipe = upipe_fsrc_to_upipe(upipe_fsrc);
    upipe_source_init(upipe, UBUF_DEFAULT_SIZE);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_FSRC_SIGNATURE;
    upipe_fsrc->fd = -1;
    upipe_fsrc->path = NULL;
    return upipe;
}

/** @internal @This reads data from the source and outputs it.
 * It is called either when the idler triggers (permanent storage mode) or
 * when data is available on the file descriptor (live stream mode).
 *
 * @param upump description structure of the read watcher
 */
static void upipe_fsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    size_t read_size = upipe_source_read_size(upipe);
    uint64_t systime = 0; /* to keep gcc quiet */
    struct uclock *uclock = upipe_source_uclock(upipe);
    if (unlikely(uclock != NULL))
        systime = uclock_now(uclock);

    struct uref *uref = uref_block_alloc(upipe_linear_uref_mgr(upipe),
                                         upipe_linear_ubuf_mgr(upipe),
                                         read_size);
    if (unlikely(uref == NULL)) {
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return;
    }

    uint8_t *buffer = uref_block_buffer(uref, NULL);
    ssize_t ret = read(upipe_fsrc->fd, buffer, read_size);
    if (unlikely(ret == -1)) {
        uref_block_release(uref);
        switch (errno) {
            case EINTR:
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                /* not an issue, try again later */
                return;
            case EBADF:
            case EINVAL:
            case EIO:
            default:
                break;
        }
        ulog_error(upipe->ulog, "read error from %s (%s)", upipe_fsrc->path,
                   ulog_strerror(upipe->ulog, errno));
        upipe_source_set_upump(upipe, NULL);
        upipe_throw_read_end(upipe, upipe_fsrc->path);
        return;
    }
    if (unlikely(ret == 0)) {
        uref_block_release(uref);
        if (likely(uclock == NULL)) {
            ulog_notice(upipe->ulog, "end of file %s", upipe_fsrc->path);
            upipe_source_set_upump(upipe, NULL);
            upipe_throw_read_end(upipe, upipe_fsrc->path);
        }
        return;
    }
    if (unlikely(uclock != NULL))
        uref_clock_set_systime(&uref, systime);
    if (unlikely(ret != read_size))
        uref_block_resize(&uref, upipe_linear_ubuf_mgr(upipe), ret, 0);
    upipe_source_output(upipe, uref);
}

/** @internal @This returns the path of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param path_p filled in with the path of the file
 * @return false in case of error
 */
static bool _upipe_fsrc_get_path(struct upipe *upipe, const char **path_p)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    assert(path_p != NULL);
    *path_p = upipe_fsrc->path;
    return true;
}

/** @internal @This asks to open the given file.
 *
 * @param upipe description structure of the pipe
 * @param path relative or absolute path of the file
 * @return false in case of error
 */
static bool _upipe_fsrc_set_path(struct upipe *upipe, const char *path)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);

    if (unlikely(upipe_fsrc->fd != -1)) {
        if (likely(upipe_fsrc->path != NULL))
            ulog_notice(upipe->ulog, "closing file %s", upipe_fsrc->path);
        close(upipe_fsrc->fd);
    }
    free(upipe_fsrc->path);
    upipe_fsrc->path = NULL;
    upipe_source_set_upump(upipe, NULL);

    if (likely(path != NULL)) {
        upipe_fsrc->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (unlikely(upipe_fsrc->fd == -1)) {
            ulog_error(upipe->ulog, "can't open file %s (%s)", path,
                       ulog_strerror(upipe->ulog, errno));
            return false;
        }

        upipe_fsrc->path = strdup(path);
        ulog_notice(upipe->ulog, "opening file %s", upipe_fsrc->path);
    }
    return true;
}

/** @internal @This returns the size of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param size_p filled in with the size of the file, in octets
 * @return false in case of error
 */
static bool _upipe_fsrc_get_size(struct upipe *upipe, uint64_t *size_p)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    assert(size_p != NULL);
    if (unlikely(upipe_fsrc->fd == -1)) return false;
    struct stat st;
    if (unlikely(fstat(upipe_fsrc->fd, &st) == -1)) return false;
    *size_p = st.st_size;
    return true;
}

/** @internal @This returns the position of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param position_p filled in with the reading position, in octets
 * @return false in case of error
 */
static bool _upipe_fsrc_get_position(struct upipe *upipe, uint64_t *position_p)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    assert(position_p != NULL);
    if (unlikely(upipe_fsrc->fd == -1)) return false;
    off_t position = lseek(upipe_fsrc->fd, 0, SEEK_CUR);
    if (unlikely(position == (off_t)-1)) return false;
    *position_p = position;
    return true;
}

/** @internal @This asks to read at the given position.
 *
 * @param upipe description structure of the pipe
 * @param position new reading position, in octets (between 0 and the size)
 * @return false in case of error
 */
static bool _upipe_fsrc_set_position(struct upipe *upipe, uint64_t position)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    if (unlikely(upipe_fsrc->fd == -1)) return false;
    return lseek(upipe_fsrc->fd, position, SEEK_SET) != (off_t)-1;
}

/** @internal @This processes control commands on a file source pipe.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_fsrc_control(struct upipe *upipe, enum upipe_control control,
                                va_list args)
{
    switch (control) {
        case UPIPE_FSRC_GET_PATH: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_FSRC_SIGNATURE);
            const char **path_p = va_arg(args, const char **);
            return _upipe_fsrc_get_path(upipe, path_p);
        }
        case UPIPE_FSRC_SET_PATH: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_FSRC_SIGNATURE);
            const char *path = va_arg(args, const char *);
            return _upipe_fsrc_set_path(upipe, path);
        }
        case UPIPE_FSRC_GET_SIZE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_FSRC_SIGNATURE);
            uint64_t *size_p = va_arg(args, uint64_t *);
            return _upipe_fsrc_get_size(upipe, size_p);
        }
        case UPIPE_FSRC_GET_POSITION: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_FSRC_SIGNATURE);
            uint64_t *position_p = va_arg(args, uint64_t *);
            return _upipe_fsrc_get_position(upipe, position_p);
        }
        case UPIPE_FSRC_SET_POSITION: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_FSRC_SIGNATURE);
            uint64_t position = va_arg(args, uint64_t);
            return _upipe_fsrc_set_position(upipe, position);
        }
        default:
            return upipe_source_control(upipe, control, args);
    }
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_fsrc_control(struct upipe *upipe, enum upipe_control control,
                               va_list args)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    bool ret = _upipe_fsrc_control(upipe, control, args);
    struct upump *upump;

    if (unlikely(upipe_fsrc_ready(upipe))) {
        if (unlikely(upipe_linear_flow_def(upipe) == NULL)) {
            struct uref *flow_def =
                uref_block_flow_alloc_definition(upipe_linear_uref_mgr(upipe),
                                                 NULL);
            if (likely(flow_def != NULL))
                upipe_source_set_flow_def(upipe, flow_def);
        }

        if (likely(upipe_source_upump(upipe) == NULL)) {
            struct uclock *uclock = upipe_source_uclock(upipe);
            if (likely(uclock == NULL))
                upump = upump_alloc_idler(upipe_source_upump_mgr(upipe),
                                          upipe_fsrc_worker, upipe, true);
            else
                upump = upump_alloc_fd_read(upipe_source_upump_mgr(upipe),
                                            upipe_fsrc_worker, upipe, true,
                                            upipe_fsrc->fd);
            if (unlikely(upump == NULL)) {
                ulog_error(upipe->ulog, "can't create worker");
                return false;
            }
            upipe_source_set_upump(upipe, upump);
            upump_start(upump);
        }

    } else if (unlikely((upump = upipe_source_upump(upipe)) != NULL))
        upump_stop(upump);

    return ret;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_fsrc_free(struct upipe *upipe)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    if (likely(upipe_fsrc->fd != -1)) {
        if (likely(upipe_fsrc->path != NULL))
            ulog_notice(upipe->ulog, "closing file %s", upipe_fsrc->path);
        close(upipe_fsrc->fd);
    }
    free(upipe_fsrc->path);
    upipe_source_clean(upipe);
    free(upipe_fsrc);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_fsrc_mgr = {
    /* no need to initialize refcount as we don't use it */

    .upipe_alloc = upipe_fsrc_alloc,
    .upipe_control = upipe_fsrc_control,
    .upipe_free = upipe_fsrc_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all file sources
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_fsrc_mgr_alloc(void)
{
    return &upipe_fsrc_mgr;
}
