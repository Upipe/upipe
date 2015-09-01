/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short Upipe module video continuity
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
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe-modules/upipe_audiocont.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** only accept sound */
#define EXPECTED_FLOW_DEF "sound."

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
    /** true if flow definition is up to date */
    bool flow_def_uptodate;
    /** input flow definition packet */
    struct uref *flow_def_input;
    /** number of planes */
    uint8_t planes;
    /** samplerate */
    uint64_t samplerate;

    /** list of input subpipes */
    struct uchain subs;

    /** current input */
    struct upipe *input_cur;
    /** next input */
    char *input_name;

    /** pts latency */
    uint64_t latency;

    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_audiocont, upipe, UPIPE_AUDIOCONT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_audiocont, urefcount, upipe_audiocont_free)
UPIPE_HELPER_VOID(upipe_audiocont)
UPIPE_HELPER_OUTPUT(upipe_audiocont, output, flow_def, output_state, request_list)
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
UPIPE_HELPER_UREFCOUNT(upipe_audiocont_sub, urefcount, upipe_audiocont_sub_dead)
UPIPE_HELPER_VOID(upipe_audiocont_sub)

UPIPE_HELPER_SUBPIPE(upipe_audiocont, upipe_audiocont_sub, sub, sub_mgr,
                     subs, uchain)

/** @hidden */
static enum ubase_err upipe_audiocont_switch_input(struct upipe *upipe,
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
static enum ubase_err upipe_audiocont_sub_set_flow_def(struct upipe *upipe,
                                                       struct uref *flow_def)
{
    struct upipe_audiocont_sub *upipe_audiocont_sub =
           upipe_audiocont_sub_from_upipe(upipe);
    struct upipe_audiocont *upipe_audiocont =
                            upipe_audiocont_from_sub_mgr(upipe->mgr);

    if (flow_def == NULL) {
        return UBASE_ERR_INVALID;
    }
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    if (unlikely(upipe_audiocont_sub->flow_def)) {
        uref_free(upipe_audiocont_sub->flow_def);
    }
    upipe_audiocont_sub->flow_def = flow_def_dup;
    upipe_audiocont->flow_def_uptodate = false;

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
static inline enum ubase_err _upipe_audiocont_sub_set_input(struct upipe *upipe)
{
    struct upipe_audiocont *upipe_audiocont =
                            upipe_audiocont_from_sub_mgr(upipe->mgr);
    struct upipe *grandpipe = upipe_audiocont_to_upipe(upipe_audiocont);
    ubase_clean_str(&upipe_audiocont->input_name);
    return upipe_audiocont_switch_input(grandpipe, upipe);
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
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_throw_provide_request(upipe, request);
            struct upipe_audiocont *upipe_audiocont =
                                    upipe_audiocont_from_sub_mgr(upipe->mgr);
            return upipe_audiocont_alloc_output_proxy(
                    upipe_audiocont_to_upipe(upipe_audiocont), request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
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

/** @This marks an input subpipe as dead.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_audiocont_sub_dead(struct upipe *upipe)
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

    if (likely(upipe_audiocont_sub->flow_def)) {
        uref_free(upipe_audiocont_sub->flow_def);
    }

    upipe_throw_dead(upipe);
    upipe_audiocont_sub_clean_sub(upipe);
    upipe_audiocont_sub_clean_urefcount(upipe);
    upipe_audiocont_sub_free_void(upipe);
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
    struct upipe *upipe = upipe_audiocont_alloc_void(mgr,
                                 uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_audiocont_init_urefcount(upipe);
    upipe_audiocont_init_ubuf_mgr(upipe);
    upipe_audiocont_init_output(upipe);
    upipe_audiocont_init_sub_mgr(upipe);
    upipe_audiocont_init_sub_subs(upipe);

    struct upipe_audiocont *upipe_audiocont = upipe_audiocont_from_upipe(upipe);
    upipe_audiocont->input_cur = NULL;
    upipe_audiocont->input_name = NULL;
    upipe_audiocont->flow_def_input = NULL;
    upipe_audiocont->flow_def_uptodate = false;
    upipe_audiocont->planes = 0;
    upipe_audiocont->samplerate = 0;
    upipe_audiocont->latency = 0;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This copies format-related information from
 * input flow to output flow.
 *
 * @param upipe description structure of the pipe
 * @param out_flow destination flow
 * @param in_flow input flow
 * @return an error code
 */
static int upipe_audiocont_switch_format(struct upipe *upipe,
                                         struct uref *out_flow,
                                         struct uref *in_flow)
{
    uref_sound_flow_clear_format(out_flow);
    uref_sound_flow_copy_format(out_flow, in_flow);
    uint8_t channels;
    if (likely(ubase_check(uref_sound_flow_get_channels(in_flow, &channels)))) {
        uref_sound_flow_set_channels(out_flow, channels);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This switches to a new input.
 *
 * @param upipe description structure of the pipe
 * @param input description structure of the input pipe
 * @return an error code
 */
static enum ubase_err upipe_audiocont_switch_input(struct upipe *upipe,
                                                   struct upipe *input)
{
    struct upipe_audiocont *upipe_audiocont = upipe_audiocont_from_upipe(upipe);
    char *name = upipe_audiocont->input_name ?
                 upipe_audiocont->input_name : "(noname)";
    upipe_audiocont->input_cur = input;
    upipe_audiocont->flow_def_uptodate = false;
    upipe_notice_va(upipe, "switched to input \"%s\" (%p)", name, input);

    return UBASE_ERR_NONE;
}

/** @internal @This resizes a sound uref.
 *
 * @param uref uref structure
 * @param offset number of samples to drop from uref
 * @param samplerate flow samplerate
 * @return an error code
 */
static inline int upipe_audiocont_resize_uref(struct uref *uref, size_t offset,
                                              uint64_t samplerate)
{
    size_t size;
    ubase_assert(uref_sound_resize(uref, offset, -1));
    uref_sound_size(uref, &size, NULL);
    uint64_t duration = (uint64_t)size * UCLOCK_FREQ / samplerate;
    uint64_t ts_offset = (uint64_t)offset * UCLOCK_FREQ / samplerate;
    uint64_t pts;
    uref_clock_set_duration(uref, duration);
    if (ubase_check(uref_clock_get_pts_prog(uref, &pts)))
        uref_clock_set_pts_prog(uref, pts + ts_offset);
    if (ubase_check(uref_clock_get_pts_sys(uref, &pts)))
        uref_clock_set_pts_sys(uref, pts + ts_offset);
    if (ubase_check(uref_clock_get_pts_orig(uref, &pts)))
        uref_clock_set_pts_orig(uref, pts + ts_offset);

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
    struct uchain *uchain, *uchain_sub, *uchain_tmp;
    uint64_t next_pts = 0, next_duration = 0;

    if (unlikely(!upipe_audiocont->flow_def_input)) {
        upipe_warn_va(upipe, "need to define flow def first");
        uref_free(uref);
        return;
    }

    /* flow_def_input or input_cur has changed */
    if (!upipe_audiocont->flow_def_uptodate) {
        /* output flow def */
        struct uref *flow_def = upipe_audiocont->flow_def_input;
        if (unlikely((flow_def = uref_dup(flow_def)) == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }

        if (upipe_audiocont->input_cur &&
                upipe_audiocont_sub_from_upipe(upipe_audiocont->input_cur)->flow_def) {
            upipe_audiocont_switch_format(upipe, flow_def,
                    upipe_audiocont_sub_from_upipe(
                        upipe_audiocont->input_cur)->flow_def);
        }
        upipe_audiocont_store_flow_def(upipe, flow_def);
        upipe_audiocont->flow_def_uptodate = true;

        if (upipe_audiocont->ubuf_mgr) {
            ubuf_mgr_release(upipe_audiocont->ubuf_mgr);
            upipe_audiocont->ubuf_mgr = NULL;
        }

        uref_sound_flow_get_planes(flow_def, &upipe_audiocont->planes);
        uref_sound_flow_get_rate(flow_def, &upipe_audiocont->samplerate);
    }

    if (unlikely(upipe_audiocont->ubuf_mgr == NULL &&
                 !upipe_audiocont_demand_ubuf_mgr(upipe,
                    uref_dup(upipe_audiocont->flow_def)))) {
        uref_free(uref);
        return;
    }

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
    int subs = 0;
    ulist_foreach(&upipe_audiocont->subs, uchain_sub) {
        struct upipe_audiocont_sub *sub =
               upipe_audiocont_sub_from_uchain(uchain_sub);
        ulist_delete_foreach(&sub->urefs, uchain, uchain_tmp) {
            uint64_t pts = 0;
            uint64_t duration = 0;
            size_t size = 0;
            struct uref *uref_uchain = uref_from_uchain(uchain);
            uref_clock_get_pts_sys(uref_uchain, &pts);
            uref_clock_get_duration(uref_uchain, &duration);
            uref_sound_size(uref_uchain, &size, NULL);

            if (pts + upipe_audiocont->latency + duration
                                               + next_duration < next_pts) {
                /* packet too old
                 * next_duration acts as a tolerance */
                upipe_verbose_va(upipe,
                        "(%d) deleted %p (%"PRIu64") next %"PRIu64"",
                                subs, uref_uchain, pts, next_pts);
                ulist_delete(uchain);
                uref_free(uref_uchain);
            } else if (pts + upipe_audiocont->latency > next_pts) {
                /* packet in the future */
                upipe_verbose_va(upipe, "(%d) packet in the future %"PRIu64,
                                 subs, pts);
                break;
            } else {
            #if 0 /* way too precise, leads to audio drops in the end */
                /* resize buffer (drop begining of packet) */
                size_t offset = (next_pts - pts - upipe_audiocont->latency)
                                  * upipe_audiocont->samplerate
                                  / UCLOCK_FREQ;
                upipe_verbose_va(upipe,
                    "(%d) %p next_pts %"PRIu64" pts %"PRIu64
                        " samplerate %"PRIu64" size %zu offset %zu",
                    subs, uref_uchain,
                    next_pts, pts, upipe_audiocont->samplerate, size, offset);
                
                if (unlikely(offset > size)) {
                    ulist_delete(uchain);
                    uref_free(uref_uchain);
                } else {
                    upipe_audiocont_resize_uref(uref_uchain, offset,
                                                upipe_audiocont->samplerate);
                    break;
                }
            #endif
            }
        }
        subs++;
    }

    uint8_t planes = upipe_audiocont->planes;
    uint8_t *ref_buffers[planes];

    if (unlikely(!upipe_audiocont->input_cur)) {
        goto output;
    }
    struct upipe_audiocont_sub *input =
        upipe_audiocont_sub_from_upipe(upipe_audiocont->input_cur);

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
    uint8_t *buf;
    while (ubase_check(uref_sound_plane_iterate(uref, &channel)) && channel) {
        uref_sound_plane_write_uint8_t(uref, channel, 0, -1, &buf);
        memset(buf, 0, sample_size * ref_size);
        uref_sound_plane_unmap(uref, channel, 0, -1);
    }

    if (unlikely(!ubase_check(uref_sound_write_uint8_t(uref, 0,
                                     -1, ref_buffers, planes)))) {
        upipe_warn_va(upipe, "could not map ref packet");
        uref_free(uref);
        return;
    }

    /* copy input sound buffer to output stream */
    size_t offset = 0;
    while (offset < ref_size) {
        struct uchain *uchain = ulist_peek(&input->urefs);
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

        size_t extracted = ((ref_size - offset) < size ) ?
                           (ref_size - offset) : size;
        upipe_verbose_va(upipe, "%p off %zu ext %zu size %zu ref %zu",
                         input_uref, offset, extracted, size, ref_size);
        const uint8_t *in_buffers[planes];
        if (unlikely(!ubase_check(uref_sound_read_uint8_t(input_uref, 0,
                                       extracted, in_buffers, planes)))) {
            upipe_warn(upipe, "invalid input buffer");
            uref_free(uref_from_uchain(ulist_pop(&input->urefs)));
            break;
        }
        int i;
        for (i=0; (i < planes) && ref_buffers[i] && in_buffers[i]; i++) {
            memcpy(ref_buffers[i] + offset * sample_size, in_buffers[i],
                   extracted * sample_size);
        }
        uref_sound_unmap(input_uref, 0, extracted, planes);

        offset += extracted;
        if (extracted == size) {
            /* input buffer entirely copied */
            uref_free(uref_from_uchain(ulist_pop(&input->urefs)));
        } else {
            /* resize input buffer (drop copied segment) */
            upipe_audiocont_resize_uref(input_uref, extracted,
                                        upipe_audiocont->samplerate);
        }
    }

    uref_sound_unmap(uref, 0, -1, planes);
    
output:
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
    struct upipe_audiocont *upipe_audiocont = upipe_audiocont_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    uint8_t planes;
    uint64_t rate;
    if (unlikely(!ubase_check(uref_sound_flow_get_planes(flow_def, &planes))
                 || !ubase_check(uref_sound_flow_get_rate(flow_def, &rate)))) {
        return UBASE_ERR_INVALID;
    }

    /* local copy of input (ref) flow def */
    if (unlikely((flow_def = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    if (upipe_audiocont->flow_def_input) {
        uref_free(upipe_audiocont->flow_def_input);
    }
    upipe_audiocont->flow_def_input = flow_def;
    upipe_audiocont->flow_def_uptodate = false;

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
    if (upipe_audiocont->flow_def_input) {
        uref_free(upipe_audiocont->flow_def_input);
    }

    upipe_audiocont_clean_sub_subs(upipe);
    upipe_audiocont_clean_output(upipe);
    upipe_audiocont_clean_ubuf_mgr(upipe);
    upipe_audiocont_clean_urefcount(upipe);
    upipe_audiocont_free_void(upipe);
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
