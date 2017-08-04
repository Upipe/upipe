/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module converting sound and pic ubuf to block
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_sound.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-modules/upipe_convert_to_block.h>

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
static bool upipe_tblk_handle(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p);
/** @hidden */
static int upipe_tblk_check(struct upipe *upipe, struct uref *flow_format);

/** upipe_tblk structure */
struct upipe_tblk {
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
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** current input allocator */
    uint32_t input_alloc;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_tblk, upipe, UPIPE_TBLK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_tblk, urefcount, upipe_tblk_free);
UPIPE_HELPER_VOID(upipe_tblk);
UPIPE_HELPER_OUTPUT(upipe_tblk, output, flow_def, output_state, request_list);
UPIPE_HELPER_UBUF_MGR(upipe_tblk, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_tblk_check,
                      upipe_tblk_register_output_request,
                      upipe_tblk_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_tblk, urefs, nb_urefs, max_urefs, blockers, upipe_tblk_handle)

/** @internal @This allocates a tblk pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_tblk_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_tblk_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_tblk *upipe_tblk = upipe_tblk_from_upipe(upipe);
    upipe_tblk_init_urefcount(upipe);
    upipe_tblk_init_ubuf_mgr(upipe);
    upipe_tblk_init_output(upipe);
    upipe_tblk_init_input(upipe);
    upipe_tblk->input_alloc = UBUF_ALLOC_BLOCK;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This handles data from pic allocator.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_tblk_handle_pic(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_tblk *upipe_tblk = upipe_tblk_from_upipe(upipe);

    /* Always operate on the first chroma plane. */
    const char *chroma = NULL;
    if (unlikely(uref->ubuf == NULL ||
                 !ubase_check(ubuf_pic_plane_iterate(uref->ubuf, &chroma)) ||
                 chroma == NULL)) {
        uref_free(uref);
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        return;
    }

    /* First try the ubuf_mem_shared method. */
    struct ubuf *ubuf = ubuf_block_mem_alloc_from_pic(upipe_tblk->ubuf_mgr,
                                                      uref->ubuf, chroma);
    if (unlikely(ubuf == NULL)) {
        /* We have to memcpy the thing. */
        size_t hsize, vsize, stride;
        uint8_t macropixel, hsub, vsub, macropixel_size;
        if (unlikely(!ubase_check(uref_pic_size(uref, NULL, &vsize,
                                                NULL)) ||
                     !ubase_check(uref_pic_plane_size(uref, chroma,
                             &stride, NULL, &vsub, NULL)))) {
            uref_free(uref);
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
            return;
        }

        size_t size = stride * vsize / vsub;
        ubuf = ubuf_block_alloc(upipe_tblk->ubuf_mgr, size);
        if (unlikely(ubuf == NULL)) {
            uref_free(uref);
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
            return;
        }

        const uint8_t *r;
        if (unlikely(!ubase_check(uref_pic_plane_read(uref, chroma,
                            0, 0, -1, -1, &r)))) {
            ubuf_free(ubuf);
            uref_free(uref);
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
            return;
        }

        uint8_t *w;
        int end = -1;
        if (unlikely(!ubase_check(ubuf_block_write(ubuf, 0, &end, &w)))) {
            uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
            ubuf_free(ubuf);
            uref_free(uref);
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
            return;
        }

        memcpy(w, r, size);
        ubuf_block_unmap(ubuf, 0);
        uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
    }

    uref_attach_ubuf(uref, ubuf);
    upipe_tblk_output(upipe, uref, upump_p);
}

/** @internal @This handles data from sound allocator.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_tblk_handle_sound(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_tblk *upipe_tblk = upipe_tblk_from_upipe(upipe);

    /* Always operate on the first channel plane. */
    const char *channel = NULL;
    if (unlikely(uref->ubuf == NULL ||
                 !ubase_check(ubuf_sound_plane_iterate(uref->ubuf, &channel)) ||
                 channel == NULL)) {
        uref_free(uref);
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        return;
    }

    /* First try the ubuf_mem_shared method. */
    struct ubuf *ubuf = ubuf_block_mem_alloc_from_sound(upipe_tblk->ubuf_mgr,
                                                        uref->ubuf, channel);
    if (unlikely(ubuf == NULL)) {
        /* We have to memcpy the thing. */
        size_t samples;
        uint8_t sample_size;
        if (unlikely(!ubase_check(uref_sound_size(uref, &samples,
                                                  &sample_size)))) {
            uref_free(uref);
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
            return;
        }

        size_t size = samples * sample_size;
        ubuf = ubuf_block_alloc(upipe_tblk->ubuf_mgr, size);
        if (unlikely(ubuf == NULL)) {
            uref_free(uref);
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
            return;
        }

        const uint8_t *r;
        if (unlikely(!ubase_check(uref_sound_plane_read_uint8_t(uref, channel,
                                                                0, -1, &r)))) {
            ubuf_free(ubuf);
            uref_free(uref);
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
            return;
        }

        uint8_t *w;
        int end = -1;
        if (unlikely(!ubase_check(ubuf_block_write(ubuf, 0, &end, &w)))) {
            uref_sound_plane_unmap(uref, channel, 0, -1);
            ubuf_free(ubuf);
            uref_free(uref);
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
            return;
        }

        memcpy(w, r, size);
        ubuf_block_unmap(ubuf, 0);
        uref_sound_plane_unmap(uref, channel, 0, -1);
    }

    uref_attach_ubuf(uref, ubuf);
    upipe_tblk_output(upipe, uref, upump_p);
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_tblk_handle(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_tblk *upipe_tblk = upipe_tblk_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        if (!ubase_ncmp(def, "pic.")) {
            upipe_tblk->input_alloc = UBUF_ALLOC_PICTURE;
            uref_pic_flow_clear_format(uref);
            uref_flow_set_def(uref, "block.");
        } else if (!ubase_ncmp(def, "sound.")) {
            upipe_tblk->input_alloc = UBUF_ALLOC_SOUND;
            uref_sound_flow_clear_format(uref);
            uref_flow_set_def(uref, "block.");
        } else
            upipe_tblk->input_alloc = UBUF_ALLOC_BLOCK;

        upipe_tblk_store_flow_def(upipe, NULL);
        upipe_tblk_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_tblk->flow_def == NULL)
        return false;
    assert(upipe_tblk->ubuf_mgr != NULL);

    switch (upipe_tblk->input_alloc) {
        case UBUF_ALLOC_PICTURE:
            upipe_tblk_handle_pic(upipe, uref, upump_p);
            break;
        case UBUF_ALLOC_SOUND:
            upipe_tblk_handle_sound(upipe, uref, upump_p);
            break;
        default:
            upipe_tblk_output(upipe, uref, upump_p);
            break;
    }
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_tblk_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    if (!upipe_tblk_check_input(upipe)) {
        upipe_tblk_hold_input(upipe, uref);
        upipe_tblk_block_input(upipe, upump_p);
    } else if (!upipe_tblk_handle(upipe, uref, upump_p)) {
        upipe_tblk_hold_input(upipe, uref);
        upipe_tblk_block_input(upipe, upump_p);
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
static int upipe_tblk_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_tblk *upipe_tblk = upipe_tblk_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_tblk_store_flow_def(upipe, flow_format);

    if (upipe_tblk->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_tblk_check_input(upipe);
    upipe_tblk_output_input(upipe);
    upipe_tblk_unblock_input(upipe);
    if (was_buffered && upipe_tblk_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_tblk_input. */
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
static int upipe_tblk_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 (ubase_ncmp(def, "block.") && ubase_ncmp(def, "pic.") &&
                  ubase_ncmp(def, "sound."))))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a tblk pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_tblk_control(struct upipe *upipe,
                                int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_tblk_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_tblk_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_tblk_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_tblk_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_tblk_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_tblk_clean_input(upipe);
    upipe_tblk_clean_ubuf_mgr(upipe);
    upipe_tblk_clean_output(upipe);
    upipe_tblk_clean_urefcount(upipe);
    upipe_tblk_free_void(upipe);
}

static struct upipe_mgr upipe_tblk_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TBLK_SIGNATURE,

    .upipe_alloc = upipe_tblk_alloc,
    .upipe_input = upipe_tblk_input,
    .upipe_control = upipe_tblk_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for tblk pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_tblk_mgr_alloc(void)
{
    return &upipe_tblk_mgr;
}
