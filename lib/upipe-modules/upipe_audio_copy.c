/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe module to output fixed size sound buffers.
 */

#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_input.h"
#include "upipe/upipe_helper_flow.h"
#include "upipe/upipe_helper_flow_def.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_ubuf_mgr.h"

#include "upipe/upipe.h"
#include "upipe/uref_sound.h"
#include "upipe/uref_sound_flow.h"
#include "upipe/uref_pic_flow.h"

#include "upipe-modules/upipe_audio_copy.h"

/** @internal @This is the expected input flow definition. */
#define EXPECTED_FLOW_DEF   UREF_SOUND_FLOW_DEF

/** @internal @This is the private structure for audio frame pipe. */
struct upipe_audio_copy {
    /** generic pipe structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** input flow format */
    struct uref *input_flow_def;
    /** flow def attr */
    struct uref *flow_def_attr;
    /** output pipe */
    struct upipe *output;
    /** output flow format */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** requests list */
    struct uchain requests;
    /** list of retained urefs */
    struct uchain urefs;
    /** number of retained urefs */
    unsigned int nb_urefs;
    /** maximum of retained urefs */
    unsigned int max_urefs;
    /** list of blockers (used during request) */
    struct uchain blockers;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;
    /** list of buffered urefs */
    struct uchain buffers;
    /** output size */
    uint64_t samples;
    /** output frame rate */
    struct urational fps;
    /** current retained size */
    uint64_t size;
    /** input sample rate */
    uint64_t samplerate;
    /** sample size */
    uint8_t sample_size;
    /** input planes */
    uint8_t planes;
    /** remain from previous output */
    int64_t remain;
};

/** @hidden */
static int upipe_audio_copy_check(struct upipe *upipe,
                                  struct uref *flow_format);

/** @hidden */
static bool upipe_audio_copy_handle(struct upipe *upipe,
                                    struct uref *uref,
                                    struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_audio_copy, upipe, UPIPE_AUDIO_COPY_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_audio_copy, urefcount, upipe_audio_copy_free);
UPIPE_HELPER_INPUT(upipe_audio_copy, urefs, nb_urefs, max_urefs, blockers,
                   upipe_audio_copy_handle);
UPIPE_HELPER_FLOW(upipe_audio_copy, EXPECTED_FLOW_DEF);
UPIPE_HELPER_FLOW_DEF(upipe_audio_copy, input_flow_def, flow_def_attr);
UPIPE_HELPER_OUTPUT(upipe_audio_copy, output, flow_def, output_state, requests);
UPIPE_HELPER_UBUF_MGR(upipe_audio_copy, ubuf_mgr, flow_format,
                      ubuf_mgr_request, upipe_audio_copy_check,
                      upipe_audio_copy_register_output_request,
                      upipe_audio_copy_unregister_output_request);

/** @internal @This allocates an audio frame pipe.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments (flow definition)
 * @return pointer to allocated upipe, or NULL
 */
static struct upipe *upipe_audio_copy_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature,
                                             va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe =
        upipe_audio_copy_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(!upipe))
        return NULL;

    upipe_audio_copy_init_urefcount(upipe);
    upipe_audio_copy_init_input(upipe);
    upipe_audio_copy_init_output(upipe);
    upipe_audio_copy_init_flow_def(upipe);
    upipe_audio_copy_init_ubuf_mgr(upipe);

    struct upipe_audio_copy *upipe_audio_copy =
        upipe_audio_copy_from_upipe(upipe);
    ulist_init(&upipe_audio_copy->buffers);
    upipe_audio_copy->planes = 0;
    upipe_audio_copy->samplerate = UINT64_MAX;
    upipe_audio_copy->remain = 0;

    upipe_throw_ready(upipe);

    upipe_audio_copy->samples = 0;
    uref_sound_flow_get_samples(flow_def, &upipe_audio_copy->samples);
    upipe_audio_copy->fps.num = 0;
    upipe_audio_copy->fps.den = 1;
    uref_pic_flow_get_fps(flow_def, &upipe_audio_copy->fps);

    if (unlikely(!upipe_audio_copy->samples && !upipe_audio_copy->fps.num)) {
        upipe_err(upipe, "invalid flow def parameters");
        uref_free(flow_def);
        upipe_release(upipe);
        return NULL;
    }

    upipe_audio_copy_store_flow_def_attr(upipe, flow_def);

    return upipe;
}

/** @internal @This frees an audio frame pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audio_copy_free(struct upipe *upipe)
{
    struct upipe_audio_copy *upipe_audio_copy =
        upipe_audio_copy_from_upipe(upipe);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_audio_copy->buffers))) {
        upipe_verbose(upipe, "delete remaining uref");
        uref_free(uref_from_uchain(uchain));
    }

    upipe_throw_dead(upipe);

    upipe_audio_copy_clean_ubuf_mgr(upipe);
    upipe_audio_copy_clean_flow_def(upipe);
    upipe_audio_copy_clean_output(upipe);
    upipe_audio_copy_clean_input(upipe);
    upipe_audio_copy_clean_urefcount(upipe);
    upipe_audio_copy_free_flow(upipe);
}

/** @internal @This checks the provided flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_audio_copy_check(struct upipe *upipe,
                                  struct uref *flow_format)
{
    struct upipe_audio_copy *upipe_audio_copy =
        upipe_audio_copy_from_upipe(upipe);

    if (flow_format)
        upipe_audio_copy_store_flow_def(upipe, flow_format);

    if (upipe_audio_copy->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_audio_copy_check_input(upipe);
    upipe_audio_copy_output_input(upipe);
    upipe_audio_copy_unblock_input(upipe);
    if (was_buffered && upipe_audio_copy_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_audio_copy_input. */
        upipe_release(upipe);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This set the input flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_def new input flow format
 * @return an error code
 */
static int upipe_audio_copy_set_flow_def_real(struct upipe *upipe,
                                              struct uref *flow_def)
{
    struct upipe_audio_copy *upipe_audio_copy =
        upipe_audio_copy_from_upipe(upipe);

    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));
    UBASE_RETURN(uref_sound_flow_get_rate(flow_def,
                                          &upipe_audio_copy->samplerate));
    UBASE_RETURN(uref_sound_flow_get_planes(flow_def,
                                            &upipe_audio_copy->planes));
    UBASE_RETURN(uref_sound_flow_get_sample_size(
            flow_def, &upipe_audio_copy->sample_size));
    if (unlikely(!upipe_audio_copy->samplerate))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    struct uref *output_flow_def =
        upipe_audio_copy_store_flow_def_input(upipe, flow_def_dup);
    UBASE_ALLOC_RETURN(output_flow_def);
    uint64_t latency = 0;
    uref_clock_get_latency(flow_def, &latency);
    latency += upipe_audio_copy->samples * UCLOCK_FREQ /
        upipe_audio_copy->samplerate;
    int ret = uref_clock_set_latency(output_flow_def, latency);
    if (unlikely(!ubase_check(ret))) {
        uref_free(output_flow_def);
        return ret;
    }

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_audio_copy->buffers))) {
        upipe_warn(upipe, "delete retained buffer");
        uref_free(uref_from_uchain(uchain));
    }
    upipe_audio_copy->size = 0;
    upipe_audio_copy->remain = 0;

    upipe_audio_copy_require_ubuf_mgr(upipe, output_flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This extracts and consumes data from buffered urefs.
 *
 * @param upipe description structure of the pipe
 * @param size number of samples to extract
 * @param dst destination planes buffer
 * @param planes number of destination planes
 */
static void upipe_audio_copy_extract(struct upipe *upipe,
                                     size_t size,
                                     uint8_t **dst,
                                     uint8_t planes)
{
    struct upipe_audio_copy *upipe_audio_copy =
        upipe_audio_copy_from_upipe(upipe);

    /* copy input to output buffer */
    size_t offset = 0;
    while (offset < size) {
        struct uchain *uchain = ulist_peek(&upipe_audio_copy->buffers);
        struct uref *in = uref_from_uchain(uchain);

        size_t in_size;
        ubase_assert(uref_sound_size(in, &in_size, NULL));
        size_t extract = size - offset > in_size ?  in_size : size - offset;

        upipe_verbose_va(upipe, "extract %zu/%zu", extract, in_size);

        const uint8_t *src[planes];
        int ret = uref_sound_read_uint8_t(in, 0, extract, src, planes);
        if (unlikely(!ubase_check(ret))) {
            upipe_warn(upipe, "fail to read from sound buffer");
            continue;
        }

        for (uint8_t plane = 0; plane < planes; plane++)
            if (dst[plane] && src[plane])
                memcpy(dst[plane] + offset * upipe_audio_copy->sample_size,
                       src[plane],
                       extract * upipe_audio_copy->sample_size);
        uref_sound_unmap(in, 0, -1, planes);

        /* delete extracted */
        if (extract == in_size) {
            ulist_delete(uchain);
            uref_free(in);
        }
        else {
            uref_sound_consume(in, extract, upipe_audio_copy->samplerate);
        }

        offset += extract;
    }
}

/** @internal @This outputs a buffer if possible
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 * @return true if a buffer was outputted, false otherwise
 */
static bool upipe_audio_copy_output_buffer(struct upipe *upipe,
                                           struct upump **upump_p)
{
    struct upipe_audio_copy *upipe_audio_copy =
        upipe_audio_copy_from_upipe(upipe);

    uint64_t samples = upipe_audio_copy->samples;
    if (!samples) {
        samples = (upipe_audio_copy->samplerate * upipe_audio_copy->fps.den +
                   upipe_audio_copy->remain) / upipe_audio_copy->fps.num;
        if (samples % 2)
            samples--;
        upipe_audio_copy->remain +=
            upipe_audio_copy->samplerate * upipe_audio_copy->fps.den -
            samples * upipe_audio_copy->fps.num;
    }

    if (samples > upipe_audio_copy->size)
        return false;

    /* get first buffer */
    struct uchain *uchain = ulist_peek(&upipe_audio_copy->buffers);
    assert(uchain);
    struct uref *in = uref_from_uchain(uchain);

    size_t in_size = 0;
    uref_sound_size(in, &in_size, NULL);
    if (in_size == samples) {
        upipe_audio_copy->size -= samples;
        ulist_delete(uchain);
        upipe_audio_copy_output(upipe, in, upump_p);
        return true;
    }

    /* duplicate uref from first buffered urefs */
    struct uref *out = uref_dup_inner(in);
    if (unlikely(!out)) {
        upipe_err(upipe, "fail to duplicate uref");
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    /* allocate output sound buffer from first buffer */
    struct ubuf *ubuf = ubuf_sound_alloc(upipe_audio_copy->ubuf_mgr, samples);
    if (unlikely(!ubuf)) {
        upipe_err(upipe, "fail to allocate sound buffer");
        uref_free(out);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    uint8_t planes = upipe_audio_copy->planes;
    uint8_t *dst[planes];
    int ret = ubuf_sound_write_uint8_t(ubuf, 0, -1, dst, planes);
    if (unlikely(!ubase_check(ret))) {
        upipe_warn(upipe, "fail to write to sound buffer");
        uref_free(out);
        ubuf_free(ubuf);
        return false;
    }
    upipe_audio_copy_extract(upipe, samples, dst, planes);
    upipe_audio_copy->size -= samples;
    /* unmap */
    ubuf_sound_unmap(ubuf, 0, -1, planes);

    uint64_t duration = UCLOCK_FREQ * samples / upipe_audio_copy->samplerate;
    uref_clock_set_duration(out, duration);
    uref_attach_ubuf(out, ubuf);

    upipe_audio_copy_output(upipe, out, upump_p);
    return true;
}

/** @internal @This outputs the buffers
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_audio_copy_work(struct upipe *upipe, struct upump **upump_p)
{
    upipe_use(upipe);
    while (upipe_audio_copy_output_buffer(upipe, upump_p));
    upipe_release(upipe);
}

/** @internal @This handles buffered input.
 *
 * @param upipe description structure of the pipe
 * @param uref buffered input
 * @param upump_p reference to pump that generated the buffer
 * @return true if the uref was processed
 */
static bool upipe_audio_copy_handle(struct upipe *upipe,
                                    struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_audio_copy *upipe_audio_copy =
        upipe_audio_copy_from_upipe(upipe);
    size_t size;
    uint8_t sample_size;

    if (unlikely(ubase_check(uref_flow_get_def(uref, NULL)))) {
        upipe_audio_copy_set_flow_def_real(upipe, uref);
        return true;
    }

    if (unlikely(!ubase_check(uref_sound_size(uref, &size, &sample_size)))) {
        upipe_warn(upipe, "fail to get sound buffer size");
        uref_free(uref);
        return true;
    }

    if (unlikely(!upipe_audio_copy->input_flow_def)) {
        upipe_warn(upipe, "no input flow format set");
        uref_free(uref);
        return true;
    }

    if (unlikely(!upipe_audio_copy->ubuf_mgr)) {
        upipe_verbose(upipe, "waiting for ubuf manager");
        return false;
    }

    upipe_audio_copy->size += size;
    ulist_add(&upipe_audio_copy->buffers, uref_to_uchain(uref));
    upipe_audio_copy_work(upipe, upump_p);
    return true;
}

/** @internal @This handles input buffers.
 *
 * @param upipe description structure of the pipe
 * @param uref input sound buffer
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_audio_copy_input(struct upipe *upipe,
                                    struct uref *uref,
                                    struct upump **upump_p)
{
    if (!upipe_audio_copy_check_input(upipe)) {
        upipe_audio_copy_hold_input(upipe, uref);
        upipe_audio_copy_block_input(upipe, upump_p);
    } else if (!upipe_audio_copy_handle(upipe, uref, upump_p)) {
        upipe_audio_copy_hold_input(upipe, uref);
        upipe_audio_copy_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This forwards a new input flow format inband.
 *
 * @param upipe description structure of the pipe
 * @param flow_def new input flow format
 * @return an error code
 */
static int upipe_audio_copy_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));
    UBASE_RETURN(uref_sound_flow_get_rate(flow_def, NULL));
    UBASE_RETURN(uref_sound_flow_get_planes(flow_def, NULL));
    UBASE_RETURN(uref_sound_flow_get_sample_size(flow_def, NULL));
    upipe_input(upipe, flow_def, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This handles the control commands.
 *
 * @param upipe description structure of the pipe
 * @param cmd command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_audio_copy_control(struct upipe *upipe,
                                     int cmd, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_audio_copy_control_output(upipe, cmd, args));

    switch (cmd) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_audio_copy_set_flow_def(upipe, flow_def);
        }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the audio frame pipe management structure. */
static struct upipe_mgr upipe_audio_copy_mgr = {
    .signature = UPIPE_AUDIO_COPY_SIGNATURE,
    .refcount = NULL,
    .upipe_alloc = upipe_audio_copy_alloc,
    .upipe_input = upipe_audio_copy_input,
    .upipe_control = upipe_audio_copy_control,
};

/** @This returns the audio frame pipe management structure. */
struct upipe_mgr *upipe_audio_copy_mgr_alloc(void)
{
    return &upipe_audio_copy_mgr;
}
