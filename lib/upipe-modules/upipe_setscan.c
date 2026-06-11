/*
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module to force scan
 */

#include "upipe/uref_pic.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_flow.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_output.h"
#include "upipe-modules/upipe_setscan.h"
#include "upipe-modules/upipe_setflowdef.h"
#include "upipe-modules/upipe_probe_uref.h"

/** @internal @This is the private structure for a setscan pipe. */
struct upipe_setscan {
    /** public refcount management structure */
    struct urefcount urefcount;
    /** output pipe */
    struct upipe *output;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** output flow definition */
    struct uref *flow_def;
    /** request list */
    struct uchain requests;
    /** public upipe structure */
    struct upipe upipe;
    /** force progressive or interlaced */
    bool progressive;
};

UPIPE_HELPER_UPIPE(upipe_setscan, upipe, UPIPE_SETSCAN_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_setscan, UREF_PIC_FLOW_DEF)
UPIPE_HELPER_UREFCOUNT(upipe_setscan, urefcount, upipe_setscan_free)
UPIPE_HELPER_OUTPUT(upipe_setscan, output, flow_def, output_state, requests);


/** @internal @This allocates a setscan pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_setscan_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct uref *flow_def = NULL;
    struct upipe *upipe =
        upipe_setscan_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(!upipe))
        return NULL;

    struct upipe_setscan *upipe_setscan = upipe_setscan_from_upipe(upipe);

    upipe_setscan_init_urefcount(upipe);
    upipe_setscan_init_output(upipe);
    upipe_setscan->progressive = uref_pic_check_progressive(flow_def);
    uref_free(flow_def);

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This is called when there is no more references on the pipe and
 * frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_setscan_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_setscan_clean_output(upipe);
    upipe_setscan_clean_urefcount(upipe);
    upipe_setscan_free_flow(upipe);
}

/** @internal @This handles the input buffer.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer to handle
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_setscan_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_setscan *upipe_setscan = upipe_setscan_from_upipe(upipe);
    uref_pic_set_progressive(uref, upipe_setscan->progressive);
    upipe_setscan_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def input flow definition packet
 * @return an error code
 */
static int upipe_setscan_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    struct upipe_setscan *upipe_setscan = upipe_setscan_from_upipe(upipe);
    if (unlikely(!flow_def))
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF));
    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    uref_pic_set_progressive(flow_def_dup, upipe_setscan->progressive);
    upipe_setscan_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This handles control command on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_setscan_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_setscan_set_flow_def(upipe, flow_def);
        }
    }
    return upipe_setscan_control_output(upipe, command, args);
}

/** @internal @This is the static setscan pipe management structure. */
static struct upipe_mgr upipe_setscan_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SETSCAN_SIGNATURE,

    .upipe_alloc = upipe_setscan_alloc,
    .upipe_input = upipe_setscan_input,
    .upipe_control = upipe_setscan_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for setscan pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_setscan_mgr_alloc(void)
{
    return &upipe_setscan_mgr;
}
