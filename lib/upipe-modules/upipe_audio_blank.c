/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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
 * @short Upipe module generating blank audio for void urefs
 */

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_flow_def.h>

#include <upipe/uref_sound_flow.h>
#include <upipe/uref_void_flow.h>

#include <upipe/ubuf_sound.h>

#include <upipe-modules/upipe_audio_blank.h>

/** @internal @This is the private structure of audio blank pipe. */
struct upipe_ablk {
    /** public pipe structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** output pipe */
    struct upipe *output;
    /** output flow def */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** urequest list */
    struct uchain requests;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** blank sound */
    struct ubuf *ubuf;
    /** ubuf flow format */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;
    /** input flow def */
    struct uref *input_flow_def;
    /** flow def attributes */
    struct uref *flow_def_attr;
};

/** @hidden */
static int upipe_ablk_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_ablk, upipe, UPIPE_ABLK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_ablk, urefcount, upipe_ablk_free);
UPIPE_HELPER_FLOW(upipe_ablk, UREF_SOUND_FLOW_DEF);
UPIPE_HELPER_OUTPUT(upipe_ablk, output, flow_def, output_state, requests);
UPIPE_HELPER_UBUF_MGR(upipe_ablk, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ablk_check,
                      upipe_ablk_register_output_request,
                      upipe_ablk_unregister_output_request);
UPIPE_HELPER_FLOW_DEF(upipe_ablk, input_flow_def, flow_def_attr);

/** @internal @This frees an audio blank pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ablk_free(struct upipe *upipe)
{
    struct upipe_ablk *upipe_ablk = upipe_ablk_from_upipe(upipe);

    upipe_throw_dead(upipe);

    if (upipe_ablk->ubuf)
        ubuf_free(upipe_ablk->ubuf);
    upipe_ablk_clean_flow_def(upipe);
    upipe_ablk_clean_ubuf_mgr(upipe);
    upipe_ablk_clean_output(upipe);
    upipe_ablk_clean_urefcount(upipe);
    upipe_ablk_free_flow(upipe);
}

/** @internal @This checks the validity of a sound flow def.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition to check
 * @return an error code
 */
static int upipe_ablk_check_flow_def(struct upipe *upipe,
                                     struct uref *flow_def)
{
    uint8_t planes, sample_size, channels;
    uint64_t rate, samples;

    UBASE_RETURN(uref_flow_match_def(flow_def, UREF_SOUND_FLOW_DEF));
    UBASE_RETURN(uref_sound_flow_get_planes(flow_def, &planes));
    UBASE_RETURN(uref_sound_flow_get_channels(flow_def, &channels));
    UBASE_RETURN(uref_sound_flow_get_rate(flow_def, &rate));
    UBASE_RETURN(uref_sound_flow_get_sample_size(flow_def, &sample_size));
    UBASE_RETURN(uref_sound_flow_get_samples(flow_def, &samples));
    return UBASE_ERR_NONE;
}

/** @internal @This allocates an audio blank pipe.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe
 */
static struct upipe *upipe_ablk_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature,
                                      va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ablk_alloc_flow(mgr, uprobe, signature,
                                                args, &flow_def);
    if (unlikely(!upipe))
        return NULL;

    upipe_ablk_init_urefcount(upipe);
    upipe_ablk_init_output(upipe);
    upipe_ablk_init_ubuf_mgr(upipe);
    upipe_ablk_init_flow_def(upipe);

    struct upipe_ablk *upipe_ablk = upipe_ablk_from_upipe(upipe);
    upipe_ablk->ubuf = NULL;

    upipe_throw_ready(upipe);

    if (unlikely(!ubase_check(upipe_ablk_check_flow_def(upipe, flow_def)))) {
        uref_free(flow_def);
        upipe_release(upipe);
        return NULL;
    }

    upipe_ablk_store_flow_def(upipe, flow_def);

    return upipe;
}

/** @internal @This handles input buffers.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer to handle
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ablk_input(struct upipe *upipe,
                             struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_ablk *upipe_ablk = upipe_ablk_from_upipe(upipe);
    struct uref *input_flow_def = upipe_ablk->input_flow_def;
    struct uref *flow_def = upipe_ablk->flow_def;

    if (uref->ubuf) {
        upipe_ablk_output(upipe, uref, upump_p);
        return;
    }

    if (unlikely(!input_flow_def)) {
        upipe_warn(upipe, "no input flow definition");
        uref_free(uref);
        return;
    }

    if (unlikely(!flow_def)) {
        upipe_warn(upipe, "no output flow definition set");
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_ablk->ubuf_mgr)) {
        upipe_warn(upipe, "no ubuf manager set");
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_ablk->ubuf)) {
        upipe_verbose(upipe, "allocate blank sound");

        uint64_t samples = 0;
        uint8_t sample_size = 0;
        uref_sound_flow_get_samples(flow_def, &samples);
        uref_sound_flow_get_sample_size(flow_def, &sample_size);

        struct ubuf *ubuf = ubuf_sound_alloc(upipe_ablk->ubuf_mgr, samples);
        if (unlikely(!ubuf)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }

        const char *channel = NULL;
        uint8_t *buf = NULL;
        while (ubase_check(ubuf_sound_plane_iterate(ubuf, &channel)) &&
               channel) {
            ubuf_sound_plane_write_uint8_t(ubuf, channel, 0, -1, &buf);
            memset(buf, 0, sample_size * samples);
            ubuf_sound_plane_unmap(ubuf, channel, 0, -1);
        }

        upipe_ablk->ubuf = ubuf;
    }

    struct ubuf *ubuf = ubuf_dup(upipe_ablk->ubuf);
    if (unlikely(!ubuf)) {
        upipe_err(upipe, "fail to duplicate blank buffer");
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uref_attach_ubuf(uref, ubuf);
    upipe_ablk_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def input flow definition to set
 * @return an error code
 */
static int upipe_ablk_set_flow_def(struct upipe *upipe,
                                   struct uref *flow_def)
{
    struct upipe_ablk *upipe_ablk = upipe_ablk_from_upipe(upipe);

    if (!ubase_check(uref_flow_match_def(flow_def, UREF_VOID_FLOW_DEF)) &&
        !ubase_check(upipe_ablk_check_flow_def(upipe, flow_def)))
        return UBASE_ERR_INVALID;

    struct uref *input_flow_def = uref_dup(flow_def);
    if (unlikely(!input_flow_def)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_ablk_store_flow_def_input(upipe, input_flow_def);

    if (ubase_check(uref_flow_match_def(input_flow_def, UREF_VOID_FLOW_DEF)))
        return UBASE_ERR_NONE;

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(!flow_def_dup)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_ablk_store_flow_def(upipe, flow_def_dup);

    if (upipe_ablk->ubuf) {
        ubuf_free(upipe_ablk->ubuf);
        upipe_ablk->ubuf = NULL;
    }

    if (upipe_ablk->ubuf_mgr &&
        !ubuf_mgr_check(upipe_ablk->ubuf_mgr, flow_def_dup)) {
        ubuf_mgr_release(upipe_ablk->ubuf_mgr);
        upipe_ablk->ubuf_mgr = NULL;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This handles pipe control commands.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_ablk_control_real(struct upipe *upipe,
                                   int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ablk_control_output(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ablk_set_flow_def(upipe, flow_def);
        }
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This checks if the ubuf manager need to be required.
 *
 * @param upipe description structure of the pipe
 * @param flow_format requested flow format
 * @return an error code
 */
static int upipe_ablk_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_ablk *upipe_ablk = upipe_ablk_from_upipe(upipe);

    if (flow_format)
        upipe_ablk_store_flow_def(upipe, flow_format);

    if (!upipe_ablk->flow_def)
        return UBASE_ERR_NONE;

    if (!upipe_ablk->ubuf_mgr) {
        upipe_ablk_require_ubuf_mgr(upipe, uref_dup(upipe_ablk->flow_def));
        return UBASE_ERR_NONE;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This handles control commands and checks the ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_ablk_control(struct upipe *upipe,
                              int command, va_list args)
{
    UBASE_RETURN(upipe_ablk_control_real(upipe, command, args));
    return upipe_ablk_check(upipe, NULL);
}

/** @internal @This is the static audio blank pipe manager. */
static struct upipe_mgr upipe_ablk_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ABLK_SIGNATURE,
    .upipe_alloc = upipe_ablk_alloc,
    .upipe_input = upipe_ablk_input,
    .upipe_control = upipe_ablk_control,
};

/** @This returns the audio blank pipe manager.
 *
 * @return a pipe manager
 */
struct upipe_mgr *upipe_ablk_mgr_alloc(void)
{
    return &upipe_ablk_mgr;
}
