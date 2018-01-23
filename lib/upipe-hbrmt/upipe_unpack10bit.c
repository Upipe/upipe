/*
 * 10 bit unpacking
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/** @file
 * @short Upipe pack10bit module
 */

#include <config.h>

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_block_flow.h>
#include <upipe/upipe.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/ubuf_block.h>
#include <upipe/uref_block.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>

#include <upipe-hbrmt/upipe_unpack10bit.h>

#include "sdidec.h"

#define UBUF_ALIGN 32 /* 256-bits simd (avx2) */

/** upipe_unpack10bit structure with unpack10bit parameters */
struct upipe_unpack10bit {
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

    /** unpacking */
    void (*unpack)(const uint8_t *src, uint16_t *y, int64_t pixels);

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static bool upipe_unpack10bit_handle(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p);
/** @hidden */
static int upipe_unpack10bit_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_unpack10bit, upipe, UPIPE_UNPACK10BIT_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_unpack10bit, urefcount, upipe_unpack10bit_free);
UPIPE_HELPER_VOID(upipe_unpack10bit);
UPIPE_HELPER_OUTPUT(upipe_unpack10bit, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_unpack10bit, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_unpack10bit_check,
                      upipe_unpack10bit_register_output_request,
                      upipe_unpack10bit_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_unpack10bit, urefs, nb_urefs, max_urefs, blockers, upipe_unpack10bit_handle)

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_unpack10bit_handle(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_unpack10bit *upipe_unpack10bit = upipe_unpack10bit_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_unpack10bit_store_flow_def(upipe, NULL);
        upipe_unpack10bit_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_unpack10bit->flow_def == NULL || upipe_unpack10bit->ubuf_mgr == NULL)
        return false;

    int input_size = -1;
    const uint8_t *input = NULL;
    if (unlikely(!ubase_check(uref_block_read(uref, 0, &input_size, &input)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    struct ubuf *ubuf_out = ubuf_block_alloc(upipe_unpack10bit->ubuf_mgr,
                                             2 * (input_size * 8 / 10));
    if (unlikely(ubuf_out == NULL)) {
        uref_block_unmap(uref, 0);
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    int out_size = -1;
    uint8_t *out = NULL;
    if (unlikely(!ubase_check(ubuf_block_write(ubuf_out, 0, &out_size, &out)))) {
        uref_block_unmap(uref, 0);
        uref_free(uref);
        ubuf_free(ubuf_out);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    upipe_unpack10bit->unpack(input, (uint16_t *)out, (2*input_size) / 5);

    ubuf_block_unmap(ubuf_out, 0);
    uref_block_unmap(uref, 0);
    uref_attach_ubuf(uref, ubuf_out);

    upipe_unpack10bit_output(upipe, uref, upump_p);

    return true;
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_unpack10bit_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    if (!upipe_unpack10bit_check_input(upipe)) {
        upipe_unpack10bit_hold_input(upipe, uref);
        upipe_unpack10bit_block_input(upipe, upump_p);
    } else if (!upipe_unpack10bit_handle(upipe, uref, upump_p)) {
        upipe_unpack10bit_hold_input(upipe, uref);
        upipe_unpack10bit_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_unpack10bit_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_unpack10bit *upipe_unpack10bit = upipe_unpack10bit_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_unpack10bit_store_flow_def(upipe, flow_format);

    if (upipe_unpack10bit->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_unpack10bit_check_input(upipe);
    upipe_unpack10bit_output_input(upipe);
    upipe_unpack10bit_unblock_input(upipe);
    if (was_buffered && upipe_unpack10bit_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_unpack10bit_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This requires a ubuf manager by proxy, and amends the flow
 * format.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_unpack10bit_amend_ubuf_mgr(struct upipe *upipe,
                                            struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);

    uint64_t append;
    if (!ubase_check(uref_block_flow_get_append(flow_format, &append)) || append < 12) {
        uref_block_flow_set_append(flow_format, 12);
    }

    uint64_t align;
    if (!ubase_check(uref_block_flow_get_align(flow_format, &align)) || !align) {
        uref_block_flow_set_align(flow_format, UBUF_ALIGN);
        align = UBUF_ALIGN;
    }

    if (align % UBUF_ALIGN) {
        align = align * UBUF_ALIGN / ubase_gcd(align, UBUF_ALIGN);
        uref_block_flow_set_align(flow_format, align);
    }

    struct urequest ubuf_mgr_request;
    urequest_set_opaque(&ubuf_mgr_request, request);
    urequest_init_ubuf_mgr(&ubuf_mgr_request, flow_format,
                           upipe_unpack10bit_provide_output_proxy, NULL);
    upipe_throw_provide_request(upipe, &ubuf_mgr_request);
    urequest_clean(&ubuf_mgr_request);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_unpack10bit_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "block."))

    uint64_t append;
    UBASE_RETURN(uref_block_flow_get_append(flow_def, &append));
    if (append < 12)
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup = uref_dup(flow_def);

    if (flow_def_dup == NULL)
        return UBASE_ERR_ALLOC;

    uref_flow_set_def(flow_def_dup, "block.");
    uref_block_flow_set_align(flow_def_dup, UBUF_ALIGN);

    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_unpack10bit_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_unpack10bit_amend_ubuf_mgr(upipe, request);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_unpack10bit_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_unpack10bit_free_output_proxy(upipe, request);
        }

        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_GET_FLOW_DEF:
            return upipe_unpack10bit_control_output(upipe, command, args);
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_unpack10bit_set_flow_def(upipe, flow);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a unpack10bit pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_unpack10bit_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_unpack10bit_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_unpack10bit *upipe_unpack10bit = upipe_unpack10bit_from_upipe(upipe);

    upipe_unpack10bit->unpack = upipe_sdi_to_uyvy_c;
#if defined(HAVE_X86ASM)
#if defined(__i686__) || defined(__x86_64__)
    if (__builtin_cpu_supports("ssse3"))
        upipe_unpack10bit->unpack = upipe_sdi_to_uyvy_ssse3;

    if (__builtin_cpu_supports("avx2"))
        upipe_unpack10bit->unpack = upipe_sdi_to_uyvy_avx2;
#endif
#endif

    upipe_unpack10bit_init_urefcount(upipe);
    upipe_unpack10bit_init_ubuf_mgr(upipe);
    upipe_unpack10bit_init_output(upipe);
    upipe_unpack10bit_init_input(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_unpack10bit_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_unpack10bit_clean_input(upipe);
    upipe_unpack10bit_clean_output(upipe);
    upipe_unpack10bit_clean_ubuf_mgr(upipe);
    upipe_unpack10bit_clean_urefcount(upipe);
    upipe_unpack10bit_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_unpack10bit_mgr = {
    .refcount = NULL,
    .signature = UPIPE_UNPACK10BIT_SIGNATURE,

    .upipe_alloc = upipe_unpack10bit_alloc,
    .upipe_input = upipe_unpack10bit_input,
    .upipe_control = upipe_unpack10bit_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for pack10bit pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_unpack10bit_mgr_alloc(void)
{
    return &upipe_unpack10bit_mgr;
}
