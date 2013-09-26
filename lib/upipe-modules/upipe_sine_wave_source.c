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
 * @short Upipe source module generating a sine wave
 * This module is particularly helpful to test sound sinks.
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_source_read_size.h>
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

/** @internal @This is the private context of a sine wave source pipe. */
struct upipe_sinesrc {
    /** uref manager */
    struct uref_mgr *uref_mgr;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;

    /** PTS of the next uref */
    uint64_t next_pts;
    /** phase of the sine wave */
    double phase;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_sinesrc, upipe, UPIPE_SINESRC_SIGNATURE)
UPIPE_HELPER_VOID(upipe_sinesrc)
UPIPE_HELPER_UREF_MGR(upipe_sinesrc, uref_mgr)

UPIPE_HELPER_UBUF_MGR(upipe_sinesrc, ubuf_mgr)
UPIPE_HELPER_OUTPUT(upipe_sinesrc, output, flow_def, flow_def_sent)

UPIPE_HELPER_UPUMP_MGR(upipe_sinesrc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_sinesrc, upump, upump_mgr)
UPIPE_HELPER_UCLOCK(upipe_sinesrc, uclock)

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
static void upipe_sinesrc_timer(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_sinesrc *upipe_sinesrc = upipe_sinesrc_from_upipe(upipe);
    if (unlikely(upipe_sinesrc->uclock != NULL &&
                 upipe_sinesrc->next_pts == UINT64_MAX))
        upipe_sinesrc->next_pts = uclock_now(upipe_sinesrc->uclock) +
                                  UPIPE_SINESRC_DELAY;

    size_t size = (uint64_t)UPIPE_SINESRC_DURATION * 2 * UPIPE_SINESRC_RATE /
                  UCLOCK_FREQ;
    struct uref *uref = uref_block_alloc(upipe_sinesrc->uref_mgr,
                                         upipe_sinesrc->ubuf_mgr, size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    uint8_t *buffer;
    int read_size = -1;
    if (unlikely(!uref_block_write(uref, 0, &read_size, &buffer))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }
    assert(read_size == size);

    double phase = upipe_sinesrc->phase;
    double step = max_phase * UPIPE_SINESRC_FREQ / (double)UPIPE_SINESRC_RATE;
    /* fill the channel areas */
    while (size > 0) {
        int16_t res = sin(phase) * INT16_MAX;
        buffer[0] = (res) & 0xff;
        buffer[1] = (res >> 8) & 0xff;
        size -= 2;
        buffer += 2;
        phase += step;
        if (phase >= max_phase)
            phase -= max_phase;
    }
    upipe_sinesrc->phase = phase;

    uref_block_unmap(uref, 0);

    if (upipe_sinesrc->next_pts != UINT64_MAX) {
        uref_clock_set_pts_sys(uref, upipe_sinesrc->next_pts);
        uref_clock_set_dts_pts_delay(uref, 0);
        uref_clock_set_cr_dts_delay(uref, 0);
        upipe_sinesrc->next_pts += UPIPE_SINESRC_DURATION;
    }
    upipe_sinesrc_output(upipe, uref, upump);
}

/** @internal @This processes control commands on a sine wave source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_sinesrc_control(struct upipe *upipe,
                                   enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_sinesrc_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_sinesrc_set_uref_mgr(upipe, uref_mgr);
        }

        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_sinesrc_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_sinesrc_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_sinesrc_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sinesrc_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_sinesrc_set_output(upipe, output);
        }

        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_sinesrc_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            upipe_sinesrc_set_upump(upipe, NULL);
            return upipe_sinesrc_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_sinesrc_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            upipe_sinesrc_set_upump(upipe, NULL);
            return upipe_sinesrc_set_uclock(upipe, uclock);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on a sine wave source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_sinesrc_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    if (unlikely(!_upipe_sinesrc_control(upipe, command, args)))
        return false;

    struct upipe_sinesrc *upipe_sinesrc = upipe_sinesrc_from_upipe(upipe);
    if (upipe_sinesrc->uref_mgr != NULL && upipe_sinesrc->upump_mgr != NULL &&
        upipe_sinesrc->upump == NULL) {
        struct uref *flow_def =
            uref_sound_flow_alloc_def(upipe_sinesrc->uref_mgr, "pcm_s16le.",
                                      1, 2);
        if (flow_def == NULL) {
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return false;
        }
        uref_sound_flow_set_rate(flow_def, UPIPE_SINESRC_RATE);
        upipe_sinesrc_store_flow_def(upipe, flow_def);

        struct upump *upump = upump_alloc_idler(upipe_sinesrc->upump_mgr,
                                                upipe_sinesrc_timer, upipe);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UPROBE_ERR_UPUMP);
            return false;
        }
        upipe_sinesrc_set_upump(upipe, upump);
        upump_start(upump);
    }

    return true;
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
    upipe_sinesrc_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_sinesrc_mgr = {
    .signature = UPIPE_SINESRC_SIGNATURE,

    .upipe_alloc = upipe_sinesrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_sinesrc_control,
    .upipe_free = upipe_sinesrc_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all sine wave source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sinesrc_mgr_alloc(void)
{
    return &upipe_sinesrc_mgr;
}
