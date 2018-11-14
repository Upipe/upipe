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

#include <config.h>

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/urequest.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_block.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_flow.h>
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

#include "../upipe-hbrmt/upipe_hbrmt_common.h"

static void levelb_unpack(const uint16_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);
void upipe_sdi3g_levelb_unpack_sse2(const uint16_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);

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

    /** file descriptor */
    int fd;

    int previous_sdi_line_number;
    bool discontinuity;

    /* picture properties, same units as upipe_hbrmt_common.h, pixels */
    const struct sdi_offsets_fmt *sdi_format;
    bool sdi3g_levelb;

    int cached_read_bytes;
    uint8_t *read_buffer;

    void (*sdi3g_levelb_unpack)(const uint16_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);

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

    upipe_pciesdi_src->read_buffer = malloc(DMA_BUFFER_SIZE * DMA_BUFFER_COUNT);
    if (!upipe_pciesdi_src->read_buffer)
        return NULL;

    upipe_pciesdi_src->sdi3g_levelb_unpack = levelb_unpack;
#if defined(HAVE_X86ASM)
#if defined(__i686__) || defined(__x86_64__)
    if (__builtin_cpu_supports("sse2")) {
        upipe_pciesdi_src->sdi3g_levelb_unpack = upipe_sdi3g_levelb_unpack_sse2;
    }
#endif
#endif

    upipe_pciesdi_src->sdi3g_levelb = false;
    upipe_pciesdi_src->discontinuity = false;
    upipe_pciesdi_src->fd = -1;
    upipe_pciesdi_src->previous_sdi_line_number = -1;
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

static const char *sdi_decode_scan(uint8_t scan)
{
    switch (scan) {
    case 0: return "I";
    case 1: return "P";
    default: return "?";
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

static inline bool sdi3g_levelb_eav_match(const uint16_t *src)
{
    if (src[0] == 0x3ff
            && src[1] == 0x3ff
            && src[2] == 0x3ff
            && src[3] == 0x3ff
            && src[4] == 0x000
            && src[5] == 0x000
            && src[6] == 0x000
            && src[7] == 0x000
            && src[8] == 0x000
            && src[9] == 0x000
            && src[10] == 0x000
            && src[11] == 0x000
            && src[12] == src[13]
            && src[12] == src[14]
            && src[12] == src[15]
            && (src[12] == 0x274
                || src[12] == 0x2d8
                || src[12] == 0x368
                || src[12] == 0x3c4))
        return true;
    return false;
}

static inline bool sdi3g_levelb_sav_match(const uint16_t *src)
{
    if (src[-16] == 0x3ff
            && src[-15] == 0x3ff
            && src[-14] == 0x3ff
            && src[-13] == 0x3ff
            && src[-12] == 0x000
            && src[-11] == 0x000
            && src[-10] == 0x000
            && src[-9] == 0x000
            && src[-8] == 0x000
            && src[-7] == 0x000
            && src[-6] == 0x000
            && src[-5] == 0x000
            && src[-4] == src[-3]
            && src[-4] == src[-2]
            && src[-4] == src[-1]
            && (src[-4] == 0x200
                || src[-4] == 0x2ac
                || src[-4] == 0x31c
                || src[-4] == 0x3b0))
        return true;
    return false;
}

static void levelb_unpack(const uint16_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels)
{
    for (int i = 0; i < pixels; i++) {
        dst1[2*i + 0] = src[4*i + 0];
        dst1[2*i + 1] = src[4*i + 1];
        dst2[2*i + 0] = src[4*i + 2];
        dst2[2*i + 1] = src[4*i + 3];
    }
}

static void dump_and_exit_clean(struct upipe *upipe, uint8_t *buf, size_t size)
{
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);
    int64_t hw, sw;
    sdi_dma_writer(upipe_pciesdi_src->fd, 0, &hw, &sw); // disable
    sdi_release_dma_writer(upipe_pciesdi_src->fd); // release old locks
    close(upipe_pciesdi_src->fd);

    if (buf) {
        FILE *fh = fopen("dump.bin", "wb");
        if (!fh) {
            upipe_err(upipe, "could not open dump file");
            abort();
        }
        fwrite(buf, 1, size, fh);
        fclose(fh);
        upipe_dbg(upipe, "dumped to dump.bin");
    }

    abort();
}

/** @internal @This reads data from the source and outputs it.
*   @param upump description structure of the read watcher
 */
static void upipe_pciesdi_src_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);

    uint8_t locked, mode, family, scan, rate;
    sdi_rx(upipe_pciesdi_src->fd, &locked, &mode, &family, &scan, &rate);

    // TODO : monitor changes
    if (0) upipe_notice_va(upipe, "%s | mode %s | family %s | scan %s | rate %s",
        locked ? "LOCK" : "",
        sdi_decode_mode(mode),
        sdi_decode_family(family),
        sdi_decode_scan(scan),
        sdi_decode_rate(rate));

    int sdi_line_width = upipe_pciesdi_src->sdi_format->width * 4;
    if (upipe_pciesdi_src->sdi3g_levelb)
        sdi_line_width *= 2;

    struct uref *uref = uref_block_alloc(upipe_pciesdi_src->uref_mgr,
            upipe_pciesdi_src->ubuf_mgr, DMA_BUFFER_SIZE * DMA_BUFFER_COUNT);
    UBASE_FATAL_RETURN(upipe, uref ? UBASE_ERR_NONE : UBASE_ERR_ALLOC);

    uint8_t *block_buf;
    int block_size = -1;
    if (!ubase_check(uref_block_write(uref, 0, &block_size, &block_buf))) {
        upipe_err(upipe, "unable to map block for writing");
        dump_and_exit_clean(upipe, NULL, 0);
    }

    ssize_t ret = read(upipe_pciesdi_src->fd,
            upipe_pciesdi_src->read_buffer + upipe_pciesdi_src->cached_read_bytes,
            DMA_BUFFER_SIZE * DMA_BUFFER_COUNT/2);

    if (family == 15 || !locked) {
        ret = -1;
        upipe_err_va(upipe, "unknown family");
    }

    if (!locked)
        upipe_pciesdi_src->discontinuity = true;

    if (unlikely(ret == -1)) {
        uref_block_unmap(uref, 0);
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

    ret += upipe_pciesdi_src->cached_read_bytes;

    for (int i = 0; i < ret / sdi_line_width; i++) {
        const uint16_t *sdi_line = (uint16_t*)(upipe_pciesdi_src->read_buffer + i * sdi_line_width);
        int active_offset = 2 * upipe_pciesdi_src->sdi_format->active_offset;
        if (upipe_pciesdi_src->sdi3g_levelb)
            active_offset *= 2;
        const uint16_t *active_start = sdi_line + active_offset;

        if (upipe_pciesdi_src->sdi_format->pict_fmt->sd) {
            /* Check EAV is present. */
            if (!sd_eav_match(sdi_line)) {
                upipe_err_va(upipe, "SD EAV not found at %#x", i * sdi_line_width);
            }

            /* Check SAV is present. */
            if (!sd_sav_match(active_start)) {
                upipe_err_va(upipe, "SD SAV not found at %#x", i * sdi_line_width + active_offset);
            }
        } else if (upipe_pciesdi_src->sdi3g_levelb) {
            /* Check EAV is present. */
            if (!sdi3g_levelb_eav_match(sdi_line)) {
                upipe_err_va(upipe, "SDI-3G level B EAV not found at %#x", i * sdi_line_width);
            }

            /* Check SAV is present. */
            if (!sdi3g_levelb_sav_match(active_start)) {
                upipe_err_va(upipe, "SDI-3G level B SAV not found at %#x", i * sdi_line_width + active_offset);
            }

            /* Check line number. */
            int line = (sdi_line[16] & 0x1ff) >> 2;
            line |= ((sdi_line[20] & 0x1ff) >> 2) << 7;
            if (line > upipe_pciesdi_src->sdi_format->height  || line < 1) {
                upipe_err_va(upipe, "line %d out of range (1-%d)", line,
                        upipe_pciesdi_src->sdi_format->height);
            }

            /* Check line number is increasing correctly. */
            if (upipe_pciesdi_src->previous_sdi_line_number != upipe_pciesdi_src->sdi_format->height
                    && line != upipe_pciesdi_src->previous_sdi_line_number + 1) {
                upipe_warn_va(upipe, "sdi_line_number not linearly increasing (%d -> %d)",
                        upipe_pciesdi_src->previous_sdi_line_number, line);
            }
            upipe_pciesdi_src->previous_sdi_line_number = line;
        } else { /* HD */
            /* Check EAV is present. */
            if (!hd_eav_match(sdi_line)) {
                upipe_err_va(upipe, "HD EAV not found at %#x", i * sdi_line_width);
            }

            /* Check SAV is present. */
            if (!hd_sav_match(active_start)) {
                upipe_err_va(upipe, "HD SAV not found at %#x", i * sdi_line_width + active_offset);
            }

            /* Check line number. */
            int line = (sdi_line[8] & 0x1ff) >> 2;
            line |= ((sdi_line[10] & 0x1ff) >> 2) << 7;
            if (line > upipe_pciesdi_src->sdi_format->height  || line < 1) {
                upipe_err_va(upipe, "line %d out of range (1-%d)", line,
                        upipe_pciesdi_src->sdi_format->height);
            }

            /* Check line number is increasing correctly. */
            if (upipe_pciesdi_src->previous_sdi_line_number != upipe_pciesdi_src->sdi_format->height
                    && line != upipe_pciesdi_src->previous_sdi_line_number + 1) {
                upipe_warn_va(upipe, "sdi_line_number not linearly increasing (%d -> %d)",
                        upipe_pciesdi_src->previous_sdi_line_number, line);
            }
            upipe_pciesdi_src->previous_sdi_line_number = line;
        } /* end HD */

        /* Copy line to output uref. */
        if (upipe_pciesdi_src->sdi3g_levelb) {
            /* Note: line order is swapped. */
            uint16_t *dst1 = block_buf + (2*i + 1) * sdi_line_width/2;
            uint16_t *dst2 = block_buf + (2*i + 0) * sdi_line_width/2;
            upipe_pciesdi_src->sdi3g_levelb_unpack(sdi_line, dst1, dst2,
                    upipe_pciesdi_src->sdi_format->width);
        } else {
            memcpy(block_buf + i * sdi_line_width, sdi_line, sdi_line_width);
        }
    }

    int processed_bytes = (ret / sdi_line_width) * sdi_line_width;
    if (ret != processed_bytes) {
        memmove(upipe_pciesdi_src->read_buffer,
                upipe_pciesdi_src->read_buffer + processed_bytes,
                ret - processed_bytes);
        upipe_pciesdi_src->cached_read_bytes = ret - processed_bytes;
    } else {
        upipe_pciesdi_src->cached_read_bytes = 0;
    }

    if (upipe_pciesdi_src->discontinuity) {
        uref_flow_set_discontinuity(uref);
        upipe_pciesdi_src->discontinuity = false;
    }

    if (upipe_pciesdi_src->sdi3g_levelb)
        uref_block_set_sdi3g_levelb(uref);

    uref_block_unmap(uref, 0);
    uref_block_resize(uref, 0, processed_bytes);
    upipe_pciesdi_src_output(upipe, uref, &upipe_pciesdi_src->upump);
}

static int get_flow_def(struct upipe *upipe, struct uref **flow_format)
{
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);

    if (upipe_pciesdi_src->fd == -1) {
        upipe_err(upipe, "no open file descriptor");
        return UBASE_ERR_INVALID;
    }

    uint8_t locked, mode, family, scan, rate;
    sdi_rx(upipe_pciesdi_src->fd, &locked, &mode, &family, &scan, &rate);
    upipe_dbg_va(upipe, "locked: %d, mode: %s (%d), family: %s (%d), scan: %s (%d), rate: %s (%d)",
            locked,
            sdi_decode_mode(mode), mode,
            sdi_decode_family(family), family,
            sdi_decode_scan(scan), scan,
            sdi_decode_rate(rate), rate);

    if (!locked) {
        upipe_err(upipe, "SDI signal not locked");
        return UBASE_ERR_INVALID;
    }

    int width, height;
    bool interlaced, sdi3g_levelb = false;
    struct urational fps;

    /* set width and height */
    if (family == 0) {
        /* SMPTE274:1080 */
        width = 1920;
        height = 1080;
    } else if (family == 1) {
        /* SMPTE296:720 */
        width = 1280;
        height = 720;
    } else if (family == 8) {
        /* NTSC:486 */
        width = 720;
        height = 486;
    } else if (family == 9) {
        /* PAL:576 */
        width = 720;
        height = 576;
    } else {
        upipe_err_va(upipe, "invalid/unknown family value: %s (%d)", sdi_decode_family(family), family);
        return UBASE_ERR_INVALID;
    }

    /* set framerate */
    static const struct urational framerates[] = {
        { 24000, 1001 },
        { 24, 1 },
        { 48000, 1001 },
        { 25, 1 },
        { 30000, 1001 },
        { 30, 1 },
        { 48, 1 },
        { 50, 1 },
        { 60000, 1001 },
        { 60, 1 }
    };

    if (rate >= 2 && rate <= 11)
        fps = framerates[rate - 2];
    else {
        upipe_err_va(upipe, "invalid/unknown rate value: %s (%d)", sdi_decode_rate(rate), rate);
        return UBASE_ERR_INVALID;
    }

    /* Check for SDI-3G level B. */
    if (mode == 2 && scan == 0) {
        upipe_pciesdi_src->sdi3g_levelb = sdi3g_levelb = true;
        interlaced = false;
        fps.num *= 2;
    } else if (scan == 0) {
        /* interlaced */
        interlaced = true;
    } else if (scan == 1) {
        /* progressive */
        interlaced = false;
    } else {
        upipe_err_va(upipe, "invalid/unknown scan value: %s (%d)", sdi_decode_scan(scan), scan);
        return UBASE_ERR_INVALID;
    }

    struct uref *flow_def = uref_alloc(upipe_pciesdi_src->uref_mgr);
    if (!flow_def)
        return UBASE_ERR_ALLOC;

    UBASE_RETURN(uref_flow_set_def(flow_def, "block."));
    UBASE_RETURN(uref_pic_flow_set_fps(flow_def, fps));
    UBASE_RETURN(uref_pic_flow_set_hsize(flow_def, width));
    UBASE_RETURN(uref_pic_flow_set_vsize(flow_def, height));
    if (interlaced) {
        UBASE_RETURN(uref_pic_set_tff(flow_def));
    } else {
        UBASE_RETURN(uref_pic_set_progressive(flow_def));
    }

    if (sdi3g_levelb)
        UBASE_RETURN(uref_block_set_sdi3g_levelb(flow_def));

    upipe_pciesdi_src->sdi_format = sdi_get_offsets(flow_def);
    if (!upipe_pciesdi_src->sdi_format) {
        upipe_err(upipe, "unable to get SDI offsets/picture format");
        uref_dump(flow_def, upipe->uprobe);
        return UBASE_ERR_INVALID;
    }

    if (upipe_pciesdi_src->sdi_format->pict_fmt->active_f2.start && interlaced == false)
        upipe_warn(upipe, "SDI signal is progressive but interlaced sdi_offset struct returned");
    else if (!upipe_pciesdi_src->sdi_format->pict_fmt->active_f2.start && interlaced == true)
        upipe_warn(upipe, "SDI signal is interlaced but progressive sdi_offset struct returned");

    *flow_format = flow_def;
    return UBASE_ERR_NONE;
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
        struct uref *flow_def;
        int ret = get_flow_def(upipe, &flow_def);
        if (!ubase_check(ret)) {
            upipe_throw_fatal(upipe, ret);
            return ret;
        }
        upipe_pciesdi_src_require_ubuf_mgr(upipe, flow_def);
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

    int64_t hw, sw;
    sdi_dma_writer(upipe_pciesdi_src->fd, 0, &hw, &sw); // disable
    sdi_release_dma_writer(upipe_pciesdi_src->fd); // release old locks

    close(upipe_pciesdi_src->fd);
    upipe_pciesdi_src->fd = open(path, O_RDWR);
    if (unlikely(upipe_pciesdi_src->fd < 0)) {
        upipe_err_va(upipe, "can't open %s (%m)", path);
        return UBASE_ERR_EXTERNAL;
    }

    /* request dma */
    if (sdi_request_dma_writer(upipe_pciesdi_src->fd) == 0) {
        upipe_err(upipe, "DMA not available");
        return UBASE_ERR_EXTERNAL;
    }

    uint8_t locked, mode, family, scan, rate;
    sdi_rx(upipe_pciesdi_src->fd, &locked, &mode, &family, &scan, &rate);

    upipe_notice_va(upipe, "%s | mode %s | family %s | scan %s | rate %s",
        locked ? "LOCK" : "",
        sdi_decode_mode(mode),
        sdi_decode_family(family),
        sdi_decode_scan(scan),
        sdi_decode_rate(rate));

    sdi_dma(upipe_pciesdi_src->fd, 0, 0, 0); // disable loopback
    sdi_dma_writer(upipe_pciesdi_src->fd, 1, &hw, &sw); // enable

    return UBASE_ERR_NONE;
}

static void upipe_pciesdi_src_close(struct upipe *upipe)
{
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);

    int64_t hw, sw;
    sdi_dma_writer(upipe_pciesdi_src->fd, 0, &hw, &sw); // disable
    sdi_release_dma_writer(upipe_pciesdi_src->fd); // release old locks

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

    return UBASE_ERR_INVALID;
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
