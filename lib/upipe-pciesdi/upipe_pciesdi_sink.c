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
 * @short Upipe source module PCIE SDI cards sink
 */

#include <upipe/ubase.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/upipe.h>
#include <upipe/ulist.h>
#include <upipe/udict.h>
#include <upipe/upump.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe-pciesdi/upipe_pciesdi_sink.h>

#include <upipe/uref_pic.h>
#include <upipe/uref_dump.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "sdi_config.h"
#include "libsdi.h"
#include "csr.h"
#include "sdi.h"

#include "flags.h"
#include "../upipe-hbrmt/sdienc.h"
#include "../upipe-hbrmt/upipe_hbrmt_common.h"
#include "config.h"

/** upipe_pciesdi_sink structure */
struct upipe_pciesdi_sink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** file descriptor */
    int fd;

    /** urefs */
    struct uchain urefs;
    struct uref *uref;
    size_t written;

    int first;

    /** delay applied to systime attribute when uclock is provided */
    uint64_t latency;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;

    /** scratch buffer */
    int scratch_bytes;
    uint8_t scratch_buffer[DMA_BUFFER_SIZE + 32];

    /** hardware clock */
    struct uclock uclock;
    uint32_t previous_tick;
    uint64_t wraparounds;

    void (*uyvy_to_sdi)(uint8_t *dst, const uint8_t *src, uintptr_t pixels);

    struct sdi_ioctl_mmap_dma_info mmap_info;
    uint8_t *write_buffer;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_pciesdi_sink, upipe, UPIPE_PCIESDI_SINK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_pciesdi_sink, urefcount, upipe_pciesdi_sink_free)
UPIPE_HELPER_VOID(upipe_pciesdi_sink);
UPIPE_HELPER_UPUMP_MGR(upipe_pciesdi_sink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_pciesdi_sink, upump, upump_mgr)
UBASE_FROM_TO(upipe_pciesdi_sink, uclock, uclock, uclock)

static uint64_t upipe_pciesdi_sink_now(struct uclock *uclock)
{
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_uclock(uclock);

    if (upipe_pciesdi_sink->fd < 0)
        return 0;

    uint32_t freq, tick;
    sdi_refclk(upipe_pciesdi_sink->fd, 0, &freq, &tick);

    if (tick < upipe_pciesdi_sink->previous_tick) {
        /* log this? */
        upipe_pciesdi_sink->wraparounds += 1;
    }
    upipe_pciesdi_sink->previous_tick = tick;

    __uint128_t fullscale = (upipe_pciesdi_sink->wraparounds << 32) + tick;
    fullscale *= UCLOCK_FREQ;
    fullscale /= freq;

    return fullscale;
}

/** @internal @This allocates a null pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_pciesdi_sink_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_pciesdi_sink_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);
    upipe_pciesdi_sink_init_urefcount(upipe);
    upipe_pciesdi_sink_init_upump_mgr(upipe);
    upipe_pciesdi_sink_init_upump(upipe);
    upipe_pciesdi_sink_check_upump_mgr(upipe);

    upipe_pciesdi_sink->scratch_bytes = 0;
    upipe_pciesdi_sink->latency = 0;
    upipe_pciesdi_sink->fd = -1;
    ulist_init(&upipe_pciesdi_sink->urefs);
    upipe_pciesdi_sink->uref = NULL;
    upipe_pciesdi_sink->first = 1;
    upipe_pciesdi_sink->previous_tick = 0;
    upipe_pciesdi_sink->wraparounds = 0;
    upipe_pciesdi_sink->uclock.refcount = &upipe_pciesdi_sink->urefcount;
    upipe_pciesdi_sink->uclock.uclock_now = upipe_pciesdi_sink_now;

    upipe_pciesdi_sink->uyvy_to_sdi = upipe_uyvy_to_sdi_c;
#if defined(HAVE_X86ASM)
#if defined(__i686__) || defined(__x86_64__)
    if (__builtin_cpu_supports("ssse3")) {
        upipe_pciesdi_sink->uyvy_to_sdi = upipe_uyvy_to_sdi_ssse3;
    }

    if (__builtin_cpu_supports("avx")) {
        upipe_pciesdi_sink->uyvy_to_sdi = upipe_uyvy_to_sdi_avx;
    }

    if (__builtin_cpu_supports("avx2")) {
        upipe_pciesdi_sink->uyvy_to_sdi = upipe_uyvy_to_sdi_avx2;
    }
#endif
#endif

    upipe_throw_ready(&upipe_pciesdi_sink->upipe);

    return upipe;
}

static uint8_t *mmap_wraparound(uint8_t *mmap_buffer,
        uint64_t buffer_count, uint64_t offset)
{
    return mmap_buffer + (buffer_count * DMA_BUFFER_SIZE + offset)
        % DMA_BUFFER_TOTAL_SIZE;
}

static bool mmap_length_does_wrap(uint64_t buffer_count, uint64_t offset,
        uint64_t length)
{
    return (buffer_count * DMA_BUFFER_SIZE + offset) % DMA_BUFFER_TOTAL_SIZE
        + length > DMA_BUFFER_TOTAL_SIZE;
}

static inline void pack(uint8_t *dst, const uint16_t *src)
{
    dst[0] = src[0] >> 2;
    dst[1] = (src[0] << 6) | (src[1] >> 4);
    dst[2] = (src[1] << 4) | (src[2] >> 6);
    dst[3] = (src[2] << 2) | (src[3] >> 8);
    dst[4] = src[3];
}

static void exit_clean(struct upipe *upipe, uint8_t *buf, size_t size)
{
    struct upipe_pciesdi_sink *ctx = upipe_pciesdi_sink_from_upipe(upipe);

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

    int64_t hw, sw;
    sdi_dma_reader(ctx->fd, 0, &hw, &sw); // disable
    sdi_release_dma_reader(ctx->fd); // release old locks
    close(ctx->fd);

    abort();
}

/** @internal
 */
static void upipe_pciesdi_sink_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);

    int64_t hw = 0, sw = 0;
    sdi_dma_reader(upipe_pciesdi_sink->fd, upipe_pciesdi_sink->first == 0, &hw, &sw); // get buffer counts

    int64_t num_bufs = sw - hw;
    if (num_bufs < 0) {
        upipe_warn_va(upipe, "reading too late, hw: %"PRId64", sw: %"PRId64, hw, sw);
    } else if (num_bufs >= DMA_BUFFER_COUNT/2) {
        upipe_warn_va(upipe, "sw count at least %d ahead, hw: %"PRId64", sw: %"PRId64, DMA_BUFFER_COUNT/2, hw, sw);
        return;
    }
    num_bufs = DMA_BUFFER_COUNT/2 - num_bufs; // number of bufs to write
    //upipe_dbg_va(upipe, "hw: %"PRId64", sw: %"PRId64", to write: %"PRId64, hw, sw, num_bufs);

//upipe_dbg_va(upipe, "hw: %"PRId64", sw: %"PRId64", to write: %"PRId64, hw, sw, num_bufs);
    /* Limit num_bufs to the end of the mmap buffer. */
    if (sw % DMA_BUFFER_COUNT + num_bufs > DMA_BUFFER_COUNT)
        num_bufs = DMA_BUFFER_COUNT - sw % DMA_BUFFER_COUNT;
//upipe_dbg_va(upipe, "hw: %"PRId64", sw: %"PRId64", to write: %"PRId64, hw, sw, num_bufs);

    uint8_t txen, slew;
    sdi_tx(upipe_pciesdi_sink->fd, SDI_TX_MODE_HD, &txen, &slew);
    if (txen || slew)
        upipe_dbg_va(upipe, "txen %d slew %d", txen, slew);

    struct uref *uref = upipe_pciesdi_sink->uref;
    if (!uref) {
        struct uchain *uchain = ulist_pop(&upipe_pciesdi_sink->urefs);
        static bool underrun = false; /* FIXME: static variable */
        if (!uchain) {
            if (!underrun)
            upipe_err(upipe, "underrun");
            underrun = true;
            return;
        }
        underrun = false;
        uref = uref_from_uchain(uchain);
        upipe_pciesdi_sink->uref = uref;
        upipe_pciesdi_sink->written = 0;
    }

    size_t size = 0;
    uref_block_size(uref, &size);

    const uint8_t *src_buf;
    int src_bytes = -1;
    if (!ubase_check(uref_block_read(uref, upipe_pciesdi_sink->written, &src_bytes, &src_buf))) {
        upipe_err_va(upipe, "could not map for reading, size: %zu, written: %zu", size, upipe_pciesdi_sink->written);
        return;
    }
    int samples = src_bytes/2, pixels = src_bytes/4;
//upipe_dbg_va(upipe, "src_bytes: %d, samples: %d", src_bytes, samples);

    int bytes_to_write = num_bufs * DMA_BUFFER_SIZE;
    bool enough_samples = true;

    int samples_written = 0;
    int offset = 0;

    if (upipe_pciesdi_sink->scratch_bytes) {
        int count = upipe_pciesdi_sink->scratch_bytes;
        //upipe_dbg_va(upipe, "copying %d bytes from scratch buffer to card", count);
//upipe_dbg_va(upipe, "memcpy at "__FILE__":%d", __LINE__ + 1);
        memcpy(mmap_wraparound(upipe_pciesdi_sink->write_buffer, sw, offset),
                upipe_pciesdi_sink->scratch_buffer,
                count);
        offset += count;
        bytes_to_write -= count;
    }

//upipe_dbg_va(upipe, "at start, num_bufs: %d, bytes_to_write: %d", (int)num_bufs, bytes_to_write);
    if (bytes_to_write > samples/4 * 5) {
        /* not enough samples to fill wanted buffers */
        /* TODO: need to store or pack tail somewhere. */
        num_bufs = (samples/4 * 5) / DMA_BUFFER_SIZE;
        bytes_to_write = num_bufs * DMA_BUFFER_SIZE - upipe_pciesdi_sink->scratch_bytes;
        enough_samples = false;
    }
//upipe_dbg_va(upipe, "after sample check, num_bufs: %d, bytes_to_write: %d", (int)num_bufs, bytes_to_write);

#define SIMD_OVERWRITE 25
    if (!mmap_length_does_wrap(sw, offset, bytes_to_write / 5 * 5 + SIMD_OVERWRITE)) {
        /* no wraparound */
        upipe_pciesdi_sink->uyvy_to_sdi(mmap_wraparound(upipe_pciesdi_sink->write_buffer, sw, offset),
                src_buf, bytes_to_write / 5 * 2);

//upipe_dbg_va(upipe, "bytes_to_write: %d, packed: %d, remaining: %d",
//bytes_to_write,
//bytes_to_write/5*5,
//bytes_to_write - bytes_to_write/5*5
//);

        offset += bytes_to_write / 5 * 5;
        samples_written += bytes_to_write / 5 * 4;
        bytes_to_write -= bytes_to_write / 5 * 5;

        //upipe_dbg_va(upipe, "bytes_to_write: %d, offset: %d", bytes_to_write, offset);

        if (bytes_to_write) {
            uint8_t array[5];
            pack(array, (const uint16_t *)src_buf + samples_written);

            //upipe_dbg_va(upipe, "copying %d bytes from array to card", bytes_to_write);
//upipe_dbg_va(upipe, "memcpy at "__FILE__":%d", __LINE__ + 1);
            memcpy(mmap_wraparound(upipe_pciesdi_sink->write_buffer, sw, offset),
                    array, bytes_to_write);
//upipe_dbg(upipe, "memcpy returned");

            //upipe_dbg_va(upipe, "copying %d bytes from array to scratch buffer", 5 - bytes_to_write);
//upipe_dbg_va(upipe, "memcpy at "__FILE__":%d", __LINE__ + 1);
            memcpy(upipe_pciesdi_sink->scratch_buffer,
                    array + bytes_to_write, 5 - bytes_to_write);
            upipe_pciesdi_sink->scratch_bytes = 5 - bytes_to_write;
            samples_written += 4;
        } else {
            upipe_pciesdi_sink->scratch_bytes = 0;
        }
    }

    else { /* section to write wraps around in the mmap buffer (or the SIMD overwrite might go beyond) */
        //upipe_dbg(upipe, "wraparound");
        int bytes_remaining = DMA_BUFFER_TOTAL_SIZE - (sw * DMA_BUFFER_SIZE + offset) % DMA_BUFFER_TOTAL_SIZE;
        int rounded_bytes_rem = (bytes_remaining - SIMD_OVERWRITE) / 5 * 5;

//upipe_dbg_va(upipe, "bytes_to_write: %d, bytes_remaining: %d, rounded_bytes_rem: %d", bytes_to_write, bytes_remaining, rounded_bytes_rem);

        if (rounded_bytes_rem > 0) {
            //upipe_dbg_va(upipe, "packing into %d bytes remaining", rounded_bytes_rem);
            upipe_pciesdi_sink->uyvy_to_sdi(mmap_wraparound(upipe_pciesdi_sink->write_buffer, sw, offset), src_buf, rounded_bytes_rem / 5 * 2);
            bytes_remaining -= rounded_bytes_rem;
            bytes_to_write -= rounded_bytes_rem;
            offset += rounded_bytes_rem;
            samples_written += rounded_bytes_rem / 5 * 4;
        }

        //upipe_dbg_va(upipe, "packing into %d bytes in scratch", (bytes_remaining + 4) / 5 * 5);
        upipe_pciesdi_sink->uyvy_to_sdi(upipe_pciesdi_sink->scratch_buffer, src_buf + samples_written * sizeof(uint16_t), (bytes_remaining + 4) / 5 * 2);
        //upipe_dbg_va(upipe, "copying %d bytes from scratch to card", bytes_remaining);
//upipe_dbg_va(upipe, "memcpy at "__FILE__":%d", __LINE__ + 1);
        memcpy(mmap_wraparound(upipe_pciesdi_sink->write_buffer, sw, offset), upipe_pciesdi_sink->scratch_buffer, bytes_remaining);
        bytes_to_write -= bytes_remaining;
        offset += bytes_remaining;
        samples_written += (bytes_remaining + 4) / 5 * 4;

//upipe_dbg_va(upipe, "memcpy at "__FILE__":%d", __LINE__ + 1);
        bytes_remaining = (bytes_remaining + 4) / 5 * 5 - bytes_remaining;
        memmove(upipe_pciesdi_sink->scratch_buffer, upipe_pciesdi_sink->scratch_buffer + bytes_remaining, bytes_remaining);
        upipe_pciesdi_sink->scratch_bytes = bytes_remaining;

        if (bytes_to_write) {
            upipe_dbg_va(upipe, "hw: %"PRId64", sw: %"PRId64", to write: %"PRId64, hw, sw, num_bufs);
            upipe_err_va(upipe, "wtf?  %d bytes remaining to be written on wraparound, remaining %d", bytes_to_write, bytes_remaining);
        }
        //upipe_dbg_va(upipe, "samples_written: %d, bytes_to_write: %d", samples_written, bytes_to_write);
    }

    if (!enough_samples || (samples - samples_written) / 4 * 5 < DMA_BUFFER_SIZE) {
        int samples_remaining = samples - samples_written;
        if (samples_remaining / 4 * 5 > DMA_BUFFER_SIZE) {
            upipe_err_va(upipe, "scratch buffer not big enough for remaining %d samples", samples_remaining);
            exit_clean(upipe, NULL, 0);
        } else {
            //upipe_dbg_va(upipe, "packing remaining %d samples into scratch buffer", samples_remaining);
        }
        upipe_pciesdi_sink->uyvy_to_sdi(upipe_pciesdi_sink->scratch_buffer + upipe_pciesdi_sink->scratch_bytes, src_buf + samples_written * sizeof(uint16_t), samples_remaining/2);
        samples_written += samples_remaining;
        upipe_pciesdi_sink->scratch_bytes += samples_remaining / 4 * 5;
    }

    /* unmap */
    uref_block_unmap(uref, upipe_pciesdi_sink->written);

    /* update buffer count */
    if (num_bufs) {
        struct sdi_ioctl_mmap_dma_update mmap_update = { .sw_count = sw + num_bufs };
        if (ioctl(upipe_pciesdi_sink->fd, SDI_IOCTL_MMAP_DMA_READER_UPDATE, &mmap_update))
            upipe_err(upipe, "ioctl error incrementing SW buffer count");
    }

    /* start dma */
    upipe_pciesdi_sink->first = 0;
    sdi_dma_reader(upipe_pciesdi_sink->fd, upipe_pciesdi_sink->first == 0, &hw, &sw);

    upipe_pciesdi_sink->written += samples_written * sizeof(uint16_t);
    if (upipe_pciesdi_sink->written == size) {
        uref_free(uref);
        upipe_pciesdi_sink->uref = NULL;
    }
}

static void start_fd_write(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);
    upump = upump_alloc_fd_write(upipe_pciesdi_sink->upump_mgr,
            upipe_pciesdi_sink_worker, upipe,
            upipe->refcount, upipe_pciesdi_sink->fd);
    if (unlikely(upump == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return;
    }
    upipe_pciesdi_sink_set_upump(upipe, upump);
    upump_start(upump);
}

static void inplace_pack(uint8_t *buf, uintptr_t len)
{
    len /= 2;
    for (int i = 0; i < len; i += 1) {
        uint16_t a = *(uint16_t*)(buf + 8*i+0);
        uint16_t b = *(uint16_t*)(buf + 8*i+2);
        uint16_t c = *(uint16_t*)(buf + 8*i+4);
        uint16_t d = *(uint16_t*)(buf + 8*i+6);
        buf[5*i+0] = a >> 2;
        buf[5*i+1] = (a << 6) | (b >> 4);
        buf[5*i+2] = (b << 4) | (c >> 6);
        buf[5*i+3] = (c << 2) | (d >> 8);
        buf[5*i+4] = d;
    }
}

static int pack_uref(struct upipe *upipe, struct uref *uref)
{
    struct upipe_pciesdi_sink *ctx = upipe_pciesdi_sink_from_upipe(upipe);

    size_t size = 0;
    uref_block_size(uref, &size);

    uint8_t *buf;
    int s = -1;
    int ret = uref_block_write(uref, 0, &s, &buf);
    if (!ubase_check(ret)) {
        upipe_err(upipe, "could not map for writing");
        return ret;
    }

    if (s != size) {
        uref_block_unmap(uref, 0);
        upipe_err(upipe, "segmented buffers are not supported");
        return UBASE_ERR_INVALID;
    }

#if 0
    ctx->uyvy_to_sdi(buf, buf, size/4);
#else
    inplace_pack(buf, size/4);
#endif
    uref_block_unmap(uref, 0);

    ret = uref_block_resize(uref, 0, (size/2)*10/8);
    if (!ubase_check(ret)) {
        upipe_err(upipe, "unable to resize");
        return ret;
    }

    return UBASE_ERR_NONE;
}

/** @internal
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_pciesdi_sink_input(struct upipe *upipe, struct uref *uref, struct upump **upump_p)
{
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);

    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        uint64_t latency = 0;
        uref_clock_get_latency(uref, &latency);
        if (latency > upipe_pciesdi_sink->latency)
            upipe_pciesdi_sink->latency = latency;
        uref_free(uref);
        return;
    }

#if 0
        /* Check first EAV is correct in uref. */
        const uint8_t *buf;
        int s = 32;
        int ret = uref_block_read(uref, 0, &s, &buf);
        if (!ubase_check(ret)) {
            upipe_err(upipe, "could not map for reading");
            uref_free(uref);
            return;
        }

        if (!hd_eav_match((const uint16_t*)buf)) {
            upipe_err(upipe, "uref does not appear to be unpacked");
            uref_block_unmap(uref, 0);
            uref_free(uref);
            return;
        }
        uref_block_unmap(uref, 0);
#endif

#define CHUNK_BUFFER_COUNT 32
#define BUFFER_COUNT_PRINT_THRESHOLD(num, den) (num * CHUNK_BUFFER_COUNT / den)

    ulist_add(&upipe_pciesdi_sink->urefs, uref_to_uchain(uref));
    size_t n = ulist_depth(&upipe_pciesdi_sink->urefs);

    /* check if pump is already running */
    if (upipe_pciesdi_sink->upump)
        return;

    uint8_t txen, slew;
    sdi_tx(upipe_pciesdi_sink->fd, SDI_TX_MODE_HD, &txen, &slew);

    /* check for chunks or whole frames */
    uint64_t vpos;
    int ret = uref_pic_get_vposition(uref, &vpos);

    /* whole frames */
    if (!ubase_check(ret)) {
        /* To buffer more frames, check and return here. */
        upipe_pciesdi_sink_check_upump_mgr(upipe);
        if (upipe_pciesdi_sink->upump_mgr) {
            struct upump *upump = upump_alloc_timer(upipe_pciesdi_sink->upump_mgr,
                    start_fd_write, upipe, upipe->refcount,
                    10*UCLOCK_FREQ/1000, 0); /* wait 10ms */
            if (unlikely(upump == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
                return;
            }
            upipe_pciesdi_sink_set_upump(upipe, upump);
            upump_start(upump);
        }
        return;
    }

    /* check for enough chunks to fill DMA buffers assuming DMA_BUFFER_SIZE is a line */
    if (ubase_check(ret) && n < CHUNK_BUFFER_COUNT)
        return;

    int64_t hw = 0, sw = 0;
    sdi_dma_reader(upipe_pciesdi_sink->fd, upipe_pciesdi_sink->first == 0, &hw, &sw);

    struct upump *upump = upump_alloc_fd_write(upipe_pciesdi_sink->upump_mgr,
            upipe_pciesdi_sink_worker, upipe,
            upipe->refcount, upipe_pciesdi_sink->fd);
    if (unlikely(upump == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return;
    }
    upipe_pciesdi_sink_set_upump(upipe, upump);
    upump_start(upump);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_pciesdi_sink_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "block."));

    uint64_t height;
    struct urational fps;
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &height));
    UBASE_RETURN(uref_pic_flow_get_fps(flow_def, &fps));

#ifdef DUO2_HW
    if (fps.den == 1001) {
        upipe_err(upipe, "TX of NTSC signals is not supported on the Duo2");
        return UBASE_ERR_INVALID;
    }
#endif

    bool sd = height < 720;
    bool sdi3g = height == 1080 && (urational_cmp(&fps, &(struct urational){ 50, 1 })) >= 0;
    upipe_dbg_va(upipe, "sd: %d, 3g: %d", sd, sdi3g);

    /* TODO: init card based on given format. */

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
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);

    ubase_clean_fd(&upipe_pciesdi_sink->fd);
    upipe_pciesdi_sink_set_upump(upipe, NULL);

    upipe_pciesdi_sink->fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (unlikely(upipe_pciesdi_sink->fd < 0)) {
        upipe_err_va(upipe, "can't open %s (%m)", path);
        return UBASE_ERR_EXTERNAL;
    }

    int64_t hw = 0, sw = 0;
    sdi_dma_reader(upipe_pciesdi_sink->fd, 0, &hw, &sw);
    sdi_release_dma_reader(upipe_pciesdi_sink->fd);
    close(upipe_pciesdi_sink->fd);

    upipe_pciesdi_sink->fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (unlikely(upipe_pciesdi_sink->fd < 0)) {
        upipe_err_va(upipe, "can't open %s (%m)", path);
        return UBASE_ERR_EXTERNAL;
    }

    /* request dma */
    if (sdi_request_dma_reader(upipe_pciesdi_sink->fd) == 0) {
        upipe_err(upipe, "DMA not available");
        return UBASE_ERR_EXTERNAL;
    }

    /* disable pattern */
    //sdi_set_pattern(upipe_pciesdi_sink->fd, SDI_TX_MODE_SD, 0, 0);
    sdi_set_pattern(upipe_pciesdi_sink->fd, SDI_TX_MODE_HD, 0, 0);
    //sdi_set_pattern(upipe_pciesdi_sink->fd, SDI_TX_MODE_3G, 0, 0);

    sdi_dma(upipe_pciesdi_sink->fd, 0, 0, 0); // disable loopback

    struct sdi_ioctl_mmap_dma_info mmap_info;
    if (ioctl(upipe_pciesdi_sink->fd, SDI_IOCTL_MMAP_DMA_INFO, &mmap_info) != 0) {
        upipe_err(upipe, "error getting mmap info");
        return UBASE_ERR_EXTERNAL;
    }

    if (mmap_info.dma_tx_buf_size != DMA_BUFFER_SIZE
            || mmap_info.dma_tx_buf_count != DMA_BUFFER_COUNT) {
        upipe_err(upipe, "mmap info returned does not match compile-time constants");
        return UBASE_ERR_EXTERNAL;
    }

    upipe_notice_va(upipe, "mmap info, tx offset: %"PRIu64", tx size: %"PRIu64", tx count: %"PRIu64,
            mmap_info.dma_tx_buf_offset, mmap_info.dma_tx_buf_size, mmap_info.dma_tx_buf_count);

    void *buf = mmap(NULL, DMA_BUFFER_TOTAL_SIZE, PROT_WRITE, MAP_SHARED,
            upipe_pciesdi_sink->fd, mmap_info.dma_rx_buf_offset);
    if (buf == MAP_FAILED) {
        upipe_err(upipe, "mmap failed");
        return UBASE_ERR_EXTERNAL;
    }

    /* TODO: check need to release things on failure. */

    upipe_pciesdi_sink->mmap_info = mmap_info;
    upipe_pciesdi_sink->write_buffer = buf;

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_pciesdi_sink_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);

    switch (command) {
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
            return upipe_control_provide_request(upipe, command, args);

        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_pciesdi_sink_set_upump(upipe, NULL);
            return upipe_pciesdi_sink_attach_upump_mgr(upipe);

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_pciesdi_sink_set_flow_def(upipe, flow);
        }

        case UPIPE_SET_URI: {
            const char *path = va_arg(args, const char *);
            return upipe_pciesdi_set_uri(upipe, path);
        }

        case UPIPE_PCIESDI_SINK_GET_UCLOCK: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_PCIESDI_SINK_SIGNATURE)
            struct uclock **pp_uclock = va_arg(args, struct uclock **);
            *pp_uclock = &upipe_pciesdi_sink->uclock;
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_pciesdi_sink_free(struct upipe *upipe)
{
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);
    upipe_throw_dead(upipe);
    ubase_clean_fd(&upipe_pciesdi_sink->fd);

    uref_free(upipe_pciesdi_sink->uref);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_pciesdi_sink->urefs, uchain, uchain_tmp) {
        uref_free(uref_from_uchain(uchain));
        ulist_delete(uchain);
    }

    upipe_pciesdi_sink_clean_upump(upipe);
    upipe_pciesdi_sink_clean_upump_mgr(upipe);
    upipe_pciesdi_sink_clean_urefcount(upipe);
    upipe_pciesdi_sink_free_void(upipe);
}

/** upipe_pciesdi_sink (/dev/null) */
static struct upipe_mgr upipe_pciesdi_sink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_PCIESDI_SINK_SIGNATURE,

    .upipe_alloc = upipe_pciesdi_sink_alloc,
    .upipe_input = upipe_pciesdi_sink_input,
    .upipe_control = upipe_pciesdi_sink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for null pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_pciesdi_sink_mgr_alloc(void)
{
    return &upipe_pciesdi_sink_mgr;
}
