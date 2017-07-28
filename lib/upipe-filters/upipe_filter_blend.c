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
#include <upipe/upipe_helper_input.h>
#include <upipe-filters/upipe_filter_blend.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>

/** @hidden */
static bool upipe_filter_blend_handle(struct upipe *upipe, struct uref *uref,
                                      struct upump **upump_p);
/** @hidden */
static int upipe_filter_blend_check(struct upipe *upipe,
                                    struct uref *flow_format);

/** @internal upipe_filter_blend private structure */
struct upipe_filter_blend {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** public structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_filter_blend, upipe, UPIPE_FILTER_BLEND_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_filter_blend, urefcount, upipe_filter_blend_free)
UPIPE_HELPER_VOID(upipe_filter_blend)
UPIPE_HELPER_OUTPUT(upipe_filter_blend, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_filter_blend, ubuf_mgr, flow_format,
                      ubuf_mgr_request, upipe_filter_blend_check,
                      upipe_filter_blend_register_output_request,
                      upipe_filter_blend_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_filter_blend, urefs, nb_urefs, max_urefs, blockers, upipe_filter_blend_handle)

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
    upipe_filter_blend_init_input(upipe);
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
static void upipe_filter_merge16bit( void *_dest, const void *_s1,
                       const void *_s2, size_t bytes )
{
    uint16_t *dest = _dest;
    const uint16_t *s1 = _s1;
    const uint16_t *s2 = _s2;

    bytes /= 2;
    for( ; bytes > 0; bytes-- )
        *dest++ = ( *s1++ + *s2++ ) >> 1;
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
                                     size_t height, uint8_t macropixel_size)
{
    uint8_t *out_end = out + stride_out * height;

    // Copy first line
    memcpy(out, in, stride_in);
    out += stride_out;

    // Compute mean value for remaining lines
    while (out < out_end) {
        if (macropixel_size == 2)
            upipe_filter_merge16bit(out, in, in+stride_in,
                    (stride_in < stride_out) ? stride_in : stride_out);
        else
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
 * @return always true
 */
static bool upipe_filter_blend_handle(struct upipe *upipe, struct uref *uref,
                                      struct upump **upump_p)
{
    struct upipe_filter_blend *upipe_filter_blend = upipe_filter_blend_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_filter_blend_store_flow_def(upipe, NULL);
        upipe_filter_blend_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_filter_blend->flow_def == NULL)
        return false;

    const uint8_t *in;
    uint8_t *out;
    uint8_t hsub, vsub, macropixel_size;
    size_t stride_in = 0, stride_out = 0, width, height;
    const char *chroma = NULL;
    struct ubuf *ubuf_deint = NULL;

    // Now process frames
    uref_pic_size(uref, &width, &height, NULL);
    upipe_verbose_va(upipe, "received pic (%dx%d)", width, height);

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
                                                &hsub, &vsub, &macropixel_size)))) {
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
        upipe_filter_blend_plane(in, out, stride_in, stride_out, (size_t) height/vsub, macropixel_size);

        // unmap all
        uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
        ubuf_pic_plane_unmap(ubuf_deint, chroma, 0, 0, -1, -1);
    }

    // Attach new ubuf and output frame
    uref_attach_ubuf(uref, ubuf_deint);
    uref_pic_set_progressive(uref);
    uref_pic_delete_tff(uref);

    upipe_filter_blend_output(upipe, uref, upump_p);
    return true;

error:
    uref_free(uref);
    if (ubuf_deint) {
        ubuf_free(ubuf_deint);
    }
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_filter_blend_input(struct upipe *upipe, struct uref *uref,
                                     struct upump **upump_p)
{
    if (!upipe_filter_blend_check_input(upipe)) {
        upipe_filter_blend_hold_input(upipe, uref);
        upipe_filter_blend_block_input(upipe, upump_p);
    } else if (!upipe_filter_blend_handle(upipe, uref, upump_p)) {
        upipe_filter_blend_hold_input(upipe, uref);
        upipe_filter_blend_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This checks if the input may start.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_filter_blend_check(struct upipe *upipe,
                                    struct uref *flow_format)
{
    struct upipe_filter_blend *upipe_filter_blend =
        upipe_filter_blend_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_filter_blend_store_flow_def(upipe, flow_format);

    if (upipe_filter_blend->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_filter_blend_check_input(upipe);
    upipe_filter_blend_output_input(upipe);
    upipe_filter_blend_unblock_input(upipe);
    if (was_buffered && upipe_filter_blend_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_filter_blend_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_filter_blend_set_flow_def(struct upipe *upipe,
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
    UBASE_RETURN(uref_pic_set_progressive(flow_def_dup))
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_filter_blend_control(struct upipe *upipe,
                                      int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_filter_blend_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_filter_blend_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_filter_blend_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_filter_blend_control_output(upipe, command, args);
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

    upipe_filter_blend_clean_input(upipe);
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

/** @This returns the management structure for glx_input pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_filter_blend_mgr_alloc(void)
{
    return &upipe_filter_blend_mgr;
}
