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
 * @short Upipe module to decapsulate RTP raw audio
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
#include <upipe/upipe_helper_input.h>
#include <upipe-modules/upipe_rtp_pcm_unpack.h>
#include <upipe-modules/upipe_rtp_decaps.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_block.h>

struct upipe_rtp_pcm_unpack {
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

    /** sample rate */
    uint64_t rate;
    /** channels */
    uint8_t channels;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
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
static int upipe_rtp_pcm_unpack_check(struct upipe *upipe, struct uref *flow_format);

/** @hidden */
static bool upipe_rtp_pcm_unpack_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_rtp_pcm_unpack, upipe, UPIPE_RTP_PCM_UNPACK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_rtp_pcm_unpack, urefcount, upipe_rtp_pcm_unpack_free)
UPIPE_HELPER_VOID(upipe_rtp_pcm_unpack)
UPIPE_HELPER_OUTPUT(upipe_rtp_pcm_unpack, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_UBUF_MGR(upipe_rtp_pcm_unpack, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_rtp_pcm_unpack_check,
                      upipe_rtp_pcm_unpack_register_output_request,
                      upipe_rtp_pcm_unpack_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_rtp_pcm_unpack, urefs, nb_urefs, max_urefs, blockers,
        upipe_rtp_pcm_unpack_handle)

static int upipe_rtp_pcm_unpack_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format)
        upipe_rtp_pcm_unpack_store_flow_def(upipe, flow_format);

    bool was_buffered = !upipe_rtp_pcm_unpack_check_input(upipe);
    upipe_rtp_pcm_unpack_output_input(upipe);
    upipe_rtp_pcm_unpack_unblock_input(upipe);
    if (was_buffered && upipe_rtp_pcm_unpack_check_input(upipe)) {
        /* All unpackets have been output, release again the pipe that has been
         * used in @ref upipe_rtp_pcm_unpack_input. */
        upipe_release(upipe);
    }

    return UBASE_ERR_NONE;
}

static int upipe_rtp_pcm_unpack_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    struct upipe_rtp_pcm_unpack *upipe_rtp_pcm_unpack = upipe_rtp_pcm_unpack_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "block.s24be.sound."))
    UBASE_RETURN(uref_sound_flow_get_rate(flow_def,
                &upipe_rtp_pcm_unpack->rate))
    UBASE_RETURN(uref_sound_flow_get_channels(flow_def,
                &upipe_rtp_pcm_unpack->channels))

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uref_sound_flow_clear_format(flow_def_dup);
    uref_flow_set_def(flow_def_dup, "sound.s32.");
    uref_sound_flow_add_plane(flow_def_dup, "all");
    uref_sound_flow_set_sample_size(flow_def_dup, 4 * upipe_rtp_pcm_unpack->channels);

    upipe_rtp_pcm_unpack_require_ubuf_mgr(upipe, flow_def_dup);

    return UBASE_ERR_NONE;
}

static int upipe_rtp_pcm_unpack_control(struct upipe *upipe, int command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT ||
                request->type == UREQUEST_UBUF_MGR)
                return upipe_throw_provide_request(upipe, request);
            return upipe_rtp_pcm_unpack_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT ||
                request->type == UREQUEST_UBUF_MGR)
                return UBASE_ERR_NONE;
            return upipe_rtp_pcm_unpack_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtp_pcm_unpack_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_GET_FLOW_DEF:
            return upipe_rtp_pcm_unpack_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_rtp_pcm_unpack_free(struct upipe *upipe)
{
    upipe_rtp_pcm_unpack_clean_output(upipe);
    upipe_rtp_pcm_unpack_clean_urefcount(upipe);
    upipe_rtp_pcm_unpack_clean_ubuf_mgr(upipe);
    upipe_rtp_pcm_unpack_clean_input(upipe);
    upipe_rtp_pcm_unpack_free_void(upipe);
}

static struct upipe *upipe_rtp_pcm_unpack_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature,
                                           va_list args)
{
    struct upipe *upipe =
        upipe_rtp_pcm_unpack_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_rtp_pcm_unpack *upipe_rtp_pcm_unpack = upipe_rtp_pcm_unpack_from_upipe(upipe);

    upipe_rtp_pcm_unpack_init_urefcount(upipe);
    upipe_rtp_pcm_unpack_init_ubuf_mgr(upipe);
    upipe_rtp_pcm_unpack_init_input(upipe);
    upipe_rtp_pcm_unpack_init_output(upipe);

    return upipe;
}

static void upipe_rtp_pcm_unpack_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    if (!upipe_rtp_pcm_unpack_check_input(upipe)) {
        upipe_rtp_pcm_unpack_hold_input(upipe, uref);
        upipe_rtp_pcm_unpack_block_input(upipe, upump_p);
    } else if (!upipe_rtp_pcm_unpack_handle(upipe, uref, upump_p)) {
        upipe_rtp_pcm_unpack_hold_input(upipe, uref);
        upipe_rtp_pcm_unpack_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all unpackets
         * have been sent. */
        upipe_use(upipe);
    }
}

static bool upipe_rtp_pcm_unpack_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_rtp_pcm_unpack *upipe_rtp_pcm_unpack = upipe_rtp_pcm_unpack_from_upipe(upipe);
    if (!upipe_rtp_pcm_unpack->ubuf_mgr)
        return false;

    size_t s = 0;
    uref_block_size(uref, &s);

    s /= 3 /* 24 bits */;

    struct ubuf *ubuf = ubuf_sound_alloc(upipe_rtp_pcm_unpack->ubuf_mgr, s / upipe_rtp_pcm_unpack->channels);
    if (!ubuf) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    const uint8_t *src = NULL;
    int32_t *dst = NULL;
    int size = -1;
    uref_block_read(uref, 0, &size, &src);
    ubuf_sound_write_int32_t(ubuf, 0, -1, &dst, 1);

    for (int i = 0; i < s; i++)
        dst[i] = (src[3*i] << 24) | (src[3*i+1] << 16) | (src[3*i+2] << 8);

    ubuf_sound_unmap(ubuf, 0, -1, 1);
    uref_block_unmap(uref, 0);

    uref_attach_ubuf(uref, ubuf);

    upipe_rtp_pcm_unpack_output(upipe, uref, upump_p);

    return true;
}

static struct upipe_mgr upipe_rtp_pcm_unpack_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTP_PCM_UNPACK_SIGNATURE,

    .upipe_alloc = upipe_rtp_pcm_unpack_alloc,
    .upipe_input = upipe_rtp_pcm_unpack_input,
    .upipe_control = upipe_rtp_pcm_unpack_control,

    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_rtp_pcm_unpack_mgr_alloc(void)
{
    return &upipe_rtp_pcm_unpack_mgr;
}
