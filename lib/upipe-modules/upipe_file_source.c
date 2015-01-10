/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe source module for files
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/urequest.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_source_read_size.h>
#include <upipe-modules/upipe_file_source.h>

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

/** default size of buffers when unspecified */
#define UBUF_DEFAULT_SIZE       32768

/** @hidden */
static int upipe_fsrc_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a file source pipe. */
struct upipe_fsrc {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** read size */
    unsigned int read_size;

    /** true if the file is regular and doesn't support poll() */
    bool regular_file;
    /** file descriptor */
    int fd;
    /** file path */
    char *path;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_fsrc, upipe, UPIPE_FSRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_fsrc, urefcount, upipe_fsrc_free)
UPIPE_HELPER_VOID(upipe_fsrc)

UPIPE_HELPER_OUTPUT(upipe_fsrc, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_fsrc, uref_mgr, uref_mgr_request, upipe_fsrc_check,
                      upipe_fsrc_register_output_request,
                      upipe_fsrc_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_fsrc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_fsrc_check,
                      upipe_fsrc_register_output_request,
                      upipe_fsrc_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_fsrc, uclock, uclock_request, upipe_fsrc_check,
                    upipe_fsrc_register_output_request,
                    upipe_fsrc_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_fsrc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_fsrc, upump, upump_mgr)
UPIPE_HELPER_SOURCE_READ_SIZE(upipe_fsrc, read_size)

/** @internal @This allocates a file source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_fsrc_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe, uint32_t signature,
                                      va_list args)
{
    struct upipe *upipe = upipe_fsrc_alloc_void(mgr, uprobe, signature, args);
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    upipe_fsrc_init_urefcount(upipe);
    upipe_fsrc_init_uref_mgr(upipe);
    upipe_fsrc_init_ubuf_mgr(upipe);
    upipe_fsrc_init_output(upipe);
    upipe_fsrc_init_upump_mgr(upipe);
    upipe_fsrc_init_upump(upipe);
    upipe_fsrc_init_uclock(upipe);
    upipe_fsrc_init_read_size(upipe, UBUF_DEFAULT_SIZE);
    upipe_fsrc->fd = -1;
    upipe_fsrc->path = NULL;
    upipe_throw_ready(upipe);
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
    uint64_t systime = 0; /* to keep gcc quiet */
    if (upipe_fsrc->uclock != NULL)
        systime = uclock_now(upipe_fsrc->uclock);

    struct uref *uref = uref_block_alloc(upipe_fsrc->uref_mgr,
                                         upipe_fsrc->ubuf_mgr,
                                         upipe_fsrc->read_size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *buffer;
    int read_size = -1;
    if (unlikely(!ubase_check(uref_block_write(uref, 0, &read_size,
                                               &buffer)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    assert(read_size == upipe_fsrc->read_size);

    ssize_t ret = read(upipe_fsrc->fd, buffer, upipe_fsrc->read_size);
    uref_block_unmap(uref, 0);

    if (unlikely(ret == -1)) {
        uref_free(uref);
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
        upipe_err_va(upipe, "read error from %s (%m)", upipe_fsrc->path);
        upipe_fsrc_set_upump(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }
    if (unlikely(ret == 0)) {
        uref_free(uref);
        upipe_notice_va(upipe, "end of file %s", upipe_fsrc->path);
        upipe_fsrc_set_upump(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }
    if (upipe_fsrc->uclock != NULL)
        uref_clock_set_cr_sys(uref, systime);
    if (unlikely(ret != upipe_fsrc->read_size))
        uref_block_resize(uref, 0, ret);
    upipe_use(upipe);
    upipe_fsrc_output(upipe, uref, &upipe_fsrc->upump);
    upipe_release(upipe);
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_fsrc_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_fsrc_store_flow_def(upipe, flow_format);

    upipe_fsrc_check_upump_mgr(upipe);
    if (upipe_fsrc->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_fsrc->uref_mgr == NULL) {
        upipe_fsrc_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_fsrc->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_fsrc->uref_mgr, NULL);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_fsrc_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_fsrc->uclock == NULL &&
        urequest_get_opaque(&upipe_fsrc->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    if (upipe_fsrc->fd != -1 && upipe_fsrc->upump == NULL) {
        struct upump *upump;
        if (upipe_fsrc->regular_file)
            upump = upump_alloc_idler(upipe_fsrc->upump_mgr,
                                      upipe_fsrc_worker, upipe);
        else
            upump = upump_alloc_fd_read(upipe_fsrc->upump_mgr,
                                        upipe_fsrc_worker, upipe,
                                        upipe_fsrc->fd);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_fsrc_set_upump(upipe, upump);
        upump_start(upump);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the path of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param path_p filled in with the path of the file
 * @return an error code
 */
static int upipe_fsrc_get_uri(struct upipe *upipe, const char **path_p)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    assert(path_p != NULL);
    *path_p = upipe_fsrc->path;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given file.
 *
 * @param upipe description structure of the pipe
 * @param path relative or absolute path of the file
 * @return an error code
 */
static int upipe_fsrc_set_uri(struct upipe *upipe, const char *path)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);

    if (unlikely(upipe_fsrc->fd != -1)) {
        if (likely(upipe_fsrc->path != NULL))
            upipe_notice_va(upipe, "closing file %s", upipe_fsrc->path);
        close(upipe_fsrc->fd);
        upipe_fsrc->fd = -1;
    }
    free(upipe_fsrc->path);
    upipe_fsrc->path = NULL;
    upipe_fsrc_set_upump(upipe, NULL);

    if (unlikely(path == NULL))
        return UBASE_ERR_NONE;

    struct stat st;
    if (unlikely(stat(path, &st) == -1)) {
        upipe_err_va(upipe, "can't stat file %s (%m)", path);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_fsrc->regular_file = !!S_ISREG(st.st_mode);
    upipe_fsrc->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (unlikely(upipe_fsrc->fd == -1)) {
        upipe_err_va(upipe, "can't open file %s (%m)", path);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_fsrc->path = strdup(path);
    if (unlikely(upipe_fsrc->path == NULL)) {
        close(upipe_fsrc->fd);
        upipe_fsrc->fd = -1;
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_notice_va(upipe, "opening file %s", upipe_fsrc->path);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the size of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param size_p filled in with the size of the file, in octets
 * @return an error code
 */
static int _upipe_fsrc_get_size(struct upipe *upipe,
                                           uint64_t *size_p)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    assert(size_p != NULL);
    if (unlikely(upipe_fsrc->fd == -1))
        return UBASE_ERR_UNHANDLED;
    struct stat st;
    if (unlikely(fstat(upipe_fsrc->fd, &st) == -1))
        return UBASE_ERR_EXTERNAL;
    *size_p = st.st_size;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the position of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param position_p filled in with the reading position, in octets
 * @return an error code
 */
static int _upipe_fsrc_get_position(struct upipe *upipe,
                                               uint64_t *position_p)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    assert(position_p != NULL);
    if (unlikely(upipe_fsrc->fd == -1))
        return UBASE_ERR_UNHANDLED;
    off_t position = lseek(upipe_fsrc->fd, 0, SEEK_CUR);
    if (unlikely(position == (off_t)-1))
        return UBASE_ERR_EXTERNAL;
    *position_p = position;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to read at the given position.
 *
 * @param upipe description structure of the pipe
 * @param position new reading position, in octets (between 0 and the size)
 * @return an error code
 */
static int _upipe_fsrc_set_position(struct upipe *upipe, uint64_t position)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    if (unlikely(upipe_fsrc->fd == -1))
        return UBASE_ERR_UNHANDLED;
    return lseek(upipe_fsrc->fd, position, SEEK_SET) != (off_t)-1 ?
        UBASE_ERR_NONE : UBASE_ERR_EXTERNAL;
}

/** @internal @This processes control commands on a file source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_fsrc_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_fsrc_set_upump(upipe, NULL);
            return upipe_fsrc_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_fsrc_set_upump(upipe, NULL);
            upipe_fsrc_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_fsrc_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_fsrc_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_fsrc_set_output(upipe, output);
        }

        case UPIPE_SOURCE_GET_READ_SIZE: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_fsrc_get_read_size(upipe, p);
        }
        case UPIPE_SOURCE_SET_READ_SIZE: {
            unsigned int read_size = va_arg(args, unsigned int);
            return upipe_fsrc_set_read_size(upipe, read_size);
        }
        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_fsrc_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_fsrc_set_uri(upipe, uri);
        }

        case UPIPE_FSRC_GET_SIZE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_FSRC_SIGNATURE);
            uint64_t *size_p = va_arg(args, uint64_t *);
            return _upipe_fsrc_get_size(upipe, size_p);
        }
        case UPIPE_FSRC_GET_POSITION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_FSRC_SIGNATURE)
            uint64_t *position_p = va_arg(args, uint64_t *);
            return _upipe_fsrc_get_position(upipe, position_p);
        }
        case UPIPE_FSRC_SET_POSITION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_FSRC_SIGNATURE)
            uint64_t position = va_arg(args, uint64_t);
            return _upipe_fsrc_set_position(upipe, position);
        }
        default:
            return UBASE_ERR_NONE;
    }
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_fsrc_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_fsrc_control(upipe, command, args))

    return upipe_fsrc_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_fsrc_free(struct upipe *upipe)
{
    struct upipe_fsrc *upipe_fsrc = upipe_fsrc_from_upipe(upipe);
    if (likely(upipe_fsrc->fd != -1)) {
        if (likely(upipe_fsrc->path != NULL))
            upipe_notice_va(upipe, "closing file %s", upipe_fsrc->path);
        close(upipe_fsrc->fd);
    }
    upipe_throw_dead(upipe);

    free(upipe_fsrc->path);
    upipe_fsrc_clean_read_size(upipe);
    upipe_fsrc_clean_uclock(upipe);
    upipe_fsrc_clean_upump(upipe);
    upipe_fsrc_clean_upump_mgr(upipe);
    upipe_fsrc_clean_output(upipe);
    upipe_fsrc_clean_ubuf_mgr(upipe);
    upipe_fsrc_clean_uref_mgr(upipe);
    upipe_fsrc_clean_urefcount(upipe);
    upipe_fsrc_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_fsrc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_FSRC_SIGNATURE,

    .upipe_alloc = upipe_fsrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_fsrc_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all file source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_fsrc_mgr_alloc(void)
{
    return &upipe_fsrc_mgr;
}
