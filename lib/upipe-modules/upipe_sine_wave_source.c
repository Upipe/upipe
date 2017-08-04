/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe source module generating a sine wave
 * This module is particularly helpful to test sound sinks.
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-modules/upipe_sine_wave_source.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <assert.h>

/** @internal @This is the duration of an output buffer. */
#define UPIPE_SINESRC_DURATION (UCLOCK_FREQ / 10)
/** @internal @This is the delay between the current time and the PTS. */
#define UPIPE_SINESRC_DELAY (UCLOCK_FREQ / 10)
/** @internal @This is the output sample rate. */
#define UPIPE_SINESRC_RATE 48000
/** @internal @This is the frequency of the sound wave. */
#define UPIPE_SINESRC_FREQ 440 /* Hz */

/** @hidden */
static int upipe_sinesrc_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a sine wave source pipe. */
struct upipe_sinesrc {
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

    /** uclock */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** timer */
    struct upump *upump;

    /** PTS of the next uref */
    uint64_t next_pts;
    /** phase of the sine wave */
    double phase;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_sinesrc, upipe, UPIPE_SINESRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_sinesrc, urefcount, upipe_sinesrc_free)
UPIPE_HELPER_VOID(upipe_sinesrc)

UPIPE_HELPER_OUTPUT(upipe_sinesrc, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_sinesrc, uref_mgr, uref_mgr_request,
                      upipe_sinesrc_check,
                      upipe_sinesrc_register_output_request,
                      upipe_sinesrc_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_sinesrc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_sinesrc_check,
                      upipe_sinesrc_register_output_request,
                      upipe_sinesrc_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_sinesrc, uclock, uclock_request, upipe_sinesrc_check,
                    upipe_sinesrc_register_output_request,
                    upipe_sinesrc_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_sinesrc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_sinesrc, upump, upump_mgr)

static const double max_phase = 2. * M_PI;

/** @internal @This allocates a sine wave source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_sinesrc_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe, uint32_t signature,
                                      va_list args)
{
    struct upipe *upipe = upipe_sinesrc_alloc_void(mgr, uprobe, signature, args);
    struct upipe_sinesrc *upipe_sinesrc = upipe_sinesrc_from_upipe(upipe);
    upipe_sinesrc_init_urefcount(upipe);
    upipe_sinesrc_init_uref_mgr(upipe);
    upipe_sinesrc_init_ubuf_mgr(upipe);
    upipe_sinesrc_init_output(upipe);
    upipe_sinesrc_init_upump_mgr(upipe);
    upipe_sinesrc_init_upump(upipe);
    upipe_sinesrc_init_uclock(upipe);
    upipe_sinesrc->next_pts = UINT64_MAX;
    upipe_sinesrc->phase = 0.;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This creates sine wave data and outputs it.
 *
 * @param upump description structure of the timer
 */
static void upipe_sinesrc_idler(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_sinesrc *upipe_sinesrc = upipe_sinesrc_from_upipe(upipe);
    if (unlikely(upipe_sinesrc->uclock != NULL &&
                 upipe_sinesrc->next_pts == UINT64_MAX))
        upipe_sinesrc->next_pts = uclock_now(upipe_sinesrc->uclock) +
                                  UPIPE_SINESRC_DELAY;

    size_t size = (uint64_t)UPIPE_SINESRC_DURATION * UPIPE_SINESRC_RATE /
                  UCLOCK_FREQ;
    struct uref *uref = uref_sound_alloc(upipe_sinesrc->uref_mgr,
                                         upipe_sinesrc->ubuf_mgr, size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    int16_t *buffer;
    if (unlikely(!ubase_check(uref_sound_plane_write_int16_t(uref, "c", 0, -1,
                                                             &buffer)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    double phase = upipe_sinesrc->phase;
    double step = max_phase * UPIPE_SINESRC_FREQ / (double)UPIPE_SINESRC_RATE;
    /* fill the channel areas */
    while (size > 0) {
        *buffer = sin(phase) * INT16_MAX;
        size--;
        buffer++;
        phase += step;
        if (phase >= max_phase)
            phase -= max_phase;
    }
    upipe_sinesrc->phase = phase;

    uref_sound_plane_unmap(uref, "c", 0, -1);

    if (upipe_sinesrc->next_pts != UINT64_MAX) {
        uref_clock_set_pts_sys(uref, upipe_sinesrc->next_pts);
        upipe_sinesrc->next_pts += UPIPE_SINESRC_DURATION;
    }
    upipe_sinesrc_output(upipe, uref, &upipe_sinesrc->upump);
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_sinesrc_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_sinesrc *upipe_sinesrc = upipe_sinesrc_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_sinesrc_store_flow_def(upipe, flow_format);

    upipe_sinesrc_check_upump_mgr(upipe);
    if (upipe_sinesrc->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_sinesrc->uref_mgr == NULL) {
        upipe_sinesrc_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_sinesrc->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_sound_flow_alloc_def(upipe_sinesrc->uref_mgr, "s16.", 1, 2);
        if (flow_format == NULL) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        uref_sound_flow_add_plane(flow_format, "c");
        uref_sound_flow_set_rate(flow_format, UPIPE_SINESRC_RATE);
        upipe_sinesrc_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_sinesrc->flow_def == NULL)
        return UBASE_ERR_NONE;

    if (upipe_sinesrc->uclock == NULL &&
        urequest_get_opaque(&upipe_sinesrc->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    if (upipe_sinesrc->upump == NULL) {
        struct upump *upump = upump_alloc_idler(upipe_sinesrc->upump_mgr,
                                                upipe_sinesrc_idler, upipe,
                                                upipe->refcount);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_sinesrc_set_upump(upipe, upump);
        upump_start(upump);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a sine wave source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_sinesrc_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_sinesrc_set_upump(upipe, NULL);
            return upipe_sinesrc_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_sinesrc_set_upump(upipe, NULL);
            upipe_sinesrc_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_sinesrc_control_output(upipe, command, args);

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a sine wave source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_sinesrc_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_sinesrc_control(upipe, command, args));

    return upipe_sinesrc_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_sinesrc_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_sinesrc_clean_uclock(upipe);
    upipe_sinesrc_clean_upump(upipe);
    upipe_sinesrc_clean_upump_mgr(upipe);
    upipe_sinesrc_clean_output(upipe);
    upipe_sinesrc_clean_ubuf_mgr(upipe);
    upipe_sinesrc_clean_uref_mgr(upipe);
    upipe_sinesrc_clean_urefcount(upipe);
    upipe_sinesrc_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_sinesrc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SINESRC_SIGNATURE,

    .upipe_alloc = upipe_sinesrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_sinesrc_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all sine wave source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sinesrc_mgr_alloc(void)
{
    return &upipe_sinesrc_mgr;
}
