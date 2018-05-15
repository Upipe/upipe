/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe source module PCIE SDI cards
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/urequest.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
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
#include <upipe-pciesdi/upipe_pciesdi_source.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "libsdi.h"
#include "sdi_config.h"

#include "../upipe-hbrmt/sdidec.h"

/** @hidden */
static int upipe_pciesdi_src_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a file source pipe. */
struct upipe_pciesdi_src {
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

    /** lines granularity */
    unsigned lines;

    /** file descriptor */
    int fd;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_pciesdi_src, upipe, UPIPE_PCIESDI_SRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_pciesdi_src, urefcount, upipe_pciesdi_src_free)
UPIPE_HELPER_VOID(upipe_pciesdi_src)

UPIPE_HELPER_OUTPUT(upipe_pciesdi_src, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_pciesdi_src, uref_mgr, uref_mgr_request, upipe_pciesdi_src_check,
                      upipe_pciesdi_src_register_output_request,
                      upipe_pciesdi_src_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_pciesdi_src, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_pciesdi_src_check,
                      upipe_pciesdi_src_register_output_request,
                      upipe_pciesdi_src_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_pciesdi_src, uclock, uclock_request, upipe_pciesdi_src_check,
                    upipe_pciesdi_src_register_output_request,
                    upipe_pciesdi_src_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_pciesdi_src, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_pciesdi_src, upump, upump_mgr)

/** @internal @This allocates a pciesdi source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_pciesdi_src_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe, uint32_t signature,
                                      va_list args)
{
    struct upipe *upipe = upipe_pciesdi_src_alloc_void(mgr, uprobe, signature, args);
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);
    upipe_pciesdi_src_init_urefcount(upipe);
    upipe_pciesdi_src_init_uref_mgr(upipe);
    upipe_pciesdi_src_init_ubuf_mgr(upipe);
    upipe_pciesdi_src_init_output(upipe);
    upipe_pciesdi_src_init_upump_mgr(upipe);
    upipe_pciesdi_src_init_upump(upipe);
    upipe_pciesdi_src_init_uclock(upipe);
    upipe_pciesdi_src->fd = -1;
    upipe_pciesdi_src->lines = 0;
    upipe_throw_ready(upipe);

    return upipe;
}

static const char *sdi_decode_mode(uint8_t mode)
{
    switch (mode) {
    case 0: return "HD";
    case 1: return "SD";
    case 2: return "3G";
    default: return "??";
    }
}

static const char *sdi_decode_family(uint8_t family)
{
    switch (family) {
    case 0: return "SMPTE274:1080P";
    case 1: return "SMPTE296:720P";
    case 2: return "SMPTE2048:1080P";
    case 3: return "SMPTE295:1080P";
    case 8: return "NTSC:486P";
    case 9: return "PAL:576P";
    case 15: return "Unknown";
    default: return "Reserved";
    }
}

static const char *sdi_decode_rate(uint8_t rate)
{
    switch (rate) {
        case 0: return "None";
        case 2: return "23.98";
        case 3: return "24";
        case 4: return "47.95";
        case 5: return "25";
        case 6: return "29.97";
        case 7: return "30";
        case 8: return "48";
        case 9: return "50";
        case 10: return "59.94";
        case 11: return "60";
        default: return "Reserved";
    }
}

static void upipe_sdi_to_planar_10_c(const uint8_t *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t pixels)
{
    for (int i = 0; i < pixels; i += 2) {
        uint8_t a = *src++;
        uint8_t b = *src++;
        uint8_t c = *src++;
        uint8_t d = *src++;
        uint8_t e = *src++;
        uint8_t f = *src++;
        uint8_t g = *src++;
        uint8_t h = *src++;
        *u++ = (b << 8) | a;
        *y++ = (d << 8) | c;
        *v++ = (f << 8) | e;
        *y++ = (h << 8) | g;
    }
}


/** @internal @This reads data from the source and outputs it.
*   @param upump description structure of the read watcher
 */
static void upipe_pciesdi_src_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);

    uint8_t locked, mode, family, rate;
    sdi_rx(upipe_pciesdi_src->fd, &locked, &mode, &family, &rate);

    // TODO : monitor changes
    if (0) upipe_notice_va(upipe, "%s | mode %s | family %s | rate %s",
        locked ? "LOCK" : "",
        sdi_decode_mode(mode),
        sdi_decode_family(family),
        sdi_decode_rate(rate));

    struct uref *uref = uref_pic_alloc(upipe_pciesdi_src->uref_mgr,
                                         upipe_pciesdi_src->ubuf_mgr,
                                         1920, upipe_pciesdi_src->lines);
    UBASE_FATAL_RETURN(upipe, uref ? UBASE_ERR_NONE : UBASE_ERR_ALLOC);


    uint8_t *buf[3];
    size_t stride[3];
    static const char *chroma[3] = { "y10l", "u10l", "v10l" };
    for (int i = 0; i < 3; i++) {
        uref_pic_plane_size(uref, chroma[i], &stride[i], NULL, NULL, NULL);
        uref_pic_plane_write(uref, chroma[i], 0, 0, -1, -1, &buf[i]);
    }

    /* FIXME */
    static int lines; // # of buffered lines
    static uint8_t foo[DMA_BUFFER_SIZE * 18];
    ssize_t ret = read(upipe_pciesdi_src->fd,
            &foo[lines * DMA_BUFFER_SIZE],
            sizeof(foo) - lines * DMA_BUFFER_SIZE);

    if (family == 15 || !locked) {
        ret = -1;
        upipe_err_va(upipe, "unknown family");
    }

    if (unlikely(ret == -1)) {
        for (int i = 0; i < 3; i++)
            uref_pic_plane_unmap(uref, chroma[i], 0, 0, -1, -1);
        uref_free(uref);
        switch (errno) {
            case EINTR:
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                /* not an issue, try again later */
                return;
            default:
                break;
        }
        upipe_err_va(upipe, "couldn't read: %m");
//        upipe_pciesdi_src_set_upump(upipe, NULL);
//        upipe_throw_source_end(upipe);
        return;
    }

    size_t got_lines = ret / DMA_BUFFER_SIZE;

    static bool start;

    {
        bool prev_vbi = false, prev_f2 = false;
        for (int i = 0; i < (ret / DMA_BUFFER_SIZE); i++) {
            uint8_t *l = &foo[(lines + i) * DMA_BUFFER_SIZE];
            uint16_t *f = (uint16_t*)l;

            int line = (f[8] & 0x1ff) >> 2;
            line |= ((f[9] & 0x1ff) >> 2) << 7;
            uint16_t fvh = f[6];
            bool vbi, f2;
            if (fvh == 0x274) {
                f2 = 0;
                vbi = 0;
                if (prev_vbi && !prev_f2) {
                    start = true;
//                    upipe_dbg_va(upipe, "Start on line %d", line);
                }
            } else if (fvh == 0x2d8) {
                f2 = 0;
                vbi = 1;
            } else if (fvh == 0x368) {
                vbi = 0;
                f2 = 1;
            } else if (fvh == 0x3c4) {
                f2 = 1;
                vbi = 1;
            } else {
                upipe_err_va(upipe, "Unknown fvh 0x%x", fvh);
                abort();
            }
            //upipe_dbg_va(upipe, "Line %d | f2 %d | vbi %d", line, f2, vbi);

            if (vbi || !start) {
                if (lines + i < 17)
                    memmove(l, &foo[(lines + i + 1) * DMA_BUFFER_SIZE],
                            (17 - lines - i ) * DMA_BUFFER_SIZE);
                got_lines--;
                i--;
                ret -= DMA_BUFFER_SIZE;
            }

            prev_vbi = vbi;
            prev_f2  = f2;
        }
    }

    if (got_lines + lines < 18) {
        lines += got_lines;
        for (int i = 0; i < 3; i++)
            uref_pic_plane_unmap(uref, chroma[i], 0, 0, -1, -1);
        uref_free(uref);
        return;
    }

    lines = 0;

    for (int i = 0; i < 18; i++) {
        upipe_sdi_to_planar_10_c(&foo[i*DMA_BUFFER_SIZE + 4 * (2200-1920)],
                (uint16_t *)(&buf[0][i*stride[0]]),
                (uint16_t *)(&buf[1][i*stride[1]]),
                (uint16_t *)(&buf[2][i*stride[2]]),
                1920);
    }

//    uref_pic_set_tff(uref);
    static int x;
    uref_pic_set_vposition(uref, x);
    x += 18;
    x %= 540;
    static uint64_t pts_prog;
    uref_clock_set_pts_prog(uref, pts_prog);
    pts_prog += 18 * UCLOCK_FREQ  * 1001 / 30000 / 1080;
    uref_attr_set_unsigned(uref,
            1080, UDICT_TYPE_UNSIGNED, "original_height");
    uref_attr_set_unsigned(uref,
            (18 * UCLOCK_FREQ * 1001 / 30000) / 1080,
            UDICT_TYPE_UNSIGNED, "fraction_duration");

    for (int i = 0; i < 3; i++)
        uref_pic_plane_unmap(uref, chroma[i], 0, 0, -1, -1);

    if (upipe_pciesdi_src->uclock != NULL)
        uref_clock_set_cr_sys(uref, uclock_now(upipe_pciesdi_src->uclock));
    upipe_pciesdi_src_output(upipe, uref, &upipe_pciesdi_src->upump);
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_pciesdi_src_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);
    if (flow_format != NULL) {
        upipe_pciesdi_src_store_flow_def(upipe, flow_format);
    }

    upipe_pciesdi_src_check_upump_mgr(upipe);
    if (upipe_pciesdi_src->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_pciesdi_src->uref_mgr == NULL) {
        upipe_pciesdi_src_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_pciesdi_src->ubuf_mgr == NULL) {
        struct uref *flow_format = uref_alloc(upipe_pciesdi_src->uref_mgr);
        struct urational fps = { 60000, 1001};
        uref_flow_set_def(flow_format, "pic.");
        uref_pic_flow_set_fps(flow_format, fps);
        uref_pic_flow_set_hsize(flow_format, 1920);
        uref_pic_flow_set_vsize(flow_format, 540);
        uref_pic_set_tff(flow_format);
        uref_pic_flow_set_macropixel(flow_format, 1);
        uref_pic_flow_add_plane(flow_format, 1, 1, 2, "y10l");
        uref_pic_flow_add_plane(flow_format, 2, 1, 2, "u10l");
        uref_pic_flow_add_plane(flow_format, 2, 1, 2, "v10l");

        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_pciesdi_src_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_pciesdi_src->uclock == NULL &&
        urequest_get_opaque(&upipe_pciesdi_src->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    if (upipe_pciesdi_src->fd != -1 && upipe_pciesdi_src->upump == NULL) {
        struct upump *upump = upump_alloc_fd_read(upipe_pciesdi_src->upump_mgr,
                                        upipe_pciesdi_src_worker, upipe,
                                        upipe->refcount, upipe_pciesdi_src->fd);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_pciesdi_src_set_upump(upipe, upump);
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
static int upipe_pciesdi_set_uri(struct upipe *upipe, const char *path)
{
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);

    ubase_clean_fd(&upipe_pciesdi_src->fd);
    upipe_pciesdi_src->fd = open(path, O_RDWR);
    if (unlikely(upipe_pciesdi_src->fd < 0)) {
        upipe_err_va(upipe, "can't open %s (%m)", path);
        return UBASE_ERR_EXTERNAL;
    }

    uint8_t locked, mode, family, rate;
    sdi_rx(upipe_pciesdi_src->fd, &locked, &mode, &family, &rate);

    upipe_notice_va(upipe, "%s | mode %s | family %s | rate %s",
        locked ? "LOCK" : "",
        sdi_decode_mode(mode),
        sdi_decode_family(family),
        sdi_decode_rate(rate));

    sdi_dma_loopback(upipe_pciesdi_src->fd, 0); // disable loopback
    int64_t hw, sw;
    sdi_dma_writer(upipe_pciesdi_src->fd, 1, &hw, &sw); // enable

    return UBASE_ERR_NONE;
}

static void upipe_pciesdi_src_close(struct upipe *upipe)
{
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);

    ubase_clean_fd(&upipe_pciesdi_src->fd);
    upipe_pciesdi_src_set_upump(upipe, NULL);
}

/** @internal @This sets the content of a pciesdi_src option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return an error code
 */
static int upipe_pciesdi_src_set_option(struct upipe *upipe,
                                   const char *k, const char *v)
{
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);
    assert(k != NULL);

    if (unlikely(upipe_pciesdi_src->fd != -1))
        upipe_pciesdi_src_close(upipe);

    if (!strcmp(k, "slice_height"))
        upipe_pciesdi_src->lines = atoi(v);
    else 
        return UBASE_ERR_INVALID;

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_pciesdi_src_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_pciesdi_src_set_upump(upipe, NULL);
            return upipe_pciesdi_src_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_pciesdi_src_set_upump(upipe, NULL);
            upipe_pciesdi_src_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_pciesdi_src_control_output(upipe, command, args);

        case UPIPE_SET_URI: {
            const char *path = va_arg(args, const char *);
            return upipe_pciesdi_set_uri(upipe, path);
        }

        case UPIPE_SET_OPTION: {
            const char *k = va_arg(args, const char *);
            const char *v = va_arg(args, const char *);
            return upipe_pciesdi_src_set_option(upipe, k, v);
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
static int upipe_pciesdi_src_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_pciesdi_src_control(upipe, command, args))

    return upipe_pciesdi_src_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_pciesdi_src_free(struct upipe *upipe)
{
    upipe_pciesdi_src_close(upipe);

    upipe_throw_dead(upipe);

    upipe_pciesdi_src_clean_uclock(upipe);
    upipe_pciesdi_src_clean_upump(upipe);
    upipe_pciesdi_src_clean_upump_mgr(upipe);
    upipe_pciesdi_src_clean_output(upipe);
    upipe_pciesdi_src_clean_ubuf_mgr(upipe);
    upipe_pciesdi_src_clean_uref_mgr(upipe);
    upipe_pciesdi_src_clean_urefcount(upipe);
    upipe_pciesdi_src_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_pciesdi_src_mgr = {
    .refcount = NULL,
    .signature = UPIPE_PCIESDI_SRC_SIGNATURE,

    .upipe_alloc = upipe_pciesdi_src_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_pciesdi_src_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all file source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_pciesdi_src_mgr_alloc(void)
{
    return &upipe_pciesdi_src_mgr;
}
