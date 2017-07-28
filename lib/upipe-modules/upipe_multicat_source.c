/*
 * Copyright (C) 2012-2016 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
 * @short Upipe module - multicat file source
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref_clock.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_output_size.h>
#include <upipe-modules/upipe_multicat_source.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>

#ifndef O_CLOEXEC
#   define O_CLOEXEC 0
#endif

/** default size of buffers when unspecified - 7 TS packets */
#define UBUF_DEFAULT_SIZE       1316
/** mux number of missing segments */
#define MISSING_SEGMENTS        5

/** @internal @This is the private context of a multicat source pipe. */ 
struct upipe_msrc {
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
    unsigned int output_size;

    /** input flow def */
    struct uref *flow_def_input;

    /** data file descriptor */
    int fd;
    /** aux file pointer */
    FILE *aux_file;
    /** file index */
    uint64_t fileidx;
    /** current position */
    uint64_t pos;
    /** number of missing segments */
    unsigned long missing;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_msrc_check(struct upipe *upipe, struct uref *flow_format);
/** @hidden */
static int upipe_msrc_setup(struct upipe *upipe);

UPIPE_HELPER_UPIPE(upipe_msrc, upipe, UPIPE_MSRC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_msrc, urefcount, upipe_msrc_free)
UPIPE_HELPER_VOID(upipe_msrc)

UPIPE_HELPER_OUTPUT(upipe_msrc, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_msrc, uref_mgr, uref_mgr_request, upipe_msrc_check,
                      upipe_msrc_register_output_request,
                      upipe_msrc_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_msrc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_msrc_check,
                      upipe_msrc_register_output_request,
                      upipe_msrc_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_msrc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_msrc, upump, upump_mgr)
UPIPE_HELPER_OUTPUT_SIZE(upipe_msrc, output_size)

/** @This swaps uint64 from net-endian.
 *
 * @param buf source buffer
 * @return uint64 host-endian
 */
static inline uint64_t upipe_msrc_ntoh64(const uint8_t *buf)
{
    return ((uint64_t)buf[0] << 56)
         | ((uint64_t)buf[1] << 48)
         | ((uint64_t)buf[2] << 40)
         | ((uint64_t)buf[3] << 32)
         | ((uint64_t)buf[4] << 24)
         | ((uint64_t)buf[5] << 16)
         | ((uint64_t)buf[6] << 8)
         | ((uint64_t)buf[7] << 0);
}

/** @internal @This allocates a msrc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_msrc_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_msrc_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    upipe_msrc_init_urefcount(upipe);
    upipe_msrc_init_uref_mgr(upipe);
    upipe_msrc_init_ubuf_mgr(upipe);
    upipe_msrc_init_output(upipe);
    upipe_msrc_init_upump_mgr(upipe);
    upipe_msrc_init_upump(upipe);
    upipe_msrc_init_output_size(upipe, UBUF_DEFAULT_SIZE);
    upipe_msrc->flow_def_input = NULL;
    upipe_msrc->fd = -1;
    upipe_msrc->aux_file = NULL;
    upipe_msrc->fileidx = -1;
    upipe_msrc->pos = UINT64_MAX;
    upipe_msrc->missing = 0;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This skips the current segment in case of error.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_msrc_skip(struct upipe *upipe)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    if (upipe_msrc->missing++ >= MISSING_SEGMENTS)
        return UBASE_ERR_INVALID;
    upipe_msrc->fileidx++;
    return upipe_msrc_setup(upipe);
}

/** @internal @This sets up the reading structures.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_msrc_setup(struct upipe *upipe)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    const char *path, *data, *aux;
    UBASE_RETURN(uref_msrc_flow_get_path(upipe_msrc->flow_def_input, &path))
    UBASE_RETURN(uref_msrc_flow_get_data(upipe_msrc->flow_def_input, &data))
    UBASE_RETURN(uref_msrc_flow_get_aux(upipe_msrc->flow_def_input, &aux))

    if (upipe_msrc->fd != -1)
        ubase_clean_fd(&upipe_msrc->fd);
    if (upipe_msrc->aux_file != NULL) {
        fclose(upipe_msrc->aux_file);
        upipe_msrc->aux_file = NULL;
    }

    char data_file[strlen(path) + strlen(data) +
                   sizeof("18446744073709551615")];
    sprintf(data_file, "%s%"PRIu64"%s", path, upipe_msrc->fileidx, data);

    upipe_msrc->fd = open(data_file, O_RDONLY);
    if (unlikely(upipe_msrc->fd == -1)) {
        upipe_warn_va(upipe, "segment %"PRIu64" not found (data)",
                      upipe_msrc->fileidx);
        /* try next file anyway */
        return upipe_msrc_skip(upipe);
    }

    char aux_file[strlen(path) + strlen(aux) +
                  sizeof("18446744073709551615")];
    sprintf(aux_file, "%s%"PRIu64"%s", path, upipe_msrc->fileidx, aux);

    upipe_msrc->aux_file = fopen(aux_file, "rb");
    if (unlikely(upipe_msrc->aux_file == NULL)) {
        upipe_warn_va(upipe, "segment %"PRIu64" not found (aux)",
                      upipe_msrc->fileidx);
        /* try next file anyway */
        return upipe_msrc_skip(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This starts the reader.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_msrc_start(struct upipe *upipe)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    const char *path, *data, *aux;
    uint64_t rotate = UPIPE_MSRC_DEF_ROTATE;
    uint64_t offset = UPIPE_MSRC_DEF_OFFSET;
    UBASE_RETURN(uref_msrc_flow_get_path(upipe_msrc->flow_def_input, &path))
    UBASE_RETURN(uref_msrc_flow_get_data(upipe_msrc->flow_def_input, &data))
    UBASE_RETURN(uref_msrc_flow_get_aux(upipe_msrc->flow_def_input, &aux))
    uref_msrc_flow_get_rotate(upipe_msrc->flow_def_input, &rotate);
    uref_msrc_flow_get_offset(upipe_msrc->flow_def_input, &offset);
    upipe_msrc->fileidx = (upipe_msrc->pos - offset) / rotate;

    char aux_file[strlen(path) + strlen(aux) +
                  sizeof(".18446744073709551615")];
    sprintf(aux_file, "%s%"PRIu64"%s", path, upipe_msrc->fileidx, aux);

    int fd = open(aux_file, O_RDONLY);
    if (unlikely(fd == -1)) {
        upipe_warn_va(upipe, "segment %"PRIu64" not found (start)",
                      upipe_msrc->fileidx);
        /* try next file anyway */
        return upipe_msrc_skip(upipe);
    }

    struct stat aux_stat;
    if (unlikely(fstat(fd, &aux_stat) == -1 ||
                 aux_stat.st_size < sizeof(uint64_t))) {
        upipe_warn_va(upipe, "invalid segment %"PRIu64, upipe_msrc->fileidx);
        /* try next file anyway */
        return upipe_msrc_skip(upipe);
    }

    uint8_t *aux_buf = mmap(NULL, aux_stat.st_size, PROT_READ, MAP_SHARED,
                            fd, 0);
    if (unlikely(aux_buf == MAP_FAILED)) {
        upipe_err_va(upipe,
                     "unable to mmap segment %"PRIu64, upipe_msrc->fileidx);
        return UBASE_ERR_EXTERNAL;
    }

    uint64_t offset1 = 0;
    uint64_t offset2 = aux_stat.st_size / sizeof(uint64_t);

    for ( ; ; ) {
        uint64_t mid_offset = (offset1 + offset2) / 2;
        uint64_t mid_aux = upipe_msrc_ntoh64(aux_buf +
                                             mid_offset * sizeof(uint64_t));

        if (offset1 == mid_offset)
            break;

        if (mid_aux >= upipe_msrc->pos)
            offset2 = mid_offset;
        else
            offset1 = mid_offset;
    }

    munmap(aux_buf, aux_stat.st_size);
    close(fd);

    UBASE_RETURN(upipe_msrc_setup(upipe))
    if (unlikely(lseek(upipe_msrc->fd, (off_t)upipe_msrc->output_size * offset1,
                       SEEK_SET) == -1 ||
                 fseeko(upipe_msrc->aux_file, 8 * offset1, SEEK_SET) == -1)) {
        upipe_warn_va(upipe, "invalid segment %"PRIu64, upipe_msrc->fileidx);
        /* try next file anyway */
        return upipe_msrc_skip(upipe);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This reads data from the source and outputs it.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_msrc_handle(struct upipe *upipe)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    uint8_t aux[8];
    if (fread(aux, 8, 1, upipe_msrc->aux_file) != 1)
        return upipe_msrc_skip(upipe);
    uint64_t cr_sys = upipe_msrc_ntoh64(aux);

    struct uref *uref = uref_block_alloc(upipe_msrc->uref_mgr,
                                         upipe_msrc->ubuf_mgr,
                                         upipe_msrc->output_size);
    if (unlikely(uref == NULL)) {
        return UBASE_ERR_ALLOC;
    }

    uint8_t *buffer;
    int output_size = -1;
    if (unlikely(!ubase_check(uref_block_write(uref, 0, &output_size,
                                               &buffer)))) {
        uref_free(uref);
        return UBASE_ERR_ALLOC;
    }
    assert(output_size == upipe_msrc->output_size);

    ssize_t ret = read(upipe_msrc->fd, buffer, upipe_msrc->output_size);
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
                return UBASE_ERR_NONE;
            case EBADF:
            case EINVAL:
            case EIO:
            default:
                break;
        }

        upipe_warn_va(upipe, "premature end of segment %"PRIu64,
                      upipe_msrc->fileidx);
        return upipe_msrc_skip(upipe);
    }
    if (unlikely(ret != upipe_msrc->output_size))
        uref_block_resize(uref, 0, ret);
    uref_clock_set_cr_sys(uref, cr_sys);

    upipe_msrc->missing = 0;
    upipe_msrc_output(upipe, uref, &upipe_msrc->upump);
    return UBASE_ERR_NONE;
}

/** @internal @This reads data from the source and outputs it.
 * It is called when the idler triggers (permanent storage mode).
 *
 * @param upump description structure of the read watcher
 */
static void upipe_msrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    int err = upipe_msrc_handle(upipe);
    if (ubase_check(err))
        return;

    upipe_msrc_set_upump(upipe, NULL);
    upipe_throw_error(upipe, err);
    upipe_throw_source_end(upipe);
}

/** @internal @This asks to open the given path.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static void upipe_msrc_open(struct upipe *upipe)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    int err = upipe_msrc_start(upipe);
    if (!ubase_check(err)) {
        upipe_throw_error(upipe, err);
        upipe_throw_source_end(upipe);
        return;
    }

    if (upipe_msrc->upump == NULL) {
        struct upump *upump = upump_alloc_idler(upipe_msrc->upump_mgr,
                                                upipe_msrc_worker, upipe,
                                                upipe->refcount);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return;
        }
        upipe_msrc_set_upump(upipe, upump);
        upump_start(upump);
    }
}

/** @internal @This closes the current input.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static void upipe_msrc_close(struct upipe *upipe)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);

    if (upipe_msrc->fd != -1)
        ubase_clean_fd(&upipe_msrc->fd);
    if (upipe_msrc->aux_file != NULL) {
        fclose(upipe_msrc->aux_file);
        upipe_msrc->aux_file = NULL;
    }

    upipe_msrc_set_upump(upipe, NULL);
}

/** @internal @This builds the flow definition.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static void upipe_msrc_build_flow_def(struct upipe *upipe)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    struct uref *flow_def = uref_dup(upipe_msrc->flow_def_input);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    const char *def;
    if (!ubase_check(uref_flow_get_def(flow_def, &def)))
        uref_flow_set_def(flow_def, "block.");
    uref_block_flow_set_size(flow_def, upipe_msrc->output_size);
    upipe_msrc_require_ubuf_mgr(upipe, flow_def);
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_msrc_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_msrc_store_flow_def(upipe, flow_format);

    upipe_msrc_check_upump_mgr(upipe);
    if (upipe_msrc->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_msrc->uref_mgr == NULL) {
        upipe_msrc_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_msrc->flow_def_input == NULL)
        return UBASE_ERR_NONE;

    if (upipe_msrc->ubuf_mgr == NULL &&
        urequest_get_opaque(&upipe_msrc->ubuf_mgr_request, struct upipe *)
            == NULL) {
        upipe_msrc_build_flow_def(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_msrc->ubuf_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_msrc->pos == UINT64_MAX)
        return UBASE_ERR_NONE;

    if (upipe_msrc->upump == NULL) {
        upipe_msrc_open(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow def.
 *
 * @param upipe description structure of the pipe
 * @param flow_def input flow def
 * @return an error code
 */
static int upipe_msrc_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    const char *path, *data, *aux;
    if (!ubase_check(uref_msrc_flow_get_path(flow_def, &path)) ||
        !ubase_check(uref_msrc_flow_get_data(flow_def, &data)) ||
        !ubase_check(uref_msrc_flow_get_aux(flow_def, &aux)))
        return UBASE_ERR_INVALID;

    upipe_msrc_close(upipe);
    ubuf_mgr_release(upipe_msrc->ubuf_mgr);
    upipe_msrc->ubuf_mgr = NULL;
    uref_free(upipe_msrc->flow_def_input);
    upipe_msrc->flow_def_input = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(upipe_msrc->flow_def_input)
    return UBASE_ERR_NONE;
}

/** @internal @This sets the output size.
 *
 * @param upipe description structure of the pipe
 * @param output_size output size
 * @return an error code
 */
static int _upipe_msrc_set_output_size(struct upipe *upipe,
                                       unsigned int output_size)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    UBASE_RETURN(upipe_msrc_set_output_size(upipe, output_size))

    upipe_msrc_close(upipe);
    ubuf_mgr_release(upipe_msrc->ubuf_mgr);
    upipe_msrc->ubuf_mgr = NULL;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current position.
 *
 * @param upipe description structure of the pipe
 * @param pos_p filled in with the current position
 * @return an error code
 */
static int upipe_msrc_get_position(struct upipe *upipe, uint64_t *pos_p)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    *pos_p = upipe_msrc->pos;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the current position and starts operation.
 *
 * @param upipe description structure of the pipe
 * @param pos new position
 * @return an error code
 */
static int upipe_msrc_set_position(struct upipe *upipe, uint64_t pos)
{
    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    upipe_msrc_close(upipe);
    upipe_msrc->pos = pos;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a multicat source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_msrc_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_msrc_set_upump(upipe, NULL);
            return upipe_msrc_attach_upump_mgr(upipe);
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_msrc_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_msrc_control_output(upipe, command, args);
        case UPIPE_GET_OUTPUT_SIZE:
            return upipe_msrc_control_output_size(upipe, command, args);
        case UPIPE_SET_OUTPUT_SIZE: {
            unsigned int output_size = va_arg(args, unsigned int);
            return _upipe_msrc_set_output_size(upipe, output_size);
        }
        case UPIPE_SRC_SET_POSITION: {
            uint64_t pos = va_arg(args, uint64_t);
            return upipe_msrc_set_position(upipe, pos);
        }
        case UPIPE_SRC_GET_POSITION: {
            uint64_t *p = va_arg(args, uint64_t *);
            return upipe_msrc_get_position(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a multicat source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_msrc_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_msrc_control(upipe, command, args))

    return upipe_msrc_check(upipe, NULL);
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_msrc_free(struct upipe *upipe)
{
    upipe_msrc_close(upipe);

    upipe_throw_dead(upipe);

    struct upipe_msrc *upipe_msrc = upipe_msrc_from_upipe(upipe);
    uref_free(upipe_msrc->flow_def_input);
    upipe_msrc_clean_output_size(upipe);
    upipe_msrc_clean_upump(upipe);
    upipe_msrc_clean_upump_mgr(upipe);
    upipe_msrc_clean_output(upipe);
    upipe_msrc_clean_ubuf_mgr(upipe);
    upipe_msrc_clean_uref_mgr(upipe);
    upipe_msrc_clean_urefcount(upipe);
    upipe_msrc_free_void(upipe);
}

static struct upipe_mgr upipe_msrc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_MSRC_SIGNATURE,

    .upipe_alloc = upipe_msrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_msrc_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for msrc pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_msrc_mgr_alloc(void)
{
    return &upipe_msrc_mgr;
}
