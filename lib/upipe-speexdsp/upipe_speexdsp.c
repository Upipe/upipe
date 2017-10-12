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
 * @short Upipe speexdsp resampler module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>

#include <upipe-speexdsp/upipe_speexdsp.h>

#include <speex/speex_resampler.h>

/** @hidden */
static bool upipe_speexdsp_handle(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p);
/** @hidden */
static int upipe_speexdsp_check(struct upipe *upipe, struct uref *flow_format);

/** upipe_speexdsp structure with speexdsp parameters */
struct upipe_speexdsp {
    /** refcount management structure */
    struct urefcount urefcount;

    /** input flow */
    struct uref *flow_def_input;
    /** attributes added by the pipe */
    struct uref *flow_def_attr;
    /** output flow */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;
    /** output pipe */
    struct upipe *output;

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
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** speexdsp context */
    SpeexResamplerState *ctx;

    /** float or int16_t */
    bool f32;

    /** sampling rate */
    uint64_t rate;

    /** current drift rate */
    struct urational drift_rate;

    /** resampling quality */
    int quality;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_speexdsp, upipe, UPIPE_SPEEXDSP_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_speexdsp, urefcount, upipe_speexdsp_free);
UPIPE_HELPER_VOID(upipe_speexdsp)
UPIPE_HELPER_OUTPUT(upipe_speexdsp, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_speexdsp, flow_def_input, flow_def_attr)
UPIPE_HELPER_UBUF_MGR(upipe_speexdsp, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_speexdsp_check,
                      upipe_speexdsp_register_output_request,
                      upipe_speexdsp_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_speexdsp, urefs, nb_urefs, max_urefs, blockers, upipe_speexdsp_handle)

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_speexdsp_handle(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_speexdsp *upipe_speexdsp = upipe_speexdsp_from_upipe(upipe);

    struct urational drift_rate;
    if (!ubase_check(uref_clock_get_rate(uref, &drift_rate)))
        drift_rate = (struct urational){ 1, 1 };

    /* reinitialize resampler when drift rate changes */
    if (urational_cmp(&drift_rate, &upipe_speexdsp->drift_rate)) {
        upipe_speexdsp->drift_rate = drift_rate;
        spx_uint32_t ratio_num = drift_rate.den;
        spx_uint32_t ratio_den = drift_rate.num;
        spx_uint32_t in_rate = upipe_speexdsp->rate * ratio_num / ratio_den;
        spx_uint32_t out_rate = upipe_speexdsp->rate;
        int err = speex_resampler_set_rate_frac(upipe_speexdsp->ctx,
                ratio_num, ratio_den, in_rate, out_rate);
        if (err) {
            upipe_err_va(upipe, "Couldn't resample from %u to %u: %s",
                in_rate, out_rate, speex_resampler_strerror(err));
        } else {
            upipe_dbg_va(upipe, "Resampling from %u to %u",
                in_rate, out_rate);
        }
    }

    size_t size;
    if (!ubase_check(uref_sound_size(uref, &size, NULL /* sample_size */))) {
        uref_free(uref);
        return true;
    }

    struct ubuf *ubuf = ubuf_sound_alloc(upipe_speexdsp->ubuf_mgr, size + 10);
    if (!ubuf)
        return false;

    const void *in;
    uref_sound_read_void(uref, 0, -1, &in, 1);

    void *out;
    ubuf_sound_write_void(ubuf, 0, -1, &out, 1);

    spx_uint32_t in_len = size;         /* input size */
    spx_uint32_t out_len = size + 10;   /* available output size */

    int err;

    if (upipe_speexdsp->f32)
        err = speex_resampler_process_interleaved_float(upipe_speexdsp->ctx,
                in, &in_len, out, &out_len);
    else
        err = speex_resampler_process_interleaved_int(upipe_speexdsp->ctx,
                in, &in_len, out, &out_len);

    if (err) {
        upipe_err_va(upipe, "Could not resample: %s",
                speex_resampler_strerror(err));
    }

    uref_sound_unmap(uref, 0, -1, 1);
    ubuf_sound_unmap(ubuf, 0, -1, 1);

    if (err) {
        ubuf_free(ubuf);
    } else {
        ubuf_sound_resize(ubuf, 0, out_len);
        uref_attach_ubuf(uref, ubuf);
    }

    upipe_speexdsp_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_speexdsp_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    if (!upipe_speexdsp_check_input(upipe)) {
        upipe_speexdsp_hold_input(upipe, uref);
        upipe_speexdsp_block_input(upipe, upump_p);
    } else if (!upipe_speexdsp_handle(upipe, uref, upump_p)) {
        upipe_speexdsp_hold_input(upipe, uref);
        upipe_speexdsp_block_input(upipe, upump_p);
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
static int upipe_speexdsp_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_speexdsp *upipe_speexdsp = upipe_speexdsp_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_speexdsp_store_flow_def(upipe, flow_format);

    if (upipe_speexdsp->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_speexdsp_check_input(upipe);
    upipe_speexdsp_output_input(upipe);
    upipe_speexdsp_unblock_input(upipe);
    if (was_buffered && upipe_speexdsp_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_speexdsp_input. */
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
static int upipe_speexdsp_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_speexdsp *upipe_speexdsp = upipe_speexdsp_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))

    if (unlikely(ubase_ncmp(def, "sound.f32.") &&
                ubase_ncmp(def, "sound.s16.")))
        return UBASE_ERR_INVALID;

    uint8_t in_planes;
    if (unlikely(!ubase_check(uref_sound_flow_get_planes(flow_def,
                                                         &in_planes))))
        return UBASE_ERR_INVALID;

    if (in_planes != 1) {
        upipe_err(upipe, "only interleaved audio is supported");
        return UBASE_ERR_INVALID;
    }

    if (!ubase_check(uref_sound_flow_get_rate(flow_def,
                    &upipe_speexdsp->rate))) {
        upipe_err(upipe, "no sound rate defined");
        uref_dump(flow_def, upipe->uprobe);
        return UBASE_ERR_INVALID;
    }

    uint8_t channels;
    if (unlikely(!ubase_check(uref_sound_flow_get_channels(flow_def,
                        &channels))))
        return UBASE_ERR_INVALID;

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_speexdsp_require_ubuf_mgr(upipe, flow_def);

    if (upipe_speexdsp->ctx)
        speex_resampler_destroy(upipe_speexdsp->ctx);

    upipe_speexdsp->f32 = !ubase_ncmp(def, "sound.f32.");

    int err;
    upipe_speexdsp->ctx = speex_resampler_init(channels,
                upipe_speexdsp->rate, upipe_speexdsp->rate,
                upipe_speexdsp->quality, &err);
    if (!upipe_speexdsp->ctx) {
        upipe_err_va(upipe, "Could not create resampler: %s",
                speex_resampler_strerror(err));
        return UBASE_ERR_INVALID;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_speexdsp_provide_flow_format(struct upipe *upipe,
                                          struct urequest *request)
{
    const char *def;
    UBASE_RETURN(uref_flow_get_def(request->uref, &def))
    uint8_t channels;
    UBASE_RETURN(uref_sound_flow_get_channels(request->uref, &channels))
    uint8_t planes;
    UBASE_RETURN(uref_sound_flow_get_planes(request->uref, &planes))
    uint8_t sample_size;
    UBASE_RETURN(uref_sound_flow_get_sample_size(request->uref, &sample_size))

    struct uref *flow = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow);

    uref_sound_flow_clear_format(flow);
    uref_sound_flow_set_planes(flow, 0);
    uref_sound_flow_set_channels(flow, channels);
    uref_sound_flow_add_plane(flow, "all");
    if (ubase_ncmp(def, "sound.s16.")) {
        uref_flow_set_def(flow, "sound.f32."); /* prefer f32 over s16 */
        uref_sound_flow_set_sample_size(flow, 4 * channels);
    } else {
        uref_flow_set_def(flow, def);
        uref_sound_flow_set_sample_size(flow, (planes > 1) ? sample_size :
                sample_size / channels);
    }

    return urequest_provide_flow_format(request, flow);
}

/** @internal @This processes control commands on a speexdsp pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_speexdsp_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_speexdsp *upipe_speexdsp = upipe_speexdsp_from_upipe(upipe);

    switch (command) {
        /* generic commands */
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_speexdsp_provide_flow_format(upipe, request);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_throw_provide_request(upipe, request);
            return upipe_speexdsp_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT ||
                request->type == UREQUEST_UBUF_MGR)
                return UBASE_ERR_NONE;
            return upipe_speexdsp_free_output_proxy(upipe, request);
        }

        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_GET_FLOW_DEF:
            return upipe_speexdsp_control_output(upipe, command, args);
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_speexdsp_set_flow_def(upipe, flow);
        }
        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *value  = va_arg(args, const char *);
            if (strcmp(option, "quality"))
                return UBASE_ERR_INVALID;
            if (upipe_speexdsp->ctx)
                return UBASE_ERR_BUSY;
            int quality = atoi(value);
            if (quality > SPEEX_RESAMPLER_QUALITY_MAX) {
                quality = SPEEX_RESAMPLER_QUALITY_MAX;
                upipe_err_va(upipe, "Clamping quality to %d",
                        SPEEX_RESAMPLER_QUALITY_MAX);
            } else if (quality < SPEEX_RESAMPLER_QUALITY_MIN) {
                quality = SPEEX_RESAMPLER_QUALITY_MIN;
                upipe_err_va(upipe, "Clamping quality to %d",
                        SPEEX_RESAMPLER_QUALITY_MIN);
            }
            upipe_speexdsp->quality = quality;
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a speexdsp pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_speexdsp_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_speexdsp_alloc_void(mgr, uprobe, signature,
                                               args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_speexdsp *upipe_speexdsp = upipe_speexdsp_from_upipe(upipe);

    upipe_speexdsp->ctx = NULL;
    upipe_speexdsp->drift_rate = (struct urational){ 0, 0 };
    upipe_speexdsp->quality = SPEEX_RESAMPLER_QUALITY_MAX;

    upipe_speexdsp_init_urefcount(upipe);
    upipe_speexdsp_init_ubuf_mgr(upipe);
    upipe_speexdsp_init_output(upipe);
    upipe_speexdsp_init_flow_def(upipe);
    upipe_speexdsp_init_input(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_speexdsp_free(struct upipe *upipe)
{
    struct upipe_speexdsp *upipe_speexdsp = upipe_speexdsp_from_upipe(upipe);
    if (likely(upipe_speexdsp->ctx))
        speex_resampler_destroy(upipe_speexdsp->ctx);

    upipe_throw_dead(upipe);
    upipe_speexdsp_clean_input(upipe);
    upipe_speexdsp_clean_output(upipe);
    upipe_speexdsp_clean_flow_def(upipe);
    upipe_speexdsp_clean_ubuf_mgr(upipe);
    upipe_speexdsp_clean_urefcount(upipe);
    upipe_speexdsp_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_speexdsp_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SPEEXDSP_SIGNATURE,

    .upipe_alloc = upipe_speexdsp_alloc,
    .upipe_input = upipe_speexdsp_input,
    .upipe_control = upipe_speexdsp_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for speexdsp pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_speexdsp_mgr_alloc(void)
{
    return &upipe_speexdsp_mgr;
}
