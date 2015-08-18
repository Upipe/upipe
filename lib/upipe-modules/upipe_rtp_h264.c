/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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

#include <stdlib.h>

#include <upipe/upipe.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_block.h>
#include <upipe/uref_pic.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe-modules/upipe_rtp_h264.h>

const uint8_t *upipe_mpeg_scan(const uint8_t *p, const uint8_t *end,
                               uint8_t *len)
{
    while (p < end) {
        if ((p + 2 < end) && p[0] == 0 && p[1] == 0) {
            if ((p + 3 < end) && p[2] == 1) {
                *len = 3;
                return p;
            }
            else if ((p + 4 < end) && p[2] == 0 && p[3] == 1) {
                *len = 4;
                return p;
            }
        }
        p++;
    }
    return NULL;
}

struct upipe_rtp_h264 {
    /** refcount management structure */
    struct urefcount urefcount;

    /* output stuff */
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtp_h264, upipe, UPIPE_RTP_H264_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_rtp_h264, urefcount, upipe_rtp_h264_free)
UPIPE_HELPER_VOID(upipe_rtp_h264)
UPIPE_HELPER_OUTPUT(upipe_rtp_h264, output, flow_def, output_state,
                    request_list)

static int upipe_rtp_h264_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "block.h264."))

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_rtp_h264_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

static int upipe_rtp_h264_control(struct upipe *upipe, int command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_rtp_h264_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtp_h264_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_rtp_h264_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_rtp_h264_set_output(upipe, output);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_rtp_h264_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_rtp_h264_clean_output(upipe);
    upipe_rtp_h264_clean_urefcount(upipe);
    upipe_rtp_h264_free_void(upipe);
}

static struct upipe *upipe_rtp_h264_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature,
                                          va_list args)
{
    struct upipe *upipe =
        upipe_rtp_h264_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_rtp_h264_init_urefcount(upipe);
    upipe_rtp_h264_init_output(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

#define RTP_SPLIT_SIZE  1400
#define FU_START        (1 << 7)
#define FU_END          (1 << 6)
#define FU_A            28

#define NALU_F(Nalu)    ((Nalu) & 0x80)
#define NALU_NRI(Nalu)  ((Nalu) & 0x60)
#define NALU_TYPE(Nalu) ((Nalu) & 0x1f)
#define NALU_TYPE_DELIM 0x09

static void upipe_rtp_h264_output_nalu(struct upipe *upipe,
                                       uint8_t nalu,
                                       struct uref *uref,
                                       struct upump **upump_p)
{
    size_t size = 0;
    int offset = 0;
    if (unlikely(!ubase_check(uref_block_size(uref, &size)))) {
        upipe_err(upipe, "fail to get block size");
        return;
    }

    bool split = size > RTP_SPLIT_SIZE;
    uint32_t fragment = 0;

    while (size) {
        bool last_fragment = size <= RTP_SPLIT_SIZE;
        size_t split_size = last_fragment ? size : RTP_SPLIT_SIZE;
        uint8_t hdr[2] = { nalu, 0 };
        size_t hdr_size = 1;

        if (split) {
            if (!fragment)
                hdr[1] = FU_START;
            else if (last_fragment)
                hdr[1] = FU_END;
            hdr[1] |= NALU_TYPE(hdr[0]);

            hdr[0] = NALU_F(hdr[0]) | NALU_NRI(hdr[0]) | FU_A;
            hdr_size++;
        }

        struct uref *part = uref_block_splice(uref, offset, split_size);
        if (unlikely(part == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        struct ubuf *header =
                ubuf_block_alloc(uref->ubuf->mgr, hdr_size * sizeof (hdr[0]));
        if (unlikely(header == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(part);
            return;
        }

        uint8_t *buf = NULL;
        int buf_size = hdr_size;
        ubuf_block_write(header, 0, &buf_size, &buf);
        memcpy(buf, hdr, hdr_size * sizeof (hdr[0]));
        ubuf_block_unmap(header, 0);

        /* append payload (current ubuf) to header to form segmented ubuf */
        struct ubuf *payload = uref_detach_ubuf(part);
        if (unlikely(!ubase_check(ubuf_block_append(header, payload)))) {
            upipe_warn(upipe, "could not append payload to header");
            ubuf_free(header);
            uref_free(part);
            return;
        }
        uref_attach_ubuf(part, header);
        uref_clock_set_cr_dts_delay(part, 0);
        upipe_rtp_h264_output(upipe, part, upump_p);
        size -= split_size;
        offset += split_size;
        fragment++;
    }
}

static void upipe_rtp_h264_drop(struct upipe *upipe, struct uref *uref)
{
    upipe_warn(upipe, "drop...");
    uref_free(uref);
}

static void upipe_rtp_h264_input(struct upipe *upipe,
                                struct uref *uref,
                                struct upump **upump_p)
{
    size_t bz = 0;
    if (!ubase_check(uref_block_size(uref, &bz))) {
        upipe_err(upipe, "fail to get uref block size");
        return upipe_rtp_h264_drop(upipe, uref);
    }

    uint8_t buf[bz];
    int size = bz;
    if (unlikely(!ubase_check(uref_block_extract(uref, 0, size, buf)))) {
        upipe_err(upipe, "fail to read from uref");
        return upipe_rtp_h264_drop(upipe, uref);
    }

    uint8_t s_len;
    const uint8_t *s = upipe_mpeg_scan(buf, buf + size, &s_len);
    if (s != buf) {
        upipe_err(upipe, "uref does not start with a mpeg start code");
        return upipe_rtp_h264_drop(upipe, uref);
    }

    while (s < buf + size) {
        uint8_t e_len = 0;
        const uint8_t *e = upipe_mpeg_scan(s + s_len, buf + size, &e_len);
        if (!e)
            e = buf + size;

        struct uref *part = uref_block_splice(uref, s - buf + s_len + 1,
                                              e - (s + s_len + 1));
        upipe_rtp_h264_output_nalu(upipe, *(s + s_len), part, upump_p);
        uref_free(part);
        s = e;
        s_len = e_len;
    }

    uref_free(uref);
}

static struct upipe_mgr upipe_rtp_h264_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTP_H264_SIGNATURE,

    .upipe_alloc = upipe_rtp_h264_alloc,
    .upipe_input = upipe_rtp_h264_input,
    .upipe_control = upipe_rtp_h264_control,

    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_rtp_h264_mgr_alloc(void)
{
    return &upipe_rtp_h264_mgr;
}
