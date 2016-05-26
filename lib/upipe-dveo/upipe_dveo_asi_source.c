/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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
 * @short Upipe source module DVEO ASI cards
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
#include <upipe/upipe_helper_output_size.h>
#include <upipe-dveo/upipe_dveo_asi_source.h>

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

const char dev_fmt[] = "/dev/asirx%u";
const char sys_fmt[] = "/sys/class/asi/asirx%u/%s";
const char dvbm_sys_fmt[] = "/sys/class/dvbm/%u/%s";

/** default size of buffers when unspecified, extra 8-byte timestamp on capture */
#define BYPASS_MODE           (1)
#define CAPTURE_DEFAULT_SIZE  ((188+8)*112)
#define RX_DEFAULT_SIZE       (188*7)
#define BUFFERS               (2)
#define OPERATING_MODE        (1)
#define TIMESTAMP_MODE        (2)
#define TS_PACKETS            (7)
#define MAX_DELAY             (UCLOCK_FREQ/10)

/** @hidden */
static int upipe_dveo_asi_src_check(struct upipe *upipe, struct uref *flow_format);

static int upipe_dveo_asi_src_open(struct upipe *upipe);

/** @internal @This is the private context of a file source pipe. */
struct upipe_dveo_asi_src {
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
    unsigned int output_size;

    /** card index **/
    int card_idx;

    /** file descriptor */
    int fd;

    /** last timestamp **/
    int64_t last_ts;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dveo_asi_src, upipe, UPIPE_DVEO_ASI_SRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_dveo_asi_src, urefcount, upipe_dveo_asi_src_free)
UPIPE_HELPER_VOID(upipe_dveo_asi_src)

UPIPE_HELPER_OUTPUT(upipe_dveo_asi_src, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_dveo_asi_src, uref_mgr, uref_mgr_request, upipe_dveo_asi_src_check,
                      upipe_dveo_asi_src_register_output_request,
                      upipe_dveo_asi_src_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_dveo_asi_src, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_dveo_asi_src_check,
                      upipe_dveo_asi_src_register_output_request,
                      upipe_dveo_asi_src_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_dveo_asi_src, uclock, uclock_request, upipe_dveo_asi_src_check,
                    upipe_dveo_asi_src_register_output_request,
                    upipe_dveo_asi_src_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_dveo_asi_src, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_dveo_asi_src, upump, upump_mgr)
UPIPE_HELPER_OUTPUT_SIZE(upipe_dveo_asi_src, output_size)

/* From the example code */
static ssize_t util_read(const char *name, char *buf, size_t count)
{
    ssize_t fd, ret;

    if ((fd = open (name, O_RDONLY)) < 0) {
        return fd;
    }
    ret = read (fd, buf, count);
    close (fd);
    return ret;
}

static ssize_t util_write(const char *name, const char *buf, size_t count)
{
    ssize_t fd, ret;

    if ((fd = open (name, O_WRONLY)) < 0) {
        return fd;
    }
    ret = write (fd, buf, count);
    close (fd);
    return ret;
}

/** @internal @This allocates a dveo_asi source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_dveo_asi_src_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe, uint32_t signature,
                                      va_list args)
{
    struct upipe *upipe = upipe_dveo_asi_src_alloc_void(mgr, uprobe, signature, args);
    struct upipe_dveo_asi_src *upipe_dveo_asi_src = upipe_dveo_asi_src_from_upipe(upipe);
    upipe_dveo_asi_src_init_urefcount(upipe);
    upipe_dveo_asi_src_init_uref_mgr(upipe);
    upipe_dveo_asi_src_init_ubuf_mgr(upipe);
    upipe_dveo_asi_src_init_output(upipe);
    upipe_dveo_asi_src_init_upump_mgr(upipe);
    upipe_dveo_asi_src_init_upump(upipe);
    upipe_dveo_asi_src_init_uclock(upipe);
    upipe_dveo_asi_src_init_output_size(upipe, RX_DEFAULT_SIZE);
    upipe_dveo_asi_src->fd = -1;
    upipe_dveo_asi_src->card_idx = 0;
    upipe_dveo_asi_src->last_ts = -1;
    upipe_throw_ready(upipe);

    //upipe_dveo_asi_src_check(upipe, NULL);

    return upipe;
}

/** @internal @This reads data from the source and outputs it.
*   @param upump description structure of the read watcher
 */
static void upipe_dveo_asi_src_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_dveo_asi_src *upipe_dveo_asi_src = upipe_dveo_asi_src_from_upipe(upipe);
    uint64_t systime = 0, first_ts = UINT64_MAX;
    if (upipe_dveo_asi_src->uclock != NULL)
        systime = uclock_now(upipe_dveo_asi_src->uclock);

    struct uref *uref = uref_block_alloc(upipe_dveo_asi_src->uref_mgr,
                                         upipe_dveo_asi_src->ubuf_mgr,
                                         CAPTURE_DEFAULT_SIZE);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *buffer;
    int output_size = -1;
    if (unlikely(!ubase_check(uref_block_write(uref, 0, &output_size,
                                               &buffer)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    ssize_t ret = read(upipe_dveo_asi_src->fd, buffer, CAPTURE_DEFAULT_SIZE);
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
        upipe_err_va(upipe, "read error from device %i", upipe_dveo_asi_src->card_idx);
        upipe_dveo_asi_src_set_upump(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }

    if (upipe_dveo_asi_src->uclock != NULL)
        uref_clock_set_cr_sys(uref, systime);
    if (unlikely(ret != CAPTURE_DEFAULT_SIZE))
        uref_block_resize(uref, 0, ret);


    while (ret > 0) {
        int discontinuity;
        uint64_t ts;
        uint8_t tmp[8];
        const uint8_t *ptr = uref_block_peek(uref, 0, 8, tmp);
        ts = ((uint64_t)ptr[7] << 56) | ((uint64_t)ptr[6] << 48) | ((uint64_t)ptr[5] << 40) | ((uint64_t)ptr[4] << 32) |
             ((uint64_t)ptr[3] << 24) | ((uint64_t)ptr[2] << 16) | ((uint64_t)ptr[1] <<  8) | ((uint64_t)ptr[0] <<  0);
        discontinuity = ts < upipe_dveo_asi_src->last_ts;
        upipe_dveo_asi_src->last_ts = ts;
        uref_block_peek_unmap(uref, 0, tmp, ptr);

        /* Latter condition can happen if buffer contains data from before
         * cable unplug and after replug. Causes cr_sys with long delays */
        if (first_ts == UINT64_MAX || ts - first_ts > MAX_DELAY)
            first_ts = ts;

        /* Delete rest of timestamps */
        for (int i = 0; i < TS_PACKETS; i++)
            uref_block_delete(uref, 188*i, 8);

        struct uref *output;
        if (ret > (188+8)*TS_PACKETS) {
            output = uref_block_splice(uref, 0, 188*TS_PACKETS);
            if (unlikely(output == NULL)) {
                uref_free(uref);
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return;
            }
        }
        else
            output = uref;

        uref_clock_set_cr_sys(output, systime + (ts - first_ts));
        upipe_dveo_asi_src_output(upipe, output, &upipe_dveo_asi_src->upump);

        if (ret > (188+8)*TS_PACKETS)
            uref_block_delete(uref, 0, 188*TS_PACKETS);
        ret -= (188+8)*TS_PACKETS;
    }
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_dveo_asi_src_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_dveo_asi_src *upipe_dveo_asi_src = upipe_dveo_asi_src_from_upipe(upipe);
    if (flow_format != NULL) {
        uref_flow_set_def(flow_format, "block.mpegtsaligned.");
        upipe_dveo_asi_src_store_flow_def(upipe, flow_format);
    }

    upipe_dveo_asi_src_check_upump_mgr(upipe);
    if (upipe_dveo_asi_src->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_dveo_asi_src->uref_mgr == NULL) {
        upipe_dveo_asi_src_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_dveo_asi_src->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_dveo_asi_src->uref_mgr, NULL);
        uref_block_flow_set_size(flow_format, CAPTURE_DEFAULT_SIZE);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_dveo_asi_src_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_dveo_asi_src->uclock == NULL &&
        urequest_get_opaque(&upipe_dveo_asi_src->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    if (upipe_dveo_asi_src->fd != -1 && upipe_dveo_asi_src->upump == NULL) {
        struct upump *upump = upump_alloc_fd_read(upipe_dveo_asi_src->upump_mgr,
                                        upipe_dveo_asi_src_worker, upipe,
                                        upipe->refcount, upipe_dveo_asi_src->fd);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_dveo_asi_src_set_upump(upipe, upump);
        upump_start(upump);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given device.
 *
 * @param upipe description structure of the pipe
 * @param path relative or absolute path of the file
 * @return an error code
 */
static int upipe_dveo_asi_src_open(struct upipe *upipe)
{
    struct upipe_dveo_asi_src *upipe_dveo_asi_src = upipe_dveo_asi_src_from_upipe(upipe);
    char path[20], sys[50], buf[20];
    int ret, granularity;

    snprintf(sys, sizeof(sys), dvbm_sys_fmt, upipe_dveo_asi_src->card_idx, "bypass_mode");
    snprintf(buf, sizeof(buf), "%u", BYPASS_MODE);
    util_write(sys, buf, sizeof(buf)); /* Not all cards have this so don't fail */

    snprintf(sys, sizeof(sys), sys_fmt, upipe_dveo_asi_src->card_idx, "granularity");
    if (util_read(sys, buf, 1) < 0) {
        upipe_err_va(upipe, "Couldn't read granularity");
        return UBASE_ERR_EXTERNAL;
    }

    buf[1] = '\0';
    granularity = atoi(buf);

    snprintf(sys, sizeof(sys), sys_fmt, upipe_dveo_asi_src->card_idx, "buffers");
    snprintf(buf, sizeof(buf), "%u", BUFFERS);
    if (util_write(sys, buf, sizeof(buf)) < 0) {
        upipe_err_va(upipe, "Couldn't set buffers");
        return UBASE_ERR_EXTERNAL;
    }

    snprintf(sys, sizeof(sys), sys_fmt, upipe_dveo_asi_src->card_idx, "bufsize");
    snprintf(buf, sizeof(buf), "%u", CAPTURE_DEFAULT_SIZE);
    if (util_write(sys, buf, sizeof(buf)) < 0) {
        upipe_err_va(upipe, "Couldn't set buffer size");
        return UBASE_ERR_EXTERNAL;
    }

    snprintf(sys, sizeof(sys), sys_fmt, upipe_dveo_asi_src->card_idx, "mode");
    snprintf(buf, sizeof(buf), "%u", OPERATING_MODE);
    if (util_write(sys, buf, sizeof(buf)) < 0) {
        upipe_err_va(upipe, "Couldn't set mode");
        return UBASE_ERR_EXTERNAL;
    }

    snprintf(sys, sizeof(sys), sys_fmt, upipe_dveo_asi_src->card_idx, "timestamps");
    snprintf(buf, sizeof(buf), "%u", TIMESTAMP_MODE);
    if (util_write(sys, buf, sizeof(buf)) < 0) {
        upipe_err_va(upipe, "Couldn't set timestamp mode");
        return UBASE_ERR_EXTERNAL;
    }

    snprintf(path, sizeof(path), dev_fmt, upipe_dveo_asi_src->card_idx);
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (unlikely(fd < 0)) {
        upipe_err_va(upipe, "can't open file %s (%m)", path);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_dveo_asi_src->fd = fd;
    upipe_notice_va(upipe, "opening file %s", path);
    return UBASE_ERR_NONE;
}

static void upipe_dveo_asi_src_close(struct upipe *upipe)
{
    struct upipe_dveo_asi_src *upipe_dveo_asi_src = upipe_dveo_asi_src_from_upipe(upipe);

    if (unlikely(upipe_dveo_asi_src->fd != -1)) {
        upipe_notice_va(upipe, "closing card %i", upipe_dveo_asi_src->card_idx);
        ubase_clean_fd(&upipe_dveo_asi_src->fd);
    }
    upipe_dveo_asi_src_set_upump(upipe, NULL);
}

/** @internal @This sets the content of a dveo_asi_src option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return an error code
 */
static int upipe_dveo_asi_src_set_option(struct upipe *upipe,
                                   const char *k, const char *v)
{
    struct upipe_dveo_asi_src *upipe_dveo_asi_src = upipe_dveo_asi_src_from_upipe(upipe);
    assert(k != NULL);

    if (unlikely(upipe_dveo_asi_src->fd != -1))
        upipe_dveo_asi_src_close(upipe);

    if (!strcmp(k, "card-idx"))
        upipe_dveo_asi_src->card_idx = atoi(v);

    upipe_dveo_asi_src_open(upipe);

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_dveo_asi_src_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_dveo_asi_src_set_upump(upipe, NULL);
            return upipe_dveo_asi_src_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_dveo_asi_src_set_upump(upipe, NULL);
            upipe_dveo_asi_src_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_dveo_asi_src_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_dveo_asi_src_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_dveo_asi_src_set_output(upipe, output);
        }
        case UPIPE_SET_OPTION: {
            const char *k = va_arg(args, const char *);
            const char *v = va_arg(args, const char *);
            return upipe_dveo_asi_src_set_option(upipe, k, v);
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
static int upipe_dveo_asi_src_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_dveo_asi_src_control(upipe, command, args))

    return upipe_dveo_asi_src_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dveo_asi_src_free(struct upipe *upipe)
{
    upipe_dveo_asi_src_close(upipe);

    upipe_throw_dead(upipe);

    upipe_dveo_asi_src_clean_output_size(upipe);
    upipe_dveo_asi_src_clean_uclock(upipe);
    upipe_dveo_asi_src_clean_upump(upipe);
    upipe_dveo_asi_src_clean_upump_mgr(upipe);
    upipe_dveo_asi_src_clean_output(upipe);
    upipe_dveo_asi_src_clean_ubuf_mgr(upipe);
    upipe_dveo_asi_src_clean_uref_mgr(upipe);
    upipe_dveo_asi_src_clean_urefcount(upipe);
    upipe_dveo_asi_src_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_dveo_asi_src_mgr = {
    .refcount = NULL,
    .signature = UPIPE_DVEO_ASI_SRC_SIGNATURE,

    .upipe_alloc = upipe_dveo_asi_src_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_dveo_asi_src_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all file source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dveo_asi_src_mgr_alloc(void)
{
    return &upipe_dveo_asi_src_mgr;
}
