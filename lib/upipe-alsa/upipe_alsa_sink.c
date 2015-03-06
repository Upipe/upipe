/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
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
#include <upipe/uref_clock.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upump.h>
#include <upipe/upump_blocker.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_input.h>
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
/** number of periods to buffer */
#define BUFFER_PERIODS 2
/** max number of urefs to buffer */
#define BUFFER_UREFS 5
/** we expect sound */
#define EXPECTED_FLOW_DEF "sound."

/** @hidden */
static void upipe_alsink_timer(struct upump *upump);

/** @internal element of a list of urequests */
struct upipe_alsink_request {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** pointer to upstream request */
    struct urequest *upstream;
};

UBASE_FROM_TO(upipe_alsink_request, uchain, uchain, uchain)

/** @internal @This is the private context of a file sink pipe. */
struct upipe_alsink {
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

    /** sample rate */
    unsigned int rate;
    /** number of channels */
    unsigned int channels;
    /** sample format */
    snd_pcm_format_t format;
    /** number of planes (1 for planar formats) */
    uint8_t planes;
    /** duration of a period */
    uint64_t period_duration;
    /** remainder of the number of frames to output per period */
    long long frames_remainder;

    /** delay applied to system clock ref when uclock is provided */
    uint64_t latency;
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
    /** list of blockers */
    struct uchain blockers;

    /** list of sink_latency urequests */
    struct uchain urequests;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_alsink, upipe, UPIPE_ALSINK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_alsink, urefcount, upipe_alsink_free)
UPIPE_HELPER_VOID(upipe_alsink)
UPIPE_HELPER_UPUMP_MGR(upipe_alsink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_alsink, upump, upump_mgr)
UPIPE_HELPER_INPUT(upipe_alsink, urefs, nb_urefs, max_urefs, blockers, NULL)
UPIPE_HELPER_UCLOCK(upipe_alsink, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

/** @internal @This returns the ALSA format, computed from flow definition.
 *
 * @param flow_def flow definition packet
 * @return ALSA format, or SND_PCM_FORMAT_UNKNOWN in case of error
 */
static snd_pcm_format_t upipe_alsink_format_from_flow_def(struct uref *flow_def)
{
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return SND_PCM_FORMAT_UNKNOWN;

    def += strlen(EXPECTED_FLOW_DEF);
    snd_pcm_format_t format;
    if (!ubase_ncmp(def, "s16le."))
        format = SND_PCM_FORMAT_S16_LE;
    else if (!ubase_ncmp(def, "s16be."))
        format = SND_PCM_FORMAT_S16_BE;
    else if (!ubase_ncmp(def, "s16."))
#ifdef UPIPE_WORDS_BIGENDIAN
        format = SND_PCM_FORMAT_S16_BE;
#else
        format = SND_PCM_FORMAT_S16_LE;
#endif
    else if (!ubase_ncmp(def, "u16le."))
        format = SND_PCM_FORMAT_U16_LE;
    else if (!ubase_ncmp(def, "u16be."))
        format = SND_PCM_FORMAT_U16_BE;
    else if (!ubase_ncmp(def, "u16."))
#ifdef UPIPE_WORDS_BIGENDIAN
        format = SND_PCM_FORMAT_U16_BE;
#else
        format = SND_PCM_FORMAT_U16_LE;
#endif
    else if (!ubase_ncmp(def, "s8."))
        format = SND_PCM_FORMAT_S8;
    else if (!ubase_ncmp(def, "u8."))
        format = SND_PCM_FORMAT_U8;
    else if (!ubase_ncmp(def, "mulaw."))
        format = SND_PCM_FORMAT_MU_LAW;
    else if (!ubase_ncmp(def, "alaw."))
        format = SND_PCM_FORMAT_A_LAW;
    else if (!ubase_ncmp(def, "s32le."))
        format = SND_PCM_FORMAT_S32_LE;
    else if (!ubase_ncmp(def, "s32be."))
        format = SND_PCM_FORMAT_S32_BE;
    else if (!ubase_ncmp(def, "s32."))
#ifdef UPIPE_WORDS_BIGENDIAN
        format = SND_PCM_FORMAT_S32_BE;
#else
        format = SND_PCM_FORMAT_S32_LE;
#endif
    else if (!ubase_ncmp(def, "u32le."))
        format = SND_PCM_FORMAT_U32_LE;
    else if (!ubase_ncmp(def, "u32be."))
        format = SND_PCM_FORMAT_U32_BE;
    else if (!ubase_ncmp(def, "u32."))
#ifdef UPIPE_WORDS_BIGENDIAN
        format = SND_PCM_FORMAT_U32_BE;
#else
        format = SND_PCM_FORMAT_U32_LE;
#endif
    else if (!ubase_ncmp(def, "s24le."))
        format = SND_PCM_FORMAT_S24_LE;
    else if (!ubase_ncmp(def, "s24be."))
        format = SND_PCM_FORMAT_S24_BE;
    else if (!ubase_ncmp(def, "s24."))
#ifdef UPIPE_WORDS_BIGENDIAN
        format = SND_PCM_FORMAT_S24_BE;
#else
        format = SND_PCM_FORMAT_S24_LE;
#endif
    else if (!ubase_ncmp(def, "u24le."))
        format = SND_PCM_FORMAT_U24_LE;
    else if (!ubase_ncmp(def, "u24be."))
        format = SND_PCM_FORMAT_U24_BE;
    else if (!ubase_ncmp(def, "u24."))
#ifdef UPIPE_WORDS_BIGENDIAN
        format = SND_PCM_FORMAT_U24_BE;
#else
        format = SND_PCM_FORMAT_U24_LE;
#endif
    else if (!ubase_ncmp(def, "f32le."))
        format = SND_PCM_FORMAT_FLOAT_LE;
    else if (!ubase_ncmp(def, "f32be."))
        format = SND_PCM_FORMAT_FLOAT_BE;
    else if (!ubase_ncmp(def, "f32."))
#ifdef UPIPE_WORDS_BIGENDIAN
        format = SND_PCM_FORMAT_FLOAT_BE;
#else
        format = SND_PCM_FORMAT_FLOAT_LE;
#endif
    else if (!ubase_ncmp(def, "f64le."))
        format = SND_PCM_FORMAT_FLOAT64_LE;
    else if (!ubase_ncmp(def, "f64be."))
        format = SND_PCM_FORMAT_FLOAT64_BE;
    else if (!ubase_ncmp(def, "f64."))
#ifdef UPIPE_WORDS_BIGENDIAN
        format = SND_PCM_FORMAT_FLOAT64_BE;
#else
        format = SND_PCM_FORMAT_FLOAT64_LE;
#endif
    else
        format = SND_PCM_FORMAT_UNKNOWN;

    return format;
}

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
    struct upipe *upipe = upipe_alsink_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    upipe_alsink_init_urefcount(upipe);
    upipe_alsink_init_upump_mgr(upipe);
    upipe_alsink_init_upump(upipe);
    upipe_alsink_init_input(upipe);
    upipe_alsink_init_uclock(upipe);
    upipe_alsink->latency = 0;
    upipe_alsink->rate = 0;
    upipe_alsink->uri = strdup(DEFAULT_DEVICE);
    upipe_alsink->handle = NULL;
    upipe_alsink->max_urefs = BUFFER_UREFS;
    ulist_init(&upipe_alsink->urequests);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This updates the sink latency.
 *
 * @param upipe description structure of the pipe
 * @param latency new latency
 * @return an error code
 */
static int upipe_alsink_update_latency(struct upipe *upipe, uint64_t latency)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_alsink->urequests, uchain, uchain_tmp) {
        struct upipe_alsink_request *proxy =
            upipe_alsink_request_from_uchain(uchain);
        urequest_provide_sink_latency(proxy->upstream, latency);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This opens the ALSA device.
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool upipe_alsink_open(struct upipe *upipe)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);

    if (unlikely(upipe_alsink->uri == NULL))
        return false;

    const char *uri = upipe_alsink->uri;
    if (!strcmp(uri, "default"))
        uri = DEFAULT_DEVICE;

    if (unlikely(!ubase_check(upipe_alsink_check_upump_mgr(upipe))))
        return false;

    if (snd_pcm_open(&upipe_alsink->handle, uri, SND_PCM_STREAM_PLAYBACK,
                     SND_PCM_NONBLOCK) < 0) {
        upipe_err_va(upipe, "can't open device %s", uri);
        return false;
    }

    snd_pcm_hw_params_t *hwparams;
    snd_pcm_hw_params_alloca(&hwparams);
    if (snd_pcm_hw_params_any(upipe_alsink->handle, hwparams) < 0) {
        upipe_err_va(upipe, "can't configure device %s", uri);
        goto open_error;
    }

    if (snd_pcm_hw_params_set_rate_resample(upipe_alsink->handle, hwparams,
                                            1) < 0) {
        upipe_err_va(upipe, "can't set interleaved mode (%s)", uri);
        goto open_error;
    }

    if (snd_pcm_hw_params_set_access(upipe_alsink->handle, hwparams,
                                     upipe_alsink->planes == 1 ?
                                     SND_PCM_ACCESS_RW_INTERLEAVED :
                                     SND_PCM_ACCESS_RW_NONINTERLEAVED) < 0) {
        upipe_err_va(upipe, "can't set interleaved mode (%s)", uri);
        goto open_error;
    }

    if (snd_pcm_hw_params_set_format(upipe_alsink->handle, hwparams,
                                     upipe_alsink->format) < 0) {
        upipe_err_va(upipe, "device %s is not compatible with format", uri);
        goto open_error;
    }

    if (snd_pcm_hw_params_set_rate(upipe_alsink->handle, hwparams,
                                   upipe_alsink->rate, 0) < 0) {
        upipe_err_va(upipe, "error setting sample rate on device %s", uri);
        goto open_error;
    }

    if (snd_pcm_hw_params_set_channels(upipe_alsink->handle, hwparams,
                                       upipe_alsink->channels) < 0) {
        upipe_err_va(upipe, "error setting channels on device %s", uri);
        goto open_error;
    }

    upipe_alsink->frames_remainder = 0;
    snd_pcm_uframes_t frames_in_period = (uint64_t)DEFAULT_PERIOD_DURATION *
                                         upipe_alsink->rate / UCLOCK_FREQ;
    if (snd_pcm_hw_params_set_period_size_near(upipe_alsink->handle, hwparams,
                                               &frames_in_period, NULL) < 0) {
        upipe_err_va(upipe, "error setting period size on device %s", uri);
        goto open_error;
    }
    upipe_alsink->period_duration = (uint64_t)frames_in_period *
                                    UCLOCK_FREQ / upipe_alsink->rate;

    snd_pcm_uframes_t buffer_size = frames_in_period * 4;
    if (snd_pcm_hw_params_set_buffer_size_min(upipe_alsink->handle, hwparams,
                                              &buffer_size) < 0) {
        upipe_err_va(upipe, "error setting buffer size on device %s", uri);
        goto open_error;
    }

    upipe_alsink_update_latency(upipe, upipe_alsink->period_duration * 3);

    /* Apply HW parameter settings to PCM device. */
    if (snd_pcm_hw_params(upipe_alsink->handle, hwparams) < 0) {
        upipe_err_va(upipe, "error configuring hw device %s", uri);
        goto open_error;
    }

    snd_pcm_sw_params_t *swparams;

    snd_pcm_sw_params_alloca(&swparams);
    snd_pcm_sw_params_current(upipe_alsink->handle, swparams);

    /* Start when the buffer is full enough. */
    if (snd_pcm_sw_params_set_start_threshold(upipe_alsink->handle, swparams,
                                              frames_in_period * 2) < 0) {
        upipe_err_va(upipe, "error setting threshold on device %s", uri);
        goto open_error;
    }

    /* Apply SW parameter settings to PCM device. */
    if (snd_pcm_sw_params(upipe_alsink->handle, swparams) < 0) {
        upipe_err_va(upipe, "error configuring sw device %s", uri);
        goto open_error;
    }

    if (snd_pcm_prepare(upipe_alsink->handle) < 0) {
        upipe_err_va(upipe, "error preparing device %s", uri);
        goto open_error;
    }

    struct upump *timer = upump_alloc_timer(upipe_alsink->upump_mgr,
                                            upipe_alsink_timer, upipe, 0,
                                            upipe_alsink->period_duration);
    if (unlikely(timer == NULL)) {
        upipe_err(upipe, "can't create timer");
        goto open_error;
    }
    upipe_alsink_set_upump(upipe, timer);
    upump_start(timer);

    if (!upipe_alsink_check_input(upipe))
        upipe_use(upipe);
    upipe_notice_va(upipe, "opened device %s", uri);
    return true;

open_error:
    snd_pcm_close(upipe_alsink->handle);
    upipe_alsink->handle = NULL;
    return false;
}

/** @internal @This closes the ALSA device.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_alsink_close(struct upipe *upipe)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);

    if (unlikely(upipe_alsink->handle == NULL))
        return;

    upipe_notice_va(upipe, "closing device %s", upipe_alsink->uri);
    snd_pcm_close(upipe_alsink->handle);
    upipe_alsink->handle = NULL;

    upipe_alsink_set_upump(upipe, NULL);
    upipe_alsink_unblock_input(upipe);
    if (upipe_alsink_check_input(upipe))
        /* Release the pipe used in @ref upipe_alsink_input. */
        upipe_release(upipe);
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
        upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
        return false;
    }
    return true;
}

/** @internal @This is called to output raw data to alsa.
 *
 * @param upipe description structure of the pipe
 * @param buffers pointer to array of buffers
 * @param buffer_frames number of frames in buffer
 * @return the number of frames effectively written, or -1 in case of error
 */
static snd_pcm_sframes_t upipe_alsink_output_frames(struct upipe *upipe,
        const void **buffers, snd_pcm_uframes_t buffer_frames)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    snd_pcm_sframes_t frames;
    for ( ; ; ) {
        if (upipe_alsink->planes == 1)
            frames = snd_pcm_writei(upipe_alsink->handle, buffers[0],
                                    buffer_frames);
        else /* ALSA doesn't know about const */
            frames = snd_pcm_writen(upipe_alsink->handle, (void **)buffers,
                                    buffer_frames);
        if (frames >= 0)
            break;
        if (frames == -EAGAIN) {
            upipe_warn_va(upipe, "ALSA FIFO full, skipping tick");
            frames = 0;
            break;
        }
        if (unlikely(!upipe_alsink_recover(upipe, frames))) {
            frames = -1;
            break;
        }
    }
    return frames;
}

/** @internal @This is called to output data to alsa.
 *
 * @param upipe description structure of the pipe
 * @param uref pointer to uref
 * @param buffer_frames number of frames in buffer
 * @return the number of frames effectively written, or -1 in case of error
 */
static snd_pcm_sframes_t upipe_alsink_output(struct upipe *upipe,
                                             struct uref *uref,
                                             snd_pcm_uframes_t buffer_frames)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    /* FIXME fix planes order */
    const void *buffers[upipe_alsink->planes];
    if (unlikely(!ubase_check(uref_sound_read_void(uref, 0, -1, buffers,
                                                   upipe_alsink->planes))))
        return -1;

    snd_pcm_sframes_t frames = upipe_alsink_output_frames(upipe, buffers,
                                                          buffer_frames);
    uref_sound_unmap(uref, 0, -1, upipe_alsink->planes);
    return frames;
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
    if (upipe_alsink->planes == 1) {
        uint8_t buffer[snd_pcm_frames_to_bytes(upipe_alsink->handle,
                                               silence_frames)];
        snd_pcm_format_set_silence(upipe_alsink->format, buffer,
                                   silence_frames * upipe_alsink->channels);
        uint8_t *buffers[1];
        buffers[0] = buffer;
        return upipe_alsink_output_frames(upipe, (const void **)buffers,
                                          silence_frames);
    }

    uint8_t buffer[snd_pcm_samples_to_bytes(upipe_alsink->handle,
                                            silence_frames)];
    snd_pcm_format_set_silence(upipe_alsink->format, buffer, silence_frames);
    uint8_t *buffers[upipe_alsink->planes];
    int i;
    for (i = 0; i < upipe_alsink->planes; i++)
        buffers[i] = buffer;
    return upipe_alsink_output_frames(upipe, (const void **)buffers,
                                      silence_frames);
}

/** @internal @This is called to consume frames out of the first buffered uref.
 *
 * @param upipe description structure of the pipe
 * @param frames number of frames to consume
 */
static void upipe_alsink_consume(struct upipe *upipe, snd_pcm_uframes_t frames)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    struct uchain *uchain = ulist_peek(&upipe_alsink->urefs);
    struct uref *uref = uref_from_uchain(uchain);

    size_t uref_size;
    if (ubase_check(uref_sound_size(uref, &uref_size, NULL)) &&
        uref_size <= frames) {
        upipe_alsink_pop_input(upipe);
        uref_free(uref);
        frames = uref_size;
    } else {
        uref_sound_resize(uref, frames, -1);
        uint64_t pts;
        if (ubase_check(uref_clock_get_pts_sys(uref, &pts)))
            uref_clock_set_pts_sys(uref,
                    pts + frames * UCLOCK_FREQ / upipe_alsink->rate);
        /* We should also change the duration but we don't use it. */
    }
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
    bool was_empty = upipe_alsink_check_input(upipe);

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
            upipe_dbg_va(upipe, "playing %u frames of silence (empty)", frames);
            result = upipe_alsink_silence(upipe, frames);
            if (result <= 0)
                break;
            frames -= result;
            continue;
        }

        struct uref *uref = uref_from_uchain(uchain);
        if (next_pts != UINT64_MAX) {
            uint64_t uref_pts;
            if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &uref_pts)))) {
                upipe_alsink_pop_input(upipe);
                uref_free(uref);
                upipe_warn(upipe, "non-dated uref received");
                continue;
            }
            uref_pts += upipe_alsink->latency;

            int64_t tolerance = uref_pts - next_pts;
            if (tolerance > (int64_t)PTS_TOLERANCE) {
                snd_pcm_uframes_t silence_frames =
                    tolerance * upipe_alsink->rate / UCLOCK_FREQ;
                if (silence_frames > frames)
                    silence_frames = frames;
                upipe_dbg_va(upipe, "playing %u frames of silence (wait)",
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
            upipe_verbose_va(upipe, "playing from uref pts %"PRIu64,
                             uref_pts);
        }

        size_t size;
        if (unlikely(!ubase_check(uref_sound_size(uref, &size, NULL)))) {
            upipe_alsink_pop_input(upipe);
            uref_free(uref);
            upipe_warn(upipe, "cannot read ubuf buffer");
            continue;
        }

        result = upipe_alsink_output(upipe, uref, size < frames ?
                                                  size : frames);
        if (result <= 0)
            break;
        if (result > 0)
            upipe_alsink_consume(upipe, result);
        frames -= result;
    }

    upipe_alsink_unblock_input(upipe);
    if (!was_empty && upipe_alsink_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_alsink_input. */
        upipe_release(upipe);
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_alsink_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    size_t uref_size;
    if (unlikely(!ubase_check(uref_sound_size(uref, &uref_size, NULL)))) {
        upipe_warn(upipe, "unable to read uref");
        uref_free(uref);
        return;
    }

    if (unlikely(upipe_alsink->handle == NULL && !upipe_alsink_open(upipe))) {
        upipe_warn(upipe, "unable to open device");
        uref_free(uref);
        return;
    }

    if (upipe_alsink_check_input(upipe)) {
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }

    upipe_alsink_hold_input(upipe, uref);
    upipe_alsink_block_input(upipe, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the flow definition is not handled
 */
static int upipe_alsink_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    uint64_t rate;
    uint8_t channels;
    uint8_t planes;
    snd_pcm_format_t format = upipe_alsink_format_from_flow_def(flow_def);
    if (format == SND_PCM_FORMAT_UNKNOWN) {
        upipe_err(upipe, "unknown sound format");
        return UBASE_ERR_INVALID;
    }
    UBASE_RETURN(uref_sound_flow_get_rate(flow_def, &rate))
    UBASE_RETURN(uref_sound_flow_get_channels(flow_def, &channels))
    UBASE_RETURN(uref_sound_flow_get_planes(flow_def, &planes))

    if (upipe_alsink->handle != NULL) {
        if (format != upipe_alsink->format ||
            rate != upipe_alsink->rate ||
            channels != upipe_alsink->channels)
            return UBASE_ERR_INVALID;
    } else {
        upipe_alsink->rate = rate;
        upipe_alsink->channels = channels;
        upipe_alsink->format = format;
        upipe_alsink->planes = planes;
    }

    upipe_alsink->latency = 0;
    uref_clock_get_latency(flow_def, &upipe_alsink->latency);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the name of the opened ALSA device.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the device name
 * @return an error code
 */
static int upipe_alsink_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_alsink->uri;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given ALSA device.
 *
 * @param upipe description structure of the pipe
 * @param uri name of the ALSA device
 * @return an error code
 */
static int upipe_alsink_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);

    upipe_alsink_close(upipe);
    free(upipe_alsink->uri);
    upipe_alsink->uri = NULL;

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    upipe_alsink->uri = strdup(uri);
    if (unlikely(upipe_alsink->uri == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This flushes all currently held buffers, and unblocks the
 * sources.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_alsink_flush(struct upipe *upipe)
{
    if (upipe_alsink_flush_input(upipe))
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_alsink_input. */
        upipe_release(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This registers a urequest.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_alsink_register_request(struct upipe *upipe,
                                         struct urequest *request)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    if (request->type != UREQUEST_SINK_LATENCY)
        return upipe_throw_provide_request(upipe, request);

    struct upipe_alsink_request *proxy =
        malloc(sizeof(struct upipe_alsink_request));
    UBASE_ALLOC_RETURN(proxy)
    uchain_init(upipe_alsink_request_to_uchain(proxy));
    proxy->upstream = request;
    ulist_add(&upipe_alsink->urequests,
              upipe_alsink_request_to_uchain(proxy));

    return urequest_provide_sink_latency(proxy->upstream,
            upipe_alsink->handle != NULL ? upipe_alsink->period_duration * 3 :
                                           0);
}

/** @internal @This unregisters a urequest.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_alsink_unregister_request(struct upipe *upipe,
                                           struct urequest *request)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    if (request->type != UREQUEST_SINK_LATENCY)
        return UBASE_ERR_NONE;

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_alsink->urequests, uchain, uchain_tmp) {
        struct upipe_alsink_request *proxy =
            upipe_alsink_request_from_uchain(uchain);
        if (proxy->upstream == request) {
            ulist_delete(uchain);
            free(proxy);
            return UBASE_ERR_NONE;
        }
    }
    return UBASE_ERR_INVALID;
}

/** @internal @This processes control commands on a file sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_alsink_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_alsink_set_upump(upipe, NULL);
            return upipe_alsink_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_alsink_set_upump(upipe, NULL);
            upipe_alsink_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_alsink_register_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_alsink_unregister_request(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_alsink_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_alsink_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_alsink_set_uri(upipe, uri);
        }
        case UPIPE_FLUSH:
            return upipe_alsink_flush(upipe);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_alsink_free(struct upipe *upipe)
{
    struct upipe_alsink *upipe_alsink = upipe_alsink_from_upipe(upipe);
    upipe_alsink_close(upipe);
    upipe_throw_dead(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_alsink->urequests,
                          uchain, uchain_tmp) {
        struct upipe_alsink_request *proxy =
            upipe_alsink_request_from_uchain(uchain);
        ulist_delete(uchain);
        free(proxy);
    }

    free(upipe_alsink->uri);
    upipe_alsink_clean_uclock(upipe);
    upipe_alsink_clean_upump(upipe);
    upipe_alsink_clean_upump_mgr(upipe);
    upipe_alsink_clean_input(upipe);
    upipe_alsink_clean_urefcount(upipe);
    upipe_alsink_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_alsink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ALSINK_SIGNATURE,

    .upipe_alloc = upipe_alsink_alloc,
    .upipe_input = upipe_alsink_input,
    .upipe_control = upipe_alsink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all file sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_alsink_mgr_alloc(void)
{
    return &upipe_alsink_mgr;
}
