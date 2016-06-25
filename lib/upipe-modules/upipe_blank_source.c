/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe source module generating a black/blank signal
 */

#include <upipe/ubase.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-modules/upipe_blank_source.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <assert.h>

/** @hidden */
static int upipe_blksrc_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the flow type */
enum upipe_blksrc_type {
    UPIPE_BLKSRC_TYPE_PIC,
    UPIPE_BLKSRC_TYPE_SOUND,
};

/** @internal @This is the private context of a blank source pipe. */
struct upipe_blksrc {
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
    uint64_t pts;
    /** timer interval */
    uint64_t interval;

    /** blank uref */
    struct uref *blank_uref;

    /** flow type */
    enum upipe_blksrc_type type;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_blksrc, upipe, UPIPE_BLKSRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_blksrc, urefcount, upipe_blksrc_free)
UPIPE_HELPER_FLOW(upipe_blksrc, "")

UPIPE_HELPER_OUTPUT(upipe_blksrc, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_blksrc, uref_mgr, uref_mgr_request,
                      upipe_blksrc_check,
                      upipe_blksrc_register_output_request,
                      upipe_blksrc_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_blksrc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_blksrc_check,
                      upipe_blksrc_register_output_request,
                      upipe_blksrc_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_blksrc, uclock, uclock_request, upipe_blksrc_check,
                    upipe_blksrc_register_output_request,
                    upipe_blksrc_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_blksrc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_blksrc, upump, upump_mgr)

/** @This allocates a blank uref
 *
 * @param upipe description structure of the pipe
 * @return uref pointer or NULL in case of error
 */
static struct uref *upipe_blksrc_alloc_uref(struct upipe *upipe)
{
    struct upipe_blksrc *upipe_blksrc = upipe_blksrc_from_upipe(upipe);

    switch (upipe_blksrc->type) {
        case UPIPE_BLKSRC_TYPE_PIC: {
            uint64_t hsize = 0, vsize = 0;
            uref_pic_flow_get_hsize(upipe_blksrc->flow_def, &hsize);
            uref_pic_flow_get_vsize(upipe_blksrc->flow_def, &vsize);

            struct uref *uref = uref_pic_alloc(upipe_blksrc->uref_mgr,
                upipe_blksrc->ubuf_mgr, hsize, vsize);
            if (unlikely(!uref)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return NULL;
            }
            uref_pic_clear(uref, 0, 0, -1, -1, 1);
            uref_clock_set_duration(uref, upipe_blksrc->interval);

            if (ubase_check(uref_pic_get_progressive(upipe_blksrc->flow_def))) {
                uref_pic_set_progressive(uref);
            }

            return uref;
        }
        case UPIPE_BLKSRC_TYPE_SOUND: {
            uint64_t samples = 0;
            uint8_t sample_size = 0;
            uref_sound_flow_get_samples(upipe_blksrc->flow_def, &samples);
            uref_sound_flow_get_sample_size(upipe_blksrc->flow_def,
                                            &sample_size);

            struct uref *uref = uref_sound_alloc(upipe_blksrc->uref_mgr,
                upipe_blksrc->ubuf_mgr, samples);
            if (unlikely(!uref)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return NULL;
            }
            const char *channel = NULL;
            uint8_t *buf = NULL;
            while (ubase_check(uref_sound_plane_iterate(uref, &channel))
                                                              && channel) {
                uref_sound_plane_write_uint8_t(uref, channel, 0, -1, &buf);
                memset(buf, 0, sample_size * samples);
                uref_sound_plane_unmap(uref, channel, 0, -1);
            }
            uref_clock_set_duration(uref, upipe_blksrc->interval);

            return uref;
        }
    }
    return NULL;
}

/** @internal @This creates blank data and outputs it.
 *
 * @param upump description structure of the timer
 */
static void upipe_blksrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_blksrc *upipe_blksrc = upipe_blksrc_from_upipe(upipe);
    uint64_t current_time;

    if (unlikely(!upipe_blksrc->blank_uref)) {
        upipe_blksrc->blank_uref = upipe_blksrc_alloc_uref(upipe);
        if (unlikely(!upipe_blksrc->blank_uref)) {
            return;
        }
    }

    if (unlikely(upipe_blksrc->pts == UINT64_MAX)) {
        upipe_blksrc->pts = uclock_now(upipe_blksrc->uclock);
    }

    current_time = uclock_now(upipe_blksrc->uclock);
    upipe_verbose_va(upipe, "delay %"PRId64, current_time - upipe_blksrc->pts);

    struct uref *uref = uref_dup(upipe_blksrc->blank_uref);
    uref_clock_set_duration(uref, upipe_blksrc->interval);
    uref_clock_set_pts_sys(uref, upipe_blksrc->pts);
    uref_clock_set_pts_prog(uref, upipe_blksrc->pts);
    upipe_blksrc->pts += upipe_blksrc->interval;

    upipe_blksrc_output(upipe, uref, &upipe_blksrc->upump);

    /* derive timer from (next) pts and current time */
    current_time = uclock_now(upipe_blksrc->uclock);
    int64_t wait = upipe_blksrc->pts - current_time;

    /* missed ticks */
    while (wait < 0) {
        /* check if we have been released in the meantime */
        if (upipe_single(upipe))
            return;

        upipe_warn_va(upipe, "late packet wait %"PRId64, wait);

        struct uref *uref = uref_dup(upipe_blksrc->blank_uref);
        uref_clock_set_duration(uref, upipe_blksrc->interval);
        uref_clock_set_pts_sys(uref, upipe_blksrc->pts);
        uref_clock_set_pts_prog(uref, upipe_blksrc->pts);
        upipe_blksrc->pts += upipe_blksrc->interval;

        upipe_blksrc_output(upipe, uref, &upipe_blksrc->upump);
        wait += upipe_blksrc->interval;
    }

    upipe_verbose_va(upipe,
        "interval %"PRIu64" nextpts %"PRIu64" current %"PRIu64" wait %"PRId64,
        upipe_blksrc->interval, upipe_blksrc->pts,
        current_time, wait);

    /* realloc oneshot timer */
    upipe_blksrc_wait_upump(upipe, wait, upipe_blksrc_worker);
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_blksrc_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_blksrc *upipe_blksrc = upipe_blksrc_from_upipe(upipe);
    if (likely(upipe_blksrc->blank_uref)) {
        uref_free(upipe_blksrc->blank_uref);
    }
    upipe_blksrc->blank_uref = uref;
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_blksrc_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_blksrc *upipe_blksrc = upipe_blksrc_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_blksrc_store_flow_def(upipe, flow_format);

    upipe_blksrc_check_upump_mgr(upipe);
    if (upipe_blksrc->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_blksrc->uref_mgr == NULL) {
        upipe_blksrc_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_blksrc->flow_def == NULL)
        return UBASE_ERR_NONE;

    if (upipe_blksrc->ubuf_mgr == NULL) {
        upipe_blksrc_require_ubuf_mgr(upipe, uref_dup(upipe_blksrc->flow_def));
        return UBASE_ERR_NONE;
    }

    if (upipe_blksrc->uclock == NULL) {
        upipe_blksrc_require_uclock(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_blksrc->upump == NULL) {
        struct upump *upump = upump_alloc_timer(upipe_blksrc->upump_mgr,
            upipe_blksrc_worker, upipe, upipe->refcount,
            upipe_blksrc->interval, 0);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_blksrc_set_upump(upipe, upump);
        upump_start(upump);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_blksrc_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_blksrc *upipe_blksrc = upipe_blksrc_from_upipe(upipe);
    if (flow_def == NULL) {
        return UBASE_ERR_INVALID;
    }

    /* copy output flow def */
    struct uref *flow_def_dup = uref_dup(upipe_blksrc->flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uint64_t hsize, vsize;
    struct urational sar;
    uref_pic_flow_clear_format(flow_def_dup);
    uref_pic_flow_copy_format(flow_def_dup, flow_def);
    if (likely(ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize)))) {
        uref_pic_flow_set_hsize(flow_def_dup, hsize);
    } else {
        uref_pic_flow_delete_hsize(flow_def_dup);
    }
    if (likely(ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize)))) {
        uref_pic_flow_set_vsize(flow_def_dup, vsize);
    } else {
        uref_pic_flow_delete_vsize(flow_def_dup);
    }
    if (likely(ubase_check(uref_pic_flow_get_sar(flow_def, &sar)))) {
        uref_pic_flow_set_sar(flow_def_dup, sar);
    } else {
        uref_pic_flow_delete_sar(flow_def_dup);
    }
    bool overscan;
    if (likely(ubase_check(uref_pic_flow_get_overscan(flow_def, &overscan)))) {
        uref_pic_flow_set_overscan(flow_def_dup, overscan);
    } else {
        uref_pic_flow_delete_overscan(flow_def_dup);
    }
    if (likely(ubase_check(uref_pic_get_progressive(flow_def)))) {
        uref_pic_set_progressive(flow_def_dup);
    } else {
        uref_pic_delete_progressive(flow_def_dup);
    }
    upipe_blksrc_store_flow_def(upipe, flow_def_dup);

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a blank source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_blksrc_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_blksrc_set_upump(upipe, NULL);
            return upipe_blksrc_attach_upump_mgr(upipe);
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_blksrc_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_blksrc_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_blksrc_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_blksrc_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_blksrc_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_blksrc_set_output(upipe, output);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a blank source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_blksrc_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_blksrc_control(upipe, command, args));

    return upipe_blksrc_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_blksrc_free(struct upipe *upipe)
{
    struct upipe_blksrc *upipe_blksrc = upipe_blksrc_from_upipe(upipe);
    upipe_throw_dead(upipe);

    if (unlikely(upipe_blksrc->blank_uref)) {
        uref_free(upipe_blksrc->blank_uref);
    }

    upipe_blksrc_clean_upump(upipe);
    upipe_blksrc_clean_upump_mgr(upipe);
    upipe_blksrc_clean_output(upipe);
    upipe_blksrc_clean_uclock(upipe);
    upipe_blksrc_clean_ubuf_mgr(upipe);
    upipe_blksrc_clean_uref_mgr(upipe);
    upipe_blksrc_clean_urefcount(upipe);
    upipe_blksrc_free_flow(upipe);
}

/** @internal @This allocates a blank source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_blksrc_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_blksrc_alloc_flow(mgr, uprobe, signature, args,
                                                  &flow_def);
    if (unlikely(!upipe)) {
        return NULL;
    }

    struct upipe_blksrc *upipe_blksrc = upipe_blksrc_from_upipe(upipe);
    const char *def = NULL;
    uref_flow_get_def(flow_def, &def);
    if (!ubase_ncmp(def, "pic.")) {
        struct urational fps;
        uint8_t planes;
        uint64_t hsize, vsize;
        if (unlikely(!ubase_check(uref_pic_flow_get_planes(flow_def, &planes)))
            || !ubase_check(uref_pic_flow_get_fps(flow_def, &fps))
            || !ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize))
            || !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize))) {
            upipe_blksrc_free_flow(upipe);
            return NULL;
        }
        upipe_blksrc->interval = (uint64_t)UCLOCK_FREQ * fps.den / fps.num;
        upipe_blksrc->type = UPIPE_BLKSRC_TYPE_PIC;
    } else if (!ubase_ncmp(def, "sound.")) {
        uint8_t planes, ssize, channels;
        uint64_t rate, samples;
        if (unlikely(!ubase_check(uref_sound_flow_get_planes(flow_def,
                                                             &planes)))
            || !ubase_check(uref_sound_flow_get_channels(flow_def, &channels))
            || !ubase_check(uref_sound_flow_get_rate(flow_def, &rate))
            || !ubase_check(uref_sound_flow_get_sample_size(flow_def, &ssize))
            || !ubase_check(uref_sound_flow_get_samples(flow_def, &samples))) {
            upipe_blksrc_free_flow(upipe);
            return NULL;
        }
        upipe_blksrc->interval = samples * UCLOCK_FREQ / rate;
        upipe_blksrc->type = UPIPE_BLKSRC_TYPE_SOUND;
    } else {
        upipe_blksrc_free_flow(upipe);
        return NULL;
    }

    upipe_blksrc_init_urefcount(upipe);
    upipe_blksrc_init_uref_mgr(upipe);
    upipe_blksrc_init_ubuf_mgr(upipe);
    upipe_blksrc_init_uclock(upipe);
    upipe_blksrc_init_output(upipe);
    upipe_blksrc_init_upump_mgr(upipe);
    upipe_blksrc_init_upump(upipe);

    upipe_blksrc->pts = UINT64_MAX;
    upipe_blksrc->blank_uref = NULL;
    upipe_throw_ready(upipe);

    upipe_blksrc_store_flow_def(upipe, flow_def);

    upipe_blksrc_check_upump_mgr(upipe);
    return upipe;
}

/** module manager static descriptor */
static struct upipe_mgr upipe_blksrc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_BLKSRC_SIGNATURE,

    .upipe_alloc = upipe_blksrc_alloc,
    .upipe_input = upipe_blksrc_input,
    .upipe_control = upipe_blksrc_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all blank source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_blksrc_mgr_alloc(void)
{
    return &upipe_blksrc_mgr;
}
