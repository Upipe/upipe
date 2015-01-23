/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Xavier Boulet
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
 * @short Upipe NaCl module to play audio samples
 */

#include <upipe/ubase.h>
#include <upipe/uclock.h>
#include <upipe/uqueue.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-nacl/upipe_nacl_audio.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <ppapi/c/pp_resource.h>
#include <ppapi/c/pp_time.h>
#include <ppapi/c/ppb_audio.h>
#include <ppapi/c/ppb_audio_config.h>
#include <ppapi/c/ppb_core.h>
#include <ppapi_simple/ps.h>

/** maximum length of the internal uqueue */
#define MAX_QUEUE_LENGTH 6
/** tolerance on PTSs (plus or minus) */
#define PTS_TOLERANCE (UCLOCK_FREQ / 25)
/** we expect s16 sound */
#define EXPECTED_FLOW_DEF "sound.s16."
/** we only accept 48 kHz */
#define SAMPLE_RATE 48000
/** we ask NaCl to output this number of samples each tick */
#define NB_SAMPLES 1024

/** @hidden */
static void upipe_nacl_audio_worker(void *sample_buffer, uint32_t buffer_size,
                                    PP_TimeDelta latency, void *user_data);

/** @internal upipe_nacl_audio private structure */
struct upipe_nacl_audio {
    /** refcount management structure */
    struct urefcount urefcount;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *upump;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** delay applied to system clock ref when uclock is provided */
    uint64_t latency;
    /** buffered uref (accessed from a different thread) */
    struct uref *uref;
    /** queue of urefs to play */
    struct uqueue uqueue;
    /** number of samples per chunk */
    uint32_t nb_samples;
    /** pointer to NaCl core interface */
    PPB_Core *core_interface;
    /** pointer to NaCl audio interface */
    PPB_Audio *audio_interface;
    /** handle to NaCl audio config resource */
    PP_Resource audio_config;
    /** handle to NaCl audio resource */
    PP_Resource audio;
    /** true if the playback was started */
    bool started;

    /** temporary uref storage */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;

    /** public upipe structure */
    struct upipe upipe;

    /** extra data for the queue structure */
    uint8_t uqueue_extra[];
};

UPIPE_HELPER_UPIPE(upipe_nacl_audio, upipe, UPIPE_NACL_AUDIO_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_nacl_audio, urefcount, upipe_nacl_audio_free);
UPIPE_HELPER_UPUMP_MGR(upipe_nacl_audio, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_nacl_audio, upump, upump_mgr)
UPIPE_HELPER_INPUT(upipe_nacl_audio, urefs, nb_urefs, max_urefs, blockers, NULL)
UPIPE_HELPER_UCLOCK(upipe_nacl_audio, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL);

/** @internal @This allocates an nacl audio pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_nacl_audio_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    if (signature != UPIPE_VOID_SIGNATURE)
        return NULL;
    struct upipe_nacl_audio *upipe_nacl_audio =
        malloc(sizeof(struct upipe_nacl_audio) +
               uqueue_sizeof(MAX_QUEUE_LENGTH));
    if (unlikely(upipe_nacl_audio == NULL))
        return NULL;

    if(unlikely(!uqueue_init(&upipe_nacl_audio->uqueue, MAX_QUEUE_LENGTH,
                             upipe_nacl_audio->uqueue_extra))) {
        free(upipe_nacl_audio);
        return NULL;
    }

    struct upipe *upipe = upipe_nacl_audio_to_upipe(upipe_nacl_audio);
    upipe_init(upipe, mgr, uprobe);

    upipe_nacl_audio_init_urefcount(upipe);
    upipe_nacl_audio_init_input(upipe);
    upipe_nacl_audio_init_upump_mgr(upipe);
    upipe_nacl_audio_init_upump(upipe);
    upipe_nacl_audio_init_uclock(upipe);
    upipe_nacl_audio->started = false;

    PPB_AudioConfig *audio_config_interface =
        (PPB_AudioConfig *)PSGetInterface(PPB_AUDIO_CONFIG_INTERFACE);
    upipe_nacl_audio->nb_samples =
        audio_config_interface->RecommendSampleFrameCount(PSGetInstanceId(),
                SAMPLE_RATE, NB_SAMPLES);
    upipe_nacl_audio->audio_config =
        audio_config_interface->CreateStereo16Bit(PSGetInstanceId(),
                SAMPLE_RATE, upipe_nacl_audio->nb_samples);

    upipe_nacl_audio->core_interface =
        (PPB_Core *)PSGetInterface(PPB_CORE_INTERFACE);
    upipe_nacl_audio->audio_interface =
        (PPB_Audio *)PSGetInterface(PPB_AUDIO_INTERFACE);
    upipe_nacl_audio->audio =
        upipe_nacl_audio->audio_interface->Create(PSGetInstanceId(),
                                upipe_nacl_audio->audio_config,
                                upipe_nacl_audio_worker, upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This is called when the queue can be written again.
 * Unblock the sink.
 *
 * @param upump description structure of the watcher
 */
static void upipe_nacl_audio_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    if (upipe_nacl_audio_output_input(upipe)) {
        upump_stop(upump);
    }
    upipe_nacl_audio_unblock_input(upipe);
}

/** @internal @This checks and creates the upump watcher to wait for the
 * availability of the queue.
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool upipe_nacl_audio_check_watcher(struct upipe *upipe)
{
    struct upipe_nacl_audio *upipe_nacl_audio =
        upipe_nacl_audio_from_upipe(upipe);
    if (likely(upipe_nacl_audio->upump != NULL))
        return true;

    upipe_nacl_audio_check_upump_mgr(upipe);
    if (upipe_nacl_audio->upump_mgr == NULL)
        return false;

    struct upump *upump =
        uqueue_upump_alloc_push(&upipe_nacl_audio->uqueue,
                                upipe_nacl_audio->upump_mgr,
                                upipe_nacl_audio_watcher, upipe);
    if (unlikely(upump == NULL)) {
        upipe_err_va(upipe, "can't create watcher");
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return false;
    }
    upipe_nacl_audio_set_upump(upipe, upump);
    return true;
}

/** @internal @This is called to consume frames out of the first buffered uref.
 *
 * @param upipe description structure of the pipe
 * @param frames number of frames to consume
 */
static void upipe_nacl_audio_consume(struct upipe *upipe, uint32_t frames)
{
    struct upipe_nacl_audio *upipe_nacl_audio =
        upipe_nacl_audio_from_upipe(upipe);
    struct uref *uref = upipe_nacl_audio->uref;
    assert(uref != NULL);

    size_t uref_size;
    if (ubase_check(uref_sound_size(uref, &uref_size, NULL)) &&
        uref_size <= frames) {
        upipe_nacl_audio->uref = NULL;
        uref_free(uref);
    } else {
        uref_sound_resize(uref, frames, -1);
        uint64_t pts;
        if (ubase_check(uref_clock_get_pts_sys(uref, &pts)))
            uref_clock_set_pts_sys(uref,
                    pts + frames * UCLOCK_FREQ / SAMPLE_RATE);
        /* We should also change the duration but we don't use it. */
    }
}

/** @internal @This is called back to fill NaCl audio buffer.
 * Please note that this function runs in a different thread.
 *
 * @param sample_buffer buffer to fill
 * @param buffer_size size of the buffer in octets
 * @param latency how long before the audio data is to be presented
 * @param user_data opaque pointing to the pipe
 */
static void upipe_nacl_audio_worker(void *sample_buffer, uint32_t buffer_size,
                                    PP_TimeDelta latency, void *user_data)
{
    struct upipe *upipe = (struct upipe *)user_data;
    struct upipe_nacl_audio *upipe_nacl_audio =
        upipe_nacl_audio_from_upipe(upipe);
    uint64_t next_pts = UINT64_MAX;

    if (likely(upipe_nacl_audio->uclock != NULL)) {
        /* This is slightly off. */
        next_pts = uclock_now(upipe_nacl_audio->uclock) +
                   (uint64_t)(latency * UCLOCK_FREQ);
    }

    uint32_t frames = buffer_size / 4;

    while (frames > 0) {
        if (upipe_nacl_audio->uref == NULL)
            upipe_nacl_audio->uref = uqueue_pop(&upipe_nacl_audio->uqueue,
                                                struct uref *);
        if (unlikely(upipe_nacl_audio->uref == NULL)) {
            upipe_dbg_va(upipe, "playing %u frames of silence (empty)", frames);
            memset(sample_buffer, 0, frames * 4);
            sample_buffer += frames * 4;
            break;
        }

        struct uref *uref = upipe_nacl_audio->uref;
        if (next_pts != UINT64_MAX) {
            uint64_t uref_pts;
            if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref,
                                &uref_pts)))) {
                upipe_nacl_audio->uref = NULL;
                uref_free(uref);
                upipe_warn(upipe, "non-dated uref received");
                continue;
            }

            int64_t tolerance = uref_pts - next_pts;
            if (tolerance > (int64_t)PTS_TOLERANCE) {
                uint32_t silence_frames =
                    tolerance * SAMPLE_RATE / UCLOCK_FREQ;
                if (silence_frames > frames)
                    silence_frames = frames;
                upipe_dbg_va(upipe, "playing %u frames of silence (wait)",
                             silence_frames);
                memset(sample_buffer, 0, silence_frames * 4);
                sample_buffer += silence_frames * 4;
                frames -= silence_frames;
                continue;
            } else if (-tolerance > (int64_t)PTS_TOLERANCE) {
                uint32_t dropped_frames =
                    (-tolerance) * SAMPLE_RATE / UCLOCK_FREQ;
                upipe_warn_va(upipe, "late buffer received, dropping %u frames",
                              dropped_frames);
                upipe_nacl_audio_consume(upipe, dropped_frames);
                continue;
            }
        }

        size_t size;
        const void *uref_buffer;
        if (unlikely(!ubase_check(uref_sound_size(uref, &size, NULL)) ||
                     !ubase_check(uref_sound_plane_read_void(uref, "lr", 0, -1,
                                                             &uref_buffer)))) {
            upipe_nacl_audio->uref = NULL;
            uref_free(uref);
            upipe_warn(upipe, "cannot read ubuf buffer");
            continue;
        }

        uint32_t copied_frames = size < frames ? size : frames;
        memcpy(sample_buffer, uref_buffer, copied_frames * 4);
        uref_sound_plane_unmap(uref, "lr", 0, -1);
        sample_buffer += copied_frames * 4;

        upipe_nacl_audio_consume(upipe, copied_frames);
        frames -= copied_frames;
    }
} 

/** @internal @This opens the NaCl audio interface.
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool upipe_nacl_audio_open(struct upipe *upipe)
{
    struct upipe_nacl_audio *upipe_nacl_audio =
        upipe_nacl_audio_from_upipe(upipe);

    upipe_nacl_audio->audio_interface->StartPlayback(upipe_nacl_audio->audio);
    upipe_nacl_audio->started = true;
    return true;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 * @return true if the uref was handled
 */
static bool upipe_nacl_audio_handle(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_nacl_audio *upipe_nacl_audio =
        upipe_nacl_audio_from_upipe(upipe);
    return uqueue_push(&upipe_nacl_audio->uqueue, uref);
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_nacl_audio_input(struct upipe *upipe, struct uref *uref, 
                                   struct upump **upump_p)
{    
    struct upipe_nacl_audio *upipe_nacl_audio =
        upipe_nacl_audio_from_upipe(upipe);
    size_t uref_size;
    if (unlikely(!ubase_check(uref_sound_size(uref, &uref_size, NULL)))) {
        upipe_warn(upipe, "unable to read uref");
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_nacl_audio->started && !upipe_nacl_audio_open(upipe))) {
        upipe_warn(upipe, "unable to open device");
        uref_free(uref);
        return;
    }

    uint64_t uref_pts;
    if (likely(ubase_check(uref_clock_get_pts_sys(uref, &uref_pts)))) {
        uref_pts += upipe_nacl_audio->latency;
        uref_clock_set_pts_sys(uref, uref_pts);
    }

    if (!upipe_nacl_audio_check_input(upipe)) {
        upipe_nacl_audio_hold_input(upipe, uref);
        upipe_nacl_audio_block_input(upipe, upump_p);
    } else if (!upipe_nacl_audio_handle(upipe, uref, upump_p)) {
        if (!upipe_nacl_audio_check_watcher(upipe)) {
            upipe_warn(upipe, "unable to spool uref");
            uref_free(uref);
            return;
        }
        upipe_nacl_audio_hold_input(upipe, uref);
        upipe_nacl_audio_block_input(upipe, upump_p);
        upump_start(upipe_nacl_audio->upump);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_nacl_audio_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_nacl_audio *upipe_nacl_audio =
        upipe_nacl_audio_from_upipe(upipe);
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    UBASE_RETURN(uref_sound_flow_match_rate(flow_def, SAMPLE_RATE, SAMPLE_RATE))
    UBASE_RETURN(uref_sound_flow_match_channels(flow_def, 2, 2))
    UBASE_RETURN(uref_sound_flow_match_planes(flow_def, 1, 1))
    UBASE_RETURN(uref_sound_flow_check_channel(flow_def, "lr"))

    upipe_nacl_audio->latency = 0;
    uref_clock_get_latency(flow_def, &upipe_nacl_audio->latency);
    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_nacl_audio_provide_flow_format(struct upipe *upipe,
                                                struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);
    uref_sound_flow_clear_format(flow_format);
    uref_flow_set_def(flow_format, EXPECTED_FLOW_DEF);
    uref_sound_flow_set_channels(flow_format, 2);
    uref_sound_flow_set_sample_size(flow_format, 4);
    uref_sound_flow_set_planes(flow_format, 0);
    uref_sound_flow_add_plane(flow_format, "lr");
    uref_sound_flow_set_rate(flow_format, SAMPLE_RATE);
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_nacl_audio_control(struct upipe *upipe,
                                     int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UCLOCK:
            upipe_nacl_audio_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_nacl_audio_set_upump(upipe, NULL);
            return upipe_nacl_audio_attach_upump_mgr(upipe);
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_nacl_audio_provide_flow_format(upipe, request);
            if (request->type == UREQUEST_SINK_LATENCY)
                return urequest_provide_sink_latency(request,
                    (uint64_t)NB_SAMPLES * 3 * UCLOCK_FREQ / SAMPLE_RATE);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_nacl_audio_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_nacl_audio_control(struct upipe *upipe,
                                    int command, va_list args)
{
    UBASE_RETURN(_upipe_nacl_audio_control(upipe, command, args));

    struct upipe_nacl_audio *upipe_nacl_audio =
        upipe_nacl_audio_from_upipe(upipe);
    if (unlikely(!upipe_nacl_audio_check_input(upipe))) {
        upipe_nacl_audio_check_watcher(upipe);
        upump_start(upipe_nacl_audio->upump);
    }

    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_nacl_audio_free(struct upipe *upipe)
{
    struct upipe_nacl_audio *upipe_nacl_audio =
        upipe_nacl_audio_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_nacl_audio->audio_interface->StopPlayback(upipe_nacl_audio->audio);
    upipe_nacl_audio->core_interface->ReleaseResource(upipe_nacl_audio->audio);
    upipe_nacl_audio->core_interface->ReleaseResource(upipe_nacl_audio->audio_config);

    struct uref *uref;
    while ((uref = uqueue_pop(&upipe_nacl_audio->uqueue,
                              struct uref *)) != NULL)
        uref_free(uref);

    upipe_nacl_audio_clean_uclock(upipe);
    upipe_nacl_audio_clean_upump_mgr(upipe);
    upipe_nacl_audio_clean_upump(upipe);
    uqueue_clean(&upipe_nacl_audio->uqueue);
    upipe_nacl_audio_clean_urefcount(upipe);
    free(upipe_nacl_audio);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_nacl_audio_mgr = {
    .refcount = NULL,
    .signature = UPIPE_NACL_AUDIO_SIGNATURE,

    .upipe_alloc = upipe_nacl_audio_alloc,
    .upipe_input = upipe_nacl_audio_input,
    .upipe_control = upipe_nacl_audio_control,
    .upipe_mgr_control = NULL
};

/** @This returns the management structure for sound pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_nacl_audio_mgr_alloc(void)
{
    return &upipe_nacl_audio_mgr;
}
    
