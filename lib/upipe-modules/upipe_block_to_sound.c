/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe null module - free incoming urefs
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe/udict.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_block_to_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_block_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <assert.h>

/** @hidden */
static bool upipe_block_to_sound_handle(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p);

/** upipe_upipe_block_to_sound structure */
struct upipe_block_to_sound {
    /** refcount management structure */
    struct urefcount urefcount;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_block_to_sound, upipe, UPIPE_BLOCK_TO_SOUND_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_block_to_sound, urefcount, upipe_block_to_sound_free)
UPIPE_HELPER_VOID(upipe_block_to_sound);
UPIPE_HELPER_INPUT(upipe_block_to_sound, urefs, nb_urefs, max_urefs, blockers, upipe_block_to_sound_handle);
UPIPE_HELPER_OUTPUT(upipe_block_to_sound, output, flow_def, output_state, request_list);


/** @internal @This allocates a block_to_sound pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_block_to_sound_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_block_to_sound_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_block_to_sound *upipe_block_to_sound = upipe_block_to_sound_from_upipe(upipe);
    upipe_block_to_sound_init_input(upipe);
    upipe_block_to_sound_init_urefcount(upipe);
    upipe_block_to_sound_init_output(upipe);
    upipe_throw_ready(&upipe_block_to_sound->upipe);

    return &upipe_block_to_sound->upipe;
}

/** @internal @This sends data to devnull.
 *
 * @param upipe description structure 
 of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_block_to_sound_input(struct upipe *upipe, struct uref *uref, struct upump **upump_p)
{
    if (!upipe_block_to_sound_check_input(upipe)) {
        upipe_block_to_sound_hold_input(upipe, uref);
        upipe_block_to_sound_block_input(upipe, upump_p);
    } else if (!upipe_block_to_sound_handle(upipe, uref, upump_p)) {
        upipe_block_to_sound_hold_input(upipe, uref);
        upipe_block_to_sound_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_block_to_sound_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                (ubase_ncmp(def, "block."))))
        return UBASE_ERR_INVALID;

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uref_block_flow_clear_format(flow_def);
    uref_flow_set_def(flow_def, "sound.s32.");
    uint8_t sample_size = 8;
    uref_sound_flow_set_raw_sample_size(flow_def, sample_size);
    uint8_t planes = 1;
    uref_sound_flow_set_planes(flow_def, planes);
    uint8_t channels = 2;
    uref_sound_flow_set_channels(flow_def, channels);
    upipe_input(upipe, flow_def, NULL);

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_block_to_sound_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_block_to_sound *upipe_block_to_sound = upipe_block_to_sound_from_upipe(upipe);
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_block_to_sound_set_flow_def(upipe, flow_def);
        }
        case UPIPE_BLOCK_TO_SOUND_DUMP_DICT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BLOCK_TO_SOUND_SIGNATURE)
            return UBASE_ERR_NONE;
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_block_to_sound_set_output(upipe, output);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return void
 */
static bool upipe_block_to_sound_handle(struct upipe *upipe, struct uref *uref,
                                        struct upump **upump_p)
{
	struct upipe_block_to_sound *upipe_block_to_sound = upipe_block_to_sound_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_block_to_sound_store_flow_def(upipe, uref);
        //uref_free(uref);
        return true;
    }

    upipe_block_to_sound_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_block_to_sound_free(struct upipe *upipe)
{
    struct upipe_block_to_sound *upipe_block_to_sound = upipe_block_to_sound_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_block_to_sound_clean_urefcount(upipe);
    upipe_block_to_sound_free_void(upipe);
}

/** upipe_null (/dev/null) */
static struct upipe_mgr upipe_block_to_sound_mgr = {
    .refcount = NULL,
    .signature = UPIPE_BLOCK_TO_SOUND_SIGNATURE,

    .upipe_alloc = upipe_block_to_sound_alloc,
    .upipe_input = upipe_block_to_sound_input,
    .upipe_control = upipe_block_to_sound_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for null pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_block_to_sound_mgr_alloc(void)
{
    return &upipe_block_to_sound_mgr;
}
