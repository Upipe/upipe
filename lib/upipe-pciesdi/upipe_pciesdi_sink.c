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
#include <pthread.h>

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

    struct uref *uref_next;
    struct uref *uref;
    size_t written;
    bool underrun;

    int first;
    int tx_mode;
    int clock_rate;
    int genlock;

    /** delay applied to systime attribute when uclock is provided */
    uint64_t latency;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *fd_write_upump;

    int device_number;

    /** scratch buffer */
    int scratch_bytes;
    uint8_t scratch_buffer[DMA_BUFFER_SIZE + 32];

    /** hardware clock */
    struct uclock uclock;
    uint64_t offset;
    uint32_t clock_is_inited; /* TODO: maybe replace with uatomic variable. */
    struct urational freq;

    void (*uyvy_to_sdi)(uint8_t *dst, const uint8_t *src, uintptr_t pixels);

    struct sdi_ioctl_mmap_dma_info mmap_info;
    uint8_t *write_buffer;

    pthread_mutex_t clock_mutex;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_pciesdi_sink, upipe, UPIPE_PCIESDI_SINK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_pciesdi_sink, urefcount, upipe_pciesdi_sink_free)
UPIPE_HELPER_VOID(upipe_pciesdi_sink);
UPIPE_HELPER_UPUMP_MGR(upipe_pciesdi_sink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_pciesdi_sink, fd_write_upump, upump_mgr)
UBASE_FROM_TO(upipe_pciesdi_sink, uclock, uclock, uclock)

static uint64_t upipe_pciesdi_sink_now(struct uclock *uclock)
{
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_uclock(uclock);

    pthread_mutex_lock(&upipe_pciesdi_sink->clock_mutex);

    if (upipe_pciesdi_sink->fd < 0
            || upipe_pciesdi_sink->clock_is_inited == 0) {
        pthread_mutex_unlock(&upipe_pciesdi_sink->clock_mutex);
        return UINT64_MAX;
    }

    /* read ticks from card */
    uint32_t freq;
    uint64_t tick;
    sdi_channel_get_refclk(upipe_pciesdi_sink->fd, &freq, &tick);

    if (freq == 0) {
        pthread_mutex_unlock(&upipe_pciesdi_sink->clock_mutex);
        return UINT64_MAX;
    }

    /* 128 bits needed to prevent overflow after ~2.5 hours */
    __uint128_t fullscale = tick;
    /* Use exact frequency.  This multiply was UCLOCK_FREQ so the comment above
     * is outdated.  The overflow will take longer to occur. */
    fullscale *= upipe_pciesdi_sink->freq.den;
    fullscale /= upipe_pciesdi_sink->freq.num;
    fullscale += upipe_pciesdi_sink->offset;

    pthread_mutex_unlock(&upipe_pciesdi_sink->clock_mutex);

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
    upipe_pciesdi_sink_init_fd_write_upump(upipe);
    upipe_pciesdi_sink_check_upump_mgr(upipe);

    int ret = pthread_mutex_init(&upipe_pciesdi_sink->clock_mutex, NULL);
    if (ret) {
        upipe_err_va(upipe, "pthread_mutex_init() failed with %d (%s)", ret, strerror(ret));
        return NULL;
    }

    upipe_pciesdi_sink->clock_is_inited = 0;
    upipe_pciesdi_sink->offset = 0;
    upipe_pciesdi_sink->scratch_bytes = 0;
    upipe_pciesdi_sink->latency = 0;
    upipe_pciesdi_sink->fd = -1;
    upipe_pciesdi_sink->uref = NULL;
    upipe_pciesdi_sink->uref_next = NULL;
    upipe_pciesdi_sink->underrun = false;
    upipe_pciesdi_sink->first = 1;
    upipe_pciesdi_sink->tx_mode = -1;
    upipe_pciesdi_sink->clock_rate = SDI_UNDEF_RATE;
    upipe_pciesdi_sink->genlock = SDI_GENLOCK_IS_NOT_CONFIGURED;
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

static void stop_dma(struct upipe *upipe)
{
    struct upipe_pciesdi_sink *ctx = upipe_pciesdi_sink_from_upipe(upipe);
    int64_t hw, sw;

    /* stop DMA */
    sdi_dma_reader(ctx->fd, 0, &hw, &sw);
    /* stop and clear pump */
    upipe_pciesdi_sink_set_fd_write_upump(upipe, NULL);
    /* reset state */
    ctx->first = 1;
    ctx->scratch_bytes = 0;

    /* Clear next uref. */
    uref_free(ctx->uref_next);
    ctx->uref_next = NULL;
    /* Free uref being written. */
    uref_free(ctx->uref);
    ctx->uref = NULL;
    ctx->written = 0;
}

/** @internal
 */
static void upipe_pciesdi_sink_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);

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

    bool underrun = false;
    /* Get uref, from struct or list, print 1 message if none available. */
    struct uref *uref = upipe_pciesdi_sink->uref;
    if (!uref) {
        uref = upipe_pciesdi_sink->uref_next;
        upipe_pciesdi_sink->uref_next = NULL;
        if (!uref) {
            underrun = true;
        } else {
            underrun = false;
            upipe_pciesdi_sink->uref = uref;
            upipe_pciesdi_sink->written = 0;
        }
    }

    /* If too late and there are no more urefs to write the input may have
     * stopped so stop the output. */
    if (num_bufs <= 0 && underrun) {
        upipe_warn(upipe, "too late and no input, stopping DMA and upump");
        stop_dma(upipe);
        return;
    }

    if (underrun)
        return;

    /* Check for "too late" only when there is something to write.  Prevents log
     * message spam in the case the input is released.  With the upump stop
     * above there might be many, many buffers to write when it starts again so
     * try skipping them without writing anything. */
    if (num_bufs < 0) {
        upipe_warn_va(upipe, "writing too late, stopping DMA: %"PRId64" buffers, hw: %"PRId64", sw: %"PRId64,
                -num_bufs, hw, sw);
        stop_dma(upipe);
        return;
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
        upipe_err_va(upipe, "could not map for reading, size: %zu, written: %zu, freeing uref",
                size, upipe_pciesdi_sink->written);
        uref_free(uref);
        upipe_pciesdi_sink->uref = NULL;
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
    if (upipe_pciesdi_sink->first) {
        upipe_notice(upipe, "starting DMA");
        upipe_pciesdi_sink->first = 0;
        sdi_dma_reader(upipe_pciesdi_sink->fd, 1, &hw, &sw);
    }

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
    upipe_pciesdi_sink_set_fd_write_upump(upipe, upump);
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

    if (upipe_pciesdi_sink->uref_next) {
        int64_t hw = 0, sw = 0;
        sdi_dma_reader(upipe_pciesdi_sink->fd, upipe_pciesdi_sink->first == 0, &hw, &sw); // get buffer counts
        upipe_dbg_va(upipe, "uref input before uref_next was used, buffer count hw: %"PRId64", sw: %"PRId64, hw, sw);
        uref_free(upipe_pciesdi_sink->uref_next);
    }
    upipe_pciesdi_sink->uref_next = uref;

    /* check if pump is already running */
    if (upipe_pciesdi_sink->fd_write_upump)
        return;

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
            upipe_pciesdi_sink_set_fd_write_upump(upipe, upump);
            upump_start(upump);
        }

        return;
    }
}

static int check_capabilities(struct upipe *upipe, bool genlock)
{
    struct upipe_pciesdi_sink *ctx = upipe_pciesdi_sink_from_upipe(upipe);
    int fd = ctx->fd;
    int device_number = ctx->device_number;

    uint32_t capability_flags;
    uint8_t channels;
    sdi_capabilities(fd, &capability_flags, &channels);

    if (device_number < 0 || device_number >= channels) {
        upipe_err_va(upipe, "invalid device number (%d) for number of channels (%d)",
                device_number, channels);
        return UBASE_ERR_INVALID;
    }

    if ((capability_flags & SDI_CAP_HAS_GENLOCK) == 0 && genlock) {
        upipe_err(upipe, "genlock not supported on this board");
        return UBASE_ERR_INVALID;
    }

    if (!(capability_flags == SDI_CAP_FALCON9 || capability_flags == SDI_CAP_DUO2 || capability_flags == SDI_CAP_MINI_4K)) {
        upipe_err(upipe, "unknown capabilities");
        return UBASE_ERR_INVALID;
    }

    return UBASE_ERR_NONE;
}

static void init_hardware(struct upipe *upipe, int rate, int mode)
{
    struct upipe_pciesdi_sink *ctx = upipe_pciesdi_sink_from_upipe(upipe);
    int fd = ctx->fd;

    uint32_t capability_flags;
    uint8_t channels;
    sdi_capabilities(fd, &capability_flags, &channels);

    /* sdi_pre_init */

    if (capability_flags & SDI_CAP_HAS_LMH0387) {
        /* Set direction for TX. */
        sdi_lmh0387_direction(fd, 1);
    }

    /* reset channel */
    sdi_channel_reset_tx(fd, 1);

    /* PCIe SDI (Falcon 9) */
    if (capability_flags == SDI_CAP_FALCON9) {
        if (rate & SDI_PAL_RATE) {
            /* Set channel to refclk0. */
            sdi_channel_set_pll(fd, 0);
        } else if (rate & SDI_NTSC_RATE) {
            /* Set channel 0 to refclk1. */
            sdi_channel_set_pll(fd, 1);
        }
    }

    /* Mini 4K */
    else if (capability_flags == SDI_CAP_MINI_4K) {
        /* TODO: Need to write to CSR_SDI_QPLL_REFCLK_STABLE_ADDR when changing refclk? */
        uint32_t refclk_freq;
        uint64_t refclk_counter;
        if (rate & SDI_PAL_RATE) {
            sdi_refclk(fd, 0, &refclk_freq, &refclk_counter);
        } else if (rate & SDI_NTSC_RATE) {
            sdi_refclk(fd, 1, &refclk_freq, &refclk_counter);
        }
    }

    /* Duo2 */
    else if (SDI_CAP_DUO2) {
        if (rate & SDI_PAL_RATE) {
            sdi_picxo(fd, 0, 0, 0);
        } else if (rate & SDI_NTSC_RATE) {
            /* SD: use a 270Mbps linerate, don't use PICXO */
            if (mode == SDI_TX_MODE_SD)
                sdi_picxo(fd, 0, 0, 0);
            /* HD: use PICXO to slow down 74.25MHz to 74.25MHz/1.001 clock */
            else if (mode == SDI_TX_MODE_HD)
                sdi_picxo(fd, 1, 1, 10);
            /* 3G: use PICXO to slow down 148.5Mhz to 148.5MHz/1.001 clock */
            else
                sdi_picxo(fd, 1, 1, 5);
        }
    }

    /* unreset channel */
    sdi_channel_reset_tx(fd, 0);

    /* disable pattern */
    sdi_set_pattern(fd, mode, 0, 0);

    /* set TX mode */
    uint8_t txen, slew;
    sdi_tx(fd, mode, &txen, &slew);

    /* Store rate in driver. */
    sdi_set_rate(fd, rate);
}

static int64_t get_genlock_delay(const struct sdi_offsets_fmt *sdi_format)
{
    enum sdi_type {
        SDI_TYPE_UNKNOWN,

        SDI_TYPE_PAL,
        SDI_TYPE_720P50,
        SDI_TYPE_720P60,
        SDI_TYPE_1080P24,
        SDI_TYPE_1080I25,
        SDI_TYPE_1080P25,
        SDI_TYPE_1080P30,
        SDI_TYPE_1080P50,
        SDI_TYPE_1080P60,

        SDI_TYPE_NTSC,
        SDI_TYPE_720P59,
        SDI_TYPE_1080P23,
        SDI_TYPE_1080I29,
        SDI_TYPE_1080P29,
        SDI_TYPE_1080P59,
    } sdi_type = SDI_TYPE_UNKNOWN;

    /* "advanced" means the video is too early.  "delayed" means the video is too late. */
    static const struct offset { int lines; int us; } offsets[] = {
            [SDI_TYPE_PAL]     = { -1, -24 },
            [SDI_TYPE_720P50]  = { 2, -13 },
            [SDI_TYPE_1080I25] = { 1, 6 },
            [SDI_TYPE_1080P25] = { 1, 6 },
            [SDI_TYPE_1080P50] = { 2, 1 },
    };

    int height = sdi_format->pict_fmt->active_height;
    bool interlaced = sdi_format->psf_ident == UPIPE_SDI_PSF_IDENT_I;
    const struct urational *fps = &sdi_format->fps;

    if (height == 1080) {
        if (urational_cmp(fps, &(struct urational){ 50, 1 }) == 0)
            sdi_type = SDI_TYPE_1080P50;
        if (urational_cmp(fps, &(struct urational){ 25, 1 }) == 0) {
            if (interlaced)
                sdi_type = SDI_TYPE_1080I25;
            else
                sdi_type = SDI_TYPE_1080P25;
        }
    }
    else if (height == 720) {
        if (urational_cmp(fps, &(struct urational){ 50, 1 }) == 0)
            sdi_type = SDI_TYPE_720P50;
    }
    else {
        if (urational_cmp(fps, &(struct urational){ 25, 1 }) == 0)
            sdi_type = SDI_TYPE_PAL;
    }

    if (sdi_type == SDI_TYPE_UNKNOWN)
        return 0;

    struct offset offset = offsets[sdi_type];

    int64_t delay = INT64_C(125000000) * fps->den / fps->num;
    int64_t line_delay = delay * offset.lines / sdi_format->height;
    int64_t time_delay = INT64_C(125000000) * offset.us / 1000000;

    return delay - line_delay - time_delay;
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
    const struct sdi_offsets_fmt *sdi_format = sdi_get_offsets(flow_def);
    UBASE_ALLOC_RETURN(sdi_format);

    bool genlock = upipe_pciesdi_sink->genlock & SDI_GENLOCK_IS_CONFIGURED;
    bool sd = height < 720;
    bool ntsc = sd ? 0 : fps.den == 1001;
    bool sdi3g = height == 1080 && (urational_cmp(&fps, &(struct urational){ 50, 1 })) >= 0;
    upipe_dbg_va(upipe, "sd: %d, 3g: %d", sd, sdi3g);

    int tx_mode;
    if (sd)
        tx_mode = SDI_TX_MODE_SD;
    else if (sdi3g)
        tx_mode = SDI_TX_MODE_3G;
    else
        tx_mode = SDI_TX_MODE_HD;

    int clock_rate;
    if (ntsc)
        clock_rate = SDI_NTSC_RATE;
    else
        clock_rate = SDI_PAL_RATE;

    /* If the clock for this format is provided by genlock then we want to
     * signal that genlock should be used to release and synchronize the TX. */
    if (upipe_pciesdi_sink->genlock & SDI_GENLOCK_IS_NTSC && ntsc)
        clock_rate |= SDI_GENLOCK_RATE;
    else if (upipe_pciesdi_sink->genlock & SDI_GENLOCK_IS_PAL && !ntsc)
        clock_rate |= SDI_GENLOCK_RATE;
    else
        genlock = false;

    if (upipe_pciesdi_sink->fd == -1) {
        upipe_warn(upipe, "device has not been opened, unable to init hardware");
        return UBASE_ERR_INVALID;
    }

    if (genlock) {
        uint8_t active;
        uint64_t period, seen;
        sdi_genlock_vsync(upipe_pciesdi_sink->fd, &active, &period, &seen);

        if (active) {
            int64_t delay = get_genlock_delay(sdi_format);
            if (delay == 0)
                upipe_warn(upipe, "unknown genlock delay");
            else
                upipe_dbg_va(upipe, "setting genlock delay to %"PRId64, delay);

            unsigned bitfield = SDI_GENLOCK_SYNCHRO_SOURCE_VSYNC_DELAYED;
            if (sdi_format->psf_ident == UPIPE_SDI_PSF_IDENT_I)
                bitfield |= SDI_GENLOCK_SYNCHRO_SOURCE_FIELD;

            sdi_writel(upipe_pciesdi_sink->fd, CSR_SDI_GENLOCK_SYNCHRO_SOURCE_ADDR, bitfield);
            sdi_writel(upipe_pciesdi_sink->fd, CSR_SDI_GENLOCK_SYNCHRO_DELAY_VSYNC_EDGE_ADDR, 0);
            sdi_writel(upipe_pciesdi_sink->fd, CSR_SDI_GENLOCK_SYNCHRO_DELAY_VSYNC_OFFSET_ADDR, delay);
        }

        else
            upipe_warn(upipe, "genlock not active");
    }

    /* Record time now so that we can use it as an offset to ensure that the
     * clock always goes forwards when mode changes. */
    uint64_t offset = upipe_pciesdi_sink_now(&upipe_pciesdi_sink->uclock);

    upipe_warn(upipe, "new flow_def, stopping DMA and upump");
    stop_dma(upipe);

    /* Frequencies:
     * - PAL 3G = 148.5  MHz
     * - PAL HD =  74.25 MHz
     * - PAL SD = 148.5  MHz ?
     * - NTSC 3G = 148.5  / 1.001 MHz
     * - NTSC HD =  74.25 / 1.001 MHz
     * - NTSC SD = 148.5  / 1.001 MHz ?
     */
    struct urational freq;
    if (sd) {
        freq = (struct urational){ 1485, 270 };
    } else if (sdi3g) {
        if (ntsc)
            freq = (struct urational){ 148500, 27027 };
        else
            freq = (struct urational){ 1485, 270 };
    } else {
        if (ntsc)
            freq = (struct urational){ 74250, 27027 };
        else
            freq = (struct urational){ 7425, 2700 };
    }

    /* If there is no change to the clock frequency or TX mode then there is no
     * need to reconfigure the HW. */
    if (clock_rate == upipe_pciesdi_sink->clock_rate && tx_mode == upipe_pciesdi_sink->tx_mode)
        return UBASE_ERR_NONE;

    /* Lock to begin init. */
    pthread_mutex_lock(&upipe_pciesdi_sink->clock_mutex);

    /* Throw a probe event to signal the genlock status. */
    if (clock_rate & SDI_GENLOCK_RATE)
        upipe_throw(upipe, UPROBE_PCIESDI_SINK_GENLOCK_TYPE, UPIPE_PCIESDI_SINK_SIGNATURE,
                (uint32_t)UPROBE_PCIESDI_SINK_GENLOCK_IN_USE);
    else if (upipe_pciesdi_sink->genlock & SDI_GENLOCK_IS_CONFIGURED)
        upipe_throw(upipe, UPROBE_PCIESDI_SINK_GENLOCK_TYPE, UPIPE_PCIESDI_SINK_SIGNATURE,
                (uint32_t)UPROBE_PCIESDI_SINK_GENLOCK_CONFIGURED);
    else
        upipe_throw(upipe, UPROBE_PCIESDI_SINK_GENLOCK_TYPE, UPIPE_PCIESDI_SINK_SIGNATURE,
                (uint32_t)UPROBE_PCIESDI_SINK_GENLOCK_NOT_CONFIGURED);

    /* initialize clock */
    init_hardware(upipe, clock_rate, tx_mode);
    upipe_pciesdi_sink->freq = freq;
    upipe_pciesdi_sink->offset = offset;
    upipe_pciesdi_sink->clock_is_inited = 1;
    upipe_pciesdi_sink->tx_mode = tx_mode;
    upipe_pciesdi_sink->clock_rate = clock_rate;

    /* Unlock */
    pthread_mutex_unlock(&upipe_pciesdi_sink->clock_mutex);

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
    upipe_pciesdi_sink_set_fd_write_upump(upipe, NULL);

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

    sdi_dma(upipe_pciesdi_sink->fd, 0); // disable loopback

    /* TODO: check need to release things on failure. */

    upipe_pciesdi_sink->mmap_info = mmap_info;
    upipe_pciesdi_sink->write_buffer = buf;
    upipe_pciesdi_sink->device_number = path[strlen(path) - 1] - 0x30;

    int genlock;
    upipe_pciesdi_sink->genlock = genlock = sdi_get_genlock(upipe_pciesdi_sink->fd);
    UBASE_RETURN(check_capabilities(upipe, genlock & SDI_GENLOCK_IS_CONFIGURED));

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
            upipe_pciesdi_sink_set_fd_write_upump(upipe, NULL);
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
    uref_free(upipe_pciesdi_sink->uref_next);

    upipe_pciesdi_sink_clean_fd_write_upump(upipe);
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
