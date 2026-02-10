/*
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe interlacing module
 */

#include "upipe-modules/upipe_interlace.h"
#include "upipe/ubuf.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_input.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/uref.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_flow.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_pic_flow.h"

#include <stdint.h>
#include <stdio.h>

/** @hidden */
static bool upipe_interlace_handle(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p);
/** @hidden */
static int upipe_interlace_check(struct upipe *upipe, struct uref *flow_format);

/** @internal upipe_interlace private structure */
struct upipe_interlace {
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

    /** input is already interlaced? */
    bool bypass;
    /** output top field first */
    bool tff;
    /** drop field? */
    bool drop;
    /** last input frame */
    struct uref *uref_last;
    /** current input width */
    uint64_t width;
    /** current input heigth */
    uint64_t height;

    /** public structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_interlace, upipe, UPIPE_INTERLACE_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_interlace, urefcount, upipe_interlace_free)
UPIPE_HELPER_VOID(upipe_interlace)
UPIPE_HELPER_OUTPUT(upipe_interlace, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_UBUF_MGR(upipe_interlace, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_interlace_check,
                      upipe_interlace_register_output_request,
                      upipe_interlace_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_interlace, urefs, nb_urefs, max_urefs, blockers,
                   upipe_interlace_handle)

/** @internal @This allocates a filter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_interlace_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature, va_list args)
{
    struct upipe *upipe =
        upipe_interlace_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_interlace_init_urefcount(upipe);
    upipe_interlace_init_ubuf_mgr(upipe);
    upipe_interlace_init_output(upipe);
    upipe_interlace_init_input(upipe);

    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);
    upipe_interlace->bypass = true;
    upipe_interlace->uref_last = NULL;
    upipe_interlace->tff = true;
    upipe_interlace->drop = true;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_interlace_free(struct upipe *upipe)
{
    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_interlace->uref_last);

    upipe_interlace_clean_input(upipe);
    upipe_interlace_clean_ubuf_mgr(upipe);
    upipe_interlace_clean_output(upipe);
    upipe_interlace_clean_urefcount(upipe);
    upipe_interlace_free_void(upipe);
}

/** @internal @This processes a picture plane
 *
 * @param upipe description structure of the pipe
 * @param top input frame buffer used as top lines
 * @param bottom input frame buffer used as bottom lines
 * @param ubuf output frame buffer
 * @param chroma chroma plane to interlace
 * @return an error code
 */
static int upipe_interlace_plane(struct upipe *upipe, struct uref *top,
                                 struct uref *bottom, struct ubuf *ubuf,
                                 const char *chroma)
{
    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);
    const uint8_t *t_in;
    const uint8_t *b_in;
    uint8_t *out;
    uint8_t t_hsub, b_hsub, out_hsub;
    uint8_t t_vsub, b_vsub, out_vsub;
    uint8_t t_size, b_size, out_size;
    size_t t_stride = 0, b_stride = 0, out_stride = 0;
    int ret;

    ret =
        uref_pic_plane_size(top, chroma, &t_stride, &t_hsub, &t_vsub, &t_size);
    if (unlikely(!ubase_check(ret))) {
        upipe_err(upipe, "Could not get top input plane size");
        return ret;
    }

    ret = uref_pic_plane_size(bottom, chroma, &b_stride, &b_hsub, &b_vsub,
                              &b_size);
    if (unlikely(!ubase_check(ret))) {
        upipe_err(upipe, "Could not get bottom input plane size");
        return ret;
    }

    ret = ubuf_pic_plane_size(ubuf, chroma, &out_stride, &out_hsub, &out_vsub,
                              &out_size);
    if (unlikely(!ubase_check(ret))) {
        upipe_err(upipe, "Could not get output plane size");
        return ret;
    }

    if (!t_hsub || !t_vsub || !t_size) {
        upipe_err(upipe, "Invalid input frame");
        return UBASE_ERR_INVALID;
    }

    if (t_hsub != b_hsub || t_vsub != b_vsub || t_size != b_size) {
        upipe_err(upipe, "Incompatible input frames");
        return UBASE_ERR_INVALID;
    }

    if (t_hsub != out_hsub || t_vsub != out_vsub || t_size != out_size) {
        upipe_err(upipe, "Incompatible output frame");
        return UBASE_ERR_INVALID;
    }

    // map
    uref_pic_plane_read(top, chroma, 0, 0, -1, -1, &t_in);
    uref_pic_plane_read(bottom, chroma, 0, 0, -1, -1, &b_in);
    ubuf_pic_plane_write(ubuf, chroma, 0, 0, -1, -1, &out);

    // interlace plane
    uint64_t lines = upipe_interlace->height / t_vsub;
    uint64_t size = upipe_interlace->width * t_size / t_hsub;
    for (uint64_t l = 0; l < lines; l++) {
        memcpy(out, (l % 2 ? b_in : t_in), size);
        t_in += t_stride;
        b_in += b_stride;
        out += out_stride;
    }

    // unmap
    uref_pic_plane_unmap(top, chroma, 0, 0, -1, -1);
    uref_pic_plane_unmap(bottom, chroma, 0, 0, -1, -1);
    ubuf_pic_plane_unmap(ubuf, chroma, 0, 0, -1, -1);

    return UBASE_ERR_NONE;
}

/** @internal @This updates the output flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def input flow definition packet
 */
static void upipe_interlace_set_flow_def_real(struct upipe *upipe,
                                              struct uref *flow_def)
{
    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);

    uref_free(upipe_interlace->uref_last);
    upipe_interlace->uref_last = NULL;

    if (ubase_check(uref_pic_check_progressive(flow_def))) {
        upipe_interlace->bypass = false;
        struct urational fps;
        if (upipe_interlace->drop &&
            ubase_check(uref_pic_flow_get_fps(flow_def, &fps))) {
            fps.den *= 2;
            urational_simplify(&fps);
            uref_pic_flow_set_fps(flow_def, fps);
        }
        uref_pic_set_progressive(flow_def, false);
        uref_pic_set_tff(flow_def, upipe_interlace->tff);
        upipe_interlace_store_flow_def(upipe, NULL);
        upipe_interlace_require_ubuf_mgr(upipe, flow_def);
    } else {
        upipe_interlace->bypass = true;
        upipe_interlace_store_flow_def(upipe, flow_def);
    }
}

/** @internal @This checks the input frames
 *
 * @param upipe description structure of the pipe
 * @param top input frame used for top field
 * @param bottom input frame used for bottom field
 * @return an error code
 */
static int upipe_interlace_check_frames(struct upipe *upipe, struct uref *top,
                                        struct uref *bottom)
{
    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);

    size_t t_width = 0;
    size_t t_height = 0;
    uint8_t t_macropixel = 0;
    uref_pic_size(top, &t_width, &t_height, &t_macropixel);

    size_t b_width = 0;
    size_t b_height = 0;
    uint8_t b_macropixel = 0;
    uref_pic_size(bottom, &b_width, &b_height, &b_macropixel);

    if (t_width != b_width || t_height != b_height ||
        t_macropixel != b_macropixel) {
        upipe_err(upipe, "Incompatible frames received");
        return UBASE_ERR_INVALID;
    }

    upipe_interlace->width = t_width;
    upipe_interlace->height = t_height;

    return UBASE_ERR_NONE;
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 * @return always true
 */
static bool upipe_interlace_handle(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p)
{
    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_interlace_set_flow_def_real(upipe, uref);
        return true;
    }

    if (!upipe_interlace->flow_def) {
        if (urequest_get_opaque(&upipe_interlace->ubuf_mgr_request,
                                struct upipe *) == NULL) {
            upipe_warn(upipe, "no input flow def received, dropping...");
            uref_free(uref);
            return true;
        }

        return false;
    }

    if (upipe_interlace->bypass) {
        upipe_interlace_output(upipe, uref, upump_p);
        return true;
    }

    if (!upipe_interlace->uref_last) {
        upipe_interlace->uref_last = uref;
        return true;
    }

    // Now process input frames
    struct uref *last = upipe_interlace->uref_last;
    struct uref *top = upipe_interlace->tff ? last : uref;
    struct uref *bottom = upipe_interlace->tff ? uref : last;
    upipe_interlace->uref_last = upipe_interlace->drop ? NULL : uref_dup(uref);
    if (unlikely(
            !ubase_check(upipe_interlace_check_frames(upipe, top, bottom)))) {
        uref_free(top);
        uref_free(bottom);
        return true;
    }

    // Allocate output frame
    struct ubuf *ubuf = NULL;
    if (upipe_interlace->ubuf_mgr)
        ubuf = ubuf_pic_alloc(upipe_interlace->ubuf_mgr, upipe_interlace->width,
                              upipe_interlace->height);
    if (unlikely(!ubuf)) {
        uref_free(top);
        uref_free(bottom);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    // Interlace planes
    const char *chroma;
    uref_pic_foreach_plane(uref, chroma) {
        int ret = upipe_interlace_plane(upipe, top, bottom, ubuf, chroma);
        if (unlikely(!ubase_check(ret))) {
            uref_free(top);
            uref_free(bottom);
            ubuf_free(ubuf);
            return true;
        }
    }

    // Compute output frame duration
    uint64_t t_duration = UINT64_MAX;
    uint64_t b_duration = UINT64_MAX;
    uref_clock_get_duration(top, &t_duration);
    uref_clock_get_duration(bottom, &b_duration);

    // Free last frame
    uref_free(last);

    // Attach new ubuf to current frame
    uref_attach_ubuf(uref, ubuf);

    // Update attributes
    uref_pic_set_progressive(uref, false);
    uref_pic_set_tff(uref, upipe_interlace->tff);
    if (upipe_interlace->drop) {
        uref_clock_delete_duration(uref);
        if (t_duration != UINT64_MAX && b_duration != UINT64_MAX)
          uref_clock_set_duration(uref, t_duration + b_duration);
    }

    // Output frame
    upipe_interlace_output(upipe, uref, upump_p);

    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_interlace_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    if (!upipe_interlace_check_input(upipe)) {
        upipe_interlace_hold_input(upipe, uref);
        upipe_interlace_block_input(upipe, upump_p);
    } else if (!upipe_interlace_handle(upipe, uref, upump_p)) {
        upipe_interlace_hold_input(upipe, uref);
        upipe_interlace_block_input(upipe, upump_p);
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
static int upipe_interlace_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_interlace_store_flow_def(upipe, flow_format);

    if (upipe_interlace->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_interlace_check_input(upipe);
    upipe_interlace_output_input(upipe);
    upipe_interlace_unblock_input(upipe);
    if (was_buffered && upipe_interlace_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_interlace_input. */
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
static int upipe_interlace_set_flow_def(struct upipe *upipe,
                                        struct uref *flow_def)
{
    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);

    if (unlikely(!flow_def))
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))
    if (!ubase_check(uref_pic_check_progressive(flow_def))) {
        bool input_tff = ubase_check(uref_pic_check_tff(flow_def));
        if (input_tff != upipe_interlace->tff) {
            upipe_warn_va(upipe, "input interlacing mismatch, got %s need %s",
                          input_tff ? "tff" : "bff",
                          upipe_interlace->tff ? "tff" : "bff");
        }
    }

    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the top field first output.
 *
 * @param upipe description structure of the pipe
 * @param tff true for top field first, false for bottom field first
 * @return an error code
 */
static int upipe_interlace_set_tff_real(struct upipe *upipe, bool tff)
{
    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);
    upipe_interlace->tff = tff;
    return UBASE_ERR_NONE;
}

/** @internal @This get the top field first output configuration.
 *
 * @param upipe description structure of the pipe
 * @param tff filled with the configured value
 * @return an error code
 */
static int upipe_interlace_get_tff_real(struct upipe *upipe, bool *tff)
{
    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);
    if (tff)
        *tff = upipe_interlace->tff;
    return UBASE_ERR_NONE;
}

/** @internal @This sets field drop.
 *
 * @param upipe description structure of the pipe
 * @param drop true for dropping field
 * @return an error code
 */
static int upipe_interlace_set_drop_real(struct upipe *upipe, bool drop)
{
    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);
    upipe_interlace->drop = drop;
    return UBASE_ERR_NONE;
}

/** @internal @This get the field drop configuration.
 *
 * @param upipe description structure of the pipe
 * @param drop filled with the configured value
 * @return an error code
 */
static int upipe_interlace_get_drop_real(struct upipe *upipe, bool *drop)
{
    struct upipe_interlace *upipe_interlace = upipe_interlace_from_upipe(upipe);
    if (drop)
        *drop = upipe_interlace->drop;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_interlace_control(struct upipe *upipe, int command,
                                   va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_interlace_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_interlace_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_interlace_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_interlace_control_output(upipe, command, args);
    }

    if (command < UPIPE_CONTROL_LOCAL ||
        ubase_get_signature(args) != UPIPE_INTERLACE_SIGNATURE)
        return UBASE_ERR_UNHANDLED;

    switch (command) {
        case UPIPE_INTERLACE_SET_TFF: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_INTERLACE_SIGNATURE);
            int tff = va_arg(args, int);
            return upipe_interlace_set_tff_real(upipe, tff != 0);
        }
        case UPIPE_INTERLACE_GET_TFF: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_INTERLACE_SIGNATURE);
            bool *tff = va_arg(args, bool *);
            return upipe_interlace_get_tff_real(upipe, tff);
        }
        case UPIPE_INTERLACE_SET_DROP: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_INTERLACE_SIGNATURE);
            int drop = va_arg(args, int);
            return upipe_interlace_set_drop_real(upipe, drop != 0);
        }
        case UPIPE_INTERLACE_GET_DROP: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_INTERLACE_SIGNATURE);
            bool *drop = va_arg(args, bool *);
            return upipe_interlace_get_drop_real(upipe, drop);
        }
    }

    return UBASE_ERR_UNHANDLED;
}

/** module manager static descriptor */
static struct upipe_mgr upipe_interlace_mgr = {
    .refcount = NULL,
    .signature = UPIPE_INTERLACE_SIGNATURE,

    .upipe_alloc = upipe_interlace_alloc,
    .upipe_input = upipe_interlace_input,
    .upipe_control = upipe_interlace_control,
    .upipe_command_str = upipe_interlace_command_str,
};

/** @This returns the management structure for interlace pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_interlace_mgr_alloc(void)
{
    return &upipe_interlace_mgr;
}
