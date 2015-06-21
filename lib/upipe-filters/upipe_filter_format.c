/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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
 * @short Bin pipe transforming the input to the given format
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow_format.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-modules/upipe_idem.h>
#include <upipe-filters/upipe_filter_format.h>
#include <upipe-filters/upipe_filter_blend.h>
#include <upipe-swscale/upipe_sws.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a ffmt manager. */
struct upipe_ffmt_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pointer to swscale manager */
    struct upipe_mgr *sws_mgr;
    /** pointer to swresample manager */
    struct upipe_mgr *swr_mgr;
    /** pointer to deinterlace manager */
    struct upipe_mgr *deint_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_ffmt_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_ffmt_mgr, urefcount, urefcount, urefcount)

/** @hidden */
static bool upipe_ffmt_handle(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p);
/** @hidden */
static int upipe_ffmt_check_flow_format(struct upipe *upipe,
                                        struct uref *flow_format);

/** @internal @This is the private context of a ffmt pipe. */
struct upipe_ffmt {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** flow format request */
    struct urequest request;

    /** proxy probe */
    struct uprobe proxy_probe;
    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;

    /** flow definition on the input */
    struct uref *flow_def_input;
    /** flow definition wanted on the output */
    struct uref *flow_def_wanted;
    /** list of input bin requests */
    struct uchain input_request_list;
    /** list of output bin requests */
    struct uchain output_request_list;
    /** first inner pipe of the bin (deint or sws or swr) */
    struct upipe *first_inner;
    /** last inner pipe of the bin (sws or swr) */
    struct upipe *last_inner;
    /** output */
    struct upipe *output;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** swscale flags */
    int sws_flags;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ffmt, upipe, UPIPE_FFMT_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ffmt, NULL)
UPIPE_HELPER_UREFCOUNT(upipe_ffmt, urefcount, upipe_ffmt_no_ref)
UPIPE_HELPER_INPUT(upipe_ffmt, urefs, nb_urefs, max_urefs, blockers,
                  upipe_ffmt_handle)
UPIPE_HELPER_BIN_INPUT(upipe_ffmt, first_inner, input_request_list)
UPIPE_HELPER_BIN_OUTPUT(upipe_ffmt, last_inner_probe, last_inner, output,
                        output_request_list)
UPIPE_HELPER_FLOW_FORMAT(upipe_ffmt, request,
                         upipe_ffmt_check_flow_format,
                         upipe_ffmt_register_bin_output_request,
                         upipe_ffmt_unregister_bin_output_request)

UBASE_FROM_TO(upipe_ffmt, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_ffmt_free(struct urefcount *urefcount_real);

/** @internal @This catches events coming from an inner pipe, and
 * attaches them to the bin pipe.
 *
 * @param uprobe pointer to the probe in upipe_ffmt_alloc
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ffmt_proxy_probe(struct uprobe *uprobe, struct upipe *inner,
                                  int event, va_list args)
{
    struct upipe_ffmt *s = container_of(uprobe, struct upipe_ffmt,
                                          proxy_probe);
    struct upipe *upipe = upipe_ffmt_to_upipe(s);
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This allocates a ffmt pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ffmt_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ffmt_alloc_flow(mgr, uprobe, signature, args,
                                                &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);
    upipe_ffmt_init_urefcount(upipe);
    urefcount_init(upipe_ffmt_to_urefcount_real(upipe_ffmt), upipe_ffmt_free);
    upipe_ffmt_init_flow_format(upipe);
    upipe_ffmt_init_input(upipe);
    upipe_ffmt_init_bin_input(upipe);
    upipe_ffmt_init_bin_output(upipe, upipe_ffmt_to_urefcount_real(upipe_ffmt));

    uprobe_init(&upipe_ffmt->proxy_probe, upipe_ffmt_proxy_probe, NULL);
    upipe_ffmt->proxy_probe.refcount =
        upipe_ffmt_to_urefcount_real(upipe_ffmt);
    upipe_ffmt->flow_def_input = NULL;
    upipe_ffmt->flow_def_wanted = flow_def;
    upipe_ffmt->sws_flags = 0;
    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return always true
 */
static bool upipe_ffmt_handle(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        if (upipe_ffmt->flow_def_input != NULL &&
            upipe_ffmt->flow_def_input->udict != NULL && uref->udict != NULL &&
            !udict_cmp(upipe_ffmt->flow_def_input->udict, uref->udict)) {
            uref_free(uref);
            return true;
        }
        uref_free(upipe_ffmt->flow_def_input);
        upipe_ffmt->flow_def_input = uref_dup(uref);
        if (unlikely(upipe_ffmt->flow_def_input == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return true;
        }

        char *old_def = NULL;
        if (!ubase_ncmp(def, "sound."))
            old_def = strdup(def);
        uref_attr_import(uref, upipe_ffmt->flow_def_wanted);
        if (old_def != NULL) {
            uref_flow_set_def(uref, old_def);
            free(old_def);
        }

        upipe_ffmt_store_first_inner(upipe, NULL);
        upipe_ffmt_store_last_inner(upipe, NULL);
        upipe_ffmt_require_flow_format(upipe, uref);
        return true;
    }

    if (upipe_ffmt->first_inner == NULL)
        return false;

    upipe_ffmt_bin_input(upipe, uref, upump_p);
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ffmt_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    if (!upipe_ffmt_check_input(upipe)) {
        upipe_ffmt_hold_input(upipe, uref);
        upipe_ffmt_block_input(upipe, upump_p);
    } else if (!upipe_ffmt_handle(upipe, uref, upump_p)) {
        upipe_ffmt_hold_input(upipe, uref);
        upipe_ffmt_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This receives the result of a flow format request.
 *
 * @param upipe description structure of the pipe
 * @param flow_def_dup amended flow format
 * @return an error code
 */
static int upipe_ffmt_check_flow_format(struct upipe *upipe,
                                        struct uref *flow_def_dup)
{
    struct upipe_ffmt_mgr *ffmt_mgr = upipe_ffmt_mgr_from_upipe_mgr(upipe->mgr);
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);
    if (flow_def_dup == NULL)
        return UBASE_ERR_INVALID;

    struct uref *flow_def = uref_dup(upipe_ffmt->flow_def_input);
    UBASE_ALLOC_RETURN(flow_def)
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))

    if (!ubase_ncmp(def, "pic.")) {
        /* check aspect ratio */
        struct urational sar, dar;
        if (ubase_check(uref_pic_flow_get_sar(upipe_ffmt->flow_def_wanted,
                                              &sar))) {
            uref_pic_flow_set_sar(flow_def, sar);
        } else if (ubase_check(uref_pic_flow_get_dar(
                        upipe_ffmt->flow_def_wanted, &dar))) {
            if (ubase_check(uref_pic_flow_get_overscan(
                            upipe_ffmt->flow_def_wanted)))
                uref_pic_flow_set_overscan(flow_def);
            else
                uref_pic_flow_delete_overscan(flow_def);
            uref_pic_flow_infer_sar(flow_def, dar);
        }

        /* delete sar and visible sizes to let sws set it */
        uref_pic_flow_delete_sar(flow_def_dup);
        uref_pic_flow_delete_hsize_visible(flow_def_dup);
        uref_pic_flow_delete_vsize_visible(flow_def_dup);

        bool need_deint = !!(uref_pic_cmp_progressive(flow_def, flow_def_dup));
        bool need_sws = !uref_pic_flow_compare_format(flow_def, flow_def_dup) ||
                        uref_pic_flow_cmp_hsize(flow_def, flow_def_dup) ||
                        uref_pic_flow_cmp_vsize(flow_def, flow_def_dup);

        if (need_deint) {
            struct upipe *input = upipe_void_alloc(ffmt_mgr->deint_mgr,
                    uprobe_pfx_alloc(
                        need_sws ? uprobe_use(&upipe_ffmt->proxy_probe) :
                                   uprobe_use(&upipe_ffmt->last_inner_probe),
                        UPROBE_LOG_VERBOSE, "deint"));
            if (unlikely(input == NULL))
                upipe_warn_va(upipe, "couldn't allocate deinterlace");
            else if (!need_sws)
                upipe_ffmt_store_last_inner(upipe, upipe_use(input));
            upipe_ffmt_store_first_inner(upipe, input);
        }

        if (need_sws) {
            struct upipe *sws = upipe_flow_alloc(ffmt_mgr->sws_mgr,
                    uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->last_inner_probe),
                                     UPROBE_LOG_VERBOSE, "sws"),
                    flow_def_dup);
            if (unlikely(sws == NULL)) {
                upipe_warn_va(upipe, "couldn't allocate swscale");
                udict_dump(flow_def_dup->udict, upipe->uprobe);
            } else if (!need_deint)
                upipe_ffmt_store_first_inner(upipe, upipe_use(sws));
            else
                upipe_set_output(upipe_ffmt->first_inner, sws);
            upipe_ffmt_store_last_inner(upipe, sws);
            if (upipe_ffmt->sws_flags)
                upipe_sws_set_flags(sws, upipe_ffmt->sws_flags);
        }

    } else { /* sound. */
        if (!uref_sound_flow_compare_format(flow_def, flow_def_dup) ||
            uref_sound_flow_cmp_rate(flow_def, flow_def_dup)) {
            struct upipe *input = upipe_flow_alloc(ffmt_mgr->swr_mgr,
                    uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->last_inner_probe),
                                     UPROBE_LOG_VERBOSE, "swr"),
                    flow_def_dup);
            if (unlikely(input == NULL)) {
                upipe_warn_va(upipe, "couldn't allocate swresample");
                udict_dump(flow_def_dup->udict, upipe->uprobe);
            } else {
                upipe_ffmt_store_first_inner(upipe, upipe_use(input));
                upipe_ffmt_store_last_inner(upipe, input);
            }
        }
    }
    uref_free(flow_def_dup);

    if (upipe_ffmt->first_inner == NULL) {
        struct upipe_mgr *idem_mgr = upipe_idem_mgr_alloc();
        struct upipe *input = upipe_void_alloc(idem_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->last_inner_probe),
                                 UPROBE_LOG_VERBOSE, "idem"));
        upipe_mgr_release(idem_mgr);
        if (unlikely(input == NULL))
            upipe_warn_va(upipe, "couldn't allocate idem");
        else {
            upipe_ffmt_store_first_inner(upipe, upipe_use(input));
            upipe_ffmt_store_last_inner(upipe, input);
        }
    }

    int err = upipe_set_flow_def(upipe_ffmt->first_inner, flow_def);
    uref_free(flow_def);

    if (!ubase_check(err)) {
        upipe_ffmt_store_first_inner(upipe, NULL);
        upipe_ffmt_store_last_inner(upipe, NULL);
        return err;
    }

    bool was_buffered = !upipe_ffmt_check_input(upipe);
    upipe_ffmt_output_input(upipe);
    upipe_ffmt_unblock_input(upipe);
    if (was_buffered && upipe_ffmt_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_ffmt_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ffmt_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def_wanted, *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    UBASE_RETURN(uref_flow_get_def(upipe_ffmt->flow_def_wanted, &def_wanted))
    if (!((!ubase_ncmp(def, "pic.") && !ubase_ncmp(def_wanted, "pic.")) ||
          (!ubase_ncmp(def, "sound.") && !ubase_ncmp(def_wanted, "sound."))))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the swscale flags.
 *
 * @param upipe description structure of the pipe
 * @param flags swscale flags
 * @return an error code
 */
static int upipe_ffmt_set_sws_flags(struct upipe *upipe, int flags)
{
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);
    upipe_ffmt->sws_flags = flags;
    if (upipe_ffmt->last_inner != NULL && flags)
        /* it may not be sws but it will just return an error */
        upipe_sws_set_flags(upipe_ffmt->last_inner, flags);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ffmt pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ffmt_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ffmt_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SWS_SET_FLAGS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SWS_SIGNATURE)
            int flags = va_arg(args, int);
            return upipe_ffmt_set_sws_flags(upipe, flags);
        }
    }

    int err = upipe_ffmt_control_bin_input(upipe, command, args);
    if (err == UBASE_ERR_UNHANDLED)
        return upipe_ffmt_control_bin_output(upipe, command, args);
    return err;
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_ffmt_free(struct urefcount *urefcount_real)
{
    struct upipe_ffmt *upipe_ffmt =
        upipe_ffmt_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_ffmt_to_upipe(upipe_ffmt);
    upipe_throw_dead(upipe);
    upipe_ffmt_clean_input(upipe);
    upipe_ffmt_clean_flow_format(upipe);
    uref_free(upipe_ffmt->flow_def_input);
    uref_free(upipe_ffmt->flow_def_wanted);
    uprobe_clean(&upipe_ffmt->proxy_probe);
    uprobe_clean(&upipe_ffmt->last_inner_probe);
    urefcount_clean(urefcount_real);
    upipe_ffmt_clean_urefcount(upipe);
    upipe_ffmt_free_flow(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ffmt_no_ref(struct upipe *upipe)
{
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);
    upipe_ffmt_clean_bin_input(upipe);
    upipe_ffmt_clean_bin_output(upipe);
    urefcount_release(upipe_ffmt_to_urefcount_real(upipe_ffmt));
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_ffmt_mgr_free(struct urefcount *urefcount)
{
    struct upipe_ffmt_mgr *ffmt_mgr = upipe_ffmt_mgr_from_urefcount(urefcount);
    upipe_mgr_release(ffmt_mgr->swr_mgr);
    upipe_mgr_release(ffmt_mgr->sws_mgr);
    upipe_mgr_release(ffmt_mgr->deint_mgr);

    urefcount_clean(urefcount);
    free(ffmt_mgr);
}

/** @This processes control commands on a ffmt manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ffmt_mgr_control(struct upipe_mgr *mgr,
                                  int command, va_list args)
{
    struct upipe_ffmt_mgr *ffmt_mgr = upipe_ffmt_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_FFMT_MGR_GET_##NAME##_MGR: {                             \
            UBASE_SIGNATURE_CHECK(args, UPIPE_FFMT_SIGNATURE)               \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = ffmt_mgr->name##_mgr;                                      \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_FFMT_MGR_SET_##NAME##_MGR: {                             \
            UBASE_SIGNATURE_CHECK(args, UPIPE_FFMT_SIGNATURE)               \
            if (!urefcount_single(&ffmt_mgr->urefcount))                    \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(ffmt_mgr->name##_mgr);                        \
            ffmt_mgr->name##_mgr = upipe_mgr_use(m);                        \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(sws, SWS)
        GET_SET_MGR(swr, SWR)
        GET_SET_MGR(deint, DEINT)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all ffmt pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ffmt_mgr_alloc(void)
{
    struct upipe_ffmt_mgr *ffmt_mgr = malloc(sizeof(struct upipe_ffmt_mgr));
    if (unlikely(ffmt_mgr == NULL))
        return NULL;

    ffmt_mgr->sws_mgr = NULL;
    ffmt_mgr->swr_mgr = NULL;
    ffmt_mgr->deint_mgr = upipe_filter_blend_mgr_alloc();

    urefcount_init(upipe_ffmt_mgr_to_urefcount(ffmt_mgr),
                   upipe_ffmt_mgr_free);
    ffmt_mgr->mgr.refcount = upipe_ffmt_mgr_to_urefcount(ffmt_mgr);
    ffmt_mgr->mgr.signature = UPIPE_FFMT_SIGNATURE;
    ffmt_mgr->mgr.upipe_alloc = upipe_ffmt_alloc;
    ffmt_mgr->mgr.upipe_input = upipe_ffmt_input;
    ffmt_mgr->mgr.upipe_control = upipe_ffmt_control;
    ffmt_mgr->mgr.upipe_mgr_control = upipe_ffmt_mgr_control;
    return upipe_ffmt_mgr_to_upipe_mgr(ffmt_mgr);
}

