/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd.
 *
 * Authors: James Darnley
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
 * @short Upipe module generating zoneplate video pictures
 */

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>

#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_clock.h>

#include <upipe-filters/upipe_zoneplate_source.h>

#include "zoneplate/videotestsrc.h"

/** @internal @This is the private structure of a zoneplate source pipe. */
struct upipe_zp {
    /* UPIPE_HELPER_UPIPE */
    struct upipe upipe;

    /* UPIPE_HELPER_UREFCOUNT */
    struct urefcount urefcount;

    /* UPIPE_HELPER_OUTPUT */
    struct upipe *output;
    struct uref *flow_def;
    enum upipe_helper_output_state output_state;
    struct uchain requests;

    /* UPIPE_HELPER_UREF_MGR */
    struct uref_mgr *uref_mgr;
    struct urequest uref_mgr_request;

    /* UPIPE_HELPER_UBUF_MGR */
    struct ubuf_mgr *ubuf_mgr;
    struct uref *flow_format;
    struct urequest ubuf_mgr_request;

    /* UPIPE_HELPER_UCLOCK */
    struct uclock *uclock;
    struct urequest uclock_request;

    /* UPIPE_HELPER_UPUMP_MGR */
    struct upump_mgr *upump_mgr;

    /* UPIPE_HELPER_UPUMP */
    struct upump *upump;

    int frame_counter;
    uint64_t pts, interval;
};

/** @hidden */
static int upipe_zp_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_zp, upipe, UPIPE_ZONEPLATE_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_zp, urefcount, upipe_zp_free);
UPIPE_HELPER_FLOW(upipe_zp, UREF_PIC_FLOW_DEF);
UPIPE_HELPER_OUTPUT(upipe_zp, output, flow_def, output_state, requests);
UPIPE_HELPER_UREF_MGR(upipe_zp, uref_mgr, uref_mgr_request,
                      upipe_zp_check,
                      upipe_zp_register_output_request,
                      upipe_zp_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_zp, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_zp_check,
                      upipe_zp_register_output_request,
                      upipe_zp_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_zp, uclock, uclock_request, upipe_zp_check,
                    upipe_zp_register_output_request,
                    upipe_zp_unregister_output_request)
UPIPE_HELPER_UPUMP_MGR(upipe_zp, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_zp, upump, upump_mgr)

/** @internal @This frees a zoneplate source pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_zp_free(struct upipe *upipe)
{
    struct upipe_zp *upipe_zp = upipe_zp_from_upipe(upipe);

    upipe_throw_dead(upipe);

    upipe_zp_clean_urefcount(upipe);
    upipe_zp_clean_uref_mgr(upipe);
    upipe_zp_clean_uclock(upipe);
    upipe_zp_clean_output(upipe);
    upipe_zp_clean_upump_mgr(upipe);
    upipe_zp_clean_upump(upipe);

    upipe_zp_free_flow(upipe);
}

/** @internal @This allocates a zoneplate source pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe
 */
static struct upipe *upipe_zp_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_zp_alloc_flow(mgr, uprobe, signature, args,
                                                &flow_def);
    if (unlikely(!upipe)) {
        return NULL;
    }

    struct upipe_zp *upipe_zp = upipe_zp_from_upipe(upipe);

    struct urational fps;
    uint8_t planes;
    uint64_t hsize, vsize;
    if (unlikely(!ubase_check(uref_pic_flow_get_planes(flow_def, &planes)))
            || !ubase_check(uref_pic_flow_get_fps(flow_def, &fps))
            || !ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize))
            || !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize))) {
        upipe_zp_free_flow(upipe);
        return NULL;
    }
    upipe_zp->interval = (uint64_t)UCLOCK_FREQ * fps.den / fps.num;

    upipe_zp_init_urefcount(upipe);
    upipe_zp_init_uref_mgr(upipe);
    upipe_zp_init_uclock(upipe);
    upipe_zp_init_output(upipe);
    upipe_zp_init_upump_mgr(upipe);
    upipe_zp_init_upump(upipe);

    upipe_zp_store_flow_def(upipe, flow_def);

    upipe_zp->pts = UINT64_MAX;

    upipe_throw_ready(upipe);
    return upipe;
}

static int draw_zoneplate(struct upipe *upipe, struct uref *uref, int frame)
{
    const char *chroma = NULL;
    while (ubase_check(uref_pic_plane_iterate(uref, &chroma)) &&
            chroma != NULL) {
        if (!strncmp(chroma, "y8", 2)) {
            uint8_t *buf;
            size_t stride, width, height;
            UBASE_RETURN(uref_pic_size(uref, &width, &height, NULL));
            UBASE_RETURN(uref_pic_plane_size(uref, chroma, &stride, NULL, NULL, NULL));
            UBASE_RETURN(uref_pic_plane_write(uref, chroma, 0, 0, -1, -1, &buf));
            gst_video_test_src_zoneplate_8bit(buf, width, height, stride, frame);
            UBASE_RETURN(uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1));
        }

        else if (!strncmp(chroma, "y10", 3)) {
            uint8_t *buf;
            size_t stride, width, height;
            UBASE_RETURN(uref_pic_size(uref, &width, &height, NULL));
            UBASE_RETURN(uref_pic_plane_size(uref, chroma, &stride, NULL, NULL, NULL));
            UBASE_RETURN(uref_pic_plane_write(uref, chroma, 0, 0, -1, -1, &buf));
            gst_video_test_src_zoneplate_10bit((uint16_t*)buf, width, height, stride, frame);
            UBASE_RETURN(uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1));
        }

        else {
            UBASE_RETURN(uref_pic_plane_clear(uref, chroma, 0, 0, -1, -1, 1));
        }
    }
    return UBASE_ERR_NONE;
}

/** @internal @This creates blank data and outputs it.
 *
 * @param upump description structure of the timer
 */
static void upipe_zp_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_zp *upipe_zp = upipe_zp_from_upipe(upipe);
    uint64_t current_time;

    uint64_t hsize, vsize;
    ubase_assert(uref_pic_flow_get_hsize(upipe_zp->flow_def, &hsize));
    ubase_assert(uref_pic_flow_get_vsize(upipe_zp->flow_def, &vsize));

    struct uref *uref = uref_pic_alloc(upipe_zp->uref_mgr, upipe_zp->ubuf_mgr, hsize, vsize);
    if (unlikely(!uref)) {
        upipe_err(upipe, "failed to allocate picture");
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    draw_zoneplate(upipe, uref, upipe_zp->frame_counter++);

    if (unlikely(upipe_zp->pts == UINT64_MAX)) {
        upipe_zp->pts = uclock_now(upipe_zp->uclock);
    }

    current_time = uclock_now(upipe_zp->uclock);
    upipe_verbose_va(upipe, "delay %"PRId64, current_time - upipe_zp->pts);

    uref_clock_set_duration(uref, upipe_zp->interval);
    uref_clock_set_pts_sys(uref, upipe_zp->pts);
    uref_clock_set_pts_prog(uref, upipe_zp->pts);
    upipe_zp->pts += upipe_zp->interval;

    upipe_zp_output(upipe, uref, &upipe_zp->upump);

    /* derive timer from (next) pts and current time */
    current_time = uclock_now(upipe_zp->uclock);
    int64_t wait = upipe_zp->pts - current_time;

    upipe_verbose_va(upipe,
        "interval %"PRIu64" nextpts %"PRIu64" current %"PRIu64" wait %"PRId64,
        upipe_zp->interval, upipe_zp->pts,
        current_time, wait);

    /* realloc oneshot timer */
    upipe_zp_wait_upump(upipe, wait, upipe_zp_worker);
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_zp_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_zp *upipe_zp = upipe_zp_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_zp_store_flow_def(upipe, flow_format);

    upipe_zp_check_upump_mgr(upipe);
    if (upipe_zp->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_zp->uref_mgr == NULL) {
        upipe_zp_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_zp->flow_def == NULL)
        return UBASE_ERR_NONE;

    if (upipe_zp->ubuf_mgr == NULL) {
        upipe_zp_require_ubuf_mgr(upipe, uref_dup(upipe_zp->flow_def));
        return UBASE_ERR_NONE;
    }

    if (upipe_zp->uclock == NULL) {
        upipe_zp_require_uclock(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_zp->upump == NULL) {
        struct upump *upump = upump_alloc_timer(upipe_zp->upump_mgr,
            upipe_zp_worker, upipe, upipe->refcount,
            upipe_zp->interval, 0);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_zp_set_upump(upipe, upump);
        upump_start(upump);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This handles the pipe control commands.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_zp_control_real(struct upipe *upipe,
                                   int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_zp_control_output(upipe, command, args));
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This handles control commands and checks the status of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_zp_control(struct upipe *upipe,
                              int command, va_list args)
{
    UBASE_RETURN(upipe_zp_control_real(upipe, command, args));
    return upipe_zp_check(upipe, NULL);
}

/** @internal @This is the static zoneplate source pipe manager. */
static struct upipe_mgr upipe_zp_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ZONEPLATE_SIGNATURE,
    .upipe_alloc = upipe_zp_alloc,
    .upipe_control = upipe_zp_control,
};

/** @This returns the zoneplate source pipe manager.
 *
 * @return a pointer to the zoneplate source pipe manager
 */
struct upipe_mgr *upipe_zoneplate_mgr_alloc(void)
{
    return &upipe_zp_mgr;
}
