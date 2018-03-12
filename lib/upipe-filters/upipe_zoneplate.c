/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd.
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
 *
 * Authors: James Darnley
 *          Arnaud de Turckheim
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
 * @short Upipe module drawing zoneplate video pictures
 */

#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/uref_void_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uclock.h>

#include <upipe-filters/upipe_zoneplate.h>

#include "zoneplate/videotestsrc.h"

#define EXPECTED_FLOW   UREF_PIC_FLOW_DEF

/** @internal @This is the private structure of zoneplate pipe. */
struct upipe_zp {
    /** public structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** output pipe */
    struct upipe *output;
    /** output flow def */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain requests;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;
    /** requested flow format */
    struct uref *flow_format;
    /** frame counter */
    int frame;
};

/** @hidden */
static int upipe_zp_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_zp, upipe, UPIPE_ZP_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_zp, urefcount, upipe_zp_free);
UPIPE_HELPER_FLOW(upipe_zp, EXPECTED_FLOW);
UPIPE_HELPER_OUTPUT(upipe_zp, output, flow_def, output_state, requests);
UPIPE_HELPER_UBUF_MGR(upipe_zp, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_zp_check,
                      upipe_zp_register_output_request,
                      upipe_zp_unregister_output_request);

/** @internal @This allocates a zoneplate pipe.
 *
 * @param mgr zoneplate management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe or NULL
 */
static struct upipe *upipe_zp_alloc(struct upipe_mgr *mgr,
                                    struct uprobe *uprobe,
                                    uint32_t signature,
                                    va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_zp_alloc_flow(mgr, uprobe, signature, args,
                                              &flow_def);
    if (unlikely(!upipe))
        return NULL;

    upipe_zp_init_urefcount(upipe);
    upipe_zp_init_output(upipe);
    upipe_zp_init_ubuf_mgr(upipe);

    struct upipe_zp *upipe_zp = upipe_zp_from_upipe(upipe);
    upipe_zp->frame = 0;

    upipe_throw_ready(upipe);

    if (unlikely(!ubase_check(uref_pic_flow_get_hsize(flow_def, NULL)) ||
                 !ubase_check(uref_pic_flow_get_vsize(flow_def, NULL)))) {
        upipe_err(upipe, "invalid flow format (hsize or vsize is missing");
        upipe_release(upipe);
        uref_free(flow_def);
        return NULL;
    }

    upipe_zp_store_flow_def(upipe, flow_def);

    return upipe;
}

/** @internal @This frees a zoneplate pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_zp_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_zp_clean_ubuf_mgr(upipe);
    upipe_zp_clean_output(upipe);
    upipe_zp_clean_urefcount(upipe);
    upipe_zp_free_flow(upipe);
}

/** @internal @This checks the zoneplate pipe state.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_zp_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_zp *upipe_zp = upipe_zp_from_upipe(upipe);

    if (flow_format)
        upipe_zp_store_flow_def(upipe, flow_format);

    assert(upipe_zp->flow_def);

    if (!upipe_zp->ubuf_mgr)
        upipe_zp_require_ubuf_mgr(upipe, uref_dup(upipe_zp->flow_def));
    return UBASE_ERR_NONE;
}

/** @internal @This draws the zoneplate frame.
 *
 * @param upipe description structure of the pipe
 * @param uref video frame to draw
 * @param frame frame id
 * @return an error code
 */
static int upipe_zp_draw(struct upipe *upipe, struct uref *uref)
{
    struct upipe_zp *upipe_zp = upipe_zp_from_upipe(upipe);
    int frame = upipe_zp->frame;

    uint64_t hsize, vsize;
    assert(upipe_zp->flow_def);
    ubase_assert(uref_pic_flow_get_hsize(upipe_zp->flow_def, &hsize));
    ubase_assert(uref_pic_flow_get_vsize(upipe_zp->flow_def, &vsize));

    struct ubuf *ubuf = ubuf_pic_alloc(upipe_zp->ubuf_mgr, hsize, vsize);
    UBASE_ALLOC_RETURN(ubuf);
    uref_attach_ubuf(uref, ubuf);

    uref_pic_plane_foreach(uref, chroma) {
        if (!strncmp(chroma, "y8", 2)) {
            uint8_t *buf;
            size_t stride, width, height;
            UBASE_RETURN(uref_pic_size(uref, &width, &height, NULL));
            UBASE_RETURN(uref_pic_plane_size(uref, chroma, &stride,
                                             NULL, NULL, NULL));
            UBASE_RETURN(uref_pic_plane_write(uref, chroma, 0, 0, -1, -1, &buf));
            gst_video_test_src_zoneplate_8bit(buf, width, height, stride, frame);
            UBASE_RETURN(uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1));
        }

        else if (!strncmp(chroma, "y10", 3)) {
            uint8_t *buf;
            size_t stride, width, height;
            UBASE_RETURN(uref_pic_size(uref, &width, &height, NULL));
            UBASE_RETURN(uref_pic_plane_size(uref, chroma, &stride,
                                             NULL, NULL, NULL));
            UBASE_RETURN(uref_pic_plane_write(uref, chroma, 0, 0, -1, -1, &buf));
            gst_video_test_src_zoneplate_10bit((uint16_t*)buf, width, height,
                                               stride, frame);
            UBASE_RETURN(uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1));
        }

        else {
            UBASE_RETURN(uref_pic_plane_clear(uref, chroma, 0, 0, -1, -1, 1));
        }
    }
    upipe_zp->frame++;
    return UBASE_ERR_NONE;
}

/** @internal @This handles the input buffers.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_zp_input(struct upipe *upipe, struct uref *uref,
                           struct upump **upump_p)
{
    int ret = upipe_zp_draw(upipe, uref);
    if (unlikely(!ubase_check(ret)))
        uref_free(uref);
    else
        upipe_zp_output(upipe, uref, upump_p);
}

/** @internal @This set the input flow def.
 *
 * @param upipe description structure of the pipe
 * @param flow_def input flow def
 * @return an error code
 */
static int upipe_zp_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_zp *upipe_zp = upipe_zp_from_upipe(upipe);

    UBASE_RETURN(uref_flow_match_def(flow_def, UREF_VOID_FLOW_DEF));

    uint64_t duration;
    if (ubase_check(uref_clock_get_duration(flow_def, &duration)) && duration) {
        /* convert void.duration to pic.fps */
        struct urational fps;
        fps.num = UCLOCK_FREQ;
        fps.den = duration;
        urational_simplify(&fps);
        UBASE_RETURN(uref_pic_flow_set_fps(upipe_zp->flow_def, fps));
    }

    return UBASE_ERR_NONE;
}

/** @internal @This handles control commands.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_zp_control_real(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_zp_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_zp_set_flow_def(upipe, flow_def);
        }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This handles control commands and checks the pipe state.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_zp_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(upipe_zp_control_real(upipe, command, args));
    return upipe_zp_check(upipe, NULL);
}

/** @internal @This is the zoneplate management structure. */
static struct upipe_mgr upipe_zp_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ZP_SIGNATURE,

    .upipe_alloc = upipe_zp_alloc,
    .upipe_input = upipe_zp_input,
    .upipe_control = upipe_zp_control,
};

/** @This returns the zoneplate source pipe manager.
 *
 * @return a pointer to the zoneplate pipe manager
 */
struct upipe_mgr *upipe_zp_mgr_alloc(void)
{
    return &upipe_zp_mgr;
}
