/*
 * V210 decoder
 *
 * Copyright (C) 2009 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
 * Copyright (c) 2017 Open Broadcast Systems Ltd
 *
 * This file is based on the implementation in FFmpeg.
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
 * @short Upipe v210dec module
 */

#include <config.h>

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/upipe.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <upipe-v210/upipe_v210dec.h>

#include "v210dec.h"

#define UPIPE_V210_MAX_PLANES 3
#define UBUF_ALIGN 32

static const char *v210_chroma_str = "u10y10v10y10u10y10v10y10u10y10v10y10";

enum v210dec_output_type {
    V2D_OUTPUT_PLANAR_8 = 1,
    V2D_OUTPUT_PLANAR_10,
};

/** upipe_v210dec structure with v210dec parameters */
struct upipe_v210dec {
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

    /** output type **/
    enum v210dec_output_type output_type;

    /** 8-bit line packing function **/
    void (*v210_to_planar_8)(const void *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels);
    /** 10-bit line packing function **/
    void (*v210_to_planar_10)(const void *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t pixels);

    /** output chroma map */
    const char *output_chroma_map[UPIPE_V210_MAX_PLANES+1];

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static bool upipe_v210dec_handle(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p);
/** @hidden */
static int upipe_v210dec_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_v210dec, upipe, UPIPE_V210DEC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_v210dec, urefcount, upipe_v210dec_free);
//UPIPE_HELPER_VOID(upipe_v210dec);
UPIPE_HELPER_FLOW(upipe_v210dec, "pic.");
UPIPE_HELPER_OUTPUT(upipe_v210dec, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_v210dec, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_v210dec_check,
                      upipe_v210dec_register_output_request,
                      upipe_v210dec_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_v210dec, urefs, nb_urefs, max_urefs, blockers, upipe_v210dec_handle)

// TODO: handle endianness

static inline uint32_t rl32(const void *src)
{
    const uint8_t *s = src;
    return s[0] |
        (s[1] <<  8) |
        (s[2] << 16) |
        (s[3] << 24);
}

#define READ_PIXELS_8(a, b, c) \
    do { \
        uint32_t val = rl32(src); \
        src += 4; \
        *(a)++ = (val >> 2)  & 255; \
        *(b)++ = (val >> 12) & 255; \
        *(c)++ = (val >> 22) & 255; \
    } while (0)

#define READ_PIXELS_10(a, b, c) \
    do { \
        uint32_t val = rl32(src); \
        src += 4; \
        *(a)++ = (val)       & 1023; \
        *(b)++ = (val >> 10) & 1023; \
        *(c)++ = (val >> 20) & 1023; \
    } while (0)

/** @internal @This setups convert functions
 *
 * @param upipe description structure of the pipe
 * @param assembly whether to use assembly
 */
static void v210dec_setup_asm(struct upipe *upipe, bool assembly)
{
    struct upipe_v210dec *v210dec = upipe_v210dec_from_upipe(upipe);

    v210dec->v210_to_planar_8  = upipe_v210_to_planar_8_c;
    v210dec->v210_to_planar_10 = upipe_v210_to_planar_10_c;

    if (!assembly)
        return;

#if defined(HAVE_X86ASM)
#if defined(__i686__) || defined(__x86_64__)
    if (__builtin_cpu_supports("ssse3")) {
        v210dec->v210_to_planar_8  = upipe_v210_to_planar_8_aligned_ssse3;
        v210dec->v210_to_planar_10 = upipe_v210_to_planar_10_aligned_ssse3;
    }
    if (__builtin_cpu_supports("avx")) {
        v210dec->v210_to_planar_8  = upipe_v210_to_planar_8_aligned_avx;
        v210dec->v210_to_planar_10 = upipe_v210_to_planar_10_aligned_avx;
    }
    if (__builtin_cpu_supports("avx2")) {
        v210dec->v210_to_planar_8  = upipe_v210_to_planar_8_aligned_avx2;
        v210dec->v210_to_planar_10 = upipe_v210_to_planar_10_aligned_avx2;
    }
#endif
#endif
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_v210dec_handle(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_v210dec *v210dec = upipe_v210dec_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_v210dec_store_flow_def(upipe, NULL);
        upipe_v210dec_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (v210dec->flow_def == NULL)
        return false;

    size_t input_hsize, input_vsize;
    if (!ubase_check(uref_pic_size(uref, &input_hsize, &input_vsize, NULL))) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return true;
    }

    const uint8_t *input_plane;
    size_t input_stride;
    if (unlikely(!ubase_check(uref_pic_plane_read(uref, v210_chroma_str,
                        0, 0, -1, -1, &input_plane)) ||
                 !ubase_check(uref_pic_plane_size(uref, v210_chroma_str,
                      &input_stride, 0, 0, 0)))) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return true;
    }

    uint64_t output_hsize;
    if (unlikely(!ubase_check(uref_pic_flow_get_hsize(v210dec->flow_def, &output_hsize)))) {
        upipe_warn(upipe, "could not find output picture size");
        uref_free(uref);
        return true;
    }

    uint8_t *output_planes[3];
    size_t output_strides[3];
    struct ubuf *ubuf = ubuf_pic_alloc(v210dec->ubuf_mgr, output_hsize, input_vsize);
    if (unlikely(!ubuf)) {
        // TODO free allocated memory
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    for (int i = 0; i < 3; i++) {
        const char *chroma = v210dec->output_chroma_map[i];

        if (unlikely(!ubase_check(ubuf_pic_plane_write(ubuf, chroma,
                            0, 0, -1, -1,
                            &output_planes[i])) ||
                     !ubase_check(ubuf_pic_plane_size(ubuf, chroma,
                             &output_strides[i],
                             0, 0, 0)))) {
            // TODO free allocated memory`
            upipe_warn(upipe, "invalid buffer received");
            ubuf_free(ubuf);
            uref_free(uref);
            return true;
        }
    }

    switch (v210dec->output_type) {
        case V2D_OUTPUT_PLANAR_8: {
            for (int h = 0; h < input_vsize; h++) {
                uint8_t *y = output_planes[0];
                uint8_t *u = output_planes[1];
                uint8_t *v = output_planes[2];
                const uint32_t *src = (uint32_t*)input_plane;

                int w = (output_hsize / 6) * 6;
                v210dec->v210_to_planar_8(src, y, u, v, w);

                y += w;
                u += w >> 1;
                v += w >> 1;
                src += (w * 2) / 3;

                if (w < output_hsize - 1) {
                    READ_PIXELS_8(u, y, v);
                    uint32_t val = *src++;
                    *y++ = (val >> 2) & 255;

                    if (w < output_hsize - 3) {
                        *u++ = (val >> 12) & 255;
                        *y++ = (val >> 22) & 255;

                        val = rl32(src);
                        src++;
                        *v++ = (val >>  2) & 255;
                        *y++ = (val >> 12) & 255;
                    }
                }

                for (int i = 0; i < 3; i++)
                    output_planes[i] += output_strides[i];
                input_plane += input_stride;
            }
        } break;

        case V2D_OUTPUT_PLANAR_10: {
            for (int h = 0; h < input_vsize; h++) {
                uint16_t *y = (uint16_t*)output_planes[0];
                uint16_t *u = (uint16_t*)output_planes[1];
                uint16_t *v = (uint16_t*)output_planes[2];
                const uint32_t *src = (uint32_t*)input_plane;

                int w = (output_hsize / 6) * 6;
                v210dec->v210_to_planar_10(src, y, u, v, w);

                y += w;
                u += w >> 1;
                v += w >> 1;
                src += (w * 2) / 3;

                if (w < output_hsize - 1) {
                    READ_PIXELS_10(u, y, v);
                    uint32_t val = rl32(src);
                    src++;
                    *y++ = val & 1023;

                    if (w < output_hsize - 3) {
                        *u++ = (val >> 10) & 1023;
                        *y++ = (val >> 20) & 1023;

                        val = rl32(src);
                        src++;
                        *v++ = val & 1023;
                        *y++ = (val >> 10) & 1023;
                    }
                }

                for (int i = 0; i < 3; i++)
                    output_planes[i] += output_strides[i];
                input_plane += input_stride;
            }
        } break;

        default:
            assert(0);
    }

    uref_pic_plane_unmap(uref, v210_chroma_str, 0, 0, -1, -1);
    for (int i = 0; i < 3; i++)
        ubuf_pic_plane_unmap(ubuf, v210dec->output_chroma_map[i], 0, 0, -1, -1);

    uref_attach_ubuf(uref, ubuf);
    upipe_v210dec_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_v210dec_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    if (!upipe_v210dec_check_input(upipe)) {
        upipe_v210dec_hold_input(upipe, uref);
        upipe_v210dec_block_input(upipe, upump_p);
    } else if (!upipe_v210dec_handle(upipe, uref, upump_p)) {
        upipe_v210dec_hold_input(upipe, uref);
        upipe_v210dec_block_input(upipe, upump_p);
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
static int upipe_v210dec_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_v210dec *v210dec = upipe_v210dec_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_v210dec_store_flow_def(upipe, flow_format);

    if (v210dec->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_v210dec_check_input(upipe);
    upipe_v210dec_output_input(upipe);
    upipe_v210dec_unblock_input(upipe);
    if (was_buffered && upipe_v210dec_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_v210dec_input. */
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
static int upipe_v210dec_amend_ubuf_mgr(struct upipe *upipe,
                                        struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);

    uint64_t align;
    if (!ubase_check(uref_pic_flow_get_align(flow_format, &align)) || !align) {
        uref_pic_flow_set_align(flow_format, 32);
        align = 32;
    }

    if (align % 32) {
        align = align * 32 / ubase_gcd(align, 32);
        uref_pic_flow_set_align(flow_format, align);
    }

    struct urequest ubuf_mgr_request;
    urequest_set_opaque(&ubuf_mgr_request, request);
    urequest_init_ubuf_mgr(&ubuf_mgr_request, flow_format,
                           upipe_v210dec_provide_output_proxy, NULL);
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
static int upipe_v210dec_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))

    struct upipe_v210dec *v210dec = upipe_v210dec_from_upipe(upipe);

    if (unlikely(!ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 16, v210_chroma_str)))) {
        upipe_err(upipe, "incompatible input flow def");
        uref_dump(flow_def, upipe->uprobe);
        return UBASE_ERR_EXTERNAL;
    }

    uint64_t align;
    if (!ubase_check(uref_pic_flow_get_align(flow_def, &align)))
        align = 0;

    v210dec_setup_asm(upipe, align && (align % UBUF_ALIGN) == 0);

    struct uref *output_flow = uref_dup(flow_def);
    if (output_flow == NULL)
        return UBASE_ERR_ALLOC;

    switch (v210dec->output_type) {
        case V2D_OUTPUT_PLANAR_8: {
            v210dec->output_chroma_map[0] = "y8";
            v210dec->output_chroma_map[1] = "u8";
            v210dec->output_chroma_map[2] = "v8";
            uref_pic_flow_clear_format(output_flow);
            UBASE_RETURN(uref_pic_flow_set_align(output_flow, 32));
            UBASE_RETURN(uref_pic_flow_set_macropixel(output_flow, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(output_flow, 1, 1, 1, "y8"))
            UBASE_RETURN(uref_pic_flow_add_plane(output_flow, 2, 1, 1, "u8"))
            UBASE_RETURN(uref_pic_flow_add_plane(output_flow, 2, 1, 1, "v8"))
            UBASE_RETURN(uref_pic_flow_set_hmappend(output_flow, 12 + 16));
        } break;

        case V2D_OUTPUT_PLANAR_10: {
            v210dec->output_chroma_map[0] = "y10l";
            v210dec->output_chroma_map[1] = "u10l";
            v210dec->output_chroma_map[2] = "v10l";
            uref_pic_flow_clear_format(output_flow);
            UBASE_RETURN(uref_pic_flow_set_align(output_flow, 32));
            UBASE_RETURN(uref_pic_flow_set_macropixel(output_flow, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(output_flow, 1, 1, 2, "y10l"))
            UBASE_RETURN(uref_pic_flow_add_plane(output_flow, 2, 1, 2, "u10l"))
            UBASE_RETURN(uref_pic_flow_add_plane(output_flow, 2, 1, 2, "v10l"))
            UBASE_RETURN(uref_pic_flow_set_hmappend(output_flow, 6 + 8));
        } break;

        default:
            upipe_err(upipe, "unknown output format");
            uref_dump(flow_def, upipe->uprobe);
            return UBASE_ERR_EXTERNAL;
    }

    upipe_input(upipe, output_flow, NULL);
    return UBASE_ERR_NONE;
}

#if 0
/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_v210dec_provide_flow_format(struct upipe *upipe,
                                              struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);

    uref_pic_flow_clear_format(flow_format);
    uref_pic_flow_set_align(flow_format, 32);
    uref_pic_flow_set_macropixel(flow_format, 48);
    uref_pic_flow_add_plane(flow_format, 1, 1, 128, v210_chroma_str);

    return urequest_provide_flow_format(request, flow_format);
}
#endif

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_v210dec_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_v210dec_amend_ubuf_mgr(upipe, request);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_v210dec_alloc_output_proxy(upipe, request);
        }

        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_v210dec_free_output_proxy(upipe, request);
        }

        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_GET_FLOW_DEF:
            return upipe_v210dec_control_output(upipe, command, args);

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_v210dec_set_flow_def(upipe, flow);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a v210dec pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_v210dec_alloc(struct upipe_mgr *manager,
        struct uprobe *probe,
        uint32_t signature,
        va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_v210dec_alloc_flow(manager, probe, signature,
            args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_v210dec *v210dec = upipe_v210dec_from_upipe(upipe);

#define PRINT_OUTPUT_TYPE(a) \
    upipe_dbg(upipe, "output type is " #a)

    if (ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) &&
        ubase_check(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "u8")) &&
        ubase_check(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "v8"))) {
        v210dec->output_type = V2D_OUTPUT_PLANAR_8;
        PRINT_OUTPUT_TYPE(V2D_OUTPUT_PLANAR_8);
    }

    else if (ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y10l")) &&
             ubase_check(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "u10l")) &&
             ubase_check(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "v10l"))) {
        v210dec->output_type = V2D_OUTPUT_PLANAR_10;
        PRINT_OUTPUT_TYPE(V2D_OUTPUT_PLANAR_10);
    }

    else {
        upipe_err(upipe, "unknown output format");
        upipe_v210dec_free_flow(upipe);
        return NULL;
    }

#undef PRINT_OUTPUT_TYPE

    upipe_v210dec_init_urefcount(upipe);
    upipe_v210dec_init_ubuf_mgr(upipe);
    upipe_v210dec_init_output(upipe);
    upipe_v210dec_init_input(upipe);

    uref_free(flow_def);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_v210dec_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_v210dec_clean_input(upipe);
    upipe_v210dec_clean_output(upipe);
    upipe_v210dec_clean_ubuf_mgr(upipe);
    upipe_v210dec_clean_urefcount(upipe);
    upipe_v210dec_free_flow(upipe);
}

/** @This returns the management structure for v210dec pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_v210dec_mgr_alloc(void)
{
    /** module manager static descriptor */
    static struct upipe_mgr upipe_v210dec_mgr = {
        .refcount = NULL,
        .signature = UPIPE_V210DEC_SIGNATURE,
        .upipe_alloc = upipe_v210dec_alloc,
        .upipe_input = upipe_v210dec_input,
        .upipe_control = upipe_v210dec_control,
        .upipe_mgr_control = NULL,
    };

    return &upipe_v210dec_mgr;
}
