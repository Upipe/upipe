/*
 * Copyright (C) 2015 - 2019 Open Broadcast Systems Ltd.
 *
 * Authors: Kieran Kunhya
 *          Sam Willcocks
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module merging audio from several input subpipes into a single buffer
 *
 * This module accepts audio from input subpipes and outputs a single pipe of n-channel/plane audio.
 * Each input must be sent urefs at the same rate, as the pipe will not output until every input channel
 * has received a uref. The pipe makes no attempt to retime or resample.
 * Input audio must have the same format, save for the number of channels/planes.
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe-modules/upipe_audio_merge.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @hidden */
static int upipe_audio_merge_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of an audio_merge pipe. */
struct upipe_audio_merge {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** list of input subpipes */
    struct uchain inputs;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** flow def of the first input subpipe, used to check if the format
     *  of subsequent subpipes match */
    struct uref *sub_flow_def;

    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** channels **/
    uint8_t channels;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_audio_merge, upipe, UPIPE_AUDIO_MERGE_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_audio_merge, urefcount, upipe_audio_merge_no_input)
UPIPE_HELPER_UREFCOUNT_REAL(upipe_audio_merge, urefcount_real, upipe_audio_merge_free)
UPIPE_HELPER_OUTPUT(upipe_audio_merge, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW(upipe_audio_merge, "sound.")
UPIPE_HELPER_UBUF_MGR(upipe_audio_merge, ubuf_mgr, flow_format,
                      ubuf_mgr_request,
                      upipe_audio_merge_check,
                      upipe_audio_merge_register_output_request,
                      upipe_audio_merge_unregister_output_request)

/** @internal @This is the private context of an output of an audio_merge
 * pipe. */
struct upipe_audio_merge_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** current input flow definition packet */
    struct uref *flow_def;

    /** the single uref of incoming audio */
    struct uref *uref;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_audio_merge_sub, upipe,
                   UPIPE_AUDIO_MERGE_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_audio_merge_sub, urefcount,
                       upipe_audio_merge_sub_free)
UPIPE_HELPER_VOID(upipe_audio_merge_sub)

UPIPE_HELPER_SUBPIPE(upipe_audio_merge, upipe_audio_merge_sub, input,
                     sub_mgr, inputs, uchain)

/** @internal @Checks if two flow defs match for our purposes
 *
 * @param one first uref
 * @param two second uref
 * @return true if flow defs match, false if not
 */
static bool upipe_audio_merge_match_flowdefs(struct uref *one, struct uref *two)
{
    return uref_sound_flow_cmp_rate(one, two) == 0
                && uref_sound_flow_cmp_sample_size(one, two) == 0
                && uref_sound_flow_cmp_samples(one, two) == 0
                && uref_sound_flow_cmp_align(one, two) == 0;
}

static int upipe_audio_merge_sub_set_flow_def(struct upipe *upipe,
                                              struct uref *flow_def)
{
    struct upipe_audio_merge *upipe_audio_merge =
        upipe_audio_merge_from_sub_mgr(upipe->mgr);
    struct upipe_audio_merge_sub *upipe_audio_merge_sub =
        upipe_audio_merge_sub_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    /* reject interleaved audio */
    uint8_t channels = 0, planes = 0;
    UBASE_RETURN(uref_sound_flow_get_planes(flow_def, &planes));
    UBASE_RETURN(uref_sound_flow_get_channels(flow_def, &channels));
    if (planes != channels) {
        upipe_err(upipe, "Interleaved audio not supported");
        return UBASE_ERR_INVALID;
    }
    /* compare against stored flowdef (if we have one) and reject if formats don't match */
    if (upipe_audio_merge->sub_flow_def
            && !upipe_audio_merge_match_flowdefs(flow_def, upipe_audio_merge->sub_flow_def))
    {
        upipe_err(upipe, "Subpipe has non-matching audio format!");
        return UBASE_ERR_INVALID;
    }

    /* .. and if we don't have one store this one */
    if (!upipe_audio_merge->sub_flow_def)
        upipe_audio_merge->sub_flow_def = uref_dup(flow_def);

    /* store in the subpipe structure itself for later reference */
    uref_free(upipe_audio_merge_sub->flow_def);
    upipe_audio_merge_sub->flow_def = uref_dup(flow_def);

    return UBASE_ERR_NONE;
}

/** @internal @This allocates an input subpipe of an audio_merge pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_audio_merge_sub_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_audio_merge_sub_alloc_void(mgr,
                            uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_audio_merge_sub *upipe_audio_merge_sub =
                            upipe_audio_merge_sub_from_upipe(upipe);

    upipe_audio_merge_sub_init_urefcount(upipe);
    upipe_audio_merge_sub_init_sub(upipe);
    upipe_audio_merge_sub->uref = NULL;
    upipe_audio_merge_sub->flow_def = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This processes control commands on an input subpipe of an
 *  audio_merge pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_audio_merge_sub_control(struct upipe *upipe,
                                         int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_audio_merge_sub_control_super(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_audio_merge_sub_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @Copies data from the input urefs to an output buffer.
 *
 * @param upipe description structure of the pipe
 * @param out_data reference to the output buffers
 */
static void upipe_audio_merge_copy_to_output(struct upipe *upipe, float **out_data)
{
    int8_t cur_plane = 0;
    struct upipe_audio_merge *upipe_audio_merge = upipe_audio_merge_from_upipe(upipe);
    struct uchain *uchain;

    uint64_t output_num_samples = 0;
    UBASE_ERROR(upipe, uref_sound_flow_get_samples(upipe_audio_merge->flow_def, &output_num_samples));

    uint8_t output_channels = 0;
    UBASE_ERROR(upipe, uref_sound_flow_get_channels(upipe_audio_merge->flow_def, &output_channels));

    ulist_foreach (&upipe_audio_merge->inputs, uchain) {
        struct upipe_audio_merge_sub *upipe_audio_merge_sub =
            upipe_audio_merge_sub_from_uchain(uchain);

        uint8_t planes = 0;
        UBASE_ERROR(upipe, uref_sound_flow_get_planes(upipe_audio_merge_sub->flow_def, &planes));

        float *in_data[planes];
        if(unlikely(!ubase_check(uref_sound_read_float(upipe_audio_merge_sub->uref, 0, -1, (const float**)in_data, planes))))
            upipe_err(upipe, "error reading subpipe audio, skipping");
        else {
            uint64_t input_num_samples = 0;
            UBASE_ERROR(upipe, uref_sound_flow_get_samples(upipe_audio_merge_sub->uref, &input_num_samples));

            /* If the samples of the input uref != our output sample size, throw an error and don't copy */
            if (unlikely(input_num_samples != output_num_samples))
                upipe_err_va(upipe, "input samples (%"PRIu64") != output samples (%"PRIu64")!",
                    input_num_samples, output_num_samples);
            else {
                for (int i = 0; i < planes; i++) {
                    /* Only copy up to the number of channels in the output flowdef,
                    and thus what we've allocated */
                    if ((cur_plane + i) < output_channels) {
                        for (int j = 0; j < input_num_samples; j++)
                            out_data[cur_plane+i][j] = in_data[i][j];
                    }
                }
            }
            uref_sound_unmap(upipe_audio_merge_sub->uref, 0, -1, planes);
        }
        /* cur_plane is incremented outside of the loop in case we didn't copy, in which case we'd expect
           the plane(s) to be left blank in the output rather than removed */
        cur_plane += planes;

        uref_free(upipe_audio_merge_sub->uref);
        upipe_audio_merge_sub->uref = NULL;
    }
}

/** @internal @Output a uref, if possible
 *
 * @param upipe description structure of the pipe
 * @param upump reference to upump structure
 */
static void upipe_audio_merge_produce_output(struct upipe *upipe, struct upump **upump)
{
    struct upipe_audio_merge *upipe_audio_merge = upipe_audio_merge_from_upipe(upipe);
    struct uchain *uchain;
    struct uref *output_uref = NULL;
    struct ubuf *ubuf;

    if (unlikely(upipe_audio_merge->ubuf_mgr == NULL))
        return;

    if (unlikely(upipe_audio_merge->flow_def == NULL))
        return;

    /* interate through input subpipes, checking if they all have a uref available
       and counting the number of channels */
    ulist_foreach (&upipe_audio_merge->inputs, uchain) {
        struct upipe_audio_merge_sub *upipe_audio_merge_sub =
            upipe_audio_merge_sub_from_uchain(uchain);

        /* If any of the input subpipes does not have a flowdef/uref ready
           we're not ready to output */
        if (upipe_audio_merge_sub->uref == NULL || upipe_audio_merge_sub->flow_def == NULL)
            return;
    }

    uint64_t input_channels = 0;
    ulist_foreach (&upipe_audio_merge->inputs, uchain) {
        struct upipe_audio_merge_sub *upipe_audio_merge_sub =
            upipe_audio_merge_sub_from_uchain(uchain);
        /* if we haven't got one already, copy the uref to form the basis of our output uref */
        if (output_uref == NULL)
            output_uref = uref_dup(upipe_audio_merge_sub->uref);

        uint8_t channels = 0;
        UBASE_ERROR(upipe, uref_sound_flow_get_channels(upipe_audio_merge_sub->flow_def, &channels));
        input_channels += channels;
    }

    if (unlikely(!output_uref))
        return;

    /* If total channels in input subpipes != channels in flow def throw an error */
    uint8_t output_channels = 0;
    UBASE_ERROR(upipe, uref_sound_flow_get_channels(upipe_audio_merge->flow_def, &output_channels));
    if (input_channels != output_channels)
        upipe_err_va(upipe, "total input channels (%"PRIu64") != output flow def (%d), some will be skipped or blanked!",
            input_channels, output_channels);

    uint64_t output_num_samples = 0;
    UBASE_ERROR(upipe, uref_sound_flow_get_samples(upipe_audio_merge->flow_def, &output_num_samples));

    float *out_data[output_channels];

    /* Alloc and zero the output ubuf */
    ubuf = ubuf_sound_alloc(upipe_audio_merge->ubuf_mgr, output_num_samples);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return;
    }
    if (likely(ubase_check(ubuf_sound_write_float(ubuf, 0, -1, out_data, output_channels)))) {
        for (int i = 0; i < output_channels; i++)
            memset(out_data[i], 0, sizeof(float) * output_num_samples);
    } else {
        upipe_err(upipe, "error writing output audio buffer, skipping");
        uref_free(output_uref);
        ubuf_free(ubuf);
        return;
    }

    /* copy input data to output */
    upipe_audio_merge_copy_to_output(upipe, out_data);

    /* clean up and output */
    ubuf_sound_unmap(ubuf, 0, -1, output_channels);
    uref_sound_flow_set_samples(output_uref, output_num_samples);
    uref_attach_ubuf(output_uref, ubuf);
    upipe_audio_merge_output(upipe, output_uref, upump);
}

/** @internal @This handles input data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_audio_merge_sub_input(struct upipe *upipe, struct uref *uref,
                                        struct upump **upump_p)
{
    struct upipe_audio_merge *upipe_audio_merge =
        upipe_audio_merge_from_sub_mgr(upipe->mgr);

    struct upipe_audio_merge_sub *upipe_audio_merge_sub =
                              upipe_audio_merge_sub_from_upipe(upipe);

    if (upipe_audio_merge_sub->uref != NULL) {
        uref_free(upipe_audio_merge_sub->uref);
        upipe_warn(upipe, "Got new uref before last one was output!");
    }
    upipe_audio_merge_sub->uref = uref;

    /* produce output, if we have all urefs */
    upipe_audio_merge_produce_output(upipe_audio_merge_to_upipe(upipe_audio_merge), upump_p);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audio_merge_sub_free(struct upipe *upipe)
{
    struct upipe_audio_merge_sub *upipe_audio_merge_sub =
                              upipe_audio_merge_sub_from_upipe(upipe);

    uref_free(upipe_audio_merge_sub->uref);
    uref_free(upipe_audio_merge_sub->flow_def);

    upipe_throw_dead(upipe);

    upipe_audio_merge_sub_clean_sub(upipe);
    upipe_audio_merge_sub_clean_urefcount(upipe);
    upipe_audio_merge_sub_free_void(upipe);
}

/** @internal @This initializes the output manager for an audio_merge sub pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audio_merge_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_audio_merge *upipe_audio_merge =
                              upipe_audio_merge_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_audio_merge->sub_mgr;
    sub_mgr->refcount = upipe_audio_merge_to_urefcount_real(upipe_audio_merge);
    sub_mgr->signature = UPIPE_AUDIO_MERGE_INPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_audio_merge_sub_alloc;
    sub_mgr->upipe_input = upipe_audio_merge_sub_input;
    sub_mgr->upipe_control = upipe_audio_merge_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates an audio_merge pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_audio_merge_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_audio_merge_alloc_flow(mgr,
                            uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_audio_merge *upipe_audio_merge =
                              upipe_audio_merge_from_upipe(upipe);

    upipe_audio_merge_init_urefcount(upipe);
    upipe_audio_merge_init_urefcount_real(upipe);

    upipe_audio_merge_init_output(upipe);
    upipe_audio_merge_init_sub_mgr(upipe);
    upipe_audio_merge_init_sub_inputs(upipe);
    upipe_audio_merge_init_ubuf_mgr(upipe);

    upipe_audio_merge->sub_flow_def = NULL;

    upipe_throw_ready(upipe);
    upipe_audio_merge_store_flow_def(upipe, flow_def);

    return upipe;
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_audio_merge_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_audio_merge *upipe_audio_merge = upipe_audio_merge_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_audio_merge_store_flow_def(upipe, flow_format);

    if (upipe_audio_merge->flow_def == NULL)
        return UBASE_ERR_NONE;

    if (upipe_audio_merge->ubuf_mgr == NULL) {
        upipe_audio_merge_require_ubuf_mgr(upipe, uref_dup(upipe_audio_merge->flow_def));
        return UBASE_ERR_NONE;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an audio_merge pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_audio_merge_control(struct upipe *upipe,
                                     int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_audio_merge_control_inputs(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_audio_merge_control_output(upipe, command, args));

    switch (command) {
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on an audio_merge pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_audio_merge_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_audio_merge_control(upipe, command, args));

    return upipe_audio_merge_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audio_merge_free(struct upipe *upipe)
{
    struct upipe_audio_merge *upipe_audio_merge = upipe_audio_merge_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_audio_merge->sub_flow_def);
    upipe_audio_merge_clean_output(upipe);
    upipe_audio_merge_clean_sub_inputs(upipe);
    upipe_audio_merge_clean_ubuf_mgr(upipe);
    upipe_audio_merge_clean_urefcount_real(upipe);
    upipe_audio_merge_clean_urefcount(upipe);
    upipe_audio_merge_free_flow(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audio_merge_no_input(struct upipe *upipe)
{
    upipe_audio_merge_release_urefcount_real(upipe);
}

/** audio_merge module manager static descriptor */
static struct upipe_mgr upipe_audio_merge_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AUDIO_MERGE_SIGNATURE,

    .upipe_alloc = upipe_audio_merge_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_audio_merge_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all audio_merge pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_audio_merge_mgr_alloc(void)
{
    return &upipe_audio_merge_mgr;
}
