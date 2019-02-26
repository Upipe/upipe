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
    bool underrun;

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
    upipe_pciesdi_sink->underrun = false;
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

    /* sdi tx control / status */
    uint8_t txen, slew;
    sdi_tx(upipe_pciesdi_sink->fd, SDI_TX_MODE_HD, &txen, &slew);
    if (txen || slew)
        upipe_dbg_va(upipe, "txen %d slew %d", txen, slew);

    /* set / get dma */
    int64_t hw = 0, sw = 0;
    sdi_dma_reader(upipe_pciesdi_sink->fd, upipe_pciesdi_sink->first == 0, &hw, &sw); // get buffer counts

    int64_t num_bufs = sw - hw;
    /* Return early if no more data is yet needed.  poll(2) shouldn't be giving
     * POLLOUT for this case anyway. */
    if (num_bufs >= DMA_BUFFER_COUNT/2) {
        upipe_warn_va(upipe, "sw count at least %d ahead, hw: %"PRId64", sw: %"PRId64, DMA_BUFFER_COUNT/2, hw, sw);
        return;
    }

    /* Get uref, from struct or list, print 1 message if none available. */
    struct uref *uref = upipe_pciesdi_sink->uref;
    if (!uref) {
        struct uchain *uchain = ulist_pop(&upipe_pciesdi_sink->urefs);
        if (!uchain) {
            if (!upipe_pciesdi_sink->underrun)
                upipe_err(upipe, "underrun");
            upipe_pciesdi_sink->underrun = true;
        } else {
            if (upipe_pciesdi_sink->underrun)
                upipe_warn(upipe, "underrun resolved");
            upipe_pciesdi_sink->underrun = false;
            uref = uref_from_uchain(uchain);
            upipe_pciesdi_sink->uref = uref;
            upipe_pciesdi_sink->written = 0;
        }
    }

    if (upipe_pciesdi_sink->underrun)
        return;

    /* Check for "too late" only when there is something to write.  Prevents log
     * message spam in the case the input is released. */
    if (num_bufs < 0) {
        upipe_warn_va(upipe, "writing too late, hw: %"PRId64", sw: %"PRId64, hw, sw);
    }
    num_bufs = DMA_BUFFER_COUNT/2 - num_bufs; // number of bufs to write

    /* Limit num_bufs to the end of the mmap buffer. */
    if (sw % DMA_BUFFER_COUNT + num_bufs > DMA_BUFFER_COUNT)
        num_bufs = DMA_BUFFER_COUNT - sw % DMA_BUFFER_COUNT;

    size_t size = 0;
    uref_block_size(uref, &size);

    const uint8_t *src_buf;
    int src_bytes = -1;
    if (!ubase_check(uref_block_read(uref, upipe_pciesdi_sink->written, &src_bytes, &src_buf))) {
        upipe_err_va(upipe, "could not map for reading, size: %zu, written: %zu", size, upipe_pciesdi_sink->written);
        return;
    }
    int samples = src_bytes/2;

    /* Limit num_bufs to the amount of data left in the uref. */
    int bytes_to_write = num_bufs * DMA_BUFFER_SIZE;
    if (bytes_to_write > samples/4*5 + upipe_pciesdi_sink->scratch_bytes) {
        num_bufs = (samples/4*5 + upipe_pciesdi_sink->scratch_bytes) / DMA_BUFFER_SIZE;
        bytes_to_write = num_bufs * DMA_BUFFER_SIZE;
    }

    int offset = 0;
    /* Copy packed data from scratch buffer. */
    memcpy(mmap_wraparound(upipe_pciesdi_sink->write_buffer, sw, offset),
            upipe_pciesdi_sink->scratch_buffer, upipe_pciesdi_sink->scratch_bytes);
    offset += upipe_pciesdi_sink->scratch_bytes;
    bytes_to_write -= upipe_pciesdi_sink->scratch_bytes;
    upipe_pciesdi_sink->scratch_bytes = 0;

    /* maximum number of bytes the SIMD can write beyond the end of the buffer. */
#define SIMD_OVERWRITE 25

    int samples_written = 0;
    /* Pack data from uref. */
    if (!mmap_length_does_wrap(sw, offset, bytes_to_write / 5 * 5 + SIMD_OVERWRITE)) {
        /* no wraparound */
        upipe_pciesdi_sink->uyvy_to_sdi(mmap_wraparound(upipe_pciesdi_sink->write_buffer, sw, offset),
                src_buf, bytes_to_write / 5 * 2);

        offset += bytes_to_write / 5 * 5;
        samples_written += bytes_to_write / 5 * 4;
        bytes_to_write -= bytes_to_write / 5 * 5;

        if (bytes_to_write) {
            uint8_t array[5];
            pack(array, (const uint16_t *)src_buf + samples_written);
            memcpy(mmap_wraparound(upipe_pciesdi_sink->write_buffer, sw, offset),
                    array, bytes_to_write);
            memcpy(upipe_pciesdi_sink->scratch_buffer,
                    array + bytes_to_write, 5 - bytes_to_write);
            upipe_pciesdi_sink->scratch_bytes = 5 - bytes_to_write;
            samples_written += 4;
        }
    }

    else {
        /* SIMD overwrite goes beyond the end of the buffer. */
        int rounded_bytes_rem = (bytes_to_write - SIMD_OVERWRITE) / 5 * 5;

        /* Pack data from uref safely into mmap buffer. */
        if (rounded_bytes_rem > 0) {
            upipe_pciesdi_sink->uyvy_to_sdi(mmap_wraparound(upipe_pciesdi_sink->write_buffer, sw, offset),
                    src_buf, rounded_bytes_rem / 5 * 2);
            bytes_to_write -= rounded_bytes_rem;
            offset += rounded_bytes_rem;
            samples_written += rounded_bytes_rem / 5 * 4;
        }

        /* Pack rest of needed data into scratch buffer. */
        upipe_pciesdi_sink->uyvy_to_sdi(upipe_pciesdi_sink->scratch_buffer,
                src_buf + samples_written * sizeof(uint16_t), (bytes_to_write + 4) / 5 * 2);
        samples_written += (bytes_to_write + 4) / 5 * 4;

        /* Copy needed data into mmap buffer. */
        memcpy(mmap_wraparound(upipe_pciesdi_sink->write_buffer, sw, offset),
                upipe_pciesdi_sink->scratch_buffer, bytes_to_write);
        /* Move tail into start of scratch buffer. */
        int bytes_remaining = (bytes_to_write + 4) / 5 * 5 - bytes_to_write;
        memmove(upipe_pciesdi_sink->scratch_buffer, upipe_pciesdi_sink->scratch_buffer + bytes_to_write, bytes_remaining);
        upipe_pciesdi_sink->scratch_bytes = bytes_remaining;
    }

    /* Store tail of uref in scratch buffer. */
    if ((samples - samples_written)/4*5 < DMA_BUFFER_SIZE) {
        int samples_to_write = samples - samples_written;
        upipe_pciesdi_sink->uyvy_to_sdi(upipe_pciesdi_sink->scratch_buffer + upipe_pciesdi_sink->scratch_bytes,
                src_buf + samples_written * sizeof(uint16_t), samples_to_write / 2);
        samples_written += samples_to_write;
        upipe_pciesdi_sink->scratch_bytes += samples_to_write/4*5;
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
    sdi_dma_reader(upipe_pciesdi_sink->fd, 1, &hw, &sw);

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

    if (sd) {
        upipe_err(upipe, "SD format is not yet supported");
        return UBASE_ERR_INVALID;
    }

    if (sdi3g) {
        upipe_err(upipe, "SDI-3G format is not yet supported");
        return UBASE_ERR_INVALID;
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
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);

    ubase_clean_fd(&upipe_pciesdi_sink->fd);
    upipe_pciesdi_sink_set_upump(upipe, NULL);

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
            upipe_pciesdi_sink->fd, mmap_info.dma_tx_buf_offset);
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

    int64_t hw, sw;
    sdi_dma_reader(upipe_pciesdi_sink->fd, 0, &hw, &sw);
    sdi_release_dma_reader(upipe_pciesdi_sink->fd);
    munmap(upipe_pciesdi_sink->write_buffer, DMA_BUFFER_TOTAL_SIZE);
    upipe_pciesdi_sink->write_buffer = NULL;

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
