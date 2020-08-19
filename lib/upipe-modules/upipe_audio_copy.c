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

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_output.h>

#include <upipe/upipe.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_pic_flow.h>

#include <upipe-modules/upipe_audio_copy.h>

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

UPIPE_HELPER_UPIPE(upipe_audio_copy, upipe, UPIPE_AUDIO_COPY_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_audio_copy, urefcount, upipe_audio_copy_free);
UPIPE_HELPER_FLOW(upipe_audio_copy, EXPECTED_FLOW_DEF);
UPIPE_HELPER_FLOW_DEF(upipe_audio_copy, input_flow_def, flow_def_attr);
UPIPE_HELPER_OUTPUT(upipe_audio_copy, output, flow_def, output_state,
                    requests);

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
    upipe_audio_copy_init_output(upipe);
    upipe_audio_copy_init_flow_def(upipe);

    struct upipe_audio_copy *upipe_audio_copy =
        upipe_audio_copy_from_upipe(upipe);
    ulist_init(&upipe_audio_copy->urefs);
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
    upipe_audio_copy_store_flow_def(upipe, flow_def);

    struct uref *flow_def_attr = uref_sibling_alloc_control(flow_def);
    if (unlikely(!flow_def_attr)) {
        upipe_err(upipe, "fail to allocate flow def attr");
        upipe_release(upipe);
        return NULL;
    }
    if (upipe_audio_copy->samples) {
        int ret = uref_sound_flow_set_samples(
            flow_def_attr, upipe_audio_copy->samples);
        if (unlikely(!ubase_check(ret))) {
            upipe_err(upipe, "fail to set samples");
            uref_free(flow_def_attr);
            upipe_release(upipe);
            return NULL;
        }
    }
    upipe_audio_copy_store_flow_def_attr(upipe, flow_def_attr);

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
    while ((uchain = ulist_pop(&upipe_audio_copy->urefs))) {
        upipe_verbose(upipe, "delete remaining uref");
        uref_free(uref_from_uchain(uchain));
    }

    upipe_throw_dead(upipe);

    upipe_audio_copy_clean_flow_def(upipe);
    upipe_audio_copy_clean_output(upipe);
    upipe_audio_copy_clean_urefcount(upipe);
    upipe_audio_copy_free_flow(upipe);
}

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
        struct uchain *uchain = ulist_peek(&upipe_audio_copy->urefs);
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
    struct uchain *uchain = ulist_peek(&upipe_audio_copy->urefs);
    assert(uchain);
    struct uref *in = uref_from_uchain(uchain);

    /* allocate output sound buffer from first buffer */
    struct uref *out = uref_sound_alloc(in->mgr, in->ubuf->mgr, samples);
    if (unlikely(!out)) {
        upipe_err(upipe, "fail to allocate sound buffer");
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    uint64_t date;
    int type;
    uref_attr_import(out, in);
    uref_clock_get_date_prog(in, &date, &type);
    if (type != UREF_DATE_NONE)
        uref_clock_set_date_prog(out, date, type);
    uref_clock_get_date_sys(in, &date, &type);
    if (type != UREF_DATE_NONE)
        uref_clock_set_date_sys(out, date, type);
    uref_clock_get_date_orig(in, &date, &type);
    if (type != UREF_DATE_NONE)
        uref_clock_set_date_orig(out, date, type);
    uref_attr_import(out, in);
    uint64_t duration = UCLOCK_FREQ * samples / upipe_audio_copy->samplerate;
    uref_clock_set_duration(out, duration);

    uint8_t planes = upipe_audio_copy->planes;
    uint8_t *dst[planes];
    int ret = uref_sound_write_uint8_t(out, 0, -1, dst, planes);
    if (unlikely(!ubase_check(ret))) {
        upipe_warn(upipe, "fail to write to sound buffer");
        uref_free(out);
        return false;
    }
    upipe_audio_copy_extract(upipe, samples, dst, planes);
    upipe_audio_copy->size -= samples;
    /* unmap */
    uref_sound_unmap(out, 0, -1, planes);

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
    struct upipe_audio_copy *upipe_audio_copy =
        upipe_audio_copy_from_upipe(upipe);
    size_t size;
    uint8_t sample_size;

    if (unlikely(!ubase_check(uref_sound_size(uref, &size, &sample_size)))) {
        upipe_warn(upipe, "fail to get sound buffer size");
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_audio_copy->input_flow_def)) {
        upipe_warn(upipe, "no input flow format set");
        uref_free(uref);
        return;
    }

    upipe_audio_copy->size += size;
    ulist_add(&upipe_audio_copy->urefs, uref_to_uchain(uref));

    upipe_audio_copy_work(upipe, upump_p);
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
    upipe_audio_copy_store_flow_def(upipe, output_flow_def);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_audio_copy->urefs))) {
        upipe_warn(upipe, "delete retained buffer");
        uref_free(uref_from_uchain(uchain));
    }
    upipe_audio_copy->size = 0;
    upipe_audio_copy->remain = 0;

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
