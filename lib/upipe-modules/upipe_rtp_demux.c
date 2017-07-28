/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-modules/upipe_rtp_demux.h>
#include <upipe-modules/upipe_rtp_decaps.h>
#include <upipe-modules/upipe_idem.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** max interval between clock refs */
#define MAX_CLOCK_REF_INTERVAL UCLOCK_FREQ
/** max retention time for all streams */
#define MAX_DELAY (UCLOCK_FREQ * 60)
/** RTP timestamps wrap at 32 bits */
#define POW2_32 UINT64_C(4294967296)

/** @internal @This is the private context of a rtp_demux manager. */
struct upipe_rtp_demux_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pointer to rtpd manager */
    struct upipe_mgr *rtpd_mgr;
    /** pointer to idem manager */
    struct upipe_mgr *idem_mgr;
    /** pointer to autof manager */
    struct upipe_mgr *autof_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_rtp_demux_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_rtp_demux_mgr, urefcount, urefcount, urefcount)

/** @internal @This is the private context of a rtp_demux pipe. */
struct upipe_rtp_demux {
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** offset between orig and prog */
    int64_t orig_prog_offset;
    /** cr_prog of last clock ref */
    uint64_t last_cr;
    /** highest date_prog given to a frame */
    uint64_t highest_date_prog;

    /** list of subpipes */
    struct uchain subs;
    /** manager to create subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtp_demux, upipe, UPIPE_RTP_DEMUX_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_rtp_demux, urefcount, upipe_rtp_demux_free)
UPIPE_HELPER_VOID(upipe_rtp_demux)

static void upipe_rtp_demux_handle_clock_ref(struct upipe *upipe,
        struct uref *uref, uint64_t pcr_orig, int discontinuity, uint64_t wrap);
static void upipe_rtp_demux_check_clock_ref(struct upipe *upipe);

/** @internal @This is the private context of a subpipe of a rtp_demux pipe. */
struct upipe_rtp_demux_sub {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** probe for the source */
    struct uprobe source_probe;
    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;

    /** first inner pipe of the bin (rtpd) */
    struct upipe *first_inner;
    /** last inner pipe of the bin (framer) */
    struct upipe *last_inner;
    /** list of input bin requests */
    struct uchain input_request_list;
    /** list of output bin requests */
    struct uchain output_request_list;
    /** output */
    struct upipe *output;

    /** wrap around threshold */
    uint64_t wrap;
    /** true if this pipe is used as clock reference */
    bool clock_ref;
    /** true if this pipe is a sound sub */
    bool sound;
    /** sample rate if this pipe is a sound sub */
    uint64_t rate;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtp_demux_sub, upipe, UPIPE_RTP_DEMUX_SUB_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_rtp_demux_sub, urefcount,
                       upipe_rtp_demux_sub_no_ref)
UPIPE_HELPER_VOID(upipe_rtp_demux_sub)
UPIPE_HELPER_UPROBE(upipe_rtp_demux_sub, urefcount_real, last_inner_probe,
                    NULL)
UPIPE_HELPER_INNER(upipe_rtp_demux_sub, first_inner)
UPIPE_HELPER_BIN_INPUT(upipe_rtp_demux_sub, first_inner, input_request_list)
UPIPE_HELPER_INNER(upipe_rtp_demux_sub, last_inner)
UPIPE_HELPER_BIN_OUTPUT(upipe_rtp_demux_sub, last_inner,
                        output, output_request_list)

UPIPE_HELPER_SUBPIPE(upipe_rtp_demux, upipe_rtp_demux_sub, sub, sub_mgr, subs,
                     uchain)

UBASE_FROM_TO(upipe_rtp_demux_sub, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_rtp_demux_sub_free(struct urefcount *urefcount_real);

/** @internal @This catches clock_ts events coming from rtpd inner pipes.
 *
 * @param upipe description structure of the pipe
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_rtp_demux_sub_clock_ts(struct upipe *upipe,
                                        struct upipe *inner,
                                        int event, va_list args)
{
    struct upipe_rtp_demux_sub *sub = upipe_rtp_demux_sub_from_upipe(upipe);
    struct upipe_rtp_demux *demux = upipe_rtp_demux_from_sub_mgr(upipe->mgr);

    struct uref *uref = va_arg(args, struct uref *);
    uint64_t date_orig;
    int type;
    uref_clock_get_date_orig(uref, &date_orig, &type);
    if (type != UREF_DATE_NONE) {
        if (sub->clock_ref)
            upipe_rtp_demux_handle_clock_ref(upipe_rtp_demux_to_upipe(demux),
                                             uref, date_orig, false, sub->wrap);

        /* handle wrap-arounds */
        int64_t delta = (sub->wrap * 3 / 2 + date_orig -
                        (demux->last_cr % sub->wrap)) % sub->wrap -
                        sub->wrap / 2;
        if (delta <= (int64_t)MAX_DELAY && delta >= -(int64_t)MAX_DELAY) {
            uint64_t date_prog = demux->orig_prog_offset +
                                 demux->last_cr + delta;
            uref_clock_set_date_prog(uref, date_prog, type);

            if (date_prog > demux->highest_date_prog)
                demux->highest_date_prog = date_prog;
        } else
            upipe_warn_va(upipe,
                          "too long delay for date %"PRIu64" (%"PRId64")",
                          date_orig, delta);
    }

    return upipe_throw(upipe, event, uref);
}

/** @internal @This catches new_flow_def events coming from rtpd inner pipes.
 *
 * @param upipe description structure of the pipe
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_rtp_demux_sub_new_flow_def(struct upipe *upipe,
                                            struct upipe *inner,
                                            int event, va_list args)
{
    struct upipe_rtp_demux_sub *sub = upipe_rtp_demux_sub_from_upipe(upipe);
    struct upipe_rtp_demux *demux = upipe_rtp_demux_from_sub_mgr(
                upipe_rtp_demux_sub_to_upipe(sub)->mgr);
    struct uref *uref = va_arg(args, struct uref *);
    const char *def;
    UBASE_RETURN(uref_flow_get_def(uref, &def))
    sub->sound = ubase_ncmp(def, "sound.") || strstr(def, ".sound.");
    sub->rate = 1;
    uref_sound_flow_get_rate(uref, &sub->rate);
    upipe_rtp_demux_check_clock_ref(upipe_rtp_demux_to_upipe(demux));
    return uref_clock_get_wrap(uref, &sub->wrap);
}

/** @internal @This catches need_output events coming from rtpd inner pipes.
 *
 * @param upipe description structure of the pipe
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_rtp_demux_sub_plumber(struct upipe *upipe,
                                       struct upipe *inner,
                                       int event, va_list args)
{
    struct upipe_rtp_demux_sub *sub = upipe_rtp_demux_sub_from_upipe(upipe);
    struct upipe_rtp_demux *demux = upipe_rtp_demux_from_sub_mgr(
                upipe_rtp_demux_sub_to_upipe(sub)->mgr);
    struct upipe_rtp_demux_mgr *rtp_demux_mgr =
        upipe_rtp_demux_mgr_from_upipe_mgr(upipe_rtp_demux_to_upipe(demux)->mgr);

    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(event, args, &flow_def, &def))
        return upipe_throw_proxy(upipe, inner, event, args);

    if (rtp_demux_mgr->autof_mgr != NULL) {
        /* allocate autof inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, rtp_demux_mgr->autof_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&sub->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "autof"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_rtp_demux_sub_store_bin_output(upipe, output);
        return UBASE_ERR_NONE;
    }

    upipe_warn_va(upipe, "unframed output flow definition: %s", def);
    /* allocate idem inner */
    struct upipe *output =
        upipe_void_alloc_output(inner, rtp_demux_mgr->idem_mgr,
            uprobe_pfx_alloc(
                uprobe_use(&sub->last_inner_probe),
                UPROBE_LOG_VERBOSE, "idem"));
    if (unlikely(output == NULL))
        return UBASE_ERR_ALLOC;
    upipe_rtp_demux_sub_store_bin_output(upipe, output);
    return UBASE_ERR_NONE;
}

/** @internal @This catches events coming from source inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_rtp_demux_sub
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_rtp_demux_sub_probe(struct uprobe *uprobe,
                                     struct upipe *inner,
                                     int event, va_list args)
{
    struct upipe_rtp_demux_sub *sub =
        container_of(uprobe, struct upipe_rtp_demux_sub, source_probe);
    struct upipe *upipe = upipe_rtp_demux_sub_to_upipe(sub);

    switch (event) {
        case UPROBE_CLOCK_TS:
            return upipe_rtp_demux_sub_clock_ts(upipe, inner, event, args);
        case UPROBE_NEW_FLOW_DEF:
            return upipe_rtp_demux_sub_new_flow_def(upipe, inner, event, args);
        case UPROBE_NEED_OUTPUT:
            return upipe_rtp_demux_sub_plumber(upipe, inner, event, args);
        default:
            return upipe_throw_proxy(upipe, inner, event, args);
    }
}

/** @internal @This allocates an output subpipe of a rtp_demux_program subpipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtp_demux_sub_alloc(struct upipe_mgr *mgr,
                                               struct uprobe *uprobe,
                                               uint32_t signature,
                                               va_list args)
{
    struct upipe *upipe = upipe_rtp_demux_sub_alloc_void(mgr, uprobe,
                                                         signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_rtp_demux_sub *sub = upipe_rtp_demux_sub_from_upipe(upipe);
    upipe_rtp_demux_sub_init_urefcount(upipe);
    urefcount_init(upipe_rtp_demux_sub_to_urefcount_real(sub),
                   upipe_rtp_demux_sub_free);
    upipe_rtp_demux_sub_init_last_inner_probe(upipe);
    upipe_rtp_demux_sub_init_bin_input(upipe);
    upipe_rtp_demux_sub_init_bin_output(upipe);
    uprobe_init(&sub->source_probe, upipe_rtp_demux_sub_probe, NULL);
    sub->source_probe.refcount = upipe_rtp_demux_sub_to_urefcount_real(sub);
    sub->clock_ref = false;
    sub->sound = false;
    sub->rate = 0;
    sub->wrap = POW2_32 * UCLOCK_FREQ / 90000;
    upipe_rtp_demux_sub_init_sub(upipe);
    upipe_throw_ready(upipe);

    struct upipe_rtp_demux *demux = upipe_rtp_demux_from_sub_mgr(upipe->mgr);
    struct upipe_rtp_demux_mgr *rtp_demux_mgr =
        upipe_rtp_demux_mgr_from_upipe_mgr(upipe_rtp_demux_to_upipe(demux)->mgr);
    /* set up rtpd inner pipe */
    /* TODO add rtp_reorder */
    struct upipe *source;
    if (unlikely((source = upipe_void_alloc(
                        rtp_demux_mgr->rtpd_mgr,
                        uprobe_pfx_alloc(uprobe_use(&sub->source_probe),
                                         UPROBE_LOG_VERBOSE, "rtpd"))) == NULL))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    upipe_rtp_demux_sub_store_first_inner(upipe, source);

    upipe_rtp_demux_check_clock_ref(upipe_rtp_demux_to_upipe(demux));
    return upipe;
}

/** @internal @This processes control commands on a rtp_demux_sub pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtp_demux_sub_control(struct upipe *upipe,
                                       int command, va_list args)
{
    switch (command) {
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_rtp_demux_sub_get_super(upipe, p);
        }

        default:
            break;
    }

    int err = upipe_rtp_demux_sub_control_bin_input(upipe, command, args);
    if (err == UBASE_ERR_UNHANDLED)
        return upipe_rtp_demux_sub_control_bin_output(upipe, command, args);
    return err;
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_rtp_demux_sub_free(struct urefcount *urefcount_real)
{
    struct upipe_rtp_demux_sub *sub =
        upipe_rtp_demux_sub_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_rtp_demux_sub_to_upipe(sub);

    upipe_throw_dead(upipe);
    upipe_rtp_demux_sub_clean_last_inner_probe(upipe);
    uprobe_clean(&sub->source_probe);
    urefcount_clean(urefcount_real);
    upipe_rtp_demux_sub_clean_urefcount(upipe);
    upipe_rtp_demux_sub_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtp_demux_sub_no_ref(struct upipe *upipe)
{
    struct upipe_rtp_demux_sub *sub = upipe_rtp_demux_sub_from_upipe(upipe);
    struct upipe_rtp_demux *demux = upipe_rtp_demux_from_sub_mgr(upipe->mgr);

    upipe_rtp_demux_sub_clean_bin_input(upipe);
    upipe_rtp_demux_sub_clean_bin_output(upipe);
    upipe_rtp_demux_sub_clean_sub(upipe);
    upipe_rtp_demux_check_clock_ref(upipe_rtp_demux_to_upipe(demux));
    urefcount_release(upipe_rtp_demux_sub_to_urefcount_real(sub));
}

/** @internal @This initializes the sub manager for a rtp_demux subpipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtp_demux_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_rtp_demux *demux = upipe_rtp_demux_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &demux->sub_mgr;
    sub_mgr->refcount = upipe->refcount;
    sub_mgr->signature = UPIPE_RTP_DEMUX_SUB_SIGNATURE;
    sub_mgr->upipe_err_str = NULL;
    sub_mgr->upipe_command_str = NULL;
    sub_mgr->upipe_event_str = NULL;
    sub_mgr->upipe_alloc = upipe_rtp_demux_sub_alloc;
    sub_mgr->upipe_input = upipe_rtp_demux_sub_bin_input;
    sub_mgr->upipe_control = upipe_rtp_demux_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This handles clock references coming from clock_ref events.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the PCR
 * @param pcr_orig PCR value
 * @param discontinuity true if a discontinuity occurred before
 * @param wrap wrap-around threshold
 */
static void upipe_rtp_demux_handle_clock_ref(struct upipe *upipe,
        struct uref *uref, uint64_t pcr_orig, int discontinuity, uint64_t wrap)
{
    struct upipe_rtp_demux *demux = upipe_rtp_demux_from_upipe(upipe);
    upipe_verbose_va(upipe, "read clock ref %"PRIu64, pcr_orig);

    /* handle wrap-arounds */
    uint64_t delta = (wrap + pcr_orig - (demux->last_cr % wrap)) % wrap;
    if (delta <= MAX_CLOCK_REF_INTERVAL && !discontinuity)
        demux->last_cr += delta;
    else {
        upipe_warn_va(upipe, "clock ref discontinuity %"PRIu64, delta);
        demux->last_cr = pcr_orig;
        demux->orig_prog_offset = demux->highest_date_prog - pcr_orig;
        discontinuity = 1;
    }

    upipe_throw_clock_ref(upipe, uref, demux->last_cr + demux->orig_prog_offset,
                          discontinuity);
}

/** @internal @This finds out which elementary stream is the best candidate
 * for clock references.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtp_demux_check_clock_ref(struct upipe *upipe)
{
    struct upipe_rtp_demux *demux = upipe_rtp_demux_from_upipe(upipe);
    if (upipe_dead(upipe) || ulist_empty(&demux->subs))
        return;

    struct upipe_rtp_demux_mgr *rtp_demux_mgr =
        upipe_rtp_demux_mgr_from_upipe_mgr(upipe_rtp_demux_to_upipe(demux)->mgr);
    struct uchain *uchain;
    struct upipe_rtp_demux_sub *selected = NULL;
    uint64_t selected_rate = 0;
    ulist_foreach (&demux->subs, uchain) {
        struct upipe_rtp_demux_sub *sub =
            upipe_rtp_demux_sub_from_uchain(uchain);
        sub->clock_ref = false;
        if (sub->sound && sub->rate > selected_rate) {
            selected = sub;
            selected_rate = sub->rate;
        }
    }

    if (selected == NULL) {
        uchain = ulist_peek(&demux->subs);
        selected = upipe_rtp_demux_sub_from_uchain(uchain);
    }

    upipe_dbg(upipe_rtp_demux_sub_to_upipe(selected),
              "selected as clock reference");
    selected->clock_ref = true;
}

/** @internal @This allocates a rtp_demux pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtp_demux_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_rtp_demux_alloc_void(mgr, uprobe, signature,
                                                     args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_rtp_demux *demux = upipe_rtp_demux_from_upipe(upipe);
    upipe_rtp_demux_init_urefcount(upipe);
    upipe_rtp_demux_init_sub_mgr(upipe);
    upipe_rtp_demux_init_sub_subs(upipe);
    demux->orig_prog_offset = 0;
    demux->highest_date_prog = UINT32_MAX;
    demux->last_cr = UINT32_MAX;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This processes control commands on a rtp_demux pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtp_demux_control(struct upipe *upipe,
                                   int command, va_list args)
{
    return upipe_rtp_demux_control_subs(upipe, command, args);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtp_demux_free(struct upipe *upipe)
{
    struct upipe_rtp_demux *demux = upipe_rtp_demux_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_rtp_demux_clean_sub_subs(upipe);
    upipe_rtp_demux_clean_urefcount(upipe);
    upipe_rtp_demux_free_void(upipe);
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_rtp_demux_mgr_free(struct urefcount *urefcount)
{
    struct upipe_rtp_demux_mgr *rtp_demux_mgr =
        upipe_rtp_demux_mgr_from_urefcount(urefcount);
    upipe_mgr_release(rtp_demux_mgr->rtpd_mgr);
    upipe_mgr_release(rtp_demux_mgr->idem_mgr);
    upipe_mgr_release(rtp_demux_mgr->autof_mgr);

    urefcount_clean(urefcount);
    free(rtp_demux_mgr);
}

/** @This processes control commands on a rtp_demux manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtp_demux_mgr_control(struct upipe_mgr *mgr,
                                      int command, va_list args)
{
    struct upipe_rtp_demux_mgr *rtp_demux_mgr =
        upipe_rtp_demux_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_RTP_DEMUX_MGR_GET_##NAME##_MGR: {                        \
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_DEMUX_SIGNATURE)          \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = rtp_demux_mgr->name##_mgr;                                 \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_RTP_DEMUX_MGR_SET_##NAME##_MGR: {                        \
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_DEMUX_SIGNATURE)          \
            if (!urefcount_single(&rtp_demux_mgr->urefcount))               \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(rtp_demux_mgr->name##_mgr);                   \
            rtp_demux_mgr->name##_mgr = upipe_mgr_use(m);                   \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(rtpd, RTPD)
        GET_SET_MGR(idem, IDEM)
        GET_SET_MGR(autof, AUTOF)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all rtp_demux pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtp_demux_mgr_alloc(void)
{
    struct upipe_rtp_demux_mgr *rtp_demux_mgr =
        malloc(sizeof(struct upipe_rtp_demux_mgr));
    if (unlikely(rtp_demux_mgr == NULL))
        return NULL;

    memset(rtp_demux_mgr, 0, sizeof(*rtp_demux_mgr));
    rtp_demux_mgr->rtpd_mgr = upipe_rtpd_mgr_alloc();
    rtp_demux_mgr->idem_mgr = upipe_idem_mgr_alloc();
    rtp_demux_mgr->autof_mgr = NULL;

    urefcount_init(upipe_rtp_demux_mgr_to_urefcount(rtp_demux_mgr),
                   upipe_rtp_demux_mgr_free);
    rtp_demux_mgr->mgr.refcount =
        upipe_rtp_demux_mgr_to_urefcount(rtp_demux_mgr);
    rtp_demux_mgr->mgr.signature = UPIPE_RTP_DEMUX_SIGNATURE;
    rtp_demux_mgr->mgr.upipe_alloc = upipe_rtp_demux_alloc;
    rtp_demux_mgr->mgr.upipe_input = NULL;
    rtp_demux_mgr->mgr.upipe_control = upipe_rtp_demux_control;
    rtp_demux_mgr->mgr.upipe_mgr_control = upipe_rtp_demux_mgr_control;
    return upipe_rtp_demux_mgr_to_upipe_mgr(rtp_demux_mgr);
}
