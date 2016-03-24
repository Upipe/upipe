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
 * @short Upipe chunk module - outputs fixed-length blocks from stream
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe-modules/upipe_chunk_stream.h>

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

#define EXPECTED_FLOW_DEF "block."

#define DEFAULT_MTU 1460 /* 1500 - 20 - 8 - 12 (eth - ip - udp - rtp) */
#define DEFAULT_ALIGN 4 /* 2ch s16 packed audio */

/** upipe_chunk_stream structure */ 
struct upipe_chunk_stream {
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

    /** maximum outbound block size */
    unsigned int mtu;
    /** block size alignment */
    unsigned int align;
    /** aligned block size */
    unsigned int size;

    /** next uref to be processed */
    struct uref *next_uref;
    /** original size of the next uref */
    size_t next_uref_size;
    /** urefs received after next uref */
    struct uchain urefs;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_chunk_stream, upipe, UPIPE_CHUNK_STREAM_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_chunk_stream, urefcount, upipe_chunk_stream_free);
UPIPE_HELPER_VOID(upipe_chunk_stream);
UPIPE_HELPER_OUTPUT(upipe_chunk_stream, output, flow_def, output_state, request_list);
UPIPE_HELPER_UREF_STREAM(upipe_chunk_stream, next_uref, next_uref_size, urefs, NULL)

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_chunk_stream_input(struct upipe *upipe,
                                     struct uref *uref, struct upump **upump_p)
{
    struct upipe_chunk_stream *upipe_chunk_stream =
                       upipe_chunk_stream_from_upipe(upipe);
    size_t remaining = 0;

    upipe_chunk_stream_append_uref_stream(upipe, uref);

    while(upipe_chunk_stream->next_uref
          && ubase_check(uref_block_size(upipe_chunk_stream->next_uref,
                                         &remaining))
                          && (remaining >= upipe_chunk_stream->size)) {
        uref = upipe_chunk_stream_extract_uref_stream(upipe, 
                                    upipe_chunk_stream->size);
        if (unlikely(!uref)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        upipe_chunk_stream_output(upipe, uref, upump_p);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_chunk_stream_set_flow_def(struct upipe *upipe,
                                           struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    upipe_chunk_stream_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This flushes input buffers.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_chunk_stream_flush(struct upipe *upipe)
{
    struct upipe_chunk_stream *upipe_chunk_stream =
                       upipe_chunk_stream_from_upipe(upipe);
    size_t remaining = 0;
    size_t size = 0;
    struct uref *uref;

    while(upipe_chunk_stream->next_uref
          && ubase_check(uref_block_size(upipe_chunk_stream->next_uref,
                                         &remaining)) && (remaining > 0)) {
        size = (remaining >= upipe_chunk_stream->size)
               ? upipe_chunk_stream->size
               : ((remaining / upipe_chunk_stream->align)
                           * upipe_chunk_stream->align);

        uref = upipe_chunk_stream_extract_uref_stream(upipe, size);
        if (unlikely(!uref)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        
        upipe_chunk_stream_output(upipe, uref, NULL);
    }
    upipe_chunk_stream_clean_uref_stream(upipe);
    upipe_chunk_stream_init_uref_stream(upipe);
}

/** @This sets the configured mtu of TS packets.
 * @param upipe description structure of the pipe
 * @param mtu max packet size, in octets
 * @param align packet chunk alignement, in octets
 * @return an error code
 */
static int _upipe_chunk_stream_set_mtu(struct upipe *upipe,
                                       unsigned int mtu, unsigned int align)
{
    struct upipe_chunk_stream *upipe_chunk_stream =
                       upipe_chunk_stream_from_upipe(upipe);
    if (unlikely(mtu == 0 || align == 0 || align >= mtu)) {
        upipe_warn_va(upipe, "invalid mtu (%u) or alignement (%u)",
                      mtu, align);
        return UBASE_ERR_INVALID;
    }
    upipe_chunk_stream->align = align;
    upipe_chunk_stream->mtu = mtu;
    upipe_chunk_stream->size = (mtu / align) * align;
    return UBASE_ERR_NONE;
}

/** @This returns the configured mtu of TS packets.
 *
 * @param upipe description structure of the pipe
 * @param mtu_p filled in with the configured mtu, in octets
 * @param align_p filled in with the configured alignement, in octets
 * @return an error code
 */
static int _upipe_chunk_stream_get_mtu(struct upipe *upipe,
                                       unsigned int *mtu, unsigned int *align)
{
    struct upipe_chunk_stream *upipe_chunk_stream =
                       upipe_chunk_stream_from_upipe(upipe);
    if (mtu) {
        *mtu = upipe_chunk_stream->mtu;
    }
    if (align) {
        *align = upipe_chunk_stream->align;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a chunk_stream pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_chunk_stream_control(struct upipe *upipe,
                                      int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_chunk_stream_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_chunk_stream_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_chunk_stream_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_chunk_stream_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_chunk_stream_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_chunk_stream_set_output(upipe, output);
        }

        case UPIPE_CHUNK_STREAM_GET_MTU: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_CHUNK_STREAM_SIGNATURE)
            unsigned int *mtu_p = va_arg(args, unsigned int *);
            unsigned int *align_p = va_arg(args, unsigned int *);
            return _upipe_chunk_stream_get_mtu(upipe, mtu_p, align_p);
        }
        case UPIPE_CHUNK_STREAM_SET_MTU: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_CHUNK_STREAM_SIGNATURE)
            unsigned int mtu = va_arg(args, unsigned int);
            unsigned int align = va_arg(args, unsigned int);
            return _upipe_chunk_stream_set_mtu(upipe, mtu, align);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a chunk_stream pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_chunk_stream_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_chunk_stream_alloc_void(mgr,
                          uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    _upipe_chunk_stream_set_mtu(upipe, DEFAULT_MTU, DEFAULT_ALIGN);
    upipe_chunk_stream_init_urefcount(upipe);
    upipe_chunk_stream_init_output(upipe);
    upipe_chunk_stream_init_uref_stream(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_chunk_stream_free(struct upipe *upipe)
{
    upipe_chunk_stream_flush(upipe);

    upipe_throw_dead(upipe);

    upipe_chunk_stream_clean_uref_stream(upipe);
    upipe_chunk_stream_clean_output(upipe);
    upipe_chunk_stream_clean_urefcount(upipe);
    upipe_chunk_stream_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_chunk_stream_mgr = {
    .refcount = NULL,
    .signature = UPIPE_CHUNK_STREAM_SIGNATURE,

    .upipe_alloc = upipe_chunk_stream_alloc,
    .upipe_input = upipe_chunk_stream_input,
    .upipe_control = upipe_chunk_stream_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for chunk_stream pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_chunk_stream_mgr_alloc(void)
{
    return &upipe_chunk_stream_mgr;
}
