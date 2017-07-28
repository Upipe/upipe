/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd
 * Copyright (C) 2000-2016 DekTec Digital Video B.V.
 *
 * Authors: Rafaël Carré
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

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_output_size.h>

#include <upipe-modules/upipe_dtsdi.h>

#define DTSDI_SDI_FULL          0x0001      // Full SDI frame
#define DTSDI_SDI_ACTVID        0x0002      // Active video only
#define DTSDI_SDI_HANC          0x0004      // HANC section only
#define DTSDI_SDI_VANC          0x0008      // VANC section only

// The following flags can be OR-ed with: DTSDI_SDI_FULL or DTSDI_SDI_ACTVID
#define DTSDI_SDI_8B            0x0000      // 8-bit samples
#define DTSDI_SDI_10B           0x0080      // 10-bit samples
#define DTSDI_SDI_16B           0x0100      // 16-bit samples,
#define DTSDI_SDI_HUFFMAN       0x0200      // compressed SDI samples

#define DTSDI_TYPE_SDI_UNKNOWN        -1

#define DTSDI_TYPE_SDI_625I50        0x01
#define DTSDI_TYPE_SDI_525I59_94     0x02
#define DTSDI_TYPE_SDI_720P23_98     0x03
#define DTSDI_TYPE_SDI_720P24        0x04
#define DTSDI_TYPE_SDI_720P25        0x05
#define DTSDI_TYPE_SDI_720P29_97     0x06
#define DTSDI_TYPE_SDI_720P30        0x07
#define DTSDI_TYPE_SDI_720P50        0x08
#define DTSDI_TYPE_SDI_720P59_94     0x09
#define DTSDI_TYPE_SDI_720P60        0x0A
#define DTSDI_TYPE_SDI_1080P23_98    0x0B
#define DTSDI_TYPE_SDI_1080P24       0x0C
#define DTSDI_TYPE_SDI_1080P25       0x0D
#define DTSDI_TYPE_SDI_1080P30       0x0E
#define DTSDI_TYPE_SDI_1080P29_97    0x0F
#define DTSDI_TYPE_SDI_1080I50       0x10
#define DTSDI_TYPE_SDI_1080I59_94    0x11
#define DTSDI_TYPE_SDI_1080I60       0x12
#define DTSDI_TYPE_SDI_1080P50       0x13
#define DTSDI_TYPE_SDI_1080P59_94    0x14
#define DTSDI_TYPE_SDI_1080P60       0x15
#define DTSDI_TYPE_SDI_1080PSF23_98  0x16
#define DTSDI_TYPE_SDI_1080PSF24     0x17
#define DTSDI_TYPE_SDI_1080PSF25     0x18
#define DTSDI_TYPE_SDI_1080PSF29_97  0x19
#define DTSDI_TYPE_SDI_1080PSF30     0x1A

struct upipe_dtsdi {
    struct urefcount urefcount;

    struct upipe *output;
    struct uref *flow_def;
    enum upipe_helper_output_state output_state;
    struct uchain request_list;

    unsigned int output_size;

    struct uref *uref;

    int sdi_type;
    size_t frame_size;

    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dtsdi, upipe, UPIPE_DTSDI_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_dtsdi, urefcount, upipe_dtsdi_free)
UPIPE_HELPER_VOID(upipe_dtsdi)

UPIPE_HELPER_OUTPUT(upipe_dtsdi, output, flow_def, output_state, request_list)
UPIPE_HELPER_OUTPUT_SIZE(upipe_dtsdi, output_size)

static int set_flow_def(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_dtsdi *upipe_dtsdi = upipe_dtsdi_from_upipe(upipe);
    int std = upipe_dtsdi->sdi_type;
    struct urational fps = { 0, 0 };
    uint64_t hsize, vsize;
    size_t cols = 0;
    size_t lines;

    if (std >= DTSDI_TYPE_SDI_1080P23_98 && std <= DTSDI_TYPE_SDI_1080PSF30) {
        hsize = 1920;
        vsize = 1080;
        lines = 1125;
    } else if (std >= DTSDI_TYPE_SDI_720P23_98 && std <= DTSDI_TYPE_SDI_720P60) {
        hsize = 1280;
        vsize = 720;
        lines = 750;
    } else if (std == DTSDI_TYPE_SDI_525I59_94) {
        hsize = 720;
        vsize = 480;
        cols  = 858;
        lines = 525;
    } else if (std == DTSDI_TYPE_SDI_625I50) {
        hsize = 720;
        vsize = 576;
        cols  = 864;
        lines = 625;
    } else {
        return UBASE_ERR_INVALID;
    }

    switch(std) {
    case DTSDI_TYPE_SDI_525I59_94:
        fps.num = 30000;
        fps.den = 1001;
        break;
    case DTSDI_TYPE_SDI_625I50:
        fps.num = 25;
        fps.den = 1;
        break;
    case DTSDI_TYPE_SDI_1080P23_98:
    case DTSDI_TYPE_SDI_1080PSF23_98:
    case DTSDI_TYPE_SDI_720P23_98:
        fps.num = 24000;
        fps.den = 1001;
        break;
    case DTSDI_TYPE_SDI_1080P24:
    case DTSDI_TYPE_SDI_1080PSF24:
    case DTSDI_TYPE_SDI_720P24:
        fps.num = 24;
        fps.den = 1;
        break;
    case DTSDI_TYPE_SDI_1080P25:
    case DTSDI_TYPE_SDI_1080PSF25:
    case DTSDI_TYPE_SDI_1080I50:
    case DTSDI_TYPE_SDI_720P25:
        fps.num = 25;
        fps.den = 1;
        break;
    case DTSDI_TYPE_SDI_1080P29_97:
    case DTSDI_TYPE_SDI_1080PSF29_97:
    case DTSDI_TYPE_SDI_1080I59_94:
    case DTSDI_TYPE_SDI_720P29_97:
        fps.num = 30000;
        fps.den = 1001;
        break;
    case DTSDI_TYPE_SDI_1080P30:
    case DTSDI_TYPE_SDI_1080PSF30:
    case DTSDI_TYPE_SDI_1080I60:
    case DTSDI_TYPE_SDI_720P30:
        fps.num = 30;
        fps.den = 1;
        break;
    case DTSDI_TYPE_SDI_1080P50:
//    case DTSDI_TYPE_SDI_1080P50B:
    case DTSDI_TYPE_SDI_720P50:
        fps.num = 50;
        fps.den = 1;
        break;
    case DTSDI_TYPE_SDI_1080P59_94:
//    case DTSDI_TYPE_SDI_1080P59_94B:
    case DTSDI_TYPE_SDI_720P59_94:
        fps.num = 60000;
        fps.den = 1001;
        break;
    case DTSDI_TYPE_SDI_1080P60:
//    case DTSDI_TYPE_SDI_1080P60B:
    case DTSDI_TYPE_SDI_720P60:
        fps.num = 60;
        fps.den = 1;
        break;
    default:
        return UBASE_ERR_INVALID;
    }

    if (std >= DTSDI_TYPE_SDI_1080P23_98 && std <= DTSDI_TYPE_SDI_1080PSF30) {
        if (fps.num == 24 || fps.num == 24000) {
            cols = 2750;
        } else if (fps.num == 25 || fps.num == 50) {
            cols = 2640;
        } else if (fps.num == 30 || fps.num == 30000
                || fps.num == 60 || fps.num == 60000) {
            cols = 2200;
        }
    } else if (std >= DTSDI_TYPE_SDI_720P23_98 && std <= DTSDI_TYPE_SDI_720P60) {
        if (fps.num == 50) {
            cols = 1980;
        } else if (fps.num == 60000) {
            cols = 1650;
        } else {
            /* Missing: 23.98, 24, 25, 29.97, 30, 60 */
            upipe_err_va(upipe, "Unknown SDI size for 720p %" PRId64 "/%" PRIu64,
                fps.num, fps.den);
            return UBASE_ERR_INVALID;
        }
    }

    upipe_dtsdi->frame_size = 2 * 2 /* 16-bit */ * lines * cols;
    upipe_dtsdi_set_output_size(upipe, upipe_dtsdi->frame_size);

    UBASE_RETURN(uref_pic_flow_set_hsize(flow_format, hsize));
    UBASE_RETURN(uref_pic_flow_set_vsize(flow_format, vsize));
    UBASE_RETURN(uref_pic_flow_set_fps(flow_format, fps));

    return UBASE_ERR_NONE;
}

static int upipe_dtsdi_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_dtsdi *upipe_dtsdi = upipe_dtsdi_from_upipe(upipe);

    if (flow_format != NULL) {
        if (upipe_dtsdi->sdi_type != DTSDI_TYPE_SDI_UNKNOWN)
            if (!ubase_check(set_flow_def(upipe, flow_format))) {
                upipe_err_va(upipe, "Could not find frame rate");
                return UBASE_ERR_INVALID;
            }
        upipe_dtsdi_store_flow_def(upipe, flow_format);
    }

    return UBASE_ERR_NONE;
}

static void upipe_dtsdi_free(struct upipe *upipe)
{
    struct upipe_dtsdi *upipe_dtsdi = upipe_dtsdi_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_dtsdi->uref);

    upipe_dtsdi_clean_output_size(upipe);
    upipe_dtsdi_clean_output(upipe);
    upipe_dtsdi_clean_urefcount(upipe);
    upipe_dtsdi_free_void(upipe);
}

static struct upipe *upipe_dtsdi_alloc(struct upipe_mgr *mgr,
        struct uprobe *uprobe,
        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_dtsdi_alloc_void(mgr, uprobe, signature, args);
    if (!upipe)
        return NULL;

    struct upipe_dtsdi *upipe_dtsdi = upipe_dtsdi_from_upipe(upipe);

    upipe_dtsdi_init_urefcount(upipe);
    upipe_dtsdi_init_output(upipe);

    upipe_dtsdi->sdi_type = DTSDI_TYPE_SDI_UNKNOWN;
    upipe_dtsdi->uref = NULL;
    upipe_dtsdi->frame_size = 0;

    upipe_dtsdi_init_output_size(upipe, 0);

    upipe_throw_ready(upipe);

    return upipe;
}

static int upipe_dtsdi_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_dtsdi *upipe_dtsdi = upipe_dtsdi_from_upipe(upipe);
    if (!flow_def)
        return UBASE_ERR_INVALID;

    upipe_dtsdi->sdi_type = DTSDI_TYPE_SDI_UNKNOWN;
    uref_free(upipe_dtsdi->uref);
    upipe_dtsdi->uref = NULL;
    upipe_dtsdi->frame_size = 0;

    upipe_dtsdi_store_flow_def(upipe, flow_def);

    return UBASE_ERR_NONE;
}

static int upipe_dtsdi_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *p = va_arg(args, struct uref *);
            return upipe_dtsdi_set_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT_SIZE: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_dtsdi_get_output_size(upipe, p);
        }
        case UPIPE_SET_OUTPUT_SIZE: {
            unsigned int output_size = va_arg(args, unsigned int);
            return upipe_dtsdi_set_output_size(upipe, output_size);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_dtsdi_control_output(upipe, command, args);

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static int upipe_dtsdi_header(struct upipe *upipe, struct uref *uref)
{
    struct upipe_dtsdi *upipe_dtsdi = upipe_dtsdi_from_upipe(upipe);

    uint8_t buf[24];

    const uint8_t *header = uref_block_peek(uref, 0, sizeof(buf), buf);
    if (!header) {
        upipe_err_va(upipe, "Could not read DTSDI header");
        upipe_throw_fatal(upipe, UBASE_ERR_INVALID);
        return DTSDI_TYPE_SDI_UNKNOWN;
    }

    uint8_t version = header[12];
    uint8_t type    = header[13];
    uint16_t flags  = (header[15] << 8) | header[14];

    if (memcmp(header, "DekTec.dtsdi", 12)) {
        upipe_err(upipe, "Invalid signature");
        goto fail;
    }

    if (version > 1) {
        upipe_err_va(upipe, "Unknown version %d", version);
        goto fail;
    }

    if (flags != (DTSDI_SDI_16B|DTSDI_SDI_FULL)) {
        upipe_err_va(upipe, "Unsupported flags 0x%.4x", flags);
        goto fail;
    }

    int header_size = 16;
    if (version == 1) {
        header_size += 8;
        uint32_t frame_size = (header[0x13] << 24) | (header[0x12] << 16) |
            (header[0x11] << 8) | header[0x10];
        uint32_t frames =     (header[0x17] << 24) | (header[0x16] << 16) |
            (header[0x15] << 8) | header[0x14];
        /* file frame size is not reliable */
        upipe_dbg_va(upipe, "%u frames (frame size %u)", frames, frame_size);
    }

    uref_block_peek_unmap(uref, 0, buf, header);

    if (!ubase_check(uref_block_resize(uref, header_size, -1)))
        return DTSDI_TYPE_SDI_UNKNOWN;

    return type;

fail:
    upipe_err(upipe, "Invalid DTSDI header");
    uref_block_peek_unmap(uref, 0, buf, header);
    return DTSDI_TYPE_SDI_UNKNOWN;
}

static void upipe_dtsdi_input(struct upipe *upipe, struct uref *uref,
        struct upump **upump_p)
{
    struct upipe_dtsdi *upipe_dtsdi = upipe_dtsdi_from_upipe(upipe);

    if (upipe_dtsdi->sdi_type == DTSDI_TYPE_SDI_UNKNOWN) {
        upipe_dtsdi->sdi_type = upipe_dtsdi_header(upipe, uref);
        if (upipe_dtsdi->sdi_type == DTSDI_TYPE_SDI_UNKNOWN) {
            uref_free(uref);
            return;
        }

        struct uref *flow_def = uref_sibling_alloc(upipe_dtsdi->flow_def);
        if (!ubase_check(set_flow_def(upipe, flow_def))) {
            upipe_err_va(upipe, "Could not find frame rate");
            uref_free(uref);
            upipe_dtsdi->sdi_type = DTSDI_TYPE_SDI_UNKNOWN;
            return;
        }
        upipe_dtsdi_store_flow_def(upipe, flow_def);
    }

    if (upipe_dtsdi->uref == NULL) {
        upipe_dtsdi->uref = uref;
        uref = NULL;
    } else {
        if (!ubase_check(uref_block_append(upipe_dtsdi->uref, uref->ubuf))) {
            upipe_err(upipe, "Could not append block");
            uref_free(uref);
            return;
        }

        uref->ubuf = NULL;
    }

    size_t size;
    if (!ubase_check(uref_block_size(upipe_dtsdi->uref, &size))) {
        upipe_err(upipe, "Could not read block size");
        uref_free(uref);
        return;
    }

    if (size < upipe_dtsdi->frame_size) {
        uref_free(uref);
        return; /* buffer */
    }

    if (size == upipe_dtsdi->frame_size) {
        uref = upipe_dtsdi->uref;
        upipe_dtsdi->uref = NULL;
    } else {
        uref->ubuf = ubuf_block_split(upipe_dtsdi->uref->ubuf,
                upipe_dtsdi->frame_size);

        if (!uref->ubuf) {
            upipe_err_va(upipe, "Could not split ubuf at %zu+%zu", upipe_dtsdi->frame_size);
            uref_free(uref);
            return;
        }

        struct uref *out = upipe_dtsdi->uref;
        upipe_dtsdi->uref = uref;
        uref = out;
    }

    if (!ubase_check(uref_block_merge(uref, uref->ubuf->mgr, 0,
                    upipe_dtsdi->frame_size))) {
        upipe_err(upipe, "Could not merge uref");
        uref_free(uref);
        return;
    }

    int s = -1;
    uint8_t *buf;
    if (!ubase_check(uref_block_write(uref, 0, &s, &buf))) {
        upipe_err(upipe, "Could not map ubuf");
        uref_free(uref);
        return;
    }

    for (int i = 1; i < s; i+= 2) {
        buf[i] &= 0x03; /* little endian, clamp to 0x3ff */
    }

    uref_block_unmap(uref, 0);

    upipe_dtsdi_output(upipe, uref, upump_p);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_dtsdi_mgr = {
    .refcount = NULL,
    .signature = UPIPE_DTSDI_SIGNATURE,

    .upipe_alloc = upipe_dtsdi_alloc,
    .upipe_input = upipe_dtsdi_input,
    .upipe_control = upipe_dtsdi_control,
};

struct upipe_mgr *upipe_dtsdi_mgr_alloc(void)
{
    return &upipe_dtsdi_mgr;
}
