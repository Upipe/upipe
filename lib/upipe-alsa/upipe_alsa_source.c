/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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
 * @short Upipe source module for alsa sound system
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
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe-alsa/upipe_alsa_source.h>

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

#define UBUF_POOL_DEPTH 2

/** default device name */
#define DEFAULT_DEVICE "plughw:0,0"
/** default period duration */
#define DEFAULT_PERIOD_DURATION (UCLOCK_FREQ / 25)

/** @hidden */
static int upipe_alsource_check(struct upipe *upipe, struct uref *flow_format);

/** @hidden */
static bool upipe_alsource_recover(struct upipe *upipe, int err);

/** @internal @This is the private context of an ALSA source pipe. */
struct upipe_alsource {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

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
    /** number of samples in a period */
    unsigned int period_samples;
    /** samples count */
    uint64_t samples_count;

    /** device name */
    char *uri;
    /** ALSA handle */
    snd_pcm_t *handle;
    /** poll fd **/
    struct pollfd pfd;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_alsource, upipe, UPIPE_ALSOURCE_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_alsource, urefcount, upipe_alsource_free)
UPIPE_HELPER_VOID(upipe_alsource)
UPIPE_HELPER_UPUMP_MGR(upipe_alsource, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_alsource, upump, upump_mgr)
UPIPE_HELPER_UCLOCK(upipe_alsource, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)
UPIPE_HELPER_OUTPUT(upipe_alsource, output, flow_def, output_state, request_list)

UPIPE_HELPER_UREF_MGR(upipe_alsource, uref_mgr, uref_mgr_request,
                      upipe_alsource_check,
                      upipe_alsource_register_output_request,
                      upipe_alsource_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_alsource, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_alsource_check,
                      upipe_alsource_register_output_request,
                      upipe_alsource_unregister_output_request)

/** @internal @This allocates an ALSA source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_alsource_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_alsource_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_alsource *upipe_alsource = upipe_alsource_from_upipe(upipe);
    upipe_alsource_init_urefcount(upipe);
    upipe_alsource_init_uref_mgr(upipe);
    upipe_alsource_init_ubuf_mgr(upipe);
    upipe_alsource_init_upump_mgr(upipe);
    upipe_alsource_init_upump(upipe);
    upipe_alsource_init_output(upipe);
    upipe_alsource_init_uclock(upipe);

    upipe_alsource->rate = 48000;
    upipe_alsource->channels = 2;
    upipe_alsource->format = SND_PCM_FORMAT_FLOAT;
    upipe_alsource->uri = strdup(DEFAULT_DEVICE);
    upipe_alsource->pfd.fd = -1;
    upipe_alsource->handle = NULL;

    upipe_throw_ready(upipe);
    return upipe;
}

static void upipe_alsource_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_alsource *upipe_alsource = upipe_alsource_from_upipe(upipe);
    struct uref *uref;
    float *pcm;
    uint64_t systime = 0; /* to keep gcc quiet */
    if (unlikely(upipe_alsource->uclock != NULL))
        systime = uclock_now(upipe_alsource->uclock);

    uref = uref_sound_alloc(upipe_alsource->uref_mgr, upipe_alsource->ubuf_mgr,
                            upipe_alsource->period_samples);

    uref_sound_plane_write_float(uref, "lr", 0, -1, &pcm);
    int err = snd_pcm_readi(upipe_alsource->handle, pcm, upipe_alsource->period_samples);
    uref_sound_plane_unmap(uref, "lr", 0, -1);

    if (err < 0){
        uref_free(uref);
        upipe_alsource_recover(upipe, err);
    }
    else {
        uref_clock_set_dts_pts_delay(uref, 0);
        uref_clock_set_cr_orig(uref, upipe_alsource->samples_count * UCLOCK_FREQ /
                                     upipe_alsource->rate);
        uref_clock_set_cr_prog(uref, upipe_alsource->samples_count * UCLOCK_FREQ /
                                     upipe_alsource->rate);
        uref_clock_set_pts_orig(uref, upipe_alsource->samples_count * UCLOCK_FREQ /
                                      upipe_alsource->rate);
        uref_clock_set_pts_prog(uref, upipe_alsource->samples_count * UCLOCK_FREQ /
                                      upipe_alsource->rate);

        if (unlikely(upipe_alsource->uclock != NULL))
            uref_clock_set_cr_sys(uref, systime);

        upipe_throw_clock_ref(upipe, uref, upipe_alsource->period_samples * UCLOCK_FREQ /
                                           upipe_alsource->rate, 0);
        upipe_throw_clock_ts(upipe, uref);

        upipe_alsource->samples_count += upipe_alsource->period_samples;

        upipe_use(upipe);
        upipe_alsource_output(upipe, uref, &upipe_alsource->upump);
        upipe_release(upipe);
    }

    return;
}

/** @internal @This opens the ALSA device.
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool upipe_alsource_open(struct upipe *upipe)
{
    struct upipe_alsource *upipe_alsource = upipe_alsource_from_upipe(upipe);

    if (unlikely(upipe_alsource->uri == NULL))
        return false;

    const char *uri = upipe_alsource->uri;
    if (!strcmp(uri, "default"))
        uri = DEFAULT_DEVICE;

    if (snd_pcm_open(&upipe_alsource->handle, uri, SND_PCM_STREAM_CAPTURE,
                     SND_PCM_NONBLOCK) < 0) {
        upipe_err_va(upipe, "can't open device %s", uri);
        return false;
    }

    snd_pcm_hw_params_t *hwparams;
    snd_pcm_hw_params_alloca(&hwparams);
    if (snd_pcm_hw_params_any(upipe_alsource->handle, hwparams) < 0) {
        upipe_err_va(upipe, "can't configure device %s", uri);
        goto open_error;
    }

    if (snd_pcm_hw_params_set_rate_resample(upipe_alsource->handle, hwparams,
                                            1) < 0) {
        upipe_err_va(upipe, "can't set interleaved mode (%s)", uri);
        goto open_error;
    }

    if (snd_pcm_hw_params_set_access(upipe_alsource->handle, hwparams,
                                     SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
        upipe_err_va(upipe, "can't set interleaved mode (%s)", uri);
        goto open_error;
    }

    if (snd_pcm_hw_params_set_format(upipe_alsource->handle, hwparams,
                                     upipe_alsource->format) < 0) {
        upipe_err_va(upipe, "device %s is not compatible with format", uri);
        goto open_error;
    }

    if (snd_pcm_hw_params_set_rate(upipe_alsource->handle, hwparams,
                                   upipe_alsource->rate, 0) < 0) {
        upipe_err_va(upipe, "error setting sample rate on device %s", uri);
        goto open_error;
    }

    if (snd_pcm_hw_params_set_channels(upipe_alsource->handle, hwparams,
                                       upipe_alsource->channels) < 0) {
        upipe_err_va(upipe, "error setting channels on device %s", uri);
        goto open_error;
    }

    snd_pcm_uframes_t frames_in_period = (uint64_t)DEFAULT_PERIOD_DURATION *
                                         upipe_alsource->rate / UCLOCK_FREQ;
    if (snd_pcm_hw_params_set_period_size_near(upipe_alsource->handle, hwparams,
                                               &frames_in_period, NULL) < 0) {
        upipe_err_va(upipe, "error setting period size on device %s", uri);
        goto open_error;
    }
    upipe_alsource->period_samples = frames_in_period;

    snd_pcm_uframes_t buffer_size = frames_in_period * 4;
    if (snd_pcm_hw_params_set_buffer_size_min(upipe_alsource->handle, hwparams,
                                              &buffer_size) < 0) {
        upipe_err_va(upipe, "error setting buffer size on device %s", uri);
        goto open_error;
    }

    /* Apply HW parameter settings to PCM device. */
    if (snd_pcm_hw_params(upipe_alsource->handle, hwparams) < 0) {
        upipe_err_va(upipe, "error configuring hw device %s", uri);
        goto open_error;
    }

    snd_pcm_sw_params_t *swparams;

    snd_pcm_sw_params_alloca(&swparams);
    snd_pcm_sw_params_current(upipe_alsource->handle, swparams);

    /* Start when the buffer is full enough. */
    if (snd_pcm_sw_params_set_start_threshold(upipe_alsource->handle, swparams,
                                              frames_in_period * 2) < 0) {
        upipe_err_va(upipe, "error setting threshold on device %s", uri);
        goto open_error;
    }

    /* Apply SW parameter settings to PCM device. */
    if (snd_pcm_sw_params(upipe_alsource->handle, swparams) < 0) {
        upipe_err_va(upipe, "error configuring sw device %s", uri);
        goto open_error;
    }

    if (snd_pcm_prepare(upipe_alsource->handle) < 0) {
        upipe_err_va(upipe, "error preparing device %s", uri);
        goto open_error;
    }

    int cnt = snd_pcm_poll_descriptors_count(upipe_alsource->handle);
    if (cnt > 1) {
        /* FIXME */
        upipe_err_va(upipe, "error: too many file descriptors");
        return UBASE_ERR_EXTERNAL;
    }

    snd_pcm_poll_descriptors(upipe_alsource->handle, &upipe_alsource->pfd, cnt);

    snd_pcm_start(upipe_alsource->handle);

    upipe_notice_va(upipe, "opened device %s", uri);
    return true;

open_error:
    snd_pcm_close(upipe_alsource->handle);
    upipe_alsource->handle = NULL;
    return false;
}

/** @internal @This closes the ALSA device.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_alsource_close(struct upipe *upipe)
{
    struct upipe_alsource *upipe_alsource = upipe_alsource_from_upipe(upipe);

    if (unlikely(upipe_alsource->handle == NULL))
        return;

    upipe_notice_va(upipe, "closing device %s", upipe_alsource->uri);
    snd_pcm_close(upipe_alsource->handle);
    upipe_alsource->handle = NULL;

    upipe_alsource_set_upump(upipe, NULL);
}

/** @internal @This is called to recover the ALSA device in case of error.
 *
 * @param upipe description structure of the pipe
 * @param err error returned by the device
 * @return false in case of error
 */
static bool upipe_alsource_recover(struct upipe *upipe, int err)
{
    struct upipe_alsource *upipe_alsource = upipe_alsource_from_upipe(upipe);
    upipe_warn_va(upipe, "recovering stream: %s", snd_strerror(err));
    int val = snd_pcm_recover(upipe_alsource->handle, err, 1);
    if (val < 0 && val != -EAGAIN) {
        upipe_err_va(upipe, "cannot recover capture stream: %s",
                     snd_strerror(val));
        upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
        return false;
    }
    return true;
}

/** @internal @This returns the name of the opened ALSA device.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the device name
 * @return an error code
 */
static int upipe_alsource_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_alsource *upipe_alsource = upipe_alsource_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_alsource->uri;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given ALSA device.
 *
 * @param upipe description structure of the pipe
 * @param uri name of the ALSA device
 * @return an error code
 */
static int upipe_alsource_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_alsource *upipe_alsource = upipe_alsource_from_upipe(upipe);

    upipe_alsource_close(upipe);
    ubase_clean_str(&upipe_alsource->uri);
    upipe_alsource_set_upump(upipe, NULL);

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    upipe_alsource->uri = strdup(uri);
    if (unlikely(upipe_alsource->uri == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_alsource_open(upipe);

    return UBASE_ERR_NONE;
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_alsource_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_alsource *upipe_alsource = upipe_alsource_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_alsource_store_flow_def(upipe, flow_format);

    upipe_alsource_check_upump_mgr(upipe);
    if (upipe_alsource->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_alsource->uref_mgr == NULL) {
        upipe_alsource_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_alsource->ubuf_mgr == NULL) {
        struct uref *flow_format = uref_sound_flow_alloc_def(upipe_alsource->uref_mgr, "f32.",
                                             2, 4*2);
        uref_sound_flow_add_plane(flow_format, "lr");
        uref_sound_flow_set_rate(flow_format, 48000);
        uref_sound_flow_set_align(flow_format, 32);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_alsource_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_alsource->uclock == NULL &&
        urequest_get_opaque(&upipe_alsource->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    if (upipe_alsource->pfd.fd != -1 && upipe_alsource->upump == NULL) {
        struct upump *upump;
        upump = upump_alloc_fd_read (upipe_alsource->upump_mgr,
                                     upipe_alsource_worker, upipe,
                                     upipe_alsource->pfd.fd);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_alsource_set_upump(upipe, upump);
        upump_start(upump);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an ALSA source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_alsource_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_alsource_set_upump(upipe, NULL);
            return upipe_alsource_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_alsource_set_upump(upipe, NULL);
            upipe_alsource_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_alsource_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_alsource_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_alsource_set_output(upipe, output);
        }

        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_alsource_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_alsource_set_uri(upipe, uri);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a udp socket source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_alsource_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_alsource_control(upipe, command, args));

    return upipe_alsource_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_alsource_free(struct upipe *upipe)
{
    struct upipe_alsource *upipe_alsource = upipe_alsource_from_upipe(upipe);
    upipe_alsource_close(upipe);
    upipe_throw_dead(upipe);

    free(upipe_alsource->uri);
    upipe_alsource_clean_uclock(upipe);
    upipe_alsource_clean_upump(upipe);
    upipe_alsource_clean_upump_mgr(upipe);
    upipe_alsource_clean_output(upipe);
    upipe_alsource_clean_ubuf_mgr(upipe);
    upipe_alsource_clean_uref_mgr(upipe);
    upipe_alsource_clean_urefcount(upipe);
    upipe_alsource_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_alsource_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ALSOURCE_SIGNATURE,

    .upipe_alloc = upipe_alsource_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_alsource_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ALSA source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_alsource_mgr_alloc(void)
{
    return &upipe_alsource_mgr;
}
