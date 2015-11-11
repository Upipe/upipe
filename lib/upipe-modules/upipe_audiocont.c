/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe module audio continuity
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow_def_check.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe-modules/upipe_audiocont.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** only accept sound in 32 bit floating-point */
#define EXPECTED_FLOW_DEF "sound.f32."
/** by default cross-blend for 200 ms */
#define CROSSBLEND_PERIOD (UCLOCK_FREQ / 5)
/** define to get timing verbosity */
#undef VERBOSE_TIMING

/** @hidden */
static int upipe_audiocont_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts join pipe. */
struct upipe_audiocont {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;
    /** structure to check input flow def */
    struct uref *flow_def_check;

    /** number of planes */
    uint8_t planes;
    /** samplerate */
    uint64_t samplerate;
    /** crossblend period */
    uint64_t crossblend_period;
    /** crossblend step between each sample */
    float crossblend_step;

    /** list of input subpipes */
    struct uchain subs;

    /** current input */
    struct upipe *input_cur;
    /** previous input */
    struct upipe *input_prev;
    /** next input */
    char *input_name;
    /** crossblend factor */
    float crossblend;

    /** pts latency */
    uint64_t latency;

    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_audiocont, upipe, UPIPE_AUDIOCONT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_audiocont, urefcount, upipe_audiocont_free)
UPIPE_HELPER_FLOW(upipe_audiocont, "sound.f32.")
UPIPE_HELPER_OUTPUT(upipe_audiocont, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF_CHECK(upipe_audiocont, flow_def_check)
UPIPE_HELPER_UBUF_MGR(upipe_audiocont, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_audiocont_check,
                      upipe_audiocont_register_output_request,
                      upipe_audiocont_unregister_output_request)

/** @internal @This is the private context of an input of a audiocont pipe. */
struct upipe_audiocont_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** temporary uref storage */
    struct uchain urefs;

    /** input flow definition packet */
    struct uref *flow_def;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_audiocont_sub, upipe, UPIPE_AUDIOCONT_SUB_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_audiocont_sub, urefcount, upipe_audiocont_sub_free)
UPIPE_HELPER_VOID(upipe_audiocont_sub)

UPIPE_HELPER_SUBPIPE(upipe_audiocont, upipe_audiocont_sub, sub, sub_mgr,
                     subs, uchain)

/** @hidden */
static int upipe_audiocont_switch_input(struct upipe *upipe,
                                        struct upipe *input);

/** @internal @This allocates an input subpipe of a audiocont pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_audiocont_sub_alloc(struct upipe_mgr *mgr,
                                               struct uprobe *uprobe,
                                               uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_audiocont_sub_alloc_void(mgr,
                                     uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_audiocont_sub *upipe_audiocont_sub =
        upipe_audiocont_sub_from_upipe(upipe);
    upipe_audiocont_sub_init_urefcount(upipe);
    upipe_audiocont_sub_init_sub(upipe);
    ulist_init(&upipe_audiocont_sub->urefs);
    upipe_audiocont_sub->flow_def = NULL;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_audiocont_sub_input(struct upipe *upipe, struct uref *uref,
                                      struct upump **upump_p)
{
    struct upipe_audiocont_sub *upipe_audiocont_sub =
                                upipe_audiocont_sub_from_upipe(upipe);

    uint64_t pts;
    if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &pts)))) {
        upipe_warn_va(upipe, "packet without pts");
        uref_free(uref);
        return;
    }
    uint64_t duration;
    if (unlikely(!ubase_check(uref_clock_get_duration(uref, &duration)))) {
        upipe_warn_va(upipe, "packet without duration");
        uref_free(uref);
        return;
    }

    ulist_add(&upipe_audiocont_sub->urefs, uref_to_uchain(uref));
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_audiocont_sub_set_flow_def(struct upipe *upipe,
                                            struct uref *flow_def)
{
    struct upipe_audiocont_sub *upipe_audiocont_sub =
           upipe_audiocont_sub_from_upipe(upipe);
    struct upipe_audiocont *upipe_audiocont =
                            upipe_audiocont_from_sub_mgr(upipe->mgr);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct uref *flow_def_check =
        upipe_audiocont_alloc_flow_def_check(
                upipe_audiocont_to_upipe(upipe_audiocont), flow_def);
    if (flow_def_check == NULL ||
        !ubase_check(uref_sound_flow_copy_format(flow_def_check, flow_def)) ||
        !upipe_audiocont_check_flow_def_check(
            upipe_audiocont_to_upipe(upipe_audiocont), flow_def_check)) {
        uref_free(flow_def_check);
        return UBASE_ERR_INVALID;
    }
    uref_free(flow_def_check);

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    uref_free(upipe_audiocont_sub->flow_def);
    upipe_audiocont_sub->flow_def = flow_def_dup;

    /* check flow against (next) grid input name */
    const char *name = NULL;
    uref_flow_get_name(flow_def, &name);
    if (upipe_audiocont->input_name
        && likely(ubase_check(uref_flow_get_name(flow_def, &name)))
        && !strcmp(upipe_audiocont->input_name, name)) {
        upipe_audiocont_switch_input(upipe_audiocont_to_upipe(upipe_audiocont),
                                     upipe);
    }
    return UBASE_ERR_NONE;
}

/** @This sets a audiocont subpipe as its grandpipe input.
 *
 * @param upipe description structure of the (sub)pipe
 * @return an error code
 */
static int _upipe_audiocont_sub_set_input(struct upipe *upipe)
{
    struct upipe_audiocont *upipe_audiocont =
                            upipe_audiocont_from_sub_mgr(upipe->mgr);
    struct upipe *grandpipe = upipe_audiocont_to_upipe(upipe_audiocont);
    ubase_clean_str(&upipe_audiocont->input_name);
    return upipe_audiocont_switch_input(grandpipe, upipe);
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_audiocont_sub_provide_flow_format(struct upipe *upipe,
                                                   struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);
    uref_sound_flow_clear_format(flow_format);

    struct upipe_audiocont *upipe_audiocont =
                            upipe_audiocont_from_sub_mgr(upipe->mgr);
    UBASE_RETURN(uref_sound_flow_copy_format(flow_format,
                                       upipe_audiocont->flow_def_check))
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This processes control commands on a subpipe of a audiocont
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_audiocont_sub_control(struct upipe *upipe,
                                       int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_UREF_MGR)
                return upipe_throw_provide_request(upipe, request);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_audiocont_sub_provide_flow_format(upipe, request);
            struct upipe_audiocont *upipe_audiocont =
                                    upipe_audiocont_from_sub_mgr(upipe->mgr);
            return upipe_audiocont_alloc_output_proxy(
                    upipe_audiocont_to_upipe(upipe_audiocont), request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_UREF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            struct upipe_audiocont *upipe_audiocont =
                                    upipe_audiocont_from_sub_mgr(upipe->mgr);
            return upipe_audiocont_free_output_proxy(
                    upipe_audiocont_to_upipe(upipe_audiocont), request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_audiocont_sub_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_audiocont_sub_get_super(upipe, p);
        }

        case UPIPE_AUDIOCONT_SUB_SET_INPUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUDIOCONT_SUB_SIGNATURE)
            return _upipe_audiocont_sub_set_input(upipe);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audiocont_sub_free(struct upipe *upipe)
{
    struct upipe_audiocont_sub *upipe_audiocont_sub =
                                upipe_audiocont_sub_from_upipe(upipe);
    struct upipe_audiocont *upipe_audiocont =
                            upipe_audiocont_from_sub_mgr(upipe->mgr);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_audiocont_sub->urefs, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }
    if (upipe == upipe_audiocont->input_cur) {
        upipe_audiocont_switch_input(upipe_audiocont_to_upipe(upipe_audiocont),
                                     NULL);
    }
    if (upipe == upipe_audiocont->input_prev) {
        upipe_audiocont->input_prev = NULL;
    }

    uref_free(upipe_audiocont_sub->flow_def);

    upipe_throw_dead(upipe);
    upipe_audiocont_sub_clean_sub(upipe);
    upipe_audiocont_sub_clean_urefcount(upipe);
    upipe_audiocont_sub_free_void(upipe);
}

/** @internal @This consumes samples from the uref stream.
 *
 * @param upipe description structure of the pipe
 * @param next_pts PTS of the first extracted sample
 * @param next_duration duration of the forthcoming ubuf
 * @return an error code
 */
static void upipe_audiocont_sub_consume(struct upipe *upipe,
        uint64_t next_pts, uint64_t next_duration)
{
    struct upipe_audiocont_sub *sub = upipe_audiocont_sub_from_upipe(upipe);
    struct upipe_audiocont *upipe_audiocont =
                            upipe_audiocont_from_sub_mgr(upipe->mgr);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&sub->urefs, uchain, uchain_tmp) {
        uint64_t pts = 0;
        uint64_t duration = 0;
        size_t size = 0;
        struct uref *uref_uchain = uref_from_uchain(uchain);
        uref_clock_get_pts_sys(uref_uchain, &pts);
        uref_clock_get_duration(uref_uchain, &duration);
        uref_sound_size(uref_uchain, &size, NULL);

        /* next_duration acts as a tolerance */
        if (pts + upipe_audiocont->latency + duration
                                           + next_duration >= next_pts)
            break;

        /* packet too old */
#ifdef VERBOSE_TIMING
        upipe_verbose_va(upipe, "deleted %p (%"PRIu64") next %"PRIu64"",
                         uref_uchain, pts, next_pts);
#endif
        ulist_delete(uchain);
        uref_free(uref_uchain);
    }
}

/** @internal @This extracts from the uref stream to an allocated ubuf.
 *
 * @param upipe description structure of the pipe
 * @param ubuf allocated ubuf
 * @param next_pts PTS of the first extracted sample
 * @param next_duration duration of the ubuf
 * @param initial_crossblend crossblend factor for the first extracted sample
 * @param previous true if the input is the previously selected stream
 * @return an error code
 */
static int upipe_audiocont_sub_extract(struct upipe *upipe, struct ubuf *ubuf,
        uint64_t next_pts, uint64_t next_duration,
        float initial_crossblend, bool previous)
{
    struct upipe_audiocont_sub *sub = upipe_audiocont_sub_from_upipe(upipe);
    struct upipe_audiocont *upipe_audiocont =
                            upipe_audiocont_from_sub_mgr(upipe->mgr);

    size_t ref_size;
    uint8_t sample_size;
    UBASE_RETURN(ubuf_sound_size(ubuf, &ref_size, &sample_size))

    /* We assume the ubuf is allocated by us and therefore can be writtent and
     * is contiguous. */
    uint8_t planes = upipe_audiocont->planes;
    float *ref_buffers[planes];
    UBASE_RETURN(ubuf_sound_write_float(ubuf, 0, -1, ref_buffers,
                                        upipe_audiocont->planes))

    /* copy input sound buffer to output stream */
    size_t offset = 0;
    while (offset < ref_size) {
        if (previous && initial_crossblend == 1.)
            break;

        struct uchain *uchain = ulist_peek(&sub->urefs);
        if (unlikely(!uchain)) {
            upipe_verbose_va(upipe, "no input samples found (%"PRIu64")",
                             next_pts);
            break;
        }
        struct uref *input_uref = uref_from_uchain(uchain);
        size_t size;
        uint64_t pts = 0;
        uref_clock_get_pts_sys(input_uref, &pts);
        if (pts + upipe_audiocont->latency > next_pts + next_duration) {
            /* NOTE : next_duration is needed here because packets
             * in the future are not mangled */
            upipe_verbose_va(upipe,
                "input samples in the future %"PRIu64" > %"PRIu64,
                pts + upipe_audiocont->latency, next_pts);
            break;
        }
        uref_sound_size(input_uref, &size, NULL);

        size_t extracted = ((ref_size - offset) < size) ?
                           (ref_size - offset) : size;
        upipe_verbose_va(upipe, "%p off %zu ext %zu size %zu ref %zu",
                         input_uref, offset, extracted, size, ref_size);
        const float *in_buffers[planes];
        if (unlikely(!ubase_check(uref_sound_read_float(input_uref, 0,
                                       extracted, in_buffers, planes)))) {
            upipe_warn(upipe, "invalid input buffer");
            uref_free(uref_from_uchain(ulist_pop(&sub->urefs)));
            continue;
        }
        uint8_t plane;
        for (plane = 0;
             (plane < planes) && ref_buffers[plane] && in_buffers[plane];
             plane++) {
            float *ref_buffer = ref_buffers[plane] +
                                offset * sample_size / sizeof(float);
            const float *in_buffer = in_buffers[plane];
            size_t extracted_plane = extracted;
            float crossblend = initial_crossblend;

            while (extracted_plane) {
                if (crossblend >= 1.) {
                    if (!previous)
                        memcpy(ref_buffer, in_buffer,
                               extracted_plane * sample_size);
                    break;
                }

                float real_crossblend = previous ?
                                        (1. - crossblend) : crossblend;
                for (int i = 0; i < sample_size / sizeof(float); i++)
                    ref_buffer[i] += in_buffer[i] * real_crossblend;

                ref_buffer += sample_size / sizeof(float);
                in_buffer += sample_size / sizeof(float);
                extracted_plane--;
                crossblend += upipe_audiocont->crossblend_step;
            }
        }

        uref_sound_unmap(input_uref, 0, extracted, planes);

        offset += extracted;
        initial_crossblend += upipe_audiocont->crossblend_step * extracted;
        if (extracted == size) {
            /* input buffer entirely copied */
            uref_free(uref_from_uchain(ulist_pop(&sub->urefs)));
        } else {
            /* resize input buffer (drop copied segment) */
            uref_sound_consume(input_uref, extracted,
                               upipe_audiocont->samplerate);
        }
    }

    ubuf_sound_unmap(ubuf, 0, -1, planes);
    return UBASE_ERR_NONE;
}

/** @internal @This initializes the input manager for a audiocont pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audiocont_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_audiocont *upipe_audiocont = upipe_audiocont_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_audiocont->sub_mgr;
    sub_mgr->refcount = upipe_audiocont_to_urefcount(upipe_audiocont);
    sub_mgr->signature = UPIPE_AUDIOCONT_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_audiocont_sub_alloc;
    sub_mgr->upipe_input = upipe_audiocont_sub_input;
    sub_mgr->upipe_control = upipe_audiocont_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a audiocont pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_audiocont_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_audiocont_alloc_flow(mgr,
                                 uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_audiocont *upipe_audiocont = upipe_audiocont_from_upipe(upipe);
    if (unlikely(!ubase_check(uref_sound_flow_get_planes(flow_def,
                        &upipe_audiocont->planes)) ||
                 !upipe_audiocont->planes ||
                 !ubase_check(uref_sound_flow_get_rate(flow_def,
                         &upipe_audiocont->samplerate)) ||
                 !upipe_audiocont->samplerate)) {
        uref_free(flow_def);
        upipe_audiocont_free_flow(upipe);
        return NULL;
    }

    upipe_audiocont_init_urefcount(upipe);
    upipe_audiocont_init_ubuf_mgr(upipe);
    upipe_audiocont_init_output(upipe);
    upipe_audiocont_init_flow_def_check(upipe);
    upipe_audiocont_init_sub_mgr(upipe);
    upipe_audiocont_init_sub_subs(upipe);

    upipe_audiocont->input_cur = NULL;
    upipe_audiocont->input_prev = NULL;
    upipe_audiocont->input_name = NULL;
    upipe_audiocont->crossblend_period = CROSSBLEND_PERIOD;
    upipe_audiocont->crossblend_step = (float)UCLOCK_FREQ /
                                       upipe_audiocont->samplerate /
                                       upipe_audiocont->crossblend_period;
    upipe_audiocont->crossblend = 0.;
    upipe_audiocont->latency = 0;

    upipe_throw_ready(upipe);
    upipe_dbg_va(upipe, "using crossblend step %f",
                 upipe_audiocont->crossblend_step);
    struct uref *flow_def_check =
        upipe_audiocont_alloc_flow_def_check(upipe, flow_def);
    uref_sound_flow_copy_format(flow_def_check, flow_def);
    uref_free(flow_def);
    upipe_audiocont_store_flow_def_check(upipe, flow_def_check);

    return upipe;
}

/** @internal @This switches to a new input.
 *
 * @param upipe description structure of the pipe
 * @param input description structure of the input pipe
 * @return an error code
 */
static int upipe_audiocont_switch_input(struct upipe *upipe,
                                        struct upipe *input)
{
    struct upipe_audiocont *upipe_audiocont = upipe_audiocont_from_upipe(upipe);
    const char *name = upipe_audiocont->input_name ?
                       upipe_audiocont->input_name : "(noname)";
    upipe_audiocont->input_prev = upipe_audiocont->input_cur;
    upipe_audiocont->input_cur = input;
    upipe_audiocont->crossblend = 0.;
    upipe_notice_va(upipe, "switched to input \"%s\" (%p previous %p)",
                    name, input, upipe_audiocont->input_prev);

    return UBASE_ERR_NONE;
}

/** @internal @This is called when an ubuf manager is provided.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_audiocont_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL)
        upipe_audiocont_store_flow_def(upipe, flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This processes reference ("clock") input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_audiocont_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_audiocont *upipe_audiocont = upipe_audiocont_from_upipe(upipe);
    uint64_t next_pts = 0, next_duration = 0;

    if (unlikely(upipe_audiocont->flow_def == NULL)) {
        upipe_warn_va(upipe, "need to define flow def first");
        uref_free(uref);
        return;
    }
    assert(upipe_audiocont->ubuf_mgr != NULL);

    if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &next_pts)))) {
        upipe_warn_va(upipe, "packet without pts");
        uref_free(uref);
        return;
    }
    if (unlikely(!ubase_check(uref_clock_get_duration(uref, &next_duration)))) {
        upipe_warn_va(upipe, "packet without duration");
        uref_free(uref);
        return;
    }

    size_t ref_size = 0;
    uint8_t sample_size = 0;
    if (unlikely(!ubase_check(uref_sound_size(uref, &ref_size, NULL)))) {
        upipe_warn_va(upipe, "invalid ref packet");
        uref_free(uref);
        return;
    }

    /* clean old urefs first */
    struct uchain *uchain_sub;
    ulist_foreach(&upipe_audiocont->subs, uchain_sub) {
        struct upipe_audiocont_sub *sub =
               upipe_audiocont_sub_from_uchain(uchain_sub);
        upipe_audiocont_sub_consume(upipe_audiocont_sub_to_upipe(sub),
                                    next_pts, next_duration);
    }

    if (unlikely(!upipe_audiocont->input_cur))
        goto output;

    /* alloc ubuf and attach to reference uref */
    struct ubuf *ubuf = ubuf_sound_alloc(upipe_audiocont->ubuf_mgr, ref_size);
    if (unlikely(!ubuf)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }
    uref_attach_ubuf(uref, ubuf);
    uref_sound_size(uref, NULL, &sample_size);

    /* clear sound buffer */
    const char *channel = NULL;
    while (ubase_check(uref_sound_plane_iterate(uref, &channel)) && channel) {
        float *buf;
        uref_sound_plane_write_float(uref, channel, 0, -1, &buf);
        for (int i = 0; i < ref_size; i++)
            for (int j = 0; j < sample_size / sizeof(float); j++)
                *buf++ = 0.;
        uref_sound_plane_unmap(uref, channel, 0, -1);
    }

    /* check if the previous stream is needed */
    if (unlikely(upipe_audiocont->crossblend < 1. &&
                 upipe_audiocont->input_prev != NULL)) {
        int err = upipe_audiocont_sub_extract(upipe_audiocont->input_prev, ubuf,
                next_pts, next_duration, upipe_audiocont->crossblend, true);
        if (!ubase_check(err))
            upipe_throw_error(upipe, err);
    }

    /* blend in the current stream */
    int err = upipe_audiocont_sub_extract(upipe_audiocont->input_cur, ubuf,
            next_pts, next_duration, upipe_audiocont->crossblend, false);
    if (!ubase_check(err))
        upipe_throw_error(upipe, err);

output:
    if (upipe_audiocont->crossblend < 1.) {
        upipe_audiocont->crossblend += upipe_audiocont->crossblend_step *
                                       ref_size;
        if (upipe_audiocont->crossblend >= 1.)
            upipe_dbg(upipe, "end of crossblending");
    }
    upipe_audiocont_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_audiocont_set_flow_def(struct upipe *upipe,
                                        struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct uref *flow_def_check =
        upipe_audiocont_alloc_flow_def_check(upipe, flow_def);
    if (flow_def_check == NULL ||
        !ubase_check(uref_sound_flow_copy_format(flow_def_check, flow_def)) ||
        !upipe_audiocont_check_flow_def_check(upipe, flow_def_check)) {
        uref_free(flow_def_check);
        return UBASE_ERR_INVALID;
    }
    uref_free(flow_def_check);

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_audiocont_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input by name.
 *
 * @param upipe description structure of the pipe
 * @param name input name
 * @return an error code
 */
static int _upipe_audiocont_set_input(struct upipe *upipe, const char *name)
{
    struct upipe_audiocont *upipe_audiocont = upipe_audiocont_from_upipe(upipe);
    char *name_dup = NULL;
    free(upipe_audiocont->input_name);

    if (name) {
        name_dup = strdup(name);
        if (unlikely(!name_dup)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }

        upipe_audiocont->input_name = name_dup;

        struct uchain *uchain;
        ulist_foreach(&upipe_audiocont->subs, uchain) {
            struct upipe_audiocont_sub *sub =
                       upipe_audiocont_sub_from_uchain(uchain);
            const char *flow_name = NULL;
            if (sub->flow_def
                && likely(ubase_check(uref_flow_get_name(sub->flow_def, &flow_name)))
                && !strcmp(name_dup, flow_name)) {
                upipe_audiocont_switch_input(upipe, upipe_audiocont_sub_to_upipe(sub));
                break;
            }
        }
    } else {
        upipe_audiocont->input_name = NULL;
        upipe_audiocont_switch_input(upipe, NULL);
    }

    return UBASE_ERR_NONE;
}

/** @This returns the current input name if any.
 *
 * @param upipe description structure of the pipe
 * @param name_p filled with current input name pointer or NULL
 * @return an error code
 */
static int _upipe_audiocont_get_current_input(struct upipe *upipe,
                                              const char **name_p)
{
    struct upipe_audiocont *upipe_audiocont = upipe_audiocont_from_upipe(upipe);
    if (unlikely(!name_p)) {
        return UBASE_ERR_INVALID;
    }

    *name_p = NULL;
    if (upipe_audiocont->input_cur) {
        struct upipe_audiocont_sub *sub = 
               upipe_audiocont_sub_from_upipe(upipe_audiocont->input_cur);
        if (sub->flow_def) {
            uref_flow_get_name(sub->flow_def, name_p);
        }
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_audiocont_control(struct upipe *upipe,
                                   int command, va_list args)
{
    struct upipe_audiocont *upipe_audiocont = upipe_audiocont_from_upipe(upipe);
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_audiocont_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_audiocont_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_audiocont_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_audiocont_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_audiocont_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_audiocont_set_output(upipe, output);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_audiocont_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_audiocont_iterate_sub(upipe, p);
        }

        case UPIPE_AUDIOCONT_SET_INPUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUDIOCONT_SIGNATURE)
            const char *name = va_arg(args, const char*);
            return _upipe_audiocont_set_input(upipe, name);
        }
        case UPIPE_AUDIOCONT_GET_INPUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUDIOCONT_SIGNATURE)
            *va_arg(args, const char**) = upipe_audiocont->input_name;
            return UBASE_ERR_NONE;
        }
        case UPIPE_AUDIOCONT_GET_CURRENT_INPUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUDIOCONT_SIGNATURE)
            const char **name_p = va_arg(args, const char **);
            return _upipe_audiocont_get_current_input(upipe, name_p);
        }
        case UPIPE_AUDIOCONT_SET_LATENCY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUDIOCONT_SIGNATURE)
            upipe_audiocont->latency = va_arg(args, uint64_t);
            return UBASE_ERR_NONE;
        }
        case UPIPE_AUDIOCONT_GET_LATENCY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUDIOCONT_SIGNATURE)
            *va_arg(args, uint64_t *) = upipe_audiocont->latency;
            return UBASE_ERR_NONE;
        }
        case UPIPE_AUDIOCONT_SET_CROSSBLEND: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUDIOCONT_SIGNATURE)
            upipe_audiocont->crossblend_period = va_arg(args, uint64_t);
            upipe_audiocont->crossblend_step = (float)UCLOCK_FREQ /
                upipe_audiocont->samplerate /
                upipe_audiocont->crossblend_period;
            return UBASE_ERR_NONE;
        }
        case UPIPE_AUDIOCONT_GET_CROSSBLEND: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUDIOCONT_SIGNATURE)
            *va_arg(args, uint64_t *) = upipe_audiocont->crossblend_period;
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audiocont_free(struct upipe *upipe)
{
    struct upipe_audiocont *upipe_audiocont = upipe_audiocont_from_upipe(upipe);
    upipe_throw_dead(upipe);

    free(upipe_audiocont->input_name);

    upipe_audiocont_clean_sub_subs(upipe);
    upipe_audiocont_clean_output(upipe);
    upipe_audiocont_clean_ubuf_mgr(upipe);
    upipe_audiocont_clean_flow_def_check(upipe);
    upipe_audiocont_clean_urefcount(upipe);
    upipe_audiocont_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_audiocont_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AUDIOCONT_SIGNATURE,

    .upipe_alloc = upipe_audiocont_alloc,
    .upipe_input = upipe_audiocont_input,
    .upipe_control = upipe_audiocont_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all audiocont pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_audiocont_mgr_alloc(void)
{
    return &upipe_audiocont_mgr;
}
