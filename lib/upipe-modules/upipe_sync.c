/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
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
 * @short Upipe sync module - synchronize streams for muxing
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe-modules/upipe_sync.h>

/** upipe_sync structure */
struct upipe_sync {
    /** refcount management structure */
    struct urefcount urefcount;

    /** subpipes */
    struct uchain subs;
    /** subpipes mgr */
    struct upipe_mgr sub_mgr;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    // TODO: only one video (master)
    uint64_t latency;
    uint64_t pts;

    /** linked list of buffered pics */
    struct uchain urefs;

    /* fps */
    struct urational fps;

    uint64_t ticks_per_frame;

    /** last picture output */
    struct uref *uref;

    struct uclock *uclock;
    struct urequest uclock_request;

    struct upump *upump;
    struct upump_mgr *upump_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_sync, upipe, UPIPE_SYNC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_sync, urefcount, upipe_sync_free)
UPIPE_HELPER_VOID(upipe_sync);

UPIPE_HELPER_UCLOCK(upipe_sync, uclock, uclock_request, NULL,
                    upipe_throw_provide_request, NULL);
UPIPE_HELPER_UPUMP(upipe_sync, upump, upump_mgr);
UPIPE_HELPER_UPUMP_MGR(upipe_sync, upump_mgr);
UPIPE_HELPER_OUTPUT(upipe_sync, output, flow_def, output_state, request_list);

/** upipe_sync_sub structure */
struct upipe_sync_sub {
    /** refcount management structure */
    struct urefcount urefcount;

    /** public upipe structure */
    struct upipe upipe;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** linked list of subpipes */
    struct uchain uchain;

    bool sound;

    /** linked list of buffered urefs */
    struct uchain urefs;

    /** buffered duration */
    uint64_t samples;

    /** ntsc */
    uint8_t frame_idx;

    /* samplerate */
    // TODO: subpics
    struct urational rate;
};

UPIPE_HELPER_UPIPE(upipe_sync_sub, upipe, UPIPE_SYNC_SUB_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_sync_sub, urefcount, upipe_sync_sub_free)
UPIPE_HELPER_VOID(upipe_sync_sub);
UPIPE_HELPER_OUTPUT(upipe_sync_sub, output, flow_def, output_state, request_list);

UPIPE_HELPER_SUBPIPE(upipe_sync, upipe_sync_sub, sub, sub_mgr, subs, uchain);

/** @internal @This allocates a sync_sub pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_sync_sub_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_sync_sub_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_sync_sub *upipe_sync_sub = upipe_sync_sub_from_upipe(upipe);
    ulist_init(&upipe_sync_sub->urefs);
    upipe_sync_sub->samples = 0;
    upipe_sync_sub->frame_idx = 0;
    upipe_sync_sub->sound = false;

    upipe_sync_sub_init_urefcount(upipe);
    upipe_sync_sub_init_output(upipe);
    upipe_sync_sub_init_sub(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_sync_sub_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_sync_sub *upipe_sync_sub = upipe_sync_sub_from_upipe(upipe);
    struct upipe_sync *upipe_sync = upipe_sync_from_sub_mgr(upipe->mgr);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))

    if (ubase_ncmp(def, "sound.")) {
        upipe_err_va(upipe, "Unknown def %s", def);
        return UBASE_ERR_INVALID;
    }

    if (ubase_ncmp(def, "sound.s32."))
        return UBASE_ERR_INVALID;

    uint64_t latency;
    if (!ubase_check(uref_clock_get_latency(flow_def, &latency)))
        latency = 0;

    // FIXME : estimated latency added by processing
    latency += UCLOCK_FREQ / 25;
    uref_clock_set_latency(flow_def, latency);

    if (latency > upipe_sync->latency) {
        upipe_notice_va(upipe, "Latency %" PRIu64, latency);
        upipe_sync->latency = latency;
    }

    uint8_t planes;
    UBASE_RETURN(uref_sound_flow_get_planes(flow_def, &planes));
    if (planes != 1)
        return UBASE_ERR_INVALID;

    uint8_t channels;
    UBASE_RETURN(uref_sound_flow_get_channels(flow_def, &channels));
    if (channels != 2)
        return UBASE_ERR_INVALID;

    uint64_t num;
    UBASE_RETURN(uref_sound_flow_get_rate(flow_def, &num));
    upipe_sync_sub->rate.num = num;
    upipe_sync_sub->rate.den = 1;
    upipe_sync_sub->sound = true;

    upipe_sync_sub_store_flow_def(upipe, uref_dup(flow_def));

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_sync_sub_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;

        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sync_sub_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_sync_sub_set_output(upipe, output);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_sync_sub_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_sync_sub_set_flow_def(upipe, flow);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static float pts_to_time(uint64_t pts)
{
    static uint64_t first;
    if (!first)
        first = pts;

    pts -= first;

    return (float)pts / 27000;
}

static bool can_start_sound(struct upipe *upipe, const struct urational *fps)
{
    struct upipe_sync_sub *upipe_sync_sub = upipe_sync_sub_from_upipe(upipe);
    struct upipe_sync *upipe_sync = upipe_sync_from_sub_mgr(upipe->mgr);

    const uint64_t video_pts = upipe_sync->pts;

    struct uchain *uchain_uref = NULL, *uchain_tmp;
    ulist_delete_foreach(&upipe_sync_sub->urefs, uchain_uref, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain_uref);

        uint64_t pts = 0;
        uref_clock_get_pts_sys(uref, &pts);
        pts += upipe_sync->latency;

        int64_t pts_diff = video_pts - pts;

        // XXX
        if (pts_diff > 0 && pts_diff < UCLOCK_FREQ / 1000) {
//            upipe_err_va(upipe, "pts diff %" PRId64 , pts_diff);
            pts_diff = 0;
        }

        // TODO: s337
        if (pts_diff > 0) { /* audio too early, drop */
            size_t samples = 0;
            uref_sound_size(uref, &samples, NULL);
            uint64_t duration = UCLOCK_FREQ * samples *
                upipe_sync_sub->rate.den / upipe_sync_sub->rate.num;
            if (pts_diff < duration) {
                uint64_t drop_samples = pts_diff * upipe_sync_sub->rate.num /
                    upipe_sync_sub->rate.den / UCLOCK_FREQ;
                if (drop_samples >= samples) {
                    upipe_notice_va(upipe_sync_sub_to_upipe(upipe_sync_sub),
                            "LOLDROP, duration in CLOCK %" PRIu64 "", duration);
                    uref_free(uref);
                    ulist_delete(uchain_uref);
                    continue;
                }
                // resize
                upipe_notice_va(upipe_sync_sub_to_upipe(upipe_sync_sub),
                        "RESIZE, skip %" PRIu64 " (%" PRId64 " < %" PRIu64 ")",
                        drop_samples, pts_diff, duration);
                uref_sound_resize(uref, drop_samples, -1);
                upipe_sync_sub->samples -= drop_samples;
                pts = video_pts;
                uref_clock_set_pts_sys(uref, pts);
            } else {
                float f = (float)((int64_t)pts - (int64_t)video_pts) * 1000 / UCLOCK_FREQ;
                upipe_notice_va(upipe_sync_sub_to_upipe(upipe_sync_sub),
                        "DROP %.2f, duration in CLOCK %" PRIu64 "", f, duration);
                uref_free(uref);
                ulist_delete(uchain_uref);
                upipe_sync_sub->samples -= samples;
                continue;
            }
        }
    }

    if (upipe_sync_sub->samples < upipe_sync_sub->rate.num * fps->den / fps->num)
        upipe_notice_va(&upipe_sync_sub->upipe, "SAMPLES %" PRIu64, upipe_sync_sub->samples);
    return upipe_sync_sub->samples >= upipe_sync_sub->rate.num * fps->den / fps->num;
}

static bool can_start(struct upipe *upipe, const struct urational *fps)
{
    struct upipe_sync *upipe_sync = upipe_sync_from_upipe(upipe);

    bool start = true;

    struct uchain *uchain = NULL;
    ulist_foreach(&upipe_sync->subs, uchain) {
        struct upipe_sync_sub *upipe_sync_sub = upipe_sync_sub_from_uchain(uchain);
        if (upipe_sync_sub->sound)
            if (!can_start_sound(upipe_sync_sub_to_upipe(upipe_sync_sub), fps))
                start = false;
    }

    return start;
}

static inline unsigned audio_samples_count(struct upipe *upipe,
        const struct urational *fps)
{
    struct upipe_sync_sub *upipe_sync_sub = upipe_sync_sub_from_upipe(upipe);

    const unsigned samples = (uint64_t)48000 * fps->den / fps->num;

    /* fixed number of samples for 48kHz */
    if (fps->den != 1001 || fps->num == 24000)
        return samples;

    if (unlikely(fps->num != 30000 && fps->num != 60000)) {
        upipe_err_va(upipe,
                "Unsupported rate %" PRIu64"/%" PRIu64, fps->num, fps->den);
        return samples;
    }

    /* cyclic loop of 5 different sample counts */
    if (++upipe_sync_sub->frame_idx == 5)
        upipe_sync_sub->frame_idx = 0;

    static const uint8_t samples_increment[2][5] = {
        { 1, 0, 1, 0, 1 }, /* 30000 / 1001 */
        { 1, 1, 1, 1, 0 }  /* 60000 / 1001 */
    };

    bool rate5994 = fps->num == 60000;

    return samples + samples_increment[rate5994][upipe_sync_sub->frame_idx];
}

static void output_sound(struct upipe *upipe, const struct urational *fps,
        struct upump **upump_p)
{
    struct upipe_sync *upipe_sync = upipe_sync_from_upipe(upipe);

    struct uchain *uchain = NULL;
    ulist_foreach(&upipe_sync->subs, uchain) {
        struct upipe_sync_sub *upipe_sync_sub = upipe_sync_sub_from_uchain(uchain);
        if (!upipe_sync_sub->sound)
            continue;
        struct upipe *upipe_sub = upipe_sync_sub_to_upipe(upipe_sync_sub);

        size_t samples = audio_samples_count(upipe_sub, fps);

        /* look at first uref without dequeuing */
        struct uref *src = uref_from_uchain(ulist_peek(&upipe_sync_sub->urefs));
        if (!src) {
            upipe_err_va(upipe_sub, "no urefs");
            continue;
        }

        /* output */
        struct uref *uref = uref_dup_inner(src);
        if (!uref)
            upipe_err_va(upipe_sub, "Could not allocate ubuf");
        uref->ubuf = ubuf_sound_alloc(src->ubuf->mgr, samples);
        if (!uref->ubuf)
            upipe_err_va(upipe_sub, "Could not allocate ubuf");
        int32_t *dst_buf;
        if (!ubase_check(uref_sound_write_int32_t(uref, 0, -1, &dst_buf, 1))) {
            upipe_err_va(upipe_sub, "Could not map dst");
        }
        // map

        while (samples) {
            const int32_t *src_buf;
            size_t src_samples = 0;
            uref_sound_size(src, &src_samples, NULL);

            if (!ubase_check(uref_sound_read_int32_t(src, 0, src_samples, &src_buf, 1))) {
                upipe_err_va(upipe_sub, "Could not map src");
            }

            size_t uref_samples = src_samples;
            if (uref_samples > samples) {
                uref_samples = samples;
            }

            memcpy(dst_buf, src_buf, 2 /* FIXME */ * sizeof(int32_t) * uref_samples);
            dst_buf += 2 /* FIXME */ * uref_samples;

            uref_sound_unmap(src, 0, -1, 1);

            src_samples -= uref_samples;
            samples -= uref_samples;
            upipe_sync_sub->samples -= uref_samples;
            //upipe_notice_va(upipe_sub, "pop, samples %" PRIu64, upipe_sync_sub->samples);

            if (src_samples == 0) {
                ulist_pop(&upipe_sync_sub->urefs);
                uref_free(src);
                src = uref_from_uchain(ulist_peek(&upipe_sync_sub->urefs));
                if (!src)
                    break;
            } else {
                uref_sound_resize(src, uref_samples, -1);
                assert(samples == 0);

                uint64_t pts = 0;
                uref_clock_get_pts_sys(src, &pts);
                pts += upipe_sync->latency;
                pts += uref_samples * UCLOCK_FREQ / 48000;
                uref_clock_set_pts_sys(src, pts);
            }

            // TODO : next uref
        }

        uref_sound_unmap(uref, 0, -1, 1);
        uref_clock_set_pts_sys(uref, upipe_sync->pts - upipe_sync->latency);
        upipe_sync_sub_output(upipe_sub, uref, upump_p);
    }
}

static void cb(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_sync *upipe_sync = upipe_sync_from_upipe(upipe);

    uint64_t now = uclock_now(upipe_sync->uclock);
    if (now - upipe_sync->ticks_per_frame > upipe_sync->pts)
        upipe_dbg_va(upipe, "cb after %" PRId64 "ms",
        (int64_t)((int64_t)now - (int64_t)upipe_sync->pts) / 27000);

    now = upipe_sync->pts; // the upump was scheduled for now
    struct uchain *uchain = NULL;
    for (;;) {
        uchain = ulist_peek(&upipe_sync->urefs);
        if (!uchain) {
            upipe_err_va(upipe, "no pictures");
            break;
        }

        struct uref *uref = uref_from_uchain(uchain);
        uint64_t pts = 0;
        uref_clock_get_pts_sys(uref, &pts);
        pts += upipe_sync->latency;

        /* frame duration */
        const uint64_t ticks = upipe_sync->ticks_per_frame;

               if (pts < now - ticks / 2) {
            /* frame pts too much in the past */
            upipe_warn_va(upipe, "too late");
        } else if (pts > now + ticks / 2) {
            upipe_err_va(upipe, "too early");
            uchain = NULL; /* do not drop */
            break;
        } else {
            break; // ok
        }

        ulist_pop(&upipe_sync->urefs);
        uref_free(uref);
        int64_t u = pts - now;
        upipe_err_va(upipe, "Drop pic (pts-now == %" PRId64 "ms)", u / 27000);
    }

    /* sync audio */
    if (!can_start(upipe_sync_to_upipe(upipe_sync), &upipe_sync->fps)) {
        upipe_err_va(upipe, "not enough samples");
    }

    /* output audio */
    output_sound(upipe_sync_to_upipe(upipe_sync), &upipe_sync->fps, NULL);

    /* output pic */
    if (uchain) {
        ulist_pop(&upipe_sync->urefs);
        /* buffer picture */
        uref_free(upipe_sync->uref);
        upipe_sync->uref = uref_from_uchain(uchain);
    }

    assert(upipe_sync->uref);
    struct uref *uref = uref_dup(upipe_sync->uref);
    uref_clock_set_pts_sys(uref, upipe_sync->pts - upipe_sync->latency);
    now = uclock_now(upipe_sync->uclock);

        upipe_notice_va(upipe,
                "output %.2f now %.2f latency %" PRIu64,
                pts_to_time(upipe_sync->pts - upipe_sync->latency),
                pts_to_time(now),
                upipe_sync->latency / 27000
            );
    upipe_sync_output(upipe, uref, NULL);

    /* increment pts */
    upipe_sync->pts += upipe_sync->ticks_per_frame;

    /* schedule next pic */
    now = uclock_now(upipe_sync->uclock);
    while (now > upipe_sync->pts) {
        upipe_sync->pts += upipe_sync->ticks_per_frame;
        upipe_err_va(upipe, "skipping a beat");
    }
    upipe_sync_wait_upump(upipe, upipe_sync->pts - now, cb);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_sync_sub_input(struct upipe *upipe, struct uref *uref,
        struct upump **upump_p)
{
    struct upipe_sync_sub *upipe_sync_sub = upipe_sync_sub_from_upipe(upipe);
    struct upipe_sync *upipe_sync = upipe_sync_from_sub_mgr(upipe->mgr);

    if (!upipe_sync_sub->sound) {
        // TODO subpics
        uref_free(uref);
        return;
    }

    /* need upump mgr */
    if (!ubase_check(upipe_sync_check_upump_mgr(upipe_sync_to_upipe(upipe_sync)))) {
        uref_free(uref);
        return;
    }

    /* get uref date */
    uint64_t pts;
    if (!ubase_check(uref_clock_get_pts_sys(uref, &pts))) {
        upipe_err(upipe, "undated uref");
        uref_free(uref);
        return;
    }
    pts += upipe_sync->latency;

    uint64_t now = uclock_now(upipe_sync->uclock);
    //upipe_dbg_va(upipe, "push PTS in %" PRIu64 " ms", (pts - now) / 27000);

    /* prepare to insert in linked list */
    struct uchain *uchain = uref_to_uchain(uref);
    ulist_init(uchain);

    /* buffer audio */
    size_t samples = 0;
    uref_sound_size(uref, &samples, NULL);
    upipe_sync_sub->samples += samples;
    //upipe_notice_va(upipe, "push, samples %" PRIu64, upipe_sync_sub->samples);

    ulist_add(&upipe_sync_sub->urefs, uchain);
}

/** @internal @This initializes the output manager for a dup set pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_sync_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_sync *upipe_sync = upipe_sync_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_sync->sub_mgr;
    memset(sub_mgr, 0, sizeof(*sub_mgr));
    sub_mgr->refcount = upipe->refcount;
    sub_mgr->signature = UPIPE_SYNC_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_sync_sub_alloc;
    sub_mgr->upipe_input = upipe_sync_sub_input;
    sub_mgr->upipe_control = upipe_sync_sub_control;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_sync_input(struct upipe *upipe, struct uref *uref,
        struct upump **upump_p)
{
    struct upipe_sync *upipe_sync = upipe_sync_from_upipe(upipe);

    /* need upump mgr */
    if (!ubase_check(upipe_sync_check_upump_mgr(upipe_sync_to_upipe(upipe_sync)))) {
        uref_free(uref);
        return;
    }

    /* get uref date */
    uint64_t pts;
    if (!ubase_check(uref_clock_get_pts_sys(uref, &pts))) {
        upipe_err(upipe, "undated uref");
        uref_free(uref);
        return;
    }
    pts += upipe_sync->latency;

    uint64_t now = uclock_now(upipe_sync->uclock);
    //upipe_dbg_va(upipe, "push PTS in %" PRIu64 " ms", (pts - now) / 27000);

    /* prepare to insert in linked list */
    struct uchain *uchain = uref_to_uchain(uref);
    ulist_init(uchain);

    /* reject late pics */
    if (now > pts) {
        uint64_t cr = 0;
        uref_clock_get_cr_sys(uref, &cr);
        upipe_err_va(upipe, "%s() TOO LATE by %" PRIu64 "ms, drop pic, recept %" PRIu64 "",
            __func__, (now - pts) / 27000, (now - cr) / 27000);
        uref_free(uref);
        return;
    }

    /* buffer pic */
    ulist_add(&upipe_sync->urefs, uchain);
    uref = NULL;

    /* timer already active */
    if (upipe_sync->upump)
        return;

    /* start timer */
    upipe_sync->pts = pts;
    upipe_sync_wait_upump(upipe_sync_to_upipe(upipe_sync), pts - now, cb);
}

/** @internal @This allocates a sync pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_sync_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_sync_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_sync *upipe_sync = upipe_sync_from_upipe(upipe);

    upipe_sync->latency = 0;
    upipe_sync->pts = 0;
    upipe_sync->ticks_per_frame = 0;
    upipe_sync->uref = NULL;
    ulist_init(&upipe_sync->urefs);

    upipe_sync_init_urefcount(upipe);
    upipe_sync_init_uclock(upipe);
    upipe_sync_init_upump(upipe);
    upipe_sync_init_upump_mgr(upipe);
    upipe_sync_init_output(upipe);
    upipe_sync_init_sub_subs(upipe);
    upipe_sync_init_sub_mgr(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_sync_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_sync *upipe_sync = upipe_sync_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))

    uint64_t latency;
    if (!ubase_check(uref_clock_get_latency(flow_def, &latency)))
        latency = 0;

    // FIXME : estimated latency added by processing
    latency += UCLOCK_FREQ / 25;
    uref_clock_set_latency(flow_def, latency);

    if (latency > upipe_sync->latency) {
        upipe_notice_va(upipe, "Latency %" PRIu64, latency);
        upipe_sync->latency = latency;
    }

    if (ubase_ncmp(def, "pic.")) {
        upipe_err_va(upipe, "Unknown def %s", def);
        return UBASE_ERR_INVALID;
    }

    UBASE_RETURN(uref_pic_flow_get_fps(flow_def, &upipe_sync->fps));
    upipe_sync->ticks_per_frame = UCLOCK_FREQ *
        upipe_sync->fps.den / upipe_sync->fps.num;

    upipe_sync_store_flow_def(upipe, uref_dup(flow_def));

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_sync_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sync_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_sync_set_output(upipe, output);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_sync_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_sync_set_flow_def(upipe, flow);
        }
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_sync_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sync_iterate_sub(upipe, p);
        }
        case UPIPE_ATTACH_UCLOCK:
           upipe_sync_set_upump(upipe, NULL);
           upipe_sync_require_uclock(upipe);
           return UBASE_ERR_NONE;
        case UPIPE_ATTACH_UPUMP_MGR:
           return upipe_sync_attach_upump_mgr(upipe);

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_sync_free(struct upipe *upipe)
{
    struct upipe_sync *upipe_sync = upipe_sync_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_sync->uref);

    upipe_sync_clean_urefcount(upipe);
    upipe_sync_clean_uclock(upipe);
    upipe_sync_clean_output(upipe);
    upipe_sync_clean_upump(upipe);
    upipe_sync_clean_upump_mgr(upipe);
    upipe_sync_clean_sub_subs(upipe);
    upipe_sync_free_void(upipe);
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_sync_sub_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_sync_sub_clean_urefcount(upipe);
    upipe_sync_sub_clean_output(upipe);
    upipe_sync_sub_clean_sub(upipe);
    upipe_sync_sub_free_void(upipe);
}

/** upipe_sync */
static struct upipe_mgr upipe_sync_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SYNC_SIGNATURE,

    .upipe_alloc = upipe_sync_alloc,
    .upipe_input = upipe_sync_input,
    .upipe_control = upipe_sync_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for sync pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sync_mgr_alloc(void)
{
    return &upipe_sync_mgr;
}
