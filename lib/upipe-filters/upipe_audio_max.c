/*
 * Copyright (C) 2016 Open Broadcast System Ltd
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Rafaël Carré
 *          Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe filter computing the maximum amplitude per uref
 */

#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-filters/upipe_audio_max.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>

typedef double (*upipe_amax_process)(struct upipe *, struct uref *,
                                     const char *, size_t);

/** @internal upipe_amax private structure */
struct upipe_amax {
    /** refcount management structure */
    struct urefcount urefcount;

    upipe_amax_process process;

    /** output */
    struct upipe *output;
    /** output flow */
    struct uref *output_flow;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_amax, upipe, UPIPE_AUDIO_MAX_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_amax, urefcount, upipe_amax_free)
UPIPE_HELPER_VOID(upipe_amax)
UPIPE_HELPER_OUTPUT(upipe_amax, output, output_flow, output_state, request_list)

/** @internal @This allocates a filter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_amax_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_amax_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_amax *upipe_amax = upipe_amax_from_upipe(upipe);
    upipe_amax_init_urefcount(upipe);
    upipe_amax_init_output(upipe);
    upipe_amax->process = NULL;

    upipe_throw_ready(upipe);
    return upipe;
}

#define UPIPE_AMAX_TEMPLATE(type, type_max)                                 \
/** @internal @This processes input of format type.                         \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param uref uref structure                                               \
 * @param channel channel name                                              \
 * @param samples number of samples                                         \
 */                                                                         \
static double upipe_amax_process_##type(struct upipe *upipe,                \
        struct uref *uref, const char *channel, size_t samples)             \
{                                                                           \
    const type *buf = NULL;                                                 \
    if (unlikely(!ubase_check(uref_sound_plane_read_##type(uref,            \
            channel, 0, -1, &buf)))) {                                      \
        upipe_warn(upipe, "error mapping sound buffer");                    \
        return 0.;                                                          \
    }                                                                       \
    type max = 0;                                                           \
    for (size_t i = 0; i < samples; i++) {                                  \
        type c = buf[i];                                                    \
        if (c > max)                                                        \
            max = c;                                                        \
        else if (-c > max)                                                  \
            max = -c;                                                       \
    }                                                                       \
    uref_sound_plane_unmap(uref, channel, 0, -1);                           \
    return (max * 1.0f) / type_max;                                         \
}
UPIPE_AMAX_TEMPLATE(uint8_t, UINT8_MAX)
UPIPE_AMAX_TEMPLATE(int16_t, INT16_MAX)
UPIPE_AMAX_TEMPLATE(int32_t, INT32_MAX)
UPIPE_AMAX_TEMPLATE(float, 1.)
UPIPE_AMAX_TEMPLATE(double, 1.)
#undef UPIPE_AMAX_TEMPLATE

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_amax_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_amax *upipe_amax = upipe_amax_from_upipe(upipe);
    if (unlikely(upipe_amax->process == NULL || uref->ubuf == NULL)) {
        upipe_warn(upipe, "invalid uref received");
        uref_free(uref);
        return;
    }

    size_t samples;
    if (unlikely(!ubase_check(uref_sound_size(uref, &samples, NULL)))) {
        upipe_warn(upipe, "invalid sound buffer");
        uref_free(uref);
        return;
    }
    const char *channel = NULL;
    uint8_t j = 0;
    while (ubase_check(uref_sound_plane_iterate(uref, &channel)) && channel) {
        double maxf = upipe_amax->process(upipe, uref, channel, samples);
        uref_amax_set_amplitude(uref, maxf, j++);
    }

    upipe_amax_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow flow definition packet
 * @return an error code
 */
static int upipe_amax_set_flow_def(struct upipe *upipe, struct uref *flow)
{
    struct upipe_amax *upipe_amax = upipe_amax_from_upipe(upipe);
    if (flow == NULL)
        return UBASE_ERR_INVALID;

    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow, &def))
    upipe_amax_process process = NULL;
    if (!ubase_ncmp(def, "sound.u8."))
        process = upipe_amax_process_uint8_t;
    else if (!ubase_ncmp(def, "sound.s16."))
        process = upipe_amax_process_int16_t;
    else if (!ubase_ncmp(def, "sound.s32."))
        process = upipe_amax_process_int32_t;
    else if (!ubase_ncmp(def, "sound.f32."))
        process = upipe_amax_process_float;
    else if (!ubase_ncmp(def, "sound.f64."))
        process = upipe_amax_process_double;
    else
        return UBASE_ERR_INVALID;
    uint8_t channels, planes;
    if (unlikely(!ubase_check(uref_sound_flow_get_channels(flow, &channels))
              || !ubase_check(uref_sound_flow_get_planes(flow, &planes))
              || planes != channels))
        return UBASE_ERR_INVALID;

    upipe_amax->process = process;

    struct uref *flow_dup;
    if (unlikely((flow_dup = uref_dup(flow)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_amax_store_flow_def(upipe, flow_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_amax_provide_flow_format(struct upipe *upipe,
                                          struct urequest *request)
{
    struct uref *flow = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow);

    uint8_t planes, channels, sample_size;
    const char *channels_names;
    if (ubase_check(uref_sound_flow_get_planes(request->uref, &planes)) &&
        ubase_check(uref_sound_flow_get_channels(request->uref, &channels)) &&
        planes == 1 && channels > 1 &&
        ubase_check(uref_sound_flow_get_sample_size(request->uref,
                                                    &sample_size)) &&
        ubase_check(uref_sound_flow_get_channel(request->uref,
                                                &channels_names, 0)) &&
        strlen (channels_names) >= channels) {
        /* set attributes */
        uref_sound_flow_clear_format(flow);
        UBASE_FATAL(upipe,
                uref_sound_flow_set_sample_size(flow, sample_size / channels));

        char channel_name[2];
        channel_name[1] = '\0';
        for (int i = 0; i < channels; i++) {
            channel_name[0] = channels_names[i];
            UBASE_FATAL(upipe, uref_sound_flow_add_plane(flow, channel_name));
        }
    }

    return urequest_provide_flow_format(request, flow);
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_amax_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_amax_provide_flow_format(upipe, request);
            return upipe_amax_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_amax_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_amax_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_amax_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_amax_free(struct upipe *upipe)
{
    struct upipe_amax *upipe_amax = upipe_amax_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_amax_clean_output(upipe);
    upipe_amax_clean_urefcount(upipe);
    upipe_amax_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_amax_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AUDIO_MAX_SIGNATURE,

    .upipe_alloc = upipe_amax_alloc,
    .upipe_input = upipe_amax_input,
    .upipe_control = upipe_amax_control
};

/** @This returns the management structure for glx_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_amax_mgr_alloc(void)
{
    return &upipe_amax_mgr;
}
