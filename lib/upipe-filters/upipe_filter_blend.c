/*
 * Copyright (C) 2011 VLC authors and VideoLAN
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe blend deinterlace filter
 *
 * Adapted from VLC video_filter (blend deinterlace) :
 * - modules/video_filter/deinterlace/merge.c
 * - modules/video_filter/deinterlace/algo_basic.c 
 */

#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/udict.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-filters/upipe_filter_blend.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>

/** @internal upipe_filter_blend private structure */
struct upipe_filter_blend {
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** output */
    struct upipe *output;
    /** output flow */
    struct uref *output_flow;
    /** has flow def been sent ?*/
    bool output_flow_sent;
    /** pipe refcount */
    urefcount refcount;
    /** public structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_filter_blend, upipe);
UPIPE_HELPER_UBUF_MGR(upipe_filter_blend, ubuf_mgr);
UPIPE_HELPER_OUTPUT(upipe_filter_blend, output, output_flow, output_flow_sent)

/** @internal @This allocates a filter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_filter_blend_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe)
{
    struct upipe_filter_blend *upipe_filter_blend = malloc(sizeof(struct upipe_filter_blend));
    if (unlikely(upipe_filter_blend == NULL)) {
        return NULL;
    }
    memset(upipe_filter_blend, 0, sizeof(struct upipe_filter_blend));
    struct upipe *upipe = upipe_filter_blend_to_upipe(upipe_filter_blend);
    upipe_init(upipe, mgr, uprobe);
    urefcount_init(&upipe_filter_blend->refcount);
    upipe_filter_blend_init_ubuf_mgr(upipe);
    upipe_filter_blend_init_output(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This computes the per-pixel mean of two lines
 * Code from VLC.
 * - modules/video_filter/deinterlace/merge.c
 *
 * @param _dest dest line
 * @param _s1 first source line
 * @param _s2 second source line
 * @param bytes length in bytes
 */
static void upipe_filter_merge8bit( void *_dest, const void *_s1,
                       const void *_s2, size_t bytes )
{
    uint8_t *dest = _dest;
    const uint8_t *s1 = _s1;
    const uint8_t *s2 = _s2;

    for( ; bytes > 0; bytes-- )
        *dest++ = ( *s1++ + *s2++ ) >> 1;
}

/** @internal @This processes a picture plane
 * Adapted from VLC.
 * - modules/video_filter/deinterlace/algo_basic.c 
 *
 * @param in input buffer
 * @param out output buffer
 * @param stride_in stride length of input buffer
 * @param stride_out stride length of output buffer
 * @param height picture height
 */
static void upipe_filter_blend_plane(const uint8_t *in, uint8_t *out,
                                     size_t stride_in, size_t stride_out,
                                     size_t height)
{
    uint8_t *out_end = out + stride_out * height;

    // Copy first line
    memcpy(out, in, stride_in);
    out += stride_out;

    // Compute mean value for remaining lines
    while (out < out_end) {
        upipe_filter_merge8bit(out, in, in+stride_in, stride_in);

        out += stride_out;
        in += stride_in;
    }
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_filter_blend_input(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        upipe_filter_blend_store_flow_def(upipe, uref);
        return;
    }
    if (unlikely(!uref->ubuf)) { // no ubuf in uref
        upipe_warn(upipe, "dropping empty uref");
        uref_free(uref);
        return;
    }

    struct upipe_filter_blend *upipe_filter_blend = upipe_filter_blend_from_upipe(upipe);
    const uint8_t *in;
    uint8_t *out;
    uint8_t hsub, vsub;
    size_t stride_in = 0, stride_out = 0, width, height;
    const char *chroma = NULL;
    struct ubuf *ubuf_deint = NULL;

    // Now process frames
    uref_pic_size(uref, &width, &height, NULL);
    upipe_dbg_va(upipe, "received pic (%dx%d)", width, height);

    // Alloc deint buffer
    if (unlikely(!upipe_filter_blend->ubuf_mgr)) {
        upipe_throw(upipe, UPROBE_NEED_UBUF_MGR);
        goto error;
    }
    assert(upipe_filter_blend->ubuf_mgr);
    ubuf_deint = ubuf_pic_alloc(upipe_filter_blend->ubuf_mgr, width, height);
    if (unlikely(!ubuf_deint)) {
        upipe_throw_aerror(upipe);
        goto error;
    }

    // Iterate planes
    while(uref_pic_plane_iterate(uref, &chroma) && chroma) {
        // map all
        if (unlikely(!uref_pic_plane_size(uref, chroma, &stride_in,
                                                &hsub, &vsub, NULL))) {
            upipe_err_va(upipe, "Could not read origin chroma %s", chroma);
            goto error;
        }
        if (unlikely(!ubuf_pic_plane_size(ubuf_deint, chroma, &stride_out,
                                                  NULL, NULL, NULL))) {
            upipe_err_va(upipe, "Could not read dest chroma %s", chroma);
            goto error;
        }
        uref_pic_plane_read(uref, chroma, 0, 0, -1, -1, &in);
        ubuf_pic_plane_write(ubuf_deint, chroma, 0, 0, -1, -1, &out);

        // process plane
        upipe_filter_blend_plane(in, out, stride_in, stride_out, (size_t) height/vsub);

        // unmap all
        uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
        ubuf_pic_plane_unmap(ubuf_deint, chroma, 0, 0, -1, -1);
    }

    // Attach new ubuf and output frame
    ubuf_free(uref_detach_ubuf(uref));
    uref_attach_ubuf(uref, ubuf_deint);

    upipe_filter_blend_output(upipe, uref, upump);
    return;

error:
    uref_free(uref);
    if (ubuf_deint) {
        ubuf_free(ubuf_deint);
    }
    return;
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_filter_blend_control(struct upipe *upipe,
                               enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_filter_blend_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_filter_blend_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_filter_blend_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_filter_blend_set_output(upipe, output);
        }
        default:
            return false;
    }
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_filter_blend_use(struct upipe *upipe)
{
    struct upipe_filter_blend *upipe_filter_blend = upipe_filter_blend_from_upipe(upipe);
    urefcount_use(&upipe_filter_blend->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_filter_blend_release(struct upipe *upipe)
{
    struct upipe_filter_blend *upipe_filter_blend = upipe_filter_blend_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_filter_blend->refcount))) {
        upipe_throw_dead(upipe);

        upipe_filter_blend_clean_ubuf_mgr(upipe);
        upipe_filter_blend_clean_output(upipe);
        upipe_clean(upipe);
        urefcount_clean(&upipe_filter_blend->refcount);
        free(upipe_filter_blend);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_filter_blend_mgr = {
    .signature = UPIPE_FILTER_BLEND_SIGNATURE,
    .upipe_alloc = upipe_filter_blend_alloc,
    .upipe_input = upipe_filter_blend_input,
    .upipe_control = upipe_filter_blend_control,
    .upipe_release = upipe_filter_blend_release,
    .upipe_use = upipe_filter_blend_use,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for glx_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_filter_blend_mgr_alloc(void)
{
    return &upipe_filter_blend_mgr;
}
