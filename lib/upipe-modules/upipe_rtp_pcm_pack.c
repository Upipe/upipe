/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
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
 * @short Upipe module to split raw audio into RTP packets
 */

#include <stdlib.h>
#include <limits.h>

#include <upipe/upipe.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_input.h>

#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>

#include <upipe-modules/upipe_rtp_pcm_pack.h>

struct upipe_rtp_pcm_pack {
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

    /** number of channels */
    uint8_t channels;

    /** samplerate */
    uint64_t rate;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** next uref to be processed */
    struct uref *next_uref;
    /** original size of the next uref */
    size_t next_uref_size;
    /** urefs received after next uref */
    struct uchain urefs;

    /** temporary uref storage (used during urequest) */
    struct uchain input_urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_rtp_pcm_pack_check(struct upipe *upipe, struct uref *flow_format);

/** @hidden */
static bool upipe_rtp_pcm_pack_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_rtp_pcm_pack, upipe, UPIPE_RTP_PCM_PACK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_rtp_pcm_pack, urefcount, upipe_rtp_pcm_pack_free)
UPIPE_HELPER_VOID(upipe_rtp_pcm_pack)
UPIPE_HELPER_OUTPUT(upipe_rtp_pcm_pack, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_UBUF_MGR(upipe_rtp_pcm_pack, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_rtp_pcm_pack_check,
                      upipe_rtp_pcm_pack_register_output_request,
                      upipe_rtp_pcm_pack_unregister_output_request)
UPIPE_HELPER_UREF_STREAM(upipe_rtp_pcm_pack, next_uref, next_uref_size, urefs, NULL)
UPIPE_HELPER_INPUT(upipe_rtp_pcm_pack, input_urefs, nb_urefs, max_urefs, blockers,
        upipe_rtp_pcm_pack_handle)

static int upipe_rtp_pcm_pack_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format)
        upipe_rtp_pcm_pack_store_flow_def(upipe, flow_format);

    bool was_buffered = !upipe_rtp_pcm_pack_check_input(upipe);
    upipe_rtp_pcm_pack_output_input(upipe);
    upipe_rtp_pcm_pack_unblock_input(upipe);
    if (was_buffered && upipe_rtp_pcm_pack_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_rtp_pcm_pack_input. */
        upipe_release(upipe);
    }

    return UBASE_ERR_NONE;
}

static int upipe_rtp_pcm_pack_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    struct upipe_rtp_pcm_pack *upipe_rtp_pcm_pack = upipe_rtp_pcm_pack_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "sound.s32."))

    uint8_t planes;
    UBASE_RETURN(uref_sound_flow_get_planes(flow_def, &planes));
    if (planes != 1)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_sound_flow_get_rate(flow_def, &upipe_rtp_pcm_pack->rate));
    UBASE_RETURN(uref_sound_flow_get_channels(flow_def, &upipe_rtp_pcm_pack->channels));

    struct uref *flow_def_dup = uref_sibling_alloc(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uref_flow_set_def(flow_def_dup, "block.s24be.sound.");
    /* RTP clock rate */
    uref_sound_flow_set_rate(flow_def_dup, upipe_rtp_pcm_pack->rate);

    upipe_rtp_pcm_pack_require_ubuf_mgr(upipe, flow_def_dup);

    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_rtp_pcm_pack_provide_flow_format(struct upipe *upipe,
                                          struct urequest *request)
{
    uint8_t channels;
    UBASE_RETURN(uref_sound_flow_get_channels(request->uref, &channels))
    uint8_t planes;
    UBASE_RETURN(uref_sound_flow_get_planes(request->uref, &planes))

    struct uref *flow = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow);

    uref_flow_set_def(flow, "sound.s32.");
    if (planes > 1) {
        uref_sound_flow_clear_format(flow);
        uref_sound_flow_add_plane(flow, "all");
        uref_sound_flow_set_sample_size(flow, 4 * channels);
    }

    return urequest_provide_flow_format(request, flow);
}

static int upipe_rtp_pcm_pack_control(struct upipe *upipe, int command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_rtp_pcm_pack_provide_flow_format(upipe, request);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_throw_provide_request(upipe, request);
            return upipe_rtp_pcm_pack_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT ||
                request->type == UREQUEST_UBUF_MGR)
                return UBASE_ERR_NONE;
            return upipe_rtp_pcm_pack_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtp_pcm_pack_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_rtp_pcm_pack_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_rtp_pcm_pack_free(struct upipe *upipe)
{
    upipe_rtp_pcm_pack_clean_ubuf_mgr(upipe);
    upipe_rtp_pcm_pack_clean_urefcount(upipe);
    upipe_rtp_pcm_pack_clean_uref_stream(upipe);
    upipe_rtp_pcm_pack_clean_output(upipe);
    upipe_rtp_pcm_pack_clean_input(upipe);
    upipe_rtp_pcm_pack_free_void(upipe);
}

static struct upipe *upipe_rtp_pcm_pack_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature,
                                           va_list args)
{
    struct upipe *upipe =
        upipe_rtp_pcm_pack_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_rtp_pcm_pack *upipe_rtp_pcm_pack = upipe_rtp_pcm_pack_from_upipe(upipe);

    upipe_rtp_pcm_pack_init_urefcount(upipe);
    upipe_rtp_pcm_pack_init_input(upipe);
    upipe_rtp_pcm_pack_init_ubuf_mgr(upipe);
    upipe_rtp_pcm_pack_init_uref_stream(upipe);
    upipe_rtp_pcm_pack_init_output(upipe);

    return upipe;
}

static void upipe_rtp_pcm_pack_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    if (!upipe_rtp_pcm_pack_check_input(upipe)) {
        upipe_rtp_pcm_pack_hold_input(upipe, uref);
        upipe_rtp_pcm_pack_block_input(upipe, upump_p);
    } else if (!upipe_rtp_pcm_pack_handle(upipe, uref, upump_p)) {
        upipe_rtp_pcm_pack_hold_input(upipe, uref);
        upipe_rtp_pcm_pack_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

static bool upipe_rtp_pcm_pack_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_rtp_pcm_pack *upipe_rtp_pcm_pack = upipe_rtp_pcm_pack_from_upipe(upipe);
    if (!upipe_rtp_pcm_pack->ubuf_mgr)
        return false;

    size_t s;
    uref_sound_size(uref, &s, NULL);
    s *= upipe_rtp_pcm_pack->channels;

    struct ubuf *ubuf = ubuf_block_alloc(upipe_rtp_pcm_pack->ubuf_mgr,
            s * 3 /* 24 bits */);
    if (!ubuf) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    uint8_t *dst = NULL;
    const int32_t *src = NULL;
    int size = -1;
    ubuf_block_write(ubuf, 0, &size, &dst);

    uref_sound_read_int32_t(uref, 0, -1, &src, 1);

    for (int i = 0; i < s; i++)
        for (int j = 0; j < 3; j++)
            dst[3*i+j] = (src[i] >> (8 * (3-j))) & 0xff;

    ubuf_block_unmap(ubuf, 0);
    uref_sound_unmap(uref, 0, -1, 1);

    uref_attach_ubuf(uref, ubuf);

#define MTU 1440
    const size_t chunk_size = (MTU / 3 / upipe_rtp_pcm_pack->channels)
        * 3 * upipe_rtp_pcm_pack->channels;

    upipe_rtp_pcm_pack_append_uref_stream(upipe, uref);

    if (upipe_rtp_pcm_pack->next_uref_size + s < chunk_size)
        return true;

    uint64_t pts_prog = 0;
    uref_clock_get_pts_prog(upipe_rtp_pcm_pack->next_uref, &pts_prog);

    do {
        uref = upipe_rtp_pcm_pack_extract_uref_stream(upipe, chunk_size);
        uref_clock_set_pts_prog(uref, pts_prog);
        pts_prog += (chunk_size / 3 / upipe_rtp_pcm_pack->channels)
            * UCLOCK_FREQ / upipe_rtp_pcm_pack->rate;
        upipe_rtp_pcm_pack_output(upipe, uref, upump_p);
    } while (uref && upipe_rtp_pcm_pack->next_uref_size > chunk_size);

    return true;
}

static struct upipe_mgr upipe_rtp_pcm_pack_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTP_PCM_PACK_SIGNATURE,

    .upipe_alloc = upipe_rtp_pcm_pack_alloc,
    .upipe_input = upipe_rtp_pcm_pack_input,
    .upipe_control = upipe_rtp_pcm_pack_control,

    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_rtp_pcm_pack_mgr_alloc(void)
{
    return &upipe_rtp_pcm_pack_mgr;
}
