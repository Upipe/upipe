/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe sink module for alsa sound system
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upump.h>
#include <upipe/upump_blocker.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_sink.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-alsa/upipe_alsa_sink.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>
#include <alsa/asoundlib.h>

#ifndef O_CLOEXEC
#   define O_CLOEXEC 0
#endif

/** default device name */
#define DEFAULT_DEVICE "plughw:0,0"
/** default period duration */
#define DEFAULT_PERIOD_DURATION (UCLOCK_FREQ / 25)
/** tolerance on PTSs (plus or minus) */
#define PTS_TOLERANCE (UCLOCK_FREQ / 25)
/** we expect sound */
#define EXPECTED_FLOW_DEF "block."

/** @hidden */
static void upipe_alsink_timer(struct upump *upump);

/** @internal @This is the private context of a file sink pipe. */
struct upipe_alsink {
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *upump;
    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;

    /** input flow definition */
    struct uref *flow_def;
    /** sample rate */
    unsigned int rate;
    /** sample format */
    snd_pcm_format_t format;
    /** duration of a period */
    uint64_t period_duration;
    /** remainder of the number of frames to output per period */
    long long frames_remainder;

    /** device name */
    char *uri;
    /** ALSA handle */
    snd_pcm_t *handle;
    /** temporary uref storage */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** duration of temporary uref storage */
    uint64_t urefs_duration;
    /** list of blockers */
    struct uchain blockers;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_alsink, upipe, UPIPE_ALSINK_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_alsink, EXPECTED_FLOW_DEF)
UPIPE_HELPER_UPUMP_MGR(upipe_alsink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_alsink, upump, upump_mgr)
UPIPE_HELPER_SINK(upipe_alsink, urefs, nb_urefs, max_urefs, blockers, NULL)
UPIPE_HELPER_UCLOCK(upipe_alsink, uclock)

/** @internal @This allocates a file sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_alsink_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_alsink_alloc_flow(mgr, uprobe, signature, args,
                                                  &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    upipe_alsink_init_upump_mgr(upipe);
    upipe_alsink_init_upump(upipe);
    upipe_alsink_init_sink(upipe);
    upipe_alsink_init_uclock(upipe);
    upipe_alsink->uri = NULL;
    upipe_alsink->flow_def = flow_def;
    upipe_alsink->urefs_duration = 0;
    upipe_alsink->handle = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This is called to recover the ALSA device in case of error.
 *
 * @param upipe description structure of the pipe
 * @param err error returned by the device
 * @return false in case of error
 */
static bool upipe_alsink_recover(struct upipe *upipe, int err)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    upipe_warn_va(upipe, "recovering stream: %s", snd_strerror(err));
    int val = snd_pcm_recover(upipe_alsink->handle, err, 1);
    if (val < 0 && val != -EAGAIN) {
        upipe_err_va(upipe, "cannot recover playback stream: %s",
                     snd_strerror(val));
        upipe_throw_error(upipe, UPROBE_ERR_EXTERNAL);
        return false;
    }
    return true;
}

/** @internal @This is called to output data to alsa.
 *
 * @param upipe description structure of the pipe
 * @param buffer pointer to buffer
 * @param buffer_frames number of frames in buffer
 * @return the number of frames effectively written, or -1 in case of error
 */
static snd_pcm_sframes_t upipe_alsink_output(struct upipe *upipe,
                                             const uint8_t *buffer,
                                             snd_pcm_uframes_t buffer_frames)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    for ( ; ; ) {
        snd_pcm_sframes_t frames = snd_pcm_writei(upipe_alsink->handle,
                                                  buffer, buffer_frames);
        if (frames >= 0)
            return frames;
        if (frames == -EAGAIN) {
            upipe_warn_va(upipe, "ALSA FIFO full, skipping tick");
            return 0;
        }
        if (unlikely(!upipe_alsink_recover(upipe, frames)))
            return -1;
    }
}

/** @internal @This is called to output silence to alsa.
 *
 * @param upipe description structure of the pipe
 * @param silence_frames number of frames of silence to play
 * @return the number of frames effectively written, or -1 in case of error
 */
static snd_pcm_sframes_t upipe_alsink_silence(struct upipe *upipe,
                                              snd_pcm_uframes_t silence_frames)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    uint8_t buffer[snd_pcm_frames_to_bytes(upipe_alsink->handle,
                                           silence_frames)];
    snd_pcm_format_set_silence(upipe_alsink->format, buffer, silence_frames);
    return upipe_alsink_output(upipe, buffer, silence_frames);
}

/** @internal @This is called to consume frames out of the first buffered uref.
 *
 * @param upipe description structure of the pipe
 * @param frames number of frames to consume
 */
static void upipe_alsink_consume(struct upipe *upipe, snd_pcm_uframes_t frames)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    size_t bytes = snd_pcm_frames_to_bytes(upipe_alsink->handle, frames);
    struct uchain *uchain = ulist_peek(&upipe_alsink->urefs);
    struct uref *uref = uref_from_uchain(uchain);

    size_t uref_size;
    if (uref_block_size(uref, &uref_size) && uref_size <= bytes) {
        ulist_pop(&upipe_alsink->urefs);
        uref_free(uref);
        frames = snd_pcm_bytes_to_frames(upipe_alsink->handle, uref_size);
    } else {
        uref_block_resize(uref, bytes, -1);
        uint64_t pts;
        if (uref_clock_get_pts_sys(uref, &pts))
            uref_clock_set_pts_sys(uref,
                    pts + frames * UCLOCK_FREQ / upipe_alsink->rate);
        /* We should also change the duration but we don't use it. */
    }

    upipe_alsink->urefs_duration -= (uint64_t)frames * UCLOCK_FREQ /
                                    upipe_alsink->rate;
}

/** @internal @This is called when we need to output data to alsa.
 *
 * @param upump description structure of the watcher
 */
static void upipe_alsink_timer(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    uint64_t next_pts = UINT64_MAX;
    bool was_empty = upipe_alsink_check_sink(upipe);

    snd_pcm_state_t state = snd_pcm_state(upipe_alsink->handle);
    switch (state) {
        case SND_PCM_STATE_XRUN:
            if (unlikely(!upipe_alsink_recover(upipe, -EPIPE)))
                return;
            break;

        case SND_PCM_STATE_SUSPENDED:
            if (unlikely(!upipe_alsink_recover(upipe, -ESTRPIPE)))
                return;
            break;

        default:
            break;
    }

    if (likely(upipe_alsink->uclock != NULL)) {
        snd_pcm_sframes_t delay;
        int val;
        while ((val = snd_pcm_delay(upipe_alsink->handle, &delay)) < 0)
            if (unlikely(!upipe_alsink_recover(upipe, val)))
                return;

        /* This is slightly off if we're just starting the stream. */
        next_pts = uclock_now(upipe_alsink->uclock) +
                   (uint64_t)delay * UCLOCK_FREQ / upipe_alsink->rate;
    }

    lldiv_t d = lldiv(upipe_alsink->period_duration * upipe_alsink->rate +
                      upipe_alsink->frames_remainder, UCLOCK_FREQ);
    snd_pcm_uframes_t frames = d.quot;
    upipe_alsink->frames_remainder = d.rem;

    while (frames > 0) {
        struct uchain *uchain = ulist_peek(&upipe_alsink->urefs);
        snd_pcm_sframes_t result;
        if (unlikely(uchain == NULL)) {
            upipe_dbg_va(upipe, "playing %u frames of silence", frames);
            result = upipe_alsink_silence(upipe, frames);
            if (result <= 0)
                break;
            frames -= result;
            continue;
        }

        struct uref *uref = uref_from_uchain(uchain);
        if (next_pts != UINT64_MAX) {
            uint64_t uref_pts;
            if (unlikely(!uref_clock_get_pts_sys(uref, &uref_pts))) {
                ulist_pop(&upipe_alsink->urefs);
                uref_free(uref);
                upipe_warn(upipe, "non-dated uref received");
                continue;
            }

            int64_t tolerance = uref_pts - next_pts;
            if (tolerance > (int64_t)PTS_TOLERANCE) {
                snd_pcm_uframes_t silence_frames =
                    tolerance * upipe_alsink->rate / UCLOCK_FREQ;
                if (silence_frames > frames)
                    silence_frames = frames;
                upipe_dbg_va(upipe, "playing %u frames of silence",
                             silence_frames);
                result = upipe_alsink_silence(upipe, silence_frames);
                if (result <= 0)
                    break;
                frames -= result;
                continue;
            } else if (-tolerance > (int64_t)PTS_TOLERANCE) {
                snd_pcm_uframes_t dropped_frames =
                    (-tolerance) * upipe_alsink->rate / UCLOCK_FREQ;
                upipe_warn_va(upipe, "late buffer received, dropping %u frames",
                              dropped_frames);
                upipe_alsink_consume(upipe, dropped_frames);
                continue;
            }
        }

        int size = -1;
        const uint8_t *buffer;
        if (unlikely(!uref_block_read(uref, 0, &size, &buffer))) {
            ulist_pop(&upipe_alsink->urefs);
            uref_free(uref);
            upipe_warn(upipe, "cannot read ubuf buffer");
            continue;
        }

        long uref_frames = snd_pcm_bytes_to_frames(upipe_alsink->handle, size);
        result = upipe_alsink_output(upipe, buffer, uref_frames < frames ?
                                                    uref_frames : frames);
        uref_block_unmap(uref, 0);
        if (result <= 0)
            break;
        if (result > 0)
            upipe_alsink_consume(upipe, result);
        frames -= result;
    }

    if (upipe_alsink->urefs_duration < upipe_alsink->period_duration * 2)
        upipe_alsink_unblock_sink(upipe);
    if (!was_empty && upipe_alsink_check_sink(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_alsink_input. */
        upipe_release(upipe);
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_alsink_input(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    size_t uref_size;
    if (unlikely(!uref_block_size(uref, &uref_size))) {
        upipe_warn(upipe, "unable to read uref");
        uref_free(uref);
        return;
    }

    if (unlikely(upipe_alsink->handle == NULL)) {
        upipe_warn(upipe, "received a buffer before opening a device");
        uref_free(uref);
        return;
    }

    uint64_t uref_duration =
        (uint64_t)snd_pcm_bytes_to_frames(upipe_alsink->handle,
                                          uref_size) * UCLOCK_FREQ /
        upipe_alsink->rate;

    if (upipe_alsink_check_sink(upipe)) {
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }

    upipe_alsink_hold_sink(upipe, uref);
    upipe_alsink->urefs_duration += uref_duration;
    if (upipe_alsink->urefs_duration >= upipe_alsink->period_duration * 2)
        upipe_alsink_block_sink(upipe, upump);
}

/** @internal @This returns the name of the opened ALSA device.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the device name
 * @return false in case of error
 */
static bool upipe_alsink_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_alsink->uri;
    return true;
}

/** @internal @This asks to open the given ALSA device.
 *
 * @param upipe description structure of the pipe
 * @param uri name of the ALSA device
 * @return false in case of error
 */
static bool upipe_alsink_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);

    if (unlikely(upipe_alsink->handle != NULL)) {
        upipe_notice_va(upipe, "closing device %s", upipe_alsink->uri);
        snd_pcm_close(upipe_alsink->handle);
        upipe_alsink->handle = NULL;
    }
    free(upipe_alsink->uri);
    upipe_alsink->uri = NULL;
    upipe_alsink_set_upump(upipe, NULL);
    upipe_alsink_unblock_sink(upipe);
    if (!upipe_alsink_check_sink(upipe))
        /* Release the pipe used in @ref upipe_alsink_input. */
        upipe_release(upipe);

    if (unlikely(uri == NULL))
        return true;
    if (!strcmp(uri, "default"))
        uri = DEFAULT_DEVICE;

    if (upipe_alsink->upump_mgr == NULL) {
        upipe_throw_need_upump_mgr(upipe);
        if (unlikely(upipe_alsink->upump_mgr == NULL))
            return false;
    }

    if (snd_pcm_open(&upipe_alsink->handle, uri, SND_PCM_STREAM_PLAYBACK,
                     SND_PCM_NONBLOCK) < 0) {
        upipe_err_va(upipe, "can't open device %s", uri);
        return false;
    }
  
    snd_pcm_hw_params_t *hwparams;
    snd_pcm_hw_params_alloca(&hwparams);
    if (snd_pcm_hw_params_any(upipe_alsink->handle, hwparams) < 0) {
        upipe_err_va(upipe, "can't configure device %s", uri);
        goto set_uri_error;
    }

    if (snd_pcm_hw_params_set_rate_resample(upipe_alsink->handle, hwparams,
                                            1) < 0) {
        upipe_err_va(upipe, "can't set interleaved mode (%s)", uri);
        goto set_uri_error;
    }

    if (snd_pcm_hw_params_set_access(upipe_alsink->handle, hwparams,
                                     SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
        upipe_err_va(upipe, "can't set interleaved mode (%s)", uri);
        goto set_uri_error;
    }

    const char *def;
    if (unlikely(!uref_flow_get_def(upipe_alsink->flow_def, &def))) {
        assert(0);
        goto set_uri_error;
    }
    def += strlen(EXPECTED_FLOW_DEF);

    if (!ubase_ncmp(def, "pcm_s16le."))
        upipe_alsink->format = SND_PCM_FORMAT_S16_LE;
    else if (!ubase_ncmp(def, "pcm_s16be."))
        upipe_alsink->format = SND_PCM_FORMAT_S16_BE;
    else if (!ubase_ncmp(def, "pcm_u16le."))
        upipe_alsink->format = SND_PCM_FORMAT_U16_LE;
    else if (!ubase_ncmp(def, "pcm_u16be."))
        upipe_alsink->format = SND_PCM_FORMAT_U16_BE;
    else if (!ubase_ncmp(def, "pcm_s8."))
        upipe_alsink->format = SND_PCM_FORMAT_S8;
    else if (!ubase_ncmp(def, "pcm_u8."))
        upipe_alsink->format = SND_PCM_FORMAT_U8;
    else if (!ubase_ncmp(def, "pcm_mulaw."))
        upipe_alsink->format = SND_PCM_FORMAT_MU_LAW;
    else if (!ubase_ncmp(def, "pcm_alaw."))
        upipe_alsink->format = SND_PCM_FORMAT_A_LAW;
    else if (!ubase_ncmp(def, "pcm_s32le."))
        upipe_alsink->format = SND_PCM_FORMAT_S32_LE;
    else if (!ubase_ncmp(def, "pcm_s32be."))
        upipe_alsink->format = SND_PCM_FORMAT_S32_BE;
    else if (!ubase_ncmp(def, "pcm_u32le."))
        upipe_alsink->format = SND_PCM_FORMAT_U32_LE;
    else if (!ubase_ncmp(def, "pcm_u32be."))
        upipe_alsink->format = SND_PCM_FORMAT_U32_BE;
    else if (!ubase_ncmp(def, "pcm_s24le."))
        upipe_alsink->format = SND_PCM_FORMAT_S24_LE;
    else if (!ubase_ncmp(def, "pcm_s24be."))
        upipe_alsink->format = SND_PCM_FORMAT_S24_BE;
    else if (!ubase_ncmp(def, "pcm_u24le."))
        upipe_alsink->format = SND_PCM_FORMAT_U24_LE;
    else if (!ubase_ncmp(def, "pcm_u24be."))
        upipe_alsink->format = SND_PCM_FORMAT_U24_BE;
    else if (!ubase_ncmp(def, "pcm_f32le."))
        upipe_alsink->format = SND_PCM_FORMAT_FLOAT_LE;
    else if (!ubase_ncmp(def, "pcm_f32be."))
        upipe_alsink->format = SND_PCM_FORMAT_FLOAT_BE;
    else if (!ubase_ncmp(def, "pcm_f64le."))
        upipe_alsink->format = SND_PCM_FORMAT_FLOAT64_LE;
    else if (!ubase_ncmp(def, "pcm_f64be."))
        upipe_alsink->format = SND_PCM_FORMAT_FLOAT64_BE;
    else {
        upipe_err_va(upipe, "unknown format %s", def);
        goto set_uri_error;
    }
  
    if (snd_pcm_hw_params_set_format(upipe_alsink->handle, hwparams,
                                     upipe_alsink->format) < 0) {
        upipe_err_va(upipe, "device %s is not compatible with format %s",
                     uri, def);
        goto set_uri_error;
    }

    uint64_t rate;
    if (!uref_sound_flow_get_rate(upipe_alsink->flow_def, &rate) ||
        snd_pcm_hw_params_set_rate(upipe_alsink->handle, hwparams,
                                   rate, 0) < 0) {
        upipe_err_va(upipe, "error setting sample rate on device %s", uri);
        goto set_uri_error;
    }
    upipe_alsink->rate = rate;

    uint8_t channels;
    if (!uref_sound_flow_get_channels(upipe_alsink->flow_def, &channels) ||
        snd_pcm_hw_params_set_channels(upipe_alsink->handle, hwparams,
                                       channels) < 0) {
        upipe_err_va(upipe, "error setting channels on device %s", uri);
        goto set_uri_error;
    }

    upipe_alsink->frames_remainder = 0;
    snd_pcm_uframes_t frames_in_period = (uint64_t)DEFAULT_PERIOD_DURATION *
                                         upipe_alsink->rate / UCLOCK_FREQ;
    if (snd_pcm_hw_params_set_period_size_near(upipe_alsink->handle, hwparams,
                                               &frames_in_period, NULL) < 0) {
        upipe_err_va(upipe, "error setting period size on device %s", uri);
        goto set_uri_error;
    }
    upipe_alsink->period_duration = (uint64_t)frames_in_period *
                                    UCLOCK_FREQ / upipe_alsink->rate;

    snd_pcm_uframes_t buffer_size = frames_in_period * 4;
    if (snd_pcm_hw_params_set_buffer_size_min(upipe_alsink->handle, hwparams,
                                               &buffer_size) < 0) {
        upipe_err_va(upipe, "error setting buffer size on device %s", uri);
        goto set_uri_error;
    }

    /* Apply HW parameter settings to PCM device. */
    if (snd_pcm_hw_params(upipe_alsink->handle, hwparams) < 0) {
        upipe_err_va(upipe, "error configuring hw device %s", uri);
        goto set_uri_error;
    }

    snd_pcm_sw_params_t *swparams;

    snd_pcm_sw_params_alloca(&swparams);
    snd_pcm_sw_params_current(upipe_alsink->handle, swparams);

    /* Start when the buffer is full enough. */
    if (snd_pcm_sw_params_set_start_threshold(upipe_alsink->handle, swparams,
                                              frames_in_period * 2) < 0) {
        upipe_err_va(upipe, "error setting threshold on device %s", uri);
        goto set_uri_error;
    }

    /* Apply SW parameter settings to PCM device. */
    if (snd_pcm_sw_params(upipe_alsink->handle, swparams) < 0) {
        upipe_err_va(upipe, "error configuring sw device %s", uri);
        goto set_uri_error;
    }

    if (snd_pcm_prepare(upipe_alsink->handle) < 0) {
        upipe_err_va(upipe, "error preparing device %s", uri);
        goto set_uri_error;
    }

    struct upump *timer = upump_alloc_timer(upipe_alsink->upump_mgr,
                                            upipe_alsink_timer, upipe, 0,
                                            upipe_alsink->period_duration);
    if (unlikely(timer == NULL)) {
        upipe_err(upipe, "can't create timer");
        goto set_uri_error;
    }
    upipe_alsink_set_upump(upipe, timer);
    upump_start(timer);

    upipe_alsink->uri = strdup(uri);
    if (unlikely(upipe_alsink->uri == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        goto set_uri_error;
    }
    if (!upipe_alsink_check_sink(upipe))
        /* Use again the pipe that we previously released. */
        upipe_use(upipe);
    upipe_notice_va(upipe, "opened device %s", upipe_alsink->uri);
    return true;

set_uri_error:
    snd_pcm_close(upipe_alsink->handle);
    upipe_alsink->handle = NULL;
    return false;
}

/** @internal @This flushes all currently held buffers, and unblocks the
 * sources.
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool upipe_alsink_flush(struct upipe *upipe)
{
    if (upipe_alsink_flush_sink(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_alsink_input. */
        upipe_release(upipe);
        upipe_err(upipe, "release pouet");
    }
    return true;
}

/** @internal @This processes control commands on a file sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_alsink_control(struct upipe *upipe,
                                 enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_alsink_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            upipe_alsink_set_upump(upipe, NULL);
            return upipe_alsink_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_alsink_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            upipe_alsink_set_upump(upipe, NULL);
            return upipe_alsink_set_uclock(upipe, uclock);
        }
        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_alsink_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_alsink_set_uri(upipe, uri);
        }
        case UPIPE_SINK_FLUSH:
            return upipe_alsink_flush(upipe);
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_alsink_free(struct upipe *upipe)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    if (likely(upipe_alsink->handle != NULL)) {
        upipe_notice_va(upipe, "closing device %s", upipe_alsink->uri);
        snd_pcm_close(upipe_alsink->handle);
    }
    upipe_throw_dead(upipe);

    free(upipe_alsink->uri);
    upipe_alsink_clean_uclock(upipe);
    upipe_alsink_clean_upump(upipe);
    upipe_alsink_clean_upump_mgr(upipe);
    upipe_alsink_clean_sink(upipe);

    upipe_alsink_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_alsink_mgr = {
    .signature = UPIPE_ALSINK_SIGNATURE,

    .upipe_alloc = upipe_alsink_alloc,
    .upipe_input = upipe_alsink_input,
    .upipe_control = upipe_alsink_control,
    .upipe_free = upipe_alsink_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all file sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_alsink_mgr_alloc(void)
{
    return &upipe_alsink_mgr;
}
