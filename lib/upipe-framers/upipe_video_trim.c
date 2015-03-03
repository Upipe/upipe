/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module trimming dead frames off a video stream
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-framers/upipe_video_trim.h>
#include <upipe-framers/uref_mpgv.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/mp2v.h>

/** @internal by default we buffer up to one second of 30 Hz video. */
#define DEFAULT_BUFFER 30

/** @internal @This represents the type of video codec. */
enum upipe_vtrim_type {
    /** unknown */
    UPIPE_VTRIM_UNKNOWN,
    /** ISO/IEC 11172-2 or ISO/IEC 13818-2 */
    UPIPE_VTRIM_MPGV,
    /** ISO/IEC 14496-10 */
    UPIPE_VTRIM_H264
};

/** @internal @This is the private context of an vtrim pipe. */
struct upipe_vtrim {
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

    /** temporary uref storage (used during init) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** number of reference frames we have seen so far */
    unsigned int nb_refs;
    /** type of video codec */
    enum upipe_vtrim_type type;
    /** true if we have thrown the sync_acquired event */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static bool upipe_vtrim_handle(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_vtrim, upipe, UPIPE_VTRIM_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_vtrim, urefcount, upipe_vtrim_free)
UPIPE_HELPER_VOID(upipe_vtrim)
UPIPE_HELPER_SYNC(upipe_vtrim, acquired)
UPIPE_HELPER_INPUT(upipe_vtrim, urefs, nb_urefs, max_urefs, blockers, upipe_vtrim_handle)
UPIPE_HELPER_OUTPUT(upipe_vtrim, output, flow_def, output_state, request_list)

/** @internal @This allocates a vtrim pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_vtrim_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_vtrim_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_vtrim *upipe_vtrim = upipe_vtrim_from_upipe(upipe);
    upipe_vtrim_init_urefcount(upipe);
    upipe_vtrim_init_sync(upipe);
    upipe_vtrim_init_input(upipe);
    upipe_vtrim_init_output(upipe);
    upipe_vtrim->max_urefs = DEFAULT_BUFFER;
    upipe_vtrim->type = UPIPE_VTRIM_UNKNOWN;
    upipe_vtrim->nb_refs = 0;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This handles data after synchronization was acquired.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return always true
 */
static bool upipe_vtrim_handle(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_vtrim *upipe_vtrim = upipe_vtrim_from_upipe(upipe);
    if (upipe_vtrim->type == UPIPE_VTRIM_MPGV && upipe_vtrim->nb_refs < 2) {
        uint8_t coding_type = 0;
        uref_mpgv_get_type(uref, &coding_type);
        if ((upipe_vtrim->nb_refs == 0 && coding_type != MP2VPIC_TYPE_I) ||
            (upipe_vtrim->nb_refs == 1 && coding_type != MP2VPIC_TYPE_I &&
             coding_type != MP2VPIC_TYPE_P)) {
            upipe_dbg(upipe, "trimming frame without reference");
            uref_free(uref);
            return true;
        }
        upipe_vtrim->nb_refs++;
    }

    upipe_vtrim_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_vtrim_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_vtrim *upipe_vtrim = upipe_vtrim_from_upipe(upipe);

    if (likely(upipe_vtrim->acquired)) {
        upipe_vtrim_handle(upipe, uref, upump_p);
        return;
    }

    if (ubase_check(uref_flow_get_random(uref))) {
        /* Start with a clean random access point. */
        upipe_notice_va(upipe,
                        "found a random access point, trimming %u frames",
                        upipe_vtrim->nb_urefs);
        upipe_vtrim_flush_input(upipe);
        upipe_vtrim_sync_acquired(upipe);
        upipe_vtrim->nb_refs = 0;
        upipe_vtrim_handle(upipe, uref, upump_p);
        return;
    }

    if (upipe_vtrim->nb_urefs >= upipe_vtrim->max_urefs) {
        /* Stop looking. */
        upipe_notice(upipe, "couldn't find a random access point");
        upipe_vtrim_sync_acquired(upipe);
        upipe_vtrim->nb_refs = 2;
        upipe_vtrim_output_input(upipe);
        upipe_vtrim_handle(upipe, uref, upump_p);
        return;

    }

    upipe_vtrim_hold_input(upipe, uref);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_vtrim_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (unlikely(ubase_ncmp(def, "block.")))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    struct upipe_vtrim *upipe_vtrim = upipe_vtrim_from_upipe(upipe);
    if (!ubase_ncmp(def, "block.mpeg2video."))
        upipe_vtrim->type = UPIPE_VTRIM_MPGV;
    else if (!ubase_ncmp(def, "block.h264."))
        upipe_vtrim->type = UPIPE_VTRIM_H264;
    else
        upipe_vtrim->type = UPIPE_VTRIM_UNKNOWN;

    upipe_vtrim_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a vtrim pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_vtrim_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_vtrim_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_vtrim_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_vtrim_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_vtrim_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_vtrim_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_vtrim_set_output(upipe, output);
        }
        case UPIPE_GET_MAX_LENGTH: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_vtrim_get_max_length(upipe, p);
        }
        case UPIPE_SET_MAX_LENGTH: {
            unsigned int max_length = va_arg(args, unsigned int);
            return upipe_vtrim_set_max_length(upipe, max_length);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_vtrim_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_vtrim_clean_input(upipe);
    upipe_vtrim_clean_output(upipe);
    upipe_vtrim_clean_sync(upipe);
    upipe_vtrim_clean_urefcount(upipe);
    upipe_vtrim_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_vtrim_mgr = {
    .refcount = NULL,
    .signature = UPIPE_VTRIM_SIGNATURE,

    .upipe_alloc = upipe_vtrim_alloc,
    .upipe_input = upipe_vtrim_input,
    .upipe_control = upipe_vtrim_control,

    .upipe_mgr_control = NULL

};

/** @This returns the management structure for all vtrim pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_vtrim_mgr_alloc(void)
{
    return &upipe_vtrim_mgr;
}
