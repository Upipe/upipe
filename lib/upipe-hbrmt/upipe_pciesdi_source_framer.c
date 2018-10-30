/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd
 *
 * Authors: James Darnley
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
 * @short Upipe module to make whole frames out of pciesdi source blocks.
 */

#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>

#include <upipe/uref_block.h>
#include <upipe/uref_pic_flow.h>

#include <upipe-hbrmt/upipe_pciesdi_source_framer.h>

#include "upipe_hbrmt_common.h"

struct upipe_pciesdi_source_framer {
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

    /* SDI offsets */
    const struct sdi_offsets_fmt *f;

    uint16_t prev_fvh;
    bool start;

    /* uref for output */
    struct uref *uref;

    /* Lines cached in output uref. */
    int cached_lines;
};

UPIPE_HELPER_UPIPE(upipe_pciesdi_source_framer, upipe,
        UPIPE_PCIESDI_SOURCE_FRAMER_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_pciesdi_source_framer, urefcount,
        upipe_pciesdi_source_framer_free)
UPIPE_HELPER_VOID(upipe_pciesdi_source_framer)
UPIPE_HELPER_OUTPUT(upipe_pciesdi_source_framer, output, flow_def, output_state,
        request_list)

static struct upipe *upipe_pciesdi_source_framer_alloc(struct upipe_mgr *mgr, struct uprobe
        *uprobe, uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_pciesdi_source_framer_alloc_void(mgr, uprobe,
            signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_pciesdi_source_framer *ctx = upipe_pciesdi_source_framer_from_upipe(upipe);

    ctx->cached_lines = 0;
    ctx->start = false;
    ctx->prev_fvh = 0;

    upipe_pciesdi_source_framer_init_output(upipe);
    upipe_pciesdi_source_framer_init_urefcount(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}


static void upipe_pciesdi_source_framer_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_pciesdi_source_framer_clean_output(upipe);
    upipe_pciesdi_source_framer_clean_urefcount(upipe);
    upipe_pciesdi_source_framer_free_void(upipe);
}

static int upipe_pciesdi_source_framer_set_flow_def(struct upipe *upipe, struct
        uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "block."))

    struct upipe_pciesdi_source_framer *ctx = upipe_pciesdi_source_framer_from_upipe(upipe);

    ctx->f = sdi_get_offsets(flow_def);
    if (!ctx->f) {
        upipe_err(upipe, "Could not figure out SDI offsets");
        return UBASE_ERR_INVALID;
    }

    upipe_pciesdi_source_framer_store_flow_def(upipe, uref_dup(flow_def));
    return UBASE_ERR_NONE;
}

static int upipe_pciesdi_source_framer_control(struct upipe *upipe, int command,
        va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_pciesdi_source_framer_set_flow_def(upipe, flow_def);
        }
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_pciesdi_source_framer_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_pciesdi_source_framer_input(struct upipe *upipe, struct uref
        *uref, struct upump **upump_p)
{
    struct upipe_pciesdi_source_framer *ctx = upipe_pciesdi_source_framer_from_upipe(upipe);

    if (ctx->start) {
        size_t src_size_bytes;
        int err = uref_block_size(uref, &src_size_bytes);
        if (!ubase_check(err)) {
            upipe_throw_fatal(upipe, err);
            uref_free(uref);
            return;
        }

        int sdi_width_bytes = sizeof(uint16_t) * 2 * ctx->f->width;
        int lines_in_uref = src_size_bytes / sdi_width_bytes;
        if (ctx->cached_lines + lines_in_uref < ctx->f->height) {
            if (ctx->uref) {
                uref_block_append(ctx->uref, uref_detach_ubuf(uref));
                ctx->cached_lines += lines_in_uref;
                uref_free(uref);
                return;
            } else {
                ctx->uref = uref;
                return;
            }
        }

        else if (ctx->cached_lines + lines_in_uref == ctx->f->height) {
            uref_block_append(ctx->uref, uref_detach_ubuf(uref));
            upipe_pciesdi_source_framer_output(upipe, ctx->uref, upump_p);
            ctx->uref = NULL;
            ctx->cached_lines = 0;
            uref_free(uref);
            return;
        }

        else if (ctx->cached_lines + lines_in_uref > ctx->f->height) {
            /* Duplicate and resize block to be the end of the frame. */
            struct ubuf *ubuf = ubuf_dup(uref->ubuf);
            if (!ubuf) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                uref_free(uref);
                return;
            }

            int lines_needed = ctx->f->height - ctx->cached_lines;
            ubuf_block_resize(ubuf, 0, sdi_width_bytes * lines_needed);
            uref_block_append(ctx->uref, ubuf);
            upipe_pciesdi_source_framer_output(upipe, ctx->uref, upump_p);

            /* Keep top of next frame. */
            uref_block_resize(uref, sdi_width_bytes * lines_needed, -1);
            ctx->uref = uref;
            ctx->cached_lines = lines_in_uref - lines_needed;
            return;
        }

        /* Execution should never reach here. */
        abort();
    }

    /* Find top of frame. */

    int size = -1;
    const uint8_t *src = NULL;
    int err = uref_block_read(uref, 0, &size, &src);
    if (!ubase_check(err)) {
        upipe_throw_fatal(upipe, err);
        uref_free(uref);
        return;
    }
    size /= sizeof(uint16_t);

    int eav_fvh_offset = 6;
    if (ctx->f->pict_fmt->sd)
        eav_fvh_offset = 3;
    /* TODO: 12 for SDI3G */

    int sdi_width = 2 * ctx->f->width;
    int offset;
    for (offset = 0; offset < size; offset += sdi_width) {
        const uint16_t *buf = (const uint16_t*)src + offset;
        uint16_t fvh = buf[eav_fvh_offset];
        if (ctx->prev_fvh == 0x3c4 && fvh == 0x2d8) {
            ctx->start = true;
            break;
        }
        ctx->prev_fvh = fvh;
    }
    uref_block_unmap(uref, 0);

    /* Keep the top of frame if it was found. */
    if (ctx->start) {
        uref_block_resize(uref, sizeof(uint16_t) * offset, -1);
        ctx->uref = uref;
        ctx->cached_lines = (size - offset) / sdi_width;
    } else {
        uref_free(uref);
    }

    return;
}

static struct upipe_mgr upipe_pciesdi_source_framer_mgr = {
    .signature = UPIPE_PCIESDI_SOURCE_FRAMER_SIGNATURE,
    .upipe_alloc = upipe_pciesdi_source_framer_alloc,
    .upipe_input = upipe_pciesdi_source_framer_input,
    .upipe_control = upipe_pciesdi_source_framer_control,
};

struct upipe_mgr *upipe_pciesdi_source_framer_mgr_alloc(void)
{
    return &upipe_pciesdi_source_framer_mgr;
}
