/*
 * Copyright (C) 2011 VLC authors and VideoLAN
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-filters/upipe_filter_blend.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>

/** @internal upipe_filter_blend private structure */
struct upipe_filter_blend {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** output */
    struct upipe *output;
    /** output flow */
    struct uref *output_flow;
    /** has flow def been sent ?*/
    bool output_flow_sent;
    /** public structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_filter_blend, upipe, UPIPE_FILTER_BLEND_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_filter_blend, urefcount, upipe_filter_blend_free)
UPIPE_HELPER_VOID(upipe_filter_blend)
UPIPE_HELPER_UBUF_MGR(upipe_filter_blend, ubuf_mgr, output_flow);
UPIPE_HELPER_OUTPUT(upipe_filter_blend, output, output_flow, output_flow_sent)

/** @internal @This allocates a filter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_filter_blend_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_filter_blend_alloc_void(mgr, uprobe, signature,
                                                        args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_filter_blend_init_urefcount(upipe);
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
        upipe_filter_merge8bit(out, in, in+stride_in,
           (stride_in < stride_out) ? stride_in : stride_out);

        out += stride_out;
        in += stride_in;
    }
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_filter_blend_input(struct upipe *upipe, struct uref *uref,
                                     struct upump **upump_p)
{
    struct upipe_filter_blend *upipe_filter_blend = upipe_filter_blend_from_upipe(upipe);
    const uint8_t *in;
    uint8_t *out;
    uint8_t hsub, vsub;
    size_t stride_in = 0, stride_out = 0, width, height;
    const char *chroma = NULL;
    struct ubuf *ubuf_deint = NULL;

    // Now process frames
    uref_pic_size(uref, &width, &height, NULL);
    upipe_verbose_va(upipe, "received pic (%dx%d)", width, height);

    // Alloc deint buffer
    if (unlikely(!ubase_check(upipe_filter_blend_check_ubuf_mgr(upipe))))
        goto error;

    assert(upipe_filter_blend->ubuf_mgr);
    ubuf_deint = ubuf_pic_alloc(upipe_filter_blend->ubuf_mgr, width, height);
    if (unlikely(!ubuf_deint)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        goto error;
    }

    // Iterate planes
    while (ubase_check(uref_pic_plane_iterate(uref, &chroma)) && chroma) {
        // map all
        if (unlikely(!ubase_check(uref_pic_plane_size(uref, chroma, &stride_in,
                                                &hsub, &vsub, NULL)))) {
            upipe_err_va(upipe, "Could not read origin chroma %s", chroma);
            goto error;
        }
        if (unlikely(!ubase_check(ubuf_pic_plane_size(ubuf_deint, chroma, &stride_out,
                                                  NULL, NULL, NULL)))) {
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
    uref_attach_ubuf(uref, ubuf_deint);

    upipe_filter_blend_output(upipe, uref, upump_p);
    return;

error:
    uref_free(uref);
    if (ubuf_deint) {
        ubuf_free(ubuf_deint);
    }
    return;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static enum ubase_err upipe_filter_blend_set_flow_def(struct upipe *upipe,
                                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_filter_blend_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static enum ubase_err upipe_filter_blend_control(struct upipe *upipe,
                               enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UBUF_MGR:
            return upipe_filter_blend_attach_ubuf_mgr(upipe);

        case UPIPE_AMEND_FLOW_FORMAT:
            return UBASE_ERR_NONE;
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_filter_blend_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_filter_blend_set_flow_def(upipe, flow_def);
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
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_filter_blend_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_filter_blend_clean_ubuf_mgr(upipe);
    upipe_filter_blend_clean_output(upipe);
    upipe_filter_blend_clean_urefcount(upipe);
    upipe_filter_blend_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_filter_blend_mgr = {
    .refcount = NULL,
    .signature = UPIPE_FILTER_BLEND_SIGNATURE,

    .upipe_alloc = upipe_filter_blend_alloc,
    .upipe_input = upipe_filter_blend_input,
    .upipe_control = upipe_filter_blend_control
};

/** @This returns the management structure for glx_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_filter_blend_mgr_alloc(void)
{
    return &upipe_filter_blend_mgr;
}
