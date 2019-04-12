/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd
 *
 * Authors: Judah Rand
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
 * @short Upipe block_to_sound module - converts incoming block urefs to outgoing sound urefs
 *
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe/udict.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/ubuf_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe-modules/upipe_block_to_sound.h>

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


/** upipe_upipe_block_to_sound structure */
struct upipe_block_to_sound {
    /** refcount management structure */
    struct urefcount urefcount;

    /** configuration flow_def */
    struct uref *flow_def_config;

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

    /** public upipe structure */
    struct upipe upipe;

    /**  data to set flow_def and ubuf */
    uint8_t sample_size;
    uint8_t planes;
};

UPIPE_HELPER_UPIPE(upipe_block_to_sound, upipe, UPIPE_BLOCK_TO_SOUND_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_block_to_sound, urefcount, upipe_block_to_sound_free)
UPIPE_HELPER_FLOW(upipe_block_to_sound, UREF_SOUND_FLOW_DEF);
UPIPE_HELPER_OUTPUT(upipe_block_to_sound, output, flow_def, output_state, request_list);
UPIPE_HELPER_UBUF_MGR(upipe_block_to_sound, ubuf_mgr, flow_format, ubuf_mgr_request,
                      NULL,
                      upipe_block_to_sound_register_output_request,
                      upipe_block_to_sound_unregister_output_request)


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
    struct uref *flow_def;

    struct upipe *upipe = upipe_block_to_sound_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_block_to_sound *upipe_block_to_sound = upipe_block_to_sound_from_upipe(upipe);

    if (unlikely(!ubase_check(uref_sound_flow_get_planes(flow_def,
            &upipe_block_to_sound->planes)) || upipe_block_to_sound->planes != 1 )) {
        upipe_err_va(upipe, "wrong number of planes: %d", upipe_block_to_sound->planes);
        upipe_block_to_sound_free_flow(upipe);
        uref_free(flow_def);
        return NULL;
    }

    if (unlikely(!ubase_check(uref_sound_flow_get_sample_size(flow_def,
            &upipe_block_to_sound->sample_size)))) {
        upipe_err_va(upipe, "flow def needs sample_size");
        upipe_block_to_sound_free_flow(upipe);
        uref_free(flow_def);
        return NULL;
    }

    upipe_block_to_sound->flow_def_config = flow_def;

    upipe_block_to_sound_init_urefcount(upipe);
    upipe_block_to_sound_init_output(upipe);
    upipe_block_to_sound_init_ubuf_mgr(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This converts block urefs to sound urefs.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_block_to_sound_input(struct upipe *upipe, struct uref *uref, struct upump **upump_p)
{

    struct upipe_block_to_sound *upipe_block_to_sound = upipe_block_to_sound_from_upipe(upipe);

    /* get block size */
    size_t block_size = 0;
    uref_block_size(uref, &block_size);

    /* drop incomplete samples */
    if ((block_size % upipe_block_to_sound->sample_size) != 0) {
        upipe_warn(upipe, "Incomplete samples detected");
    }

    int samples = block_size / upipe_block_to_sound->sample_size;
    block_size = samples * upipe_block_to_sound->sample_size;

    /* alloc sound ubuf */
    struct ubuf *ubuf_block_to_sound = ubuf_sound_alloc(upipe_block_to_sound->ubuf_mgr,
                                                        samples);

    if (unlikely(ubuf_block_to_sound == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }


    /* map sound ubuf for writing */
    int32_t *w;

    if (unlikely(!ubase_check(ubuf_sound_write_int32_t(ubuf_block_to_sound, 0, -1, &w,
                                                            upipe_block_to_sound->planes)))) {
        upipe_err(upipe, "could not write uref, dropping samples");
        ubuf_sound_unmap(ubuf_block_to_sound, 0, -1, upipe_block_to_sound->planes);
        ubuf_free(ubuf_block_to_sound);
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    /* map block ubuf for reading */
    const uint8_t *r;
    int end = -1;
    if (unlikely(!ubase_check(uref_block_read(uref, 0, &end, &r)))) {
        upipe_err(upipe, "could not read uref, dropping samples");
        ubuf_sound_unmap(ubuf_block_to_sound, 0, -1, upipe_block_to_sound->planes);
        ubuf_block_unmap(uref->ubuf, 0);
        ubuf_free(ubuf_block_to_sound);
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    /* copy block to sound */
    memcpy(w, r, block_size);
    /* unmap ubufs */
    ubuf_sound_unmap(ubuf_block_to_sound, 0, -1, upipe_block_to_sound->planes);
    ubuf_block_unmap(uref->ubuf, 0);
    /* attach sound ubuf to uref */
    uref_attach_ubuf(uref, ubuf_block_to_sound);
    /* output pipe */

    upipe_block_to_sound_output(upipe, uref, upump_p);
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
    struct upipe_block_to_sound *upipe_block_to_sound = upipe_block_to_sound_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    if (unlikely(!ubase_check(uref_flow_match_def(flow_def, "block.")))) {
        uref_free(flow_def);
        return UBASE_ERR_INVALID;
    }

    flow_def = uref_dup(upipe_block_to_sound->flow_def_config);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if(unlikely(!ubase_check(uref_flow_match_def(flow_def, "sound.s32.")))) {
        uref_free(flow_def);
        return UBASE_ERR_INVALID;
    }
    uint8_t channels, planes, sample_size;
    if(unlikely(!ubase_check(uref_sound_flow_get_channels(flow_def, &channels))) ||
        unlikely(!ubase_check(uref_sound_flow_get_planes(flow_def, &planes))) ||
        unlikely(!ubase_check(uref_sound_flow_get_sample_size(flow_def, &sample_size)))) {
        uref_free(flow_def);
        return UBASE_ERR_INVALID;
    }

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_block_to_sound_store_flow_def(upipe, flow_def_dup);
    upipe_block_to_sound_require_ubuf_mgr(upipe, flow_def);

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
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_block_to_sound_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            upipe_block_to_sound_free_output_proxy(upipe, request);
            return UBASE_ERR_NONE;
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_block_to_sound_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_block_to_sound_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_block_to_sound_free(struct upipe *upipe)
{
    struct upipe_block_to_sound *upipe_block_to_sound = upipe_block_to_sound_from_upipe(upipe);
    upipe_throw_dead(upipe);
    uref_free(upipe_block_to_sound->flow_def_config);
    upipe_block_to_sound_clean_output(upipe);
    upipe_block_to_sound_clean_urefcount(upipe);
    upipe_block_to_sound_clean_ubuf_mgr(upipe);
    upipe_block_to_sound_free_flow(upipe);
}

/** upipe_block_to_sound */
static struct upipe_mgr upipe_block_to_sound_mgr = {
    .refcount = NULL,
    .signature = UPIPE_BLOCK_TO_SOUND_SIGNATURE,

    .upipe_alloc = upipe_block_to_sound_alloc,
    .upipe_input = upipe_block_to_sound_input,
    .upipe_control = upipe_block_to_sound_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for block_to_sound pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_block_to_sound_mgr_alloc(void)
{
    return &upipe_block_to_sound_mgr;
}
