/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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
 * @short Upipe libzvbi encoding module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-zvbi/upipe_zvbienc.h>
#include <upipe/uref_pic_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

#include <libzvbi.h>

/** upipe_zvbienc structure */
struct upipe_zvbienc {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    vbi_sampling_par sp;

    vbi_sliced sliced[2];

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_zvbienc, upipe, UPIPE_ZVBIENC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_zvbienc, urefcount, upipe_zvbienc_free)
UPIPE_HELPER_VOID(upipe_zvbienc);
UPIPE_HELPER_OUTPUT(upipe_zvbienc, output, flow_def, output_state, request_list)

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_zvbienc_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    struct upipe_zvbienc *upipe_zvbienc = upipe_zvbienc_from_upipe(upipe);

    /* Initialise VBI data with defaults */
    for (int i = 0; i < 2; i++)
        memset(upipe_zvbienc->sliced[i].data, 0x80, 2);

    const uint8_t *pic_data = NULL;
    size_t pic_data_size = 0;
    uref_pic_get_cea_708(uref, &pic_data, &pic_data_size);
    for (int i = 0; i < pic_data_size/3; i++) {
        const uint8_t valid = (pic_data[3*i] >> 2) & 1;
        const uint8_t cc_type = pic_data[3*i] & 0x3;

        if (valid && cc_type < 2)
            memcpy(upipe_zvbienc->sliced[cc_type].data, &pic_data[3*i + 1], 2);
    }

    uint8_t *buf;
    if (ubase_check(uref_pic_plane_write(uref, "y8", 0, 1, -1, 2, &buf))) {
        if (!vbi_raw_video_image(buf, 720*2, &upipe_zvbienc->sp,
                0, 0, 0, 0x000000FF, false, upipe_zvbienc->sliced, 2)) {
            upipe_err(upipe, "Couldn't store VBI");
        }

        uref_pic_plane_unmap(uref, "y8", 0, 1, -1, 2);
    }

    upipe_zvbienc_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_zvbienc_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    uint64_t vsize, hsize;
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &vsize));
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &hsize));

    if (vsize != 486 || hsize != 720) {
        upipe_err_va(upipe,
                "incompatible input flow def, size is %"PRIu64"x%"PRIu64,
                hsize, vsize);
        return UBASE_ERR_INVALID;
    }

    UBASE_RETURN(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8"));

    struct uref *flow_def_dup = uref_dup(flow_def);

    if (unlikely(flow_def_dup == NULL))
        return UBASE_ERR_ALLOC;

    upipe_zvbienc_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an zvbienc pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_zvbienc_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_zvbienc_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_zvbienc_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_zvbi_log(vbi_log_mask level, const char *context,
        const char *message, void *user_data)
{
    struct upipe *upipe = user_data;

    enum uprobe_log_level l = UPROBE_LOG_NOTICE;
    if (level & VBI_LOG_ERROR)
        l = UPROBE_LOG_ERROR;
    else if (level & VBI_LOG_WARNING)
        l = UPROBE_LOG_WARNING;
    else if (level & VBI_LOG_DEBUG)
        l = UPROBE_LOG_DEBUG;

    upipe_log_va(upipe, l, "%s: %s", context, message);
}

/** @internal @This allocates an zvbienc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_zvbienc_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_zvbienc_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_zvbienc *upipe_zvbienc = upipe_zvbienc_from_upipe(upipe);

    vbi_set_log_fn(VBI_LOG_NOTICE|VBI_LOG_WARNING|VBI_LOG_ERROR|VBI_LOG_INFO,
            upipe_zvbi_log, upipe);

    upipe_zvbienc->sp.scanning         = 525; /* NTSC */
    upipe_zvbienc->sp.sampling_format  = VBI_PIXFMT_YUV420;
    upipe_zvbienc->sp.sampling_rate    = 13.5e6;
    upipe_zvbienc->sp.bytes_per_line   = 720;
    upipe_zvbienc->sp.offset       = 122;
    upipe_zvbienc->sp.start[0]     = 21;
    upipe_zvbienc->sp.count[0]     = 1;
    upipe_zvbienc->sp.start[1]     = 284;
    upipe_zvbienc->sp.count[1]     = 1;
    upipe_zvbienc->sp.interlaced   = TRUE;
    upipe_zvbienc->sp.synchronous  = TRUE;

    upipe_zvbienc->sliced[0].id = VBI_SLICED_CAPTION_525_F1;
    upipe_zvbienc->sliced[0].line = upipe_zvbienc->sp.start[0];
    upipe_zvbienc->sliced[1].id = VBI_SLICED_CAPTION_525_F2;
    upipe_zvbienc->sliced[1].line = upipe_zvbienc->sp.start[1];

    upipe_zvbienc_init_urefcount(upipe);
    upipe_zvbienc_init_output(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_zvbienc_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_zvbienc_clean_output(upipe);
    upipe_zvbienc_clean_urefcount(upipe);
    upipe_zvbienc_free_void(upipe);
}

static struct upipe_mgr upipe_zvbienc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ZVBIENC_SIGNATURE,

    .upipe_alloc = upipe_zvbienc_alloc,
    .upipe_input = upipe_zvbienc_input,
    .upipe_control = upipe_zvbienc_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for zvbienc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_zvbienc_mgr_alloc(void)
{
    return &upipe_zvbienc_mgr;
}
