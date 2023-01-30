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
#include <upipe/uref_block_flow.h>
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

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "csr.h"
#include "flags.h"
#include "libsdi.h"
#include "sdi_config.h"
#include "sdi.h"

#include "levelb.h"
#include "../upipe-hbrmt/upipe_hbrmt_common.h"
#include "../upipe-hbrmt/sdidec.h"

#include "x86/avx512.h"

enum upipe_pciesdi_src_err {
    /** No RX signal or signal is not locked. */
    UPIPE_PCIESDI_SRC_ERR_NOSIGNAL = UBASE_ERR_LOCAL,
};

/** @hidden */
static int upipe_pciesdi_src_check(struct upipe *upipe, struct uref *flow_format);
static int get_flow_def(struct upipe *upipe, struct uref **flow_format);

static inline bool need_init_hardware(uint32_t capability_flags)
{
    return !!(capability_flags & (SDI_CAP_HAS_GS12281 | SDI_CAP_HAS_GS12241));
}

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
    /** format watcher */
    struct upump *format_watcher;

    /** file descriptor */
    int fd;
    /* device number, read from URI */
    int device_number;
    /* bitfield of card features from driver */
    uint32_t capability_flags;

    /* picture properties, same units as upipe_hbrmt_common.h, pixels */
    const struct sdi_offsets_fmt *sdi_format;
    /* picture properties as read from card */
    int mode, family, scan, rate;
    /* input is level B */
    bool sdi3g_levelb;
    /* discontinuity needs to be flagged on next output */
    bool discontinuity;

    /* the mmap pointer */
    uint8_t *read_buffer;

    /* level B unpack function */
    void (*levelb_to_uyvy)(const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);
    /* normal sdi unpack function */
    void (*sdi_to_uyvy)(const uint8_t *src, uint16_t *y, uintptr_t pixels);

    /** public upipe structure */
    struct upipe upipe;

    /** bytes in scratch buffer */
    int scratch_buffer_count;

    /** scratch buffer to store some packed data between calls */
    uint8_t scratch_buffer[2 * DMA_BUFFER_SIZE];
};

static int init_hardware(struct upipe_pciesdi_src *upipe_pciesdi_src, bool sd);

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
UPIPE_HELPER_UPUMP(upipe_pciesdi_src, format_watcher, upump_mgr)

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
    upipe_pciesdi_src_init_format_watcher(upipe);
    upipe_pciesdi_src_init_uclock(upipe);

    upipe_pciesdi_src->levelb_to_uyvy = upipe_levelb_to_uyvy_c;
    upipe_pciesdi_src->sdi_to_uyvy = upipe_sdi_to_uyvy_c;
#if defined(HAVE_X86ASM)
#if defined(__i686__) || defined(__x86_64__)
    if (__builtin_cpu_supports("ssse3")) {
        upipe_pciesdi_src->sdi_to_uyvy = upipe_sdi_to_uyvy_ssse3;
        upipe_pciesdi_src->levelb_to_uyvy = upipe_levelb_to_uyvy_ssse3;
    }

    if (__builtin_cpu_supports("avx2")) {
        upipe_pciesdi_src->sdi_to_uyvy = upipe_sdi_to_uyvy_avx2;
        upipe_pciesdi_src->levelb_to_uyvy = upipe_levelb_to_uyvy_avx2;
    }

    if (has_avx512_support()) {
        upipe_pciesdi_src->sdi_to_uyvy = upipe_sdi_to_uyvy_avx512;
        upipe_pciesdi_src->levelb_to_uyvy = upipe_levelb_to_uyvy_avx512;
    }

    if (has_avx512icl_support()) {
        upipe_pciesdi_src->sdi_to_uyvy = upipe_sdi_to_uyvy_avx512icl;
        upipe_pciesdi_src->levelb_to_uyvy = upipe_levelb_to_uyvy_avx512icl;
    }
#endif
#endif

    upipe_pciesdi_src->mode = -1;
    upipe_pciesdi_src->family = -1;
    upipe_pciesdi_src->scan = -1;
    upipe_pciesdi_src->rate = -1;
    upipe_pciesdi_src->scratch_buffer_count = 0;
    upipe_pciesdi_src->sdi_format = NULL;
    upipe_pciesdi_src->read_buffer = NULL;
    upipe_pciesdi_src->sdi3g_levelb = false;
    upipe_pciesdi_src->discontinuity = false;
    upipe_pciesdi_src->fd = -1;
    upipe_throw_ready(upipe);

    return upipe;
}

static inline bool sdi3g_levelb_eav_match_bitpacked(const uint8_t *src)
{
    static const uint8_t prefix[15] = {
        0xff, 0xff, 0xff, 0xff, 0xff,
        0, 0, 0, 0, 0,
        0, 0, 0, 0, 0
    };
    static const uint8_t fvh[4][5] = {
        { 0x9d, 0x27, 0x49, 0xd2, 0x74}, /* 0x274 */
        { 0xb6, 0x2d, 0x8b, 0x62, 0xd8}, /* 0x2d8 */
        { 0xda, 0x36, 0x8d, 0xa3, 0x68}, /* 0x368 */
        { 0xf1, 0x3c, 0x4f, 0x13, 0xc4}  /* 0x3c4 */
    };
    if (!memcmp(src, prefix, 15)
            && (!memcmp(src + 15, fvh[0], 5)
                || !memcmp(src + 15, fvh[1], 5)
                || !memcmp(src + 15, fvh[2], 5)
                || !memcmp(src + 15, fvh[3], 5)))
        return true;
    return false;
}

static inline bool sdi3g_levelb_sav_match_bitpacked(const uint8_t *src)
{
    static const uint8_t prefix[15] = {
        0xff, 0xff, 0xff, 0xff, 0xff,
        0, 0, 0, 0, 0,
        0, 0, 0, 0, 0
    };
    static const uint8_t fvh[4][5] = {
        { 0x80, 0x20, 0x08, 0x02, 0x00}, /* 0x200 */
        { 0xab, 0x2a, 0xca, 0xb2, 0xac}, /* 0x2ac */
        { 0xc7, 0x31, 0xcc, 0x73, 0x1c}, /* 0x31c */
        { 0xec, 0x3b, 0x0e, 0xc3, 0xb0}  /* 0x3b0 */
    };
    if (!memcmp(src - 20, prefix, 15)
            && (!memcmp(src - 5, fvh[0], 5)
                || !memcmp(src - 5, fvh[1], 5)
                || !memcmp(src - 5, fvh[2], 5)
                || !memcmp(src - 5, fvh[3], 5)))
        return true;
    return false;
}

static inline bool sd_eav_match_bitpacked(const uint8_t *src)
{
    if (src[0] == 0xff
            && src[1] == 0xc0
            && src[2] == 0
            && ((src[3] == 2 && src[4] == 0x74)
                || (src[3] == 2 && src[4] == 0xd8)
                || (src[3] == 3 && src[4] == 0x68)
                || (src[3] == 3 && src[4] == 0xc4)))
        return true;
    return false;
}

static inline bool sd_sav_match_bitpacked(const uint8_t *src)
{
    if (src[-5] == 0xff
            && src[-4] == 0xc0
            && src[-3] == 0
            && ((src[-2] == 2 && src[-1] == 0)
                || (src[-2] == 2 && src[-1] == 0xac)
                || (src[-2] == 3 && src[-1] == 0x1c)
                || (src[-2] == 3 && src[-1] == 0xb0)))
        return true;
    return false;
}

/*
 * Returns the address within the circular mmap buffer using the buffer count
 * and offset.
 */
static const uint8_t *mmap_wraparound(const uint8_t *mmap_buffer,
        uint64_t buffer_count, uint64_t offset)
{
    return mmap_buffer + (buffer_count * DMA_BUFFER_SIZE + offset)
        % DMA_BUFFER_TOTAL_SIZE;
}

/*
 * Check whether the given length would wrap around within the buffer starting
 * at the position given by buffer count and offset.
 */
static bool mmap_length_does_wrap(uint64_t buffer_count, uint64_t offset,
        uint64_t length)
{
    return (buffer_count * DMA_BUFFER_SIZE + offset) % DMA_BUFFER_TOTAL_SIZE
        + length > DMA_BUFFER_TOTAL_SIZE;
}

/*
 * Give the position in the mmap buffer.
 */
static uint64_t mmap_position(uint64_t buffer_count, uint64_t offset)
{
    return (buffer_count * DMA_BUFFER_SIZE + offset) % DMA_BUFFER_TOTAL_SIZE;
}

/*
 * Handle a memcpy that might wrap around in the mmap buffer.
 */
static void mmap_memcpy(uint8_t *dst, const uint8_t *src, uint64_t length,
        uint64_t sw, uint64_t offset)
{
    if (mmap_length_does_wrap(sw, offset, length)) {
        int left = DMA_BUFFER_TOTAL_SIZE - mmap_position(sw, offset);
        int right = length - left;
        memcpy(dst, mmap_wraparound(src, sw, offset), left);
        memcpy(dst + left, mmap_wraparound(src, sw, offset + left), right);
    } else {
        memcpy(dst, mmap_wraparound(src, sw, offset), length);
    }
}

/** @internal @This reads data from the source and outputs it.
 *  @param upump description structure of the read watcher
 */
static void upipe_pciesdi_src_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);

    uint8_t locked, mode, family, scan, rate;
    sdi_rx(upipe_pciesdi_src->fd, &locked, &mode, &family, &scan, &rate);

    /* If either the core or datapath bits are unset or the "was unlocked"
     * bit is set then a discontinuity needs flaggiing on the next output. */
    if (locked != 0x3) {
        upipe_dbg_va(upipe, "unlocked (%d), setting discontinuity (%s)",
                locked, __func__);
        upipe_pciesdi_src->discontinuity = true;
        return;
    }

    /* Format change needs to change output. */
    if (mode != upipe_pciesdi_src->mode
            || family != upipe_pciesdi_src->family
            || scan != upipe_pciesdi_src->scan
            || rate != upipe_pciesdi_src->rate) {
        upipe_warn_va(upipe, "format change, changing flow_def (%s)", __func__);

        /* On a mode change some HW needs reconfiguring/reinitializing.  Store
         * the new mode so that it isn't done again. */
        if (mode != upipe_pciesdi_src->mode
                && need_init_hardware(upipe_pciesdi_src->capability_flags)) {
            upipe_warn_va(upipe, "mode change, reconfiguring HW (%s)", __func__);
            init_hardware(upipe_pciesdi_src, mode == SDI_TX_MODE_SD);
            upipe_pciesdi_src->mode = mode;
        }

        /* Stop DMA to get EAV re-aligned. */
        int64_t hw, sw;
        sdi_dma_writer(upipe_pciesdi_src->fd, 0, &hw, &sw);

        /* Get new format details. */
        struct uref *flow_def;
        int ret = get_flow_def(upipe, &flow_def);
        if (ubase_check(ret)) {
            upipe_pciesdi_src_store_flow_def(upipe, flow_def);
            if (!ubase_check(ubuf_mgr_check(upipe_pciesdi_src->ubuf_mgr, flow_def)))
                upipe_pciesdi_src_require_ubuf_mgr(upipe, flow_def);
        } else {
            /* If there was an error getting the new flow_def then the main pump
             * calling upipe_pciesdi_src_worker() should be stopped so that it
             * isn't called again with possibly invalid state. */
            upump_stop(upipe_pciesdi_src->upump);
            /* Return without starting the DMA. */
            return;
        }

        /* Start DMA and reset state. */
        sdi_dma_writer(upipe_pciesdi_src->fd, 1, &hw, &sw);
        upipe_pciesdi_src->scratch_buffer_count = 0;
        upipe_pciesdi_src->discontinuity = true;

        upipe_dbg_va(upipe, "mode or format change, setting discontinuity (%s)",
                __func__);

        /* Return because there should be no data to read. */
        return;
    }

    /* All seems good with the signal so restart the format watcher pump. */
    upump_restart(upipe_pciesdi_src->format_watcher);

    /* Size (in bytes) of a packed line. */
    int sdi_line_width = upipe_pciesdi_src->sdi_format->width * 2 * 10 / 8;
    if (upipe_pciesdi_src->sdi3g_levelb)
        sdi_line_width *= 2;

    int64_t hw, sw;
    sdi_dma_writer(upipe_pciesdi_src->fd, 1, &hw, &sw); // get buffer counts

    int64_t num_bufs = hw - sw;
    /* Calculate how many lines we can output from the available data. */
    int bytes_available = num_bufs * DMA_BUFFER_SIZE + upipe_pciesdi_src->scratch_buffer_count;
    int lines = bytes_available / sdi_line_width;

    /* If there is nothing to do then return early. */
    if (num_bufs == 0 || lines == 0)
        return;

    int processed_bytes = lines * sdi_line_width;
    int output_size = lines * upipe_pciesdi_src->sdi_format->width * 4;
    if (upipe_pciesdi_src->sdi3g_levelb)
        output_size *= 2;

    if (num_bufs > DMA_BUFFER_COUNT/2) {
        upipe_warn_va(upipe, "reading too late, hw: %"PRId64", sw: %"PRId64", skipping %d lines",
                hw, sw, lines);

        struct sdi_ioctl_mmap_dma_update mmap_update = { .sw_count = hw };
        if (ioctl(upipe_pciesdi_src->fd, SDI_IOCTL_MMAP_DMA_WRITER_UPDATE, &mmap_update))
            upipe_err(upipe, "ioctl error incrementing SW buffer count");

        /* Lie about how much data is in buffer to keep EAV alignment. */
        int bytes_remaining = bytes_available - processed_bytes;
        upipe_pciesdi_src->scratch_buffer_count = bytes_remaining;

        upipe_pciesdi_src->discontinuity = true;
        return;
    }

    if (!upipe_pciesdi_src->ubuf_mgr) {
        upipe_warn_va(upipe, "no ubuf_mgr, skipping %d lines", lines);

        struct sdi_ioctl_mmap_dma_update mmap_update = { .sw_count = hw };
        if (ioctl(upipe_pciesdi_src->fd, SDI_IOCTL_MMAP_DMA_WRITER_UPDATE, &mmap_update))
            upipe_err(upipe, "ioctl error incrementing SW buffer count");

        /* Lie about how much data is in buffer to keep EAV alignment. */
        int bytes_remaining = bytes_available - processed_bytes;
        upipe_pciesdi_src->scratch_buffer_count = bytes_remaining;

        upipe_pciesdi_src->discontinuity = true;
        return;
    }

    struct uref *uref = uref_block_alloc(upipe_pciesdi_src->uref_mgr,
            upipe_pciesdi_src->ubuf_mgr, output_size);
    if (!uref) {
        upipe_err(upipe, "error allocating output uref");
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *dst_buf;
    int block_size = -1;
    int ret = uref_block_write(uref, 0, &block_size, &dst_buf);
    if (unlikely(!ubase_check(ret))) {
        upipe_err(upipe, "unable to map block for writing");
        upipe_throw_fatal(upipe, ret);
        uref_free(uref);
        return;
    }

    int offset = 0;
    /* Use tail of previous read and head of current to unpack a line. */
    if (upipe_pciesdi_src->scratch_buffer_count) {
        offset = sdi_line_width - upipe_pciesdi_src->scratch_buffer_count;
        /* Copy to end of scratch buffer. */
        mmap_memcpy(upipe_pciesdi_src->scratch_buffer + upipe_pciesdi_src->scratch_buffer_count,
                upipe_pciesdi_src->read_buffer, offset, sw, 0);
        /* unpack */
        if (upipe_pciesdi_src->sdi3g_levelb) {
            /* Note: line order is swapped. */
            uint16_t *dst1 = (uint16_t*)dst_buf + 2*upipe_pciesdi_src->sdi_format->width;
            uint16_t *dst2 = (uint16_t*)dst_buf;
            upipe_pciesdi_src->levelb_to_uyvy(upipe_pciesdi_src->scratch_buffer,
                    dst1, dst2, upipe_pciesdi_src->sdi_format->width);
            dst_buf += upipe_pciesdi_src->sdi_format->width * 8;
        } else {
            upipe_pciesdi_src->sdi_to_uyvy(upipe_pciesdi_src->scratch_buffer,
                    (uint16_t*)dst_buf, upipe_pciesdi_src->sdi_format->width);
            dst_buf += upipe_pciesdi_src->sdi_format->width * 4;
        }
        upipe_pciesdi_src->scratch_buffer_count = 0;
        lines -= 1;
    }

    int error_count_eav = 0, error_count_sav = 0;
    for (int i = 0; i < lines; i++, offset += sdi_line_width) {
        const uint8_t *sdi_line = mmap_wraparound(upipe_pciesdi_src->read_buffer, sw, offset);

    /* maximum number of bytes the SIMD can read beyond the end of the buffer. */
#define SIMD_OVERREAD 63

        /* check whether a line wraps around in the mmap buffer */
        if (mmap_length_does_wrap(sw, offset, sdi_line_width + SIMD_OVERREAD)) {
            /* Copy both halves of line to scratch buffer. */
            mmap_memcpy(upipe_pciesdi_src->scratch_buffer,
                    upipe_pciesdi_src->read_buffer, sdi_line_width, sw, offset);
            /* Now point to the scratch buffer. */
            sdi_line = upipe_pciesdi_src->scratch_buffer;
        }

        /* Perform the debug checks. */
        if (true /* TODO: add runtime option to enable/disable these. */) {
            int active_offset = upipe_pciesdi_src->sdi_format->active_offset * 2 * 10 / 8;
            if (upipe_pciesdi_src->sdi3g_levelb)
                active_offset *= 2;
            const uint8_t *active_start = sdi_line + active_offset;

            if (upipe_pciesdi_src->sdi_format->pict_fmt->sd) {
                /* Check EAV is present. */
                if (!sd_eav_match_bitpacked(sdi_line)) {
                    error_count_eav += 1;
                }
                /* Check SAV is present. */
                if (!sd_sav_match_bitpacked(active_start)) {
                    error_count_sav += 1;
                }

            } else if (upipe_pciesdi_src->sdi3g_levelb) {
                /* Check EAV is present. */
                if (!sdi3g_levelb_eav_match_bitpacked(sdi_line)) {
                    error_count_eav += 1;
                }
                /* Check SAV is present. */
                if (!sdi3g_levelb_sav_match_bitpacked(active_start)) {
                    error_count_sav += 1;
                }

            } else { /* HD */
                /* Check EAV is present. */
                if (!hd_eav_match_bitpacked(sdi_line)) {
                    error_count_eav += 1;
                }
                /* Check SAV is present. */
                if (!hd_sav_match_bitpacked(active_start)) {
                    error_count_sav += 1;
                }
            } /* end HD */
        } /* end debug check */

        /* Unpack data into uref. */
        if (upipe_pciesdi_src->sdi3g_levelb) {
            /* Note: line order is swapped. */
            uint16_t *dst1 = (uint16_t*)dst_buf + (2*i + 1) * 2*upipe_pciesdi_src->sdi_format->width;
            uint16_t *dst2 = (uint16_t*)dst_buf + (2*i + 0) * 2*upipe_pciesdi_src->sdi_format->width;
            upipe_pciesdi_src->levelb_to_uyvy(sdi_line, dst1, dst2,
                    upipe_pciesdi_src->sdi_format->width);
        } else {
            uint16_t *dst = (uint16_t*)dst_buf + 2*i * upipe_pciesdi_src->sdi_format->width;
            upipe_pciesdi_src->sdi_to_uyvy(sdi_line, dst,
                    upipe_pciesdi_src->sdi_format->width);
        }
    }

    if (error_count_eav || error_count_sav)
        upipe_err_va(upipe, "error counts eav: %d, sav: %d (%d lines checked)",
                error_count_eav, error_count_sav, lines);


    struct sdi_ioctl_mmap_dma_update mmap_update = { .sw_count = hw };
    if (ioctl(upipe_pciesdi_src->fd, SDI_IOCTL_MMAP_DMA_WRITER_UPDATE, &mmap_update))
        upipe_err(upipe, "ioctl error incrementing SW buffer count");

    /* Copy unused data into the scratch buffer. */
    if (bytes_available != processed_bytes) {
        int bytes_remaining = bytes_available - processed_bytes;
        mmap_memcpy(upipe_pciesdi_src->scratch_buffer,
                upipe_pciesdi_src->read_buffer, bytes_remaining, sw, offset);
        upipe_pciesdi_src->scratch_buffer_count = bytes_remaining;
    }

    if (upipe_pciesdi_src->discontinuity) {
        upipe_dbg(upipe, "setting discontinuity attribute on output uref");
        uref_flow_set_discontinuity(uref);
        upipe_pciesdi_src->discontinuity = false;
    }

    if (upipe_pciesdi_src->sdi3g_levelb)
        uref_block_set_sdi3g_levelb(uref);

    if (upipe_pciesdi_src->uclock)
        uref_clock_set_cr_sys(uref, uclock_now(upipe_pciesdi_src->uclock));

    uref_block_unmap(uref, 0);
    upipe_pciesdi_src_output(upipe, uref, &upipe_pciesdi_src->upump);
}

/** @internal @This fills the flow_def for the format being received by the HW.
 *
 * @param upipe description structure of the pipe
 * @param flow_format pointer to get the allocated flow_def uref
 * @returns an error code
 */
static int get_flow_def(struct upipe *upipe, struct uref **flow_format)
{
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);

    /* Query the HW for what it thinks the received format is. */
    uint8_t locked, mode, family, scan, rate;
    sdi_rx(upipe_pciesdi_src->fd, &locked, &mode, &family, &scan, &rate);
    upipe_notice_va(upipe, "locked: %d, mode: %s (%d), family: %s (%d), scan: %s (%d), rate: %s (%d)",
            locked,
            sdi_decode_mode(mode), mode,
            sdi_decode_family(family), family,
            sdi_decode_scan(scan, mode), scan,
            sdi_decode_rate(rate, scan), rate);

    if (!locked) {
        upipe_err(upipe, "SDI signal not locked");
        return UPIPE_PCIESDI_SRC_ERR_NOSIGNAL;
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
        upipe_err_va(upipe, "invalid/unknown rate value: %s (%d)", sdi_decode_rate(rate, scan), rate);
        return UBASE_ERR_INVALID;
    }

    /* Check for SDI-3G level B. */
    if (mode == 2 && scan == 0) {
        sdi3g_levelb = true;
        interlaced = false;
        fps.num *= 2;
    } else if (scan == 0) {
        /* interlaced */
        interlaced = true;
    } else if (scan == 1) {
        /* progressive */
        interlaced = false;
    } else {
        upipe_err_va(upipe, "invalid/unknown scan value: %s (%d)", sdi_decode_scan(scan, mode), scan);
        return UBASE_ERR_INVALID;
    }

    /* Create flow_def and fill in attributes. */
    struct uref *flow_def = uref_alloc(upipe_pciesdi_src->uref_mgr);
    if (!flow_def)
        return UBASE_ERR_ALLOC;

    UBASE_RETURN(uref_flow_set_def(flow_def, "block."));
    UBASE_RETURN(uref_block_flow_set_append(flow_def, 32));
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
        uref_free(flow_def);
        return UBASE_ERR_INVALID;
    }

    /* Size (in bytes) of a packed line. */
    int sdi_line_width = upipe_pciesdi_src->sdi_format->width * 2 * 10 / 8;
    if (sdi3g_levelb)
        sdi_line_width *= 2;
    if (sdi_line_width > sizeof(upipe_pciesdi_src->scratch_buffer)) {
        upipe_err(upipe, "SDI line too large for scratch buffer");
        uref_free(flow_def);
        return UBASE_ERR_INVALID;
    }

    if (upipe_pciesdi_src->sdi_format->pict_fmt->active_f2.start && interlaced == false && sdi3g_levelb == false)
        upipe_warn(upipe, "SDI signal is progressive but interlaced sdi_offset struct returned");
    else if (!upipe_pciesdi_src->sdi_format->pict_fmt->active_f2.start && interlaced == true)
        upipe_warn(upipe, "SDI signal is interlaced but progressive sdi_offset struct returned");

    *flow_format = flow_def;

    upipe_pciesdi_src->mode = mode;
    upipe_pciesdi_src->family = family;
    upipe_pciesdi_src->scan = scan;
    upipe_pciesdi_src->rate = rate;
    upipe_pciesdi_src->sdi3g_levelb = sdi3g_levelb;

    return UBASE_ERR_NONE;
}

/** @internal @This tries to get the flow_def when the SDI signal is locked.
 *
 *  @param upump description structure of the timer pump
 */
static void get_flow_def_on_signal_lock(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);

    /* If execution makes it here the main worker has not executed for the
     * repeat time of the upump so it assumes RX signal has been lost.  Or this
     * is the first time after pipe creation. */

    /* Query the HW for what it thinks the received format is. */
    uint8_t locked, mode, family, scan, rate;
    sdi_rx(upipe_pciesdi_src->fd, &locked, &mode, &family, &scan, &rate);

    /* Stop DMA to get EAV re-aligned. */
    int64_t hw, sw;
    sdi_dma_writer(upipe_pciesdi_src->fd, 0, &hw, &sw);
    upump_stop(upipe_pciesdi_src->upump);

    if (!locked) {
        /* TODO: throw some probe event? */
        upipe_err(upipe, "SDI signal not locked");
        return;
    }

    /* Check for format change. */
    if (mode != upipe_pciesdi_src->mode
            || family != upipe_pciesdi_src->family
            || scan != upipe_pciesdi_src->scan
            || rate != upipe_pciesdi_src->rate) {
        upipe_warn_va(upipe, "format change, changing flow_def (%s)", __func__);

        if (mode != upipe_pciesdi_src->mode
                && need_init_hardware(upipe_pciesdi_src->capability_flags)) {
            upipe_warn_va(upipe, "mode change, reconfiguring HW (%s)", __func__);
            init_hardware(upipe_pciesdi_src, mode == SDI_TX_MODE_SD);
            upipe_pciesdi_src->mode = mode;
        }

        struct uref *flow_def;
        int ret = get_flow_def(upipe, &flow_def);
        /* TODO: does this need to check for errors other then NOSIGNAL and stop? */
        if (!ubase_check(ret)) {
            return;
        }
        upipe_pciesdi_src_require_ubuf_mgr(upipe, flow_def);
    }

    /* Start DMA and reset state. */
    sdi_dma_writer(upipe_pciesdi_src->fd, 1, &hw, &sw);
    upipe_pciesdi_src->scratch_buffer_count = 0;
    upipe_pciesdi_src->discontinuity = true;
    upipe_dbg_va(upipe, "setting discontinuity (%s)", __func__);

    /* Start main pump. */
    upump_start(upipe_pciesdi_src->upump);

    return;
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

    /* Get ubuf_mgr later in get_flow_def_on_signal_lock. */

    if (upipe_pciesdi_src->uclock == NULL &&
        urequest_get_opaque(&upipe_pciesdi_src->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    if (upipe_pciesdi_src->fd != -1 && upipe_pciesdi_src->upump == NULL) {
        /* Create the main fd_read pump but don't start it. */
        struct upump *upump = upump_alloc_fd_read(upipe_pciesdi_src->upump_mgr,
                upipe_pciesdi_src_worker, upipe, upipe->refcount, upipe_pciesdi_src->fd);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_pciesdi_src_set_upump(upipe, upump);

        /* Create and start format watcher pump. */
        upump = upump_alloc_timer(upipe_pciesdi_src->upump_mgr,
                get_flow_def_on_signal_lock, upipe, upipe->refcount,
                UCLOCK_FREQ, UCLOCK_FREQ);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_pciesdi_src_set_format_watcher(upipe, upump);
        upump_start(upump);
        /* TODO: shorten inital delay. */
    }

    return UBASE_ERR_NONE;
}

static int init_hardware(struct upipe_pciesdi_src *upipe_pciesdi_src, bool sd)
{
    int fd = upipe_pciesdi_src->fd;
    int device_number = upipe_pciesdi_src->device_number;

    /* sdi_pre_init */

    if (upipe_pciesdi_src->capability_flags & SDI_CAP_HAS_GS12281)
        gs12281_spi_init(fd);
    if (upipe_pciesdi_src->capability_flags & SDI_CAP_HAS_GS12241) {
        if (sd) {
            gs12241_reset(fd, device_number);
            gs12241_config_for_sd(fd, device_number);
        }
        gs12241_spi_init(fd);
    }

    if (upipe_pciesdi_src->capability_flags & SDI_CAP_HAS_LMH0387) {
        /* Set direction for RX. */
        sdi_lmh0387_direction(fd, 0);
        /* set launch amplitude to nominal */
        sdi_lmh0387_spi_write(fd, device_number, 0x02, 0x30);
    }

    /* sdi_init */

    /* disable loopback */
    sdi_dma(fd, 0);

    /* disable dmas */
    int64_t hw_count, sw_count;
    sdi_dma_writer(fd, 0, &hw_count, &sw_count);

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
    upipe_pciesdi_src->fd = open(path, O_RDONLY | O_NONBLOCK);
    if (unlikely(upipe_pciesdi_src->fd < 0)) {
        upipe_err_va(upipe, "can't open %s (%m)", path);
        return UBASE_ERR_EXTERNAL;
    }

    /* request dma */
    if (sdi_request_dma_writer(upipe_pciesdi_src->fd) == 0) {
        upipe_err(upipe, "DMA not available");
        return UBASE_ERR_EXTERNAL;
    }

    struct sdi_ioctl_mmap_dma_info mmap_info;
    if (ioctl(upipe_pciesdi_src->fd, SDI_IOCTL_MMAP_DMA_INFO, &mmap_info) != 0) {
        upipe_err(upipe, "error getting mmap info");
        return UBASE_ERR_EXTERNAL;
    }

    if (mmap_info.dma_rx_buf_size != DMA_BUFFER_SIZE
            || mmap_info.dma_rx_buf_count != DMA_BUFFER_COUNT) {
        upipe_err(upipe, "mmap info returned does not match compile-time constants");
        return UBASE_ERR_EXTERNAL;
    }

    void *buf = mmap(NULL, DMA_BUFFER_TOTAL_SIZE, PROT_READ, MAP_SHARED,
            upipe_pciesdi_src->fd, mmap_info.dma_rx_buf_offset);
    if (buf == MAP_FAILED) {
        upipe_err(upipe, "mmap failed");
        return UBASE_ERR_EXTERNAL;
    }

    /* TODO: check need to release things on failure. */

    upipe_pciesdi_src->read_buffer = buf;
    upipe_pciesdi_src->device_number = path[strlen(path) - 1] - 0x30; /* FIXME for more than 9 channels. */

    /* Get capability_flags. */
    uint8_t channels;
    sdi_capabilities(upipe_pciesdi_src->fd, &upipe_pciesdi_src->capability_flags, &channels);
    if (upipe_pciesdi_src->device_number < 0) {
        upipe_err_va(upipe, "invalid device number (%d)",
                upipe_pciesdi_src->device_number);
        return UBASE_ERR_INVALID;
    }
    if (upipe_pciesdi_src->device_number >= channels) {
        /* Wrap around the number of channels. FIXME heterogenous cards. */
        int temp = upipe_pciesdi_src->device_number % channels;
        upipe_warn_va(upipe, "wrapping device number (%d) around using number of channels (%d) to %d",
                upipe_pciesdi_src->device_number, channels, temp);
        upipe_pciesdi_src->device_number = temp;
    }

    /* initialize hardware except the clock */
    UBASE_RETURN(init_hardware(upipe_pciesdi_src, false));

    /* Set the crc and packed options (in libsdi.c). */
    uint8_t locked, mode, family, scan, rate;
    sdi_rx(upipe_pciesdi_src->fd, &locked, &mode, &family, &scan, &rate);

    return UBASE_ERR_NONE;
}

static void upipe_pciesdi_src_close(struct upipe *upipe)
{
    struct upipe_pciesdi_src *upipe_pciesdi_src = upipe_pciesdi_src_from_upipe(upipe);

    int64_t hw, sw;
    sdi_dma_writer(upipe_pciesdi_src->fd, 0, &hw, &sw); // disable
    sdi_release_dma_writer(upipe_pciesdi_src->fd); // release old locks
    munmap(upipe_pciesdi_src->read_buffer, DMA_BUFFER_TOTAL_SIZE);
    upipe_pciesdi_src->read_buffer = NULL;

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
    upipe_pciesdi_src_clean_format_watcher(upipe);
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
