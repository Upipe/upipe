/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *          Rostislav Pehlivanov
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe source module for netmap sockets
 */

#include <config.h>

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-netmap/upipe_netmap_source.h>

#include "../upipe-hbrmt/sdidec.h"
#include "../upipe-hbrmt/rfc4175_dec.h"
#include "../upipe-hbrmt/upipe_hbrmt_common.h"

#include <net/if.h>

#define NETMAP_WITH_LIBS
#include <net/netmap.h>
#include <net/netmap_user.h>

#include <bitstream/ietf/rfc4175.h>
#include <bitstream/ietf/rtp.h>

/** @hidden */
static int upipe_netmap_source_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a netmap source pipe. */
struct upipe_netmap_source {
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

    /** netmap descriptor **/
    struct nm_desc *d[2];

    /** netmap uri **/
    char *uri;

    /** netmap ring **/
    unsigned int ring_idx[2];

    /** got a discontinuity */
    bool discontinuity;

    /** expected sequence number */
    uint32_t expected_seqnum;

    /** timestamp of seqnum-1 */
    uint32_t last_timestamp;

    /** packets in uref */
    unsigned packets;

    /** hbrmt packets per frame */
    unsigned pkts_per_frame;

    /** Packed block destination */
    uint8_t *dst_buf;
    int dst_size;

    /** current frame **/
    struct uref *uref;

    /** hbrmt or rfc4175 */
    bool hbrmt;

    /** rfc4175 */
    struct uref *rfc_def;
    uint64_t hsize;
    uint64_t vsize;
    struct urational fps;
    bool output_is_v210;
    int output_bit_depth;

#define UPIPE_UNPACK_RFC4175_MAX_PLANES 3
    /** output chroma map */
    const char *output_chroma_map[UPIPE_UNPACK_RFC4175_MAX_PLANES];

    uint8_t *output_plane[UPIPE_UNPACK_RFC4175_MAX_PLANES];
    size_t output_stride[UPIPE_UNPACK_RFC4175_MAX_PLANES];

    /* SDI offsets */
    const struct sdi_offsets_fmt *f;

    /** unpack */
    void (*sdi_to_uyvy)(const uint8_t *src, uint16_t *y, uintptr_t pixels);

    /** Bitpacked to V210 conversion */
    void (*bitpacked_to_v210)(const uint8_t *src, uint32_t *dst, uintptr_t pixels);

    /** Bitpacked to Planar 8 conversion */
    void (*bitpacked_to_planar_8)(const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels);
    /** Bitpacked to Planar 10 conversion */
    void (*bitpacked_to_planar_10)(const uint8_t *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t pixels);

    /** detected format */
    uint8_t frate;
    uint8_t frame;

    uint64_t old_vss;

    /** unpack scratch buffer */
    uint8_t unpack_scratch_buffer[5];

    /** bytes in scratch buffer */
    uint8_t unpack_scratch_buffer_count;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_netmap_source, upipe, UPIPE_NETMAP_SOURCE_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_netmap_source, urefcount, upipe_netmap_source_free)
UPIPE_HELPER_FLOW(upipe_netmap_source, NULL)

UPIPE_HELPER_OUTPUT(upipe_netmap_source, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_netmap_source, uref_mgr, uref_mgr_request,
                      upipe_netmap_source_check,
                      upipe_netmap_source_register_output_request,
                      upipe_netmap_source_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_netmap_source, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_netmap_source_check,
                      upipe_netmap_source_register_output_request,
                      upipe_netmap_source_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_netmap_source, uclock, uclock_request, upipe_netmap_source_check,
                    upipe_netmap_source_register_output_request,
                    upipe_netmap_source_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_netmap_source, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_netmap_source, upump, upump_mgr)

/** @internal @This allocates a netmap source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_netmap_source_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_netmap_source_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (!upipe)
        return NULL;
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

    if (flow_def) {
        upipe_netmap_source->hbrmt = false;
        if (!ubase_check(uref_pic_flow_get_hsize(flow_def,
                        &upipe_netmap_source->hsize)) ||
            !ubase_check(uref_pic_flow_get_vsize(flow_def,
                    &upipe_netmap_source->vsize)) ||
            !ubase_check(uref_pic_flow_get_fps(flow_def,
                    &upipe_netmap_source->fps))) {
            upipe_netmap_source_free_flow(upipe);
            upipe_err(upipe, "Missing picture dimensions");
            upipe_clean(upipe);
            free(upipe_netmap_source);
            return NULL;
        }

        upipe_netmap_source->output_is_v210 = ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 16, "u10y10v10y10u10y10v10y10u10y10v10y10"));
        if (!upipe_netmap_source->output_is_v210)
            upipe_netmap_source->output_bit_depth = ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) ? 8 : 10;
        else {
            upipe_netmap_source->hsize = (upipe_netmap_source->hsize + 5) / 6 * 6; // XXX 720
        }

        upipe_netmap_source->rfc_def = flow_def;
        uref_pic_flow_set_hsize(upipe_netmap_source->rfc_def,
                upipe_netmap_source->hsize);
    } else {
        upipe_netmap_source->hbrmt = true;
        upipe_netmap_source->rfc_def = NULL;
    }

    upipe_netmap_source_init_urefcount(upipe);
    upipe_netmap_source_init_uref_mgr(upipe);
    upipe_netmap_source_init_ubuf_mgr(upipe);
    upipe_netmap_source_init_output(upipe);
    upipe_netmap_source_init_upump_mgr(upipe);
    upipe_netmap_source_init_upump(upipe);
    upipe_netmap_source_init_uclock(upipe);
    upipe_netmap_source->uri = NULL;
    upipe_netmap_source->d[0] = NULL;
    upipe_netmap_source->d[1] = NULL;

    upipe_netmap_source->uref     = NULL;
    upipe_netmap_source->dst_buf  = NULL;
    upipe_netmap_source->dst_size = 0;
    upipe_netmap_source->f = NULL;
    upipe_netmap_source->frate    = 0;
    upipe_netmap_source->frame    = 0;
    upipe_netmap_source->old_vss  = 0;

    upipe_netmap_source->expected_seqnum = UINT32_MAX;
    upipe_netmap_source->discontinuity = false;
    upipe_netmap_source->unpack_scratch_buffer_count = 0;

    upipe_netmap_source->sdi_to_uyvy = upipe_sdi_to_uyvy_c;
    upipe_netmap_source->bitpacked_to_v210 = upipe_sdi_to_v210_c;
    upipe_netmap_source->bitpacked_to_planar_8 = upipe_sdi_to_planar_8_c;

#if defined(HAVE_X86ASM)
#if defined(__i686__) || defined(__x86_64__)
    if (__builtin_cpu_supports("ssse3")) {
        upipe_netmap_source->sdi_to_uyvy = upipe_sdi_to_uyvy_unaligned_ssse3;
        upipe_netmap_source->bitpacked_to_v210 = upipe_sdi_to_v210_ssse3;
        upipe_netmap_source->bitpacked_to_planar_8 = upipe_sdi_to_planar_8_ssse3;
        upipe_netmap_source->bitpacked_to_planar_10 = upipe_sdi_to_planar_10_ssse3;
    }

   if (__builtin_cpu_supports("avx")) {
        upipe_netmap_source->bitpacked_to_v210 = upipe_sdi_to_v210_avx;
        upipe_netmap_source->bitpacked_to_planar_8 = upipe_sdi_to_planar_8_avx;
        upipe_netmap_source->bitpacked_to_planar_10 = upipe_sdi_to_planar_10_avx;
    }

   if (__builtin_cpu_supports("avx2")) {
        upipe_netmap_source->sdi_to_uyvy = upipe_sdi_to_uyvy_unaligned_avx2;
        upipe_netmap_source->bitpacked_to_v210 = upipe_sdi_to_v210_avx2;
        upipe_netmap_source->bitpacked_to_planar_8 = upipe_sdi_to_planar_8_avx2;
        upipe_netmap_source->bitpacked_to_planar_10 = upipe_sdi_to_planar_10_avx2;
   }
#endif
#endif

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal */
static int upipe_netmap_source_set_flow(struct upipe *upipe, uint8_t frate, uint8_t frame)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

    if (frate < 0x10 || frate > 0x1b) {
        upipe_err_va(upipe, "Invalid hbrmt frate 0x%x", frate);
        return UBASE_ERR_INVALID;
    }

    static const struct urational frate_fps[12] = {
        { 60,    1    }, // 0x10
        { 60000, 1001 }, // 0x11
        { 50,    1    }, // 0x12
        { 0,     0    }, // 0x13
        { 48,    1    }, // 0x14
        { 48000, 1001 }, // 0x15
        { 30,    1    }, // 0x16
        { 30000, 1001 }, // 0x17
        { 25,    1    }, // 0x18
        { 0,     0    }, // 0x19
        { 24,    1    }, // 0x1A
        { 24000, 1001 }, // 0x1B
    };

    const struct urational *fps = &frate_fps[frate - 0x10];
    if (fps->num == 0) {
        upipe_err_va(upipe, "Invalid hbrmt frate 0x%x", frate);
        return UBASE_ERR_INVALID;
    }

    struct uref *flow_format = uref_dup(upipe_netmap_source->flow_def);
    uref_pic_flow_set_fps(flow_format, *fps);
    if (frame == 0x10) {
        uref_pic_flow_set_hsize(flow_format, 720);
        uref_pic_flow_set_vsize(flow_format, 486);
        uref_pic_delete_progressive(flow_format);
        uref_pic_set_tff(flow_format);
    } else if (frame == 0x11) {
        uref_pic_flow_set_hsize(flow_format, 720);
        uref_pic_flow_set_vsize(flow_format, 576);
        uref_pic_delete_progressive(flow_format);
        uref_pic_set_tff(flow_format);
    } else if (frame >= 0x20 && frame <= 0x22) {
        uref_pic_flow_set_hsize(flow_format, 1920);
        uref_pic_flow_set_vsize(flow_format, 1080);
        if (frame == 0x20) {
            uref_pic_delete_progressive(flow_format);
            uref_pic_set_tff(flow_format);
        }
        else
            uref_pic_set_progressive(flow_format);
    } else if (frame >= 0x23 && frame <= 0x24) {
        uref_pic_flow_set_hsize(flow_format, 2048);
        uref_pic_flow_set_vsize(flow_format, 1080);
        uref_pic_set_progressive(flow_format);
    } else if (frame == 0x30) {
        uref_pic_flow_set_hsize(flow_format, 1280);
        uref_pic_flow_set_vsize(flow_format, 720);
        uref_pic_set_progressive(flow_format);
    } else {
        upipe_err_va(upipe, "Invalid hbrmt frame 0x%x", frame);
        uref_free(flow_format);
        return UBASE_ERR_INVALID;
    }

    const struct sdi_offsets_fmt *f = sdi_get_offsets(flow_format);
    if (!f) {
        upipe_err(upipe, "Couldn't figure out sdi offsets");
        uref_free(flow_format);
        return UBASE_ERR_INVALID;
    }
    upipe_netmap_source->f = f;

    uint64_t latency;
    if (!ubase_check(uref_clock_get_latency(flow_format, &latency)))
        latency = 0;
    latency += UCLOCK_FREQ * fps->den / fps->num;
    uref_clock_set_latency(flow_format, latency);
    upipe_netmap_source_store_flow_def(upipe, flow_format);

    uint64_t samples = f->width * f->height * 2;
    uint64_t bytes_per_frame = samples * 2 * 5 / 8;

    upipe_netmap_source->pkts_per_frame = (bytes_per_frame + HBRMT_DATA_SIZE - 1)
        / HBRMT_DATA_SIZE;

    return UBASE_ERR_NONE;
}

/** @internal */
static int upipe_netmap_source_alloc_output_uref(struct upipe *upipe, uint64_t systime)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);
    bool hbrmt = upipe_netmap_source->hbrmt;

    if (hbrmt) {
        const struct sdi_offsets_fmt *f = upipe_netmap_source->f;
        if (!f)
            return UBASE_ERR_INVALID;

        /* Only 422 accepted, so this assumption is fine */
        uint64_t samples = f->width * f->height * 2;

        upipe_netmap_source->uref = uref_block_alloc(upipe_netmap_source->uref_mgr,
                upipe_netmap_source->ubuf_mgr,
                2 * samples);
        if (unlikely(upipe_netmap_source->uref == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }

        upipe_netmap_source->dst_size = -1;
        if (unlikely(!ubase_check(uref_block_write(upipe_netmap_source->uref, 0,
                            &upipe_netmap_source->dst_size,
                            &upipe_netmap_source->dst_buf)))) {
            uref_free(upipe_netmap_source->uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    } else {
        struct uref *uref = uref_pic_alloc(upipe_netmap_source->uref_mgr,
                upipe_netmap_source->ubuf_mgr, upipe_netmap_source->hsize,
                upipe_netmap_source->vsize);

        if (unlikely(uref == NULL)) {
            upipe_netmap_source->uref = NULL;
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }

        /* map output */
        for (int i = 0; i < UPIPE_UNPACK_RFC4175_MAX_PLANES ; i++) {
            const char *chroma = upipe_netmap_source->output_chroma_map[i];
            if (chroma == NULL)
                break;

            if (unlikely(!ubase_check(uref_pic_plane_write(uref, chroma,
                                0, 0, -1, -1,
                                &upipe_netmap_source->output_plane[i])) ||
                        !ubase_check(uref_pic_plane_size(uref, chroma,
                                &upipe_netmap_source->output_stride[i],
                                NULL, NULL, NULL)))) {
                upipe_warn(upipe, "unable to map output");
                uref_free(uref);
                uref = NULL;
                break;
            }
        }

        upipe_netmap_source->uref = uref;
    }

    uref_clock_set_cr_sys(upipe_netmap_source->uref, systime);

    return UBASE_ERR_NONE;
}

static inline bool handle_rfc_packet(struct upipe *upipe, const uint8_t *src, uint16_t src_size, bool *eof)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

    const uint8_t *payload = rtp_payload((uint8_t*)src);
    size_t header_size = payload - src;
    if (header_size > src_size)
        return false;

    src = payload;
    src_size -= header_size;

    if (unlikely(!upipe_netmap_source->uref))
        return false;

    if (src_size < RFC_4175_EXT_SEQ_NUM_LEN)
        return false;

    src += RFC_4175_EXT_SEQ_NUM_LEN;
    src_size -= RFC_4175_EXT_SEQ_NUM_LEN;

    uint16_t length[2], field[2], line_number[2], line_offset[2];

    uint8_t continuation = rfc4175_get_line_continuation(src);
    for (int i = 0; i < 1 + !!continuation; i++) {
        if (src_size < RFC_4175_HEADER_LEN)
            return false;

        length[i]      = rfc4175_get_line_length(src);
        field[i]       = rfc4175_get_line_field_id(src);
        line_number[i] = rfc4175_get_line_number(src);
        line_offset[i] = rfc4175_get_line_offset(src);
        src += RFC_4175_HEADER_LEN;
        src_size -= RFC_4175_HEADER_LEN;
    }

    *eof = !!field[0];

    for (int i = 0; i < 1 + !!continuation; i++) {
        int interleaved_line = line_number[i] * 2 + !!field[i];
        if (src_size < length[i])
            return false;

        const size_t pixels = 2 * length[i] / 5;

        if (upipe_netmap_source->output_is_v210) {
            /* Start */
            uint8_t *dst = upipe_netmap_source->output_plane[0] +
                upipe_netmap_source->output_stride[0] * interleaved_line;

            /* Offset to a pixel/pblock within the line */
            dst += (line_offset[i] / 6) * 16;

            upipe_netmap_source->bitpacked_to_v210(src,
                    (uint32_t *)dst, pixels);
        } else {
            const int bit_depth = upipe_netmap_source->output_bit_depth;
            const unsigned pixel_size = (bit_depth == 10) ? 2 : 1;

            uint8_t *plane[UPIPE_UNPACK_RFC4175_MAX_PLANES];
            for (int j = 0 ; j < UPIPE_UNPACK_RFC4175_MAX_PLANES; j++) {
                const unsigned hsub = j ? 2 : 1;
                plane[j] = upipe_netmap_source->output_plane[j] +
                    upipe_netmap_source->output_stride[j] * interleaved_line +
                    pixel_size * line_offset[i] / hsub;
            }

            if (bit_depth == 8)  {
                upipe_netmap_source->bitpacked_to_planar_8(src,
                        plane[0], plane[1], plane[2], pixels);
            } else {
                upipe_netmap_source->bitpacked_to_planar_10(src,
                        (uint16_t*)plane[0], (uint16_t*)plane[1],
                        (uint16_t*)plane[2], pixels);
            }
        }

        src += length[i];
        src_size -= length[i];
    }

    return false;
}

static inline bool handle_hbrmt_packet(struct upipe *upipe, const uint8_t *src, uint16_t src_size)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);
    bool marker = rtp_check_marker(src);

    src_size -= RTP_HEADER_SIZE;
    const uint8_t *hbrmt = &src[RTP_HEADER_SIZE];

    const uint8_t frate = smpte_hbrmt_get_frate(hbrmt);
    const uint8_t frame = smpte_hbrmt_get_frame(hbrmt);
    const uint8_t map   = smpte_hbrmt_get_map(hbrmt);

    /* Level B not supported */
    if (map)
        return true;

    if (unlikely(!upipe_netmap_source->f)) {
        if (!ubase_check(upipe_netmap_source_set_flow(upipe, frate, frame))) {
            return true;
        }
        upipe_netmap_source->frate = frate;
        upipe_netmap_source->frame = frame;
    }

    if (frate != upipe_netmap_source->frate || frame != upipe_netmap_source->frame) {
        upipe_err_va(upipe, "Incorrect format (frate %u frame %u)",
               frate, frame);
        upipe_netmap_source->f = NULL;
        uref_free(upipe_netmap_source->uref);
        upipe_netmap_source->uref = NULL;
        return true;
    }

    if (unlikely(!upipe_netmap_source->uref)) {
        return false;
    }

    src_size -= HBRMT_HEADER_SIZE;
    const uint8_t *payload = &hbrmt[HBRMT_HEADER_SIZE];
    if (smpte_hbrmt_get_clock_frequency(hbrmt)) {
        payload += 4;
        src_size -= 4;
    }
    uint8_t ext = smpte_hbrmt_get_ext(hbrmt);
    if (ext) {
        payload += 4 * ext;
        src_size -= 4 * ext;
    }

    if (src_size != HBRMT_DATA_SIZE) {
        upipe_dbg_va(upipe, "Too small packet, ignoring, %i", src_size);
        return true; // discontinuity
    }

    /* If there is data in the scratch buffer... */

    unsigned n = upipe_netmap_source->unpack_scratch_buffer_count;
    if (n && upipe_netmap_source->dst_size > 4 * 2) {
        /* Copy from the new "packet" into the end... */
        memcpy(&upipe_netmap_source->unpack_scratch_buffer[n], payload, 5 - n);

        /* Advance input buffer. */
        payload += 5-n;
        src_size -= 5-n;

        /* Unpack from the scratch buffer. */
        upipe_sdi_to_uyvy_c(upipe_netmap_source->unpack_scratch_buffer,
                (uint16_t*)upipe_netmap_source->dst_buf, 2);

        /* Advance output buffer by 2 pixels */
        upipe_netmap_source->dst_buf += 4 * 2;
        upipe_netmap_source->dst_size -= 4 * 2;
        //printf("HI SCRATCH 2\n");

        /* Set scratch count to 0. */
        upipe_netmap_source->unpack_scratch_buffer_count = 0;
    }

    if (src_size > upipe_netmap_source->dst_size * 5 / 8) {
        src_size = upipe_netmap_source->dst_size * 5 / 8;
        if (!marker) {
            return true; // discontinuity
            upipe_err_va(upipe, "Not overflowing output packet: %d, %d",
                    src_size, upipe_netmap_source->dst_size);
        }
    }

    int unpack_bytes = (src_size / 5) * 5;
    int unpack_pixels = (unpack_bytes * 2) / 5;
    upipe_netmap_source->sdi_to_uyvy(payload, (uint16_t*)upipe_netmap_source->dst_buf, unpack_pixels);
    upipe_netmap_source->dst_buf += 4 * unpack_pixels;
    upipe_netmap_source->dst_size -= 4 * unpack_pixels;

    /* If we have any bytes remaining... */
    if (unpack_bytes < src_size) {
        /* Copy them into the scratch buffer. */
        memcpy(upipe_netmap_source->unpack_scratch_buffer, &payload[unpack_bytes],
                src_size - unpack_bytes);
        upipe_netmap_source->unpack_scratch_buffer_count = src_size - unpack_bytes;
    }

    return false;
}

static uint64_t get_vss(const uint8_t *vss)
{
    uint64_t s = (vss[0] << 24) |
        (vss[1] << 16) |
        (vss[2] <<  8) |
        (vss[3]      );
    uint8_t clksrc = vss[4] >> 6;
    assert(clksrc == 0); // internal
    uint32_t ns = ((vss[4] & 0x3f) << 24) |
        (vss[5] << 16) |
        (vss[6] <<  8) |
        (vss[7]      );
    return s * 1000000000 + ns;
}

static const uint8_t *get_rtp(struct upipe *upipe, struct netmap_ring *rxring,
        struct netmap_slot *slot, uint16_t *payload_len)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);
    uint8_t *src = (uint8_t*)NETMAP_BUF(rxring, slot->buf_idx);

    if (ethernet_get_lentype(src) != ETHERNET_TYPE_IP)
        return NULL;

    uint8_t *ip = &src[ETHERNET_HEADER_LEN];
    if (ip_get_proto(ip) != IP_PROTO_UDP)
        return NULL;

    uint8_t *udp = ip_payload(ip);
    const uint8_t *rtp = udp_payload(udp);
    *payload_len = udp_get_len(udp) - UDP_HEADER_SIZE;

    if (!upipe_netmap_source->hbrmt)
        return rtp;

    unsigned min_pkt_size = RTP_HEADER_SIZE + HBRMT_HEADER_SIZE + HBRMT_DATA_SIZE;

    if (*payload_len < min_pkt_size) {
        return NULL;
    }

    min_pkt_size += UDP_HEADER_SIZE + 4 * ip_get_ihl(ip) + ETHERNET_HEADER_LEN;

    if (slot->len < min_pkt_size) {
        return NULL;
    }

    if (slot->len - 9 >= min_pkt_size) {
        const uint8_t *vss = &src[slot->len-9];
        if (vss[8] == 0xc3) {
            struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);
            uint64_t ts = get_vss(vss);
            if (ts - upipe_netmap_source->old_vss > 70 * 1000 /* us */)
                upipe_err_va(upipe, "Spike: %" PRIu64, ts - upipe_netmap_source->old_vss);
            upipe_netmap_source->old_vss = ts;
        }
    }

    return rtp;
}

/** @internal */
static void upipe_netmap_source_prepare_frame(struct upipe *upipe, uint32_t timestamp)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);
    struct uref *uref = upipe_netmap_source->uref;

    /* unmap output */
    for (int i = 0; i < UPIPE_UNPACK_RFC4175_MAX_PLANES; i++) {
        const char *chroma = upipe_netmap_source->output_chroma_map[i];
        if (chroma == NULL)
            break;
        uref_pic_plane_unmap(upipe_netmap_source->uref,
                chroma, 0, 0, -1, -1);
    }

    uint64_t delta =
        (UINT32_MAX + timestamp -
         (upipe_netmap_source->last_timestamp % UINT32_MAX)) % UINT32_MAX;
    upipe_netmap_source->last_timestamp += delta;

    uint64_t pts = upipe_netmap_source->last_timestamp;
    pts = pts * UCLOCK_FREQ / 90000;

    uref_clock_set_pts_prog(uref, pts);
    uref_clock_set_pts_orig(uref, timestamp * UCLOCK_FREQ / 90000);
    uref_clock_set_dts_pts_delay(uref, 0);

    upipe_throw_clock_ref(upipe, uref, pts, 0);
    upipe_throw_clock_ts(upipe, uref);
}

#define GOT_SEQNUM (1LLU<<49)
/*
 * do_packet : examine next packet in queue and handle if possible
 *
 * Return value:
 * UINT64_MAX       discontinuity, packet is in the future, keep it for later
 * != UINT64_MAX    packet handled or discontinuity, drop it
 * 0 -              invalid packet, drop it
 * 0000000XTTTTTTTTTTTTTTTTSSSSSSSS
 *      T = 32-bit RTP timestamp
 *      S = 16-bit RTP seqnum
 *      bit 48 is set to account for timestamp == seqnum == 0
 *      bit 49 is set when we found the seqnum we were looking for
 *
 *
 */
static uint64_t do_packet(struct upipe *upipe, struct netmap_ring *rxring,
        const uint32_t cur, uint64_t systime)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

    uint16_t pkt_size;
    const uint8_t *rtp = get_rtp(upipe, rxring, &rxring->slot[cur], &pkt_size);
    if (!rtp || !rtp_check_hdr(rtp))
        return 0;

    uint16_t seqnum = rtp_get_seqnum(rtp);
    uint32_t timestamp = rtp_get_timestamp(rtp);

    /* 1 << 48 to signal valid packet */
    uint64_t ret = (UINT64_C(1) << 48) | (((uint64_t)timestamp) << 16) | seqnum;

    if (unlikely(upipe_netmap_source->expected_seqnum != UINT32_MAX &&
                seqnum != upipe_netmap_source->expected_seqnum)) {
        uint16_t diff = seqnum - upipe_netmap_source->expected_seqnum;
        uint32_t timestamp_diff = timestamp - upipe_netmap_source->last_timestamp;

        if (diff > 0x8000 ) {
            /* seqnum < expected, drop */
            return ret;
        }

        if (timestamp_diff > 0x80000000) {
            /* packet is way too far in the future, drop */
            return ret;
        }

        /* seqnum > expected, keep */
        return UINT64_MAX;
    }

    ret |= GOT_SEQNUM;
    /* We have a valid packet and expected sequence number from here on */

    bool marker = rtp_check_marker(rtp);
    bool hbrmt = upipe_netmap_source->hbrmt;

    if (!upipe_netmap_source->discontinuity) {
        if (hbrmt) {
            if (handle_hbrmt_packet(upipe, rtp, pkt_size))
                /* error handling packet, drop */
                return ret;
        } else {
            bool eof = false;
            if (handle_rfc_packet(upipe, rtp, pkt_size, &eof))
                /* error handling packet, drop */
                return ret;

            marker &= eof;
        }

        upipe_netmap_source->packets++;
    }

    if ((marker || upipe_netmap_source->discontinuity) && upipe_netmap_source->uref) {
        if (hbrmt) {
            uref_block_unmap(upipe_netmap_source->uref, 0);

            if (upipe_netmap_source->packets != upipe_netmap_source->pkts_per_frame)
                upipe_netmap_source->discontinuity = true;
        } else {
            upipe_netmap_source_prepare_frame(upipe, timestamp);
        }

        if (upipe_netmap_source->discontinuity)
            uref_flow_set_discontinuity(upipe_netmap_source->uref);

        /* output current block */
        upipe_netmap_source_output(upipe, upipe_netmap_source->uref, &upipe_netmap_source->upump);
        upipe_netmap_source->uref = NULL;
    }

    if (marker && (upipe_netmap_source->discontinuity || !upipe_netmap_source->uref)) {
        /* reset discontinuity when we see the next marker */
        upipe_netmap_source->discontinuity = false;
        upipe_netmap_source_alloc_output_uref(upipe, systime);
        upipe_netmap_source->packets = 0;
    }

    upipe_netmap_source->expected_seqnum = ++seqnum & UINT16_MAX;
    upipe_netmap_source->last_timestamp = timestamp;
    return ret;
}

static void upipe_netmap_source_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

    uint64_t systime = 0;
    if (likely(upipe_netmap_source->uclock != NULL))
        systime = uclock_now(upipe_netmap_source->uclock);

    struct netmap_ring *rxring[2];
    uint32_t pkts[2];

    /* update both interfaces */
    for (int i = 0; i < 2; i++) {
        if (!upipe_netmap_source->d[i]) {
            rxring[i] = NULL;
            pkts[i] = 0;
            continue;
        }

        ioctl(NETMAP_FD(upipe_netmap_source->d[i]), NIOCRXSYNC, NULL);
        rxring[i] = NETMAP_RXRING(upipe_netmap_source->d[i]->nifp,
                upipe_netmap_source->ring_idx[i]);

        pkts[i] = nm_ring_space(rxring[i]);
    }

    int sources = !!pkts[0] + !!pkts[1];
    while (pkts[0] || pkts[1]) {
        int discontinuity = 0;
        for (int idx = 0; idx < 2; idx++) { // one queue then the other
            if (!upipe_netmap_source->d[idx] || !pkts[idx])
                continue;

            do {
                const uint32_t cur = rxring[idx]->cur;
                uint64_t ret = do_packet(upipe, rxring[idx], cur, systime);
                if (ret == UINT64_MAX) {
                    /* discontinuity, packet is in the future */
                    discontinuity++;

                    /* we did not find the packet we wanted */
                    if (pkts[!idx] == 0) {
                        /* if the other queue is empty, end the loop here.
                         * we might find the packet we want in the other queue,
                         * when it fills again
                         */
                        pkts[idx] = 0;
                    }
                    break; /* keep packet */
                } else if (ret & GOT_SEQNUM) {
                    discontinuity = 0;
                }
                rxring[idx]->head = rxring[idx]->cur = nm_ring_next(rxring[idx], cur);
                pkts[idx]--;
            } while (pkts[idx]);
        }

        if (discontinuity == sources) {
            //upipe_err(upipe, "DISCONTINUITY"); // TODO: # of packets lost
            upipe_netmap_source->discontinuity = true;
            if (upipe_netmap_source->uref) {
                if (upipe_netmap_source->hbrmt) {
                    uref_block_unmap(upipe_netmap_source->uref, 0);
                } else {
                    /* unmap output */
                    for (int i = 0; i < UPIPE_UNPACK_RFC4175_MAX_PLANES; i++) {
                        const char *chroma = upipe_netmap_source->output_chroma_map[i];
                        if (chroma == NULL)
                            break;
                        uref_pic_plane_unmap(upipe_netmap_source->uref,
                                chroma, 0, 0, -1, -1);
                    }
                }

                uref_free(upipe_netmap_source->uref);
            }
            upipe_netmap_source->uref = NULL;
            upipe_netmap_source->expected_seqnum = UINT32_MAX;
        }
    }
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_netmap_source_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);
    if (flow_format != NULL) {
        // FIXME WTF
        if (upipe_netmap_source->flow_def == NULL)
            upipe_netmap_source_store_flow_def(upipe, flow_format);
        else
            uref_free(flow_format);
    }

    upipe_netmap_source_check_upump_mgr(upipe);
    if (upipe_netmap_source->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_netmap_source->uref_mgr == NULL) {
        upipe_netmap_source_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_netmap_source->ubuf_mgr == NULL) {
        struct uref *flow_def = NULL;
        if (upipe_netmap_source->hbrmt) {
            flow_def = uref_block_flow_alloc_def(upipe_netmap_source->uref_mgr,
                    NULL);
            if (unlikely(flow_def == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return UBASE_ERR_ALLOC;
            }
        } else {
            flow_def = uref_dup(upipe_netmap_source->rfc_def);
            uref_dump(flow_def, upipe->uprobe);

            if (upipe_netmap_source->output_is_v210) {
                upipe_netmap_source->output_chroma_map[0] = "u10y10v10y10u10y10v10y10u10y10v10y10";
                upipe_netmap_source->output_chroma_map[1] = NULL;
                upipe_netmap_source->output_chroma_map[2] = NULL;
            } else if (upipe_netmap_source->output_bit_depth == 8) {
                upipe_netmap_source->output_chroma_map[0] = "y8";
                upipe_netmap_source->output_chroma_map[1] = "u8";
                upipe_netmap_source->output_chroma_map[2] = "v8";
            } else {
                upipe_netmap_source->output_chroma_map[0] = "y10l";
                upipe_netmap_source->output_chroma_map[1] = "u10l";
                upipe_netmap_source->output_chroma_map[2] = "v10l";
            }
        }

        upipe_netmap_source_require_ubuf_mgr(upipe, flow_def);
        return UBASE_ERR_NONE;
    }

    if (upipe_netmap_source->uclock == NULL &&
        urequest_get_opaque(&upipe_netmap_source->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    int idx = 0; // only need to check first interface
    if (!upipe_netmap_source->d[idx])
        return UBASE_ERR_NONE;

    if (NETMAP_FD(upipe_netmap_source->d[idx]) == -1)
        return UBASE_ERR_NONE;

    if (upipe_netmap_source->upump)
        return UBASE_ERR_NONE;

    struct upump *upump = upump_alloc_timer(upipe_netmap_source->upump_mgr,
            upipe_netmap_source_worker, upipe, upipe->refcount, 0,
            UCLOCK_FREQ/1000);

    upipe_netmap_source_set_upump(upipe, upump);
    upump_start(upump);

    return UBASE_ERR_NONE;
}

/** @internal @This returns the uri of the currently opened netmap.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the uri of the netmap source
 * @return an error code
 */
static int upipe_netmap_source_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_netmap_source->uri;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given netmap source.
 *
 * @param upipe description structure of the pipe
 * @param uri of the netmap source
 * @return an error code
 */
static int upipe_netmap_source_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

    for (int idx = 0; idx < 2; idx++) {
        if (upipe_netmap_source->d[idx]) {
            nm_close(upipe_netmap_source->d[idx]);
            upipe_netmap_source->d[idx] = NULL;
        }
    }
    ubase_clean_str(&upipe_netmap_source->uri);
    upipe_netmap_source_set_upump(upipe, NULL);

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    upipe_netmap_source->uri = strdup(uri);
    if (unlikely(upipe_netmap_source->uri == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    char *p = strchr(upipe_netmap_source->uri, '+');
    if (p)
        *p++ = '\0';

    for (int idx = 0; idx < 2; idx++) {
        char *uri = upipe_netmap_source->uri;
        if (idx) {
            uri = p;
            if (!uri)
                break;
        }

        if (sscanf(uri, "%*[^-]-%u/R",
                    &upipe_netmap_source->ring_idx[idx]) != 1) {
            upipe_netmap_source->ring_idx[idx] = 0;
        }

        upipe_netmap_source->d[idx] = nm_open(uri, NULL, 0, 0);
        if (unlikely(!upipe_netmap_source->d[idx])) {
            upipe_err_va(upipe, "can't open netmap socket %s", uri);
            return UBASE_ERR_EXTERNAL;
        }

        upipe_notice_va(upipe, "opening netmap socket %s ring %u",
                uri, upipe_netmap_source->ring_idx[idx]);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a netmap source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_netmap_source_control(struct upipe *upipe,
                                 int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_netmap_source_set_upump(upipe, NULL);
            return upipe_netmap_source_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_netmap_source_set_upump(upipe, NULL);
            upipe_netmap_source_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_netmap_source_control_output(upipe, command, args);

        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_netmap_source_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_netmap_source_set_uri(upipe, uri);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a netmap source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_netmap_source_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_netmap_source_control(upipe, command, args));

    return upipe_netmap_source_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_netmap_source_free(struct upipe *upipe)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

    for (int idx = 0; idx < 2; idx++)
        if (upipe_netmap_source->d[idx])
            nm_close(upipe_netmap_source->d[idx]);

    upipe_throw_dead(upipe);

    if (upipe_netmap_source->uref) {
        if (upipe_netmap_source->hbrmt) {
            uref_block_unmap(upipe_netmap_source->uref, 0);
        } else {
            /* unmap output */
            for (int i = 0; i < UPIPE_UNPACK_RFC4175_MAX_PLANES; i++) {
                const char *chroma = upipe_netmap_source->output_chroma_map[i];
                if (chroma == NULL)
                    break;
                uref_pic_plane_unmap(upipe_netmap_source->uref,
                        chroma, 0, 0, -1, -1);
            }
        }
        uref_free(upipe_netmap_source->uref);
    }
    free(upipe_netmap_source->uri);

    uref_free(upipe_netmap_source->rfc_def);
    upipe_netmap_source_clean_uclock(upipe);
    upipe_netmap_source_clean_upump(upipe);
    upipe_netmap_source_clean_upump_mgr(upipe);
    upipe_netmap_source_clean_output(upipe);
    upipe_netmap_source_clean_ubuf_mgr(upipe);
    upipe_netmap_source_clean_uref_mgr(upipe);
    upipe_netmap_source_clean_urefcount(upipe);
    upipe_netmap_source_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_netmap_source_mgr = {
    .refcount = NULL,
    .signature = UPIPE_NETMAP_SOURCE_SIGNATURE,

    .upipe_alloc = upipe_netmap_source_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_netmap_source_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all netmap sources
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_netmap_source_mgr_alloc(void)
{
    return &upipe_netmap_source_mgr;
}
