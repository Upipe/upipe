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
 * @short Upipe module generating zoneplate video pictures
 */

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_bin_output.h>

#include <upipe/uprobe_prefix.h>
#include <upipe/uref_void_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_clock.h>

#include <upipe-modules/upipe_void_source.h>
#include <upipe-filters/upipe_zoneplate.h>
#include <upipe-filters/upipe_zoneplate_source.h>

/** @internal @This is the private structure of a zoneplate source pipe. */
struct upipe_zpsrc {
    /* UPIPE_HELPER_UPIPE */
    struct upipe upipe;

    /* UPIPE_HELPER_UREFCOUNT */
    struct urefcount urefcount;
    /** real refcount */
    struct urefcount urefcount_real;

    /** inner source pipe probe */
    struct uprobe src_probe;
    /** last inner pipe probe */
    struct uprobe zp_probe;

    /** inner source pipe */
    struct upipe *src;
    /** last inner pipe */
    struct upipe *zp;

    /** output pipe  */
    struct upipe *output;
    /** output flow format */
    struct uref *flow_def;
    /** list of requests */
    struct uchain requests;
};

UPIPE_HELPER_UPIPE(upipe_zpsrc, upipe, UPIPE_ZPSRC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_zpsrc, urefcount, upipe_zpsrc_noref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_zpsrc, urefcount_real, upipe_zpsrc_free);
UPIPE_HELPER_FLOW(upipe_zpsrc, UREF_PIC_FLOW_DEF);
UPIPE_HELPER_UPROBE(upipe_zpsrc, urefcount_real, src_probe, NULL);
UPIPE_HELPER_UPROBE(upipe_zpsrc, urefcount_real, zp_probe, NULL);
UPIPE_HELPER_INNER(upipe_zpsrc, src);
UPIPE_HELPER_INNER(upipe_zpsrc, zp);
UPIPE_HELPER_BIN_OUTPUT(upipe_zpsrc, zp, output, requests);

/** @internal @This frees a zoneplate source pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_zpsrc_free(struct upipe *upipe)
{
    struct upipe_zpsrc *upipe_zpsrc = upipe_zpsrc_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_zpsrc->flow_def);
    upipe_zpsrc_clean_bin_output(upipe);
    upipe_zpsrc_clean_src(upipe);
    upipe_zpsrc_clean_zp_probe(upipe);
    upipe_zpsrc_clean_src_probe(upipe);
    upipe_zpsrc_clean_urefcount_real(upipe);
    upipe_zpsrc_clean_urefcount(upipe);
    upipe_zpsrc_free_flow(upipe);
}

/** @internal @This is called when there is no more external reference on the
 * pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_zpsrc_noref(struct upipe *upipe)
{
    upipe_zpsrc_store_src(upipe, NULL);
    upipe_zpsrc_store_bin_output(upipe, NULL);
    upipe_zpsrc_release_urefcount_real(upipe);
}

/** @internal @This allocates a zoneplate source pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe
 */
static struct upipe *upipe_zpsrc_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_zpsrc_alloc_flow(mgr, uprobe, signature, args,
                                                &flow_def);
    if (unlikely(!upipe)) {
        return NULL;
    }

    struct upipe_zpsrc *upipe_zpsrc = upipe_zpsrc_from_upipe(upipe);

    struct urational fps;
    uint8_t planes;
    uint64_t hsize, vsize;
    if (unlikely(!ubase_check(uref_pic_flow_get_planes(flow_def, &planes)))
            || !ubase_check(uref_pic_flow_get_fps(flow_def, &fps))
            || !ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize))
            || !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize))) {
        upipe_zpsrc_free_flow(upipe);
        return NULL;
    }

    upipe_zpsrc_init_urefcount(upipe);
    upipe_zpsrc_init_urefcount_real(upipe);
    upipe_zpsrc_init_src_probe(upipe);
    upipe_zpsrc_init_zp_probe(upipe);
    upipe_zpsrc_init_src(upipe);
    upipe_zpsrc_init_bin_output(upipe);
    upipe_zpsrc->flow_def = flow_def;

    upipe_throw_ready(upipe);

    uint64_t duration = (uint64_t)UCLOCK_FREQ * fps.den / fps.num;
    struct uref *flow_def_src = uref_sibling_alloc_control(flow_def);
    int ret = uref_flow_set_def(flow_def_src, UREF_VOID_FLOW_DEF);
    if (unlikely(!ubase_check(ret))) {
        upipe_err(upipe, "fail to set flow def");
        uref_free(flow_def_src);
        upipe_release(upipe);
        return NULL;
    }
    ret = uref_clock_set_duration(flow_def_src, duration);
    if (unlikely(!ubase_check(ret))) {
        upipe_err(upipe, "fail to set duration");
        uref_free(flow_def_src);
        upipe_release(upipe);
        return NULL;
    }
    if (unlikely(!flow_def_src)) {
        upipe_err(upipe, "fail to duplicate flow def");
        upipe_release(upipe);
        return NULL;
    }

    struct upipe_mgr *upipe_voidsrc_mgr = upipe_voidsrc_mgr_alloc();
    if (unlikely(!upipe_voidsrc_mgr)) {
        upipe_release(upipe);
        uref_free(flow_def_src);
        return NULL;
    }
    struct upipe *src = upipe_flow_alloc(
        upipe_voidsrc_mgr,
        uprobe_pfx_alloc(uprobe_use(&upipe_zpsrc->src_probe),
                         UPROBE_LOG_VERBOSE, "src"),
        flow_def_src);
    uref_free(flow_def_src);
    upipe_mgr_release(upipe_voidsrc_mgr);
    if (unlikely(!src)) {
        upipe_err(upipe, "fail to allocate source pipe");
        upipe_release(upipe);
        return NULL;
    }
    upipe_zpsrc_store_src(upipe, src);

    struct upipe_mgr *upipe_zp_mgr = upipe_zp_mgr_alloc();
    if (unlikely(!upipe_zp_mgr)) {
        upipe_release(upipe);
        return NULL;
    }
    struct upipe *zp = upipe_flow_alloc_output(
        src, upipe_zp_mgr,
        uprobe_pfx_alloc(uprobe_use(&upipe_zpsrc->zp_probe),
                         UPROBE_LOG_VERBOSE, "zp"),
        flow_def);
    upipe_mgr_release(upipe_zp_mgr);
    if (unlikely(!zp)) {
        upipe_err(upipe, "fail to allocate zoneplate pipe");
        upipe_release(upipe);
        return NULL;
    }
    upipe_zpsrc_store_bin_output(upipe, zp);

    return upipe;
}

/** @internal @This handles the pipe control commands.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_zpsrc_control(struct upipe *upipe,
                            int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_GET_FLOW_DEF:
            return upipe_zpsrc_control_bin_output(upipe, command, args);
    }
    return upipe_zpsrc_control_src(upipe, command, args);
}

/** @internal @This is the static zoneplate source pipe manager. */
static struct upipe_mgr upipe_zpsrc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ZPSRC_SIGNATURE,
    .upipe_alloc = upipe_zpsrc_alloc,
    .upipe_control = upipe_zpsrc_control,
};

/** @This returns the zoneplate source pipe manager.
 *
 * @return a pointer to the zoneplate source pipe manager
 */
struct upipe_mgr *upipe_zpsrc_mgr_alloc(void)
{
    return &upipe_zpsrc_mgr;
}
