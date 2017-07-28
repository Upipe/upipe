/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
 * @short Upipe module splitting packed audio to several planar outputs
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe-modules/upipe_audio_split.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of an audio_split pipe. */
struct upipe_audio_split {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** list of output subpipes */
    struct uchain outputs;
    /** flow definition packet */
    struct uref *flow_def;
    /** sample size in octets */
    uint8_t sample_size;
    /** sample size in octets for 1 channel */
    uint8_t channel_sample_size;
    /** number of channels */
    uint8_t channels;

    /** manager to create output subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_audio_split, upipe, UPIPE_AUDIO_SPLIT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_audio_split, urefcount, upipe_audio_split_no_input)
UPIPE_HELPER_VOID(upipe_audio_split)

UBASE_FROM_TO(upipe_audio_split, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_audio_split_free(struct urefcount *urefcount_real);

/** @internal @This is the private context of an output of an audio_split
 * pipe. */
struct upipe_audio_split_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** attributes / parameters from application */
    struct uref *flow_def_params;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** number of planes requested */
    uint8_t planes;
    /** number of channels requested */
    uint8_t channels;
    /** bit field of channels to copy */
    uint64_t bitfield;
    /** sample size in octets */
    uint8_t sample_size;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_audio_split_sub_check(struct upipe *upipe,
                                       struct uref *flow_format);
/** @hidden */
static void upipe_audio_split_sub_build_flow_def(struct upipe *upipe);

UPIPE_HELPER_UPIPE(upipe_audio_split_sub, upipe,
                   UPIPE_AUDIO_SPLIT_OUTPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_audio_split_sub, urefcount,
                       upipe_audio_split_sub_free)
UPIPE_HELPER_OUTPUT(upipe_audio_split_sub, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW(upipe_audio_split_sub, "sound.")
UPIPE_HELPER_UBUF_MGR(upipe_audio_split_sub, ubuf_mgr, flow_format,
                      ubuf_mgr_request,
                      upipe_audio_split_sub_check,
                      upipe_audio_split_sub_register_output_request,
                      upipe_audio_split_sub_unregister_output_request)

UPIPE_HELPER_SUBPIPE(upipe_audio_split, upipe_audio_split_sub, output,
                     sub_mgr, outputs, uchain)

/** @internal @This allocates an output subpipe of an audio_split pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_audio_split_sub_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature,
                                                 va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_audio_split_sub_alloc_flow(mgr,
                            uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL)) {
        return NULL;
    }

    struct upipe_audio_split_sub *sub = upipe_audio_split_sub_from_upipe(upipe);
    sub->flow_def_params = flow_def;
    if (unlikely(!ubase_check(uref_audio_split_get_bitfield(flow_def,
                                                            &sub->bitfield)) ||
                 !ubase_check(uref_sound_flow_get_planes(flow_def,
                                                         &sub->planes)) ||
                 !ubase_check(uref_sound_flow_get_channels(flow_def,
                                                           &sub->channels)))) {
        upipe_release(upipe);
        return NULL;
    }
    upipe_audio_split_sub_init_urefcount(upipe);
    upipe_audio_split_sub_init_output(upipe);
    upipe_audio_split_sub_init_ubuf_mgr(upipe);
    upipe_audio_split_sub_init_sub(upipe);
    upipe_throw_ready(upipe);
    upipe_audio_split_sub_build_flow_def(upipe);
    return upipe;
}

/** @internal @This processes data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_audio_split_sub_process(struct upipe *upipe,
                                          struct uref *uref,
                                          struct upump **upump_p)
{
    struct upipe_audio_split_sub *sub = upipe_audio_split_sub_from_upipe(upipe);
    struct upipe_audio_split *split = upipe_audio_split_from_sub_mgr(upipe->mgr);
    if (unlikely(sub->ubuf_mgr == NULL))
        return;

    size_t samples;
    const uint8_t *in_buf;
    if (unlikely(!ubase_check(uref_sound_size(uref, &samples, NULL)) ||
                 !ubase_check(uref_sound_read_uint8_t(uref, 0, -1,
                                                      &in_buf, 1)))) {
        upipe_warn(upipe, "invalid sound uref");
        uref_free(uref);
        return;
    }

    struct ubuf *ubuf = ubuf_sound_alloc(sub->ubuf_mgr, samples);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        goto upipe_audio_split_sub_process_err;
    }

    /* interate through output channels */
    uint8_t in_idx = 0;
    const char *channel = NULL;
    while (ubase_check(ubuf_sound_plane_iterate(ubuf, &channel)) && channel) {
        uint8_t out_idx = 0;

        do {
            while (in_idx < split->channels && !(sub->bitfield & (1 << in_idx)))
                in_idx++;

            if (unlikely(in_idx == split->channels)) {
                upipe_warn(upipe, "couldn't find channels");
                break;
            }

            uint8_t *out_buf;
            if (unlikely(!ubase_check(ubuf_sound_plane_write_uint8_t(ubuf,
                                channel, 0, -1, &out_buf)))) {
                upipe_throw_error(upipe, UBASE_ERR_ALLOC);
                break;
            }

            const uint8_t *in = in_buf + in_idx * split->channel_sample_size;
            uint8_t *out = out_buf + out_idx * split->channel_sample_size;
            int i, j;
            for (i = 0; i < samples; i++) {
                for (j = 0; j < split->channel_sample_size; j++) {
                    out[j] = in[j];
                }
                in += split->sample_size;
                out += sub->sample_size;
            }
            ubuf_sound_plane_unmap(ubuf, channel, 0, -1);

            in_idx++;
            out_idx++;
        } while (sub->planes == 1 && out_idx < sub->channels);
    }

    /* dup uref, allocate new ubuf */
    struct uref *output = uref_dup(uref);
    if (unlikely(output == NULL)) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        ubuf_free(ubuf);
        goto upipe_audio_split_sub_process_err;
    }
    uref_attach_ubuf(output, ubuf);

    upipe_audio_split_sub_output(upipe, output, upump_p);

upipe_audio_split_sub_process_err:
    uref_sound_unmap(uref, 0, -1, 1);
}

/** @internal @This receives the result of ubuf manager requests.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_audio_split_sub_check(struct upipe *upipe,
                                       struct uref *flow_format)
{
    if (flow_format != NULL)
        upipe_audio_split_sub_store_flow_def(upipe, flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This builds the subpipe flow definition.
 *
 * @param upipe description structure of the subpipe
 */
static void upipe_audio_split_sub_build_flow_def(struct upipe *upipe)
{
    struct upipe_audio_split_sub *sub = upipe_audio_split_sub_from_upipe(upipe);
    struct upipe_audio_split *split = upipe_audio_split_from_sub_mgr(upipe->mgr);
    if (split->flow_def == NULL)
        return;

    if (sub->ubuf_mgr) {
        ubuf_mgr_release(sub->ubuf_mgr);
        sub->ubuf_mgr = NULL;
    }

    struct uref *flow_def = uref_dup(sub->flow_def_params);
    if (unlikely(!flow_def)) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* We need to keep input flow definition, latency and rate, and compute
     * new sample size. */
    const char *def;
    if (likely(ubase_check(uref_flow_get_def(split->flow_def, &def)))) {
        UBASE_ERROR(upipe, uref_flow_set_def(flow_def, def))
    }
    uint64_t latency;
    if (likely(ubase_check(uref_clock_get_latency(split->flow_def, &latency)))) {
        UBASE_ERROR(upipe, uref_clock_set_latency(flow_def, latency))
    }
    uint64_t rate;
    if (likely(ubase_check(uref_sound_flow_get_rate(split->flow_def, &rate)))) {
        UBASE_ERROR(upipe, uref_sound_flow_set_rate(flow_def, rate))
    }

    sub->sample_size = split->sample_size / split->channels;
    if (sub->planes == 1)
        sub->sample_size *= sub->channels;
    UBASE_ERROR(upipe, uref_sound_flow_set_sample_size(flow_def,
                                                       sub->sample_size))

    upipe_audio_split_sub_demand_ubuf_mgr(upipe, flow_def);
}

/** @internal @This processes control commands on an output subpipe of an
 * audio_split pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_audio_split_sub_control(struct upipe *upipe,
                                         int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_audio_split_sub_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_audio_split_sub_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_audio_split_sub_set_output(upipe, output);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_audio_split_sub_get_super(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audio_split_sub_free(struct upipe *upipe)
{
    struct upipe_audio_split_sub *sub = upipe_audio_split_sub_from_upipe(upipe);
    upipe_throw_dead(upipe);

    uref_free(sub->flow_def_params);
    upipe_audio_split_sub_clean_output(upipe);
    upipe_audio_split_sub_clean_sub(upipe);
    upipe_audio_split_sub_clean_ubuf_mgr(upipe);
    upipe_audio_split_sub_clean_urefcount(upipe);
    upipe_audio_split_sub_free_flow(upipe);
}

/** @internal @This initializes the output manager for an audio_split sub pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audio_split_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_audio_split *upipe_audio_split =
                              upipe_audio_split_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_audio_split->sub_mgr;
    sub_mgr->refcount = upipe_audio_split_to_urefcount_real(upipe_audio_split);
    sub_mgr->signature = UPIPE_AUDIO_SPLIT_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_audio_split_sub_alloc;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_audio_split_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates an audio_split pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_audio_split_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_audio_split_alloc_void(mgr,
                                    uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_audio_split *upipe_audio_split =
                              upipe_audio_split_from_upipe(upipe);
    upipe_audio_split_init_urefcount(upipe);
    urefcount_init(upipe_audio_split_to_urefcount_real(upipe_audio_split),
                   upipe_audio_split_free);
    upipe_audio_split_init_sub_mgr(upipe);
    upipe_audio_split_init_sub_outputs(upipe);
    upipe_audio_split->flow_def = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_audio_split_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_audio_split *split = upipe_audio_split_from_upipe(upipe);

    /* interate through output subpipes */
    struct uchain *uchain;
    ulist_foreach (&split->outputs, uchain) {
        struct upipe_audio_split_sub *sub =
            upipe_audio_split_sub_from_uchain(uchain);
        upipe_audio_split_sub_process(upipe_audio_split_sub_to_upipe(sub),
                                      uref, upump_p);
    }

    uref_free(uref);
}

/** @internal @This changes the flow definition on all outputs.
 *
 * @param upipe description structure of the pipe
 * @param flow_def new flow definition
 * @return an error code
 */
static int upipe_audio_split_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_audio_split *split = upipe_audio_split_from_upipe(upipe);
    UBASE_RETURN(uref_flow_match_def(flow_def, "sound."))
    UBASE_RETURN(uref_sound_flow_match_planes(flow_def, 1, 1))
    UBASE_RETURN(uref_sound_flow_get_sample_size(flow_def,
                                                 &split->sample_size));
    UBASE_RETURN(uref_sound_flow_get_channels(flow_def, &split->channels));
    if (unlikely(!split->channels || split->channels > 64))
        return UBASE_ERR_INVALID;

    split->channel_sample_size = split->sample_size / split->channels;
    if (unlikely(!split->channel_sample_size))
        return UBASE_ERR_INVALID;

    uref_free(split->flow_def);
    if ((split->flow_def = uref_dup(flow_def)) == NULL) {
        return UBASE_ERR_ALLOC;
    }

    /* rebuild output flow definitions */
    struct uchain *uchain;
    ulist_foreach (&split->outputs, uchain) {
        struct upipe_audio_split_sub *sub =
            upipe_audio_split_sub_from_uchain(uchain);
        upipe_audio_split_sub_build_flow_def(upipe_audio_split_sub_to_upipe(sub));
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an audio_split pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_audio_split_control(struct upipe *upipe,
                                     int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_audio_split_control_outputs(upipe, command, args));

    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            return upipe_audio_split_set_flow_def(upipe, uref);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_audio_split_free(struct urefcount *urefcount_real)
{
    struct upipe_audio_split *upipe_audio_split =
           upipe_audio_split_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_audio_split_to_upipe(upipe_audio_split);
    upipe_throw_dead(upipe);
    upipe_audio_split_clean_sub_outputs(upipe);
    uref_free(upipe_audio_split->flow_def);
    urefcount_clean(urefcount_real);
    upipe_audio_split_clean_urefcount(upipe);
    upipe_audio_split_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audio_split_no_input(struct upipe *upipe)
{
    struct upipe_audio_split *upipe_audio_split =
                              upipe_audio_split_from_upipe(upipe);
    upipe_audio_split_throw_sub_outputs(upipe, UPROBE_SOURCE_END);
    urefcount_release(upipe_audio_split_to_urefcount_real(upipe_audio_split));
}

/** audio_split module manager static descriptor */
static struct upipe_mgr upipe_audio_split_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AUDIO_SPLIT_SIGNATURE,

    .upipe_alloc = upipe_audio_split_alloc,
    .upipe_input = upipe_audio_split_input,
    .upipe_control = upipe_audio_split_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all audio_split pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_audio_split_mgr_alloc(void)
{
    return &upipe_audio_split_mgr;
}
