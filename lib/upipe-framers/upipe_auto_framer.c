/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe framers automatic detection
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe-modules/upipe_idem.h>
#include <upipe-framers/upipe_auto_framer.h>
#include <upipe-framers/upipe_mpga_framer.h>
#include <upipe-framers/upipe_a52_framer.h>
#include <upipe-framers/upipe_mpgv_framer.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/upipe_h265_framer.h>
#include <upipe-framers/upipe_telx_framer.h>
#include <upipe-framers/upipe_dvbsub_framer.h>
#include <upipe-framers/upipe_opus_framer.h>
#include <upipe-framers/upipe_s302_framer.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of an autof manager. */
struct upipe_autof_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pointer to idem manager */
    struct upipe_mgr *idem_mgr;

    /* ES */
    /** pointer to mpgaf manager */
    struct upipe_mgr *mpgaf_mgr;
    /** pointer to a52f manager */
    struct upipe_mgr *a52f_mgr;
    /** pointer to mpgvf manager */
    struct upipe_mgr *mpgvf_mgr;
    /** pointer to h264f manager */
    struct upipe_mgr *h264f_mgr;
    /** pointer to h265f manager */
    struct upipe_mgr *h265f_mgr;
    /** pointer to telxf manager */
    struct upipe_mgr *telxf_mgr;
    /** pointer to dvbsubf manager */
    struct upipe_mgr *dvbsubf_mgr;
    /** pointer to opusf manager */
    struct upipe_mgr *opusf_mgr;
    /** pointer to s302f manager */
    struct upipe_mgr *s302f_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_autof_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_autof_mgr, urefcount, urefcount, urefcount)

/** @internal @This is the private context of an autof pipe. */
struct upipe_autof {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure */
    struct urefcount urefcount;

    /** input flow def */
    struct uref *flow_def;

    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;

    /** list of input bin requests */
    struct uchain input_request_list;
    /** list of output bin requests */
    struct uchain output_request_list;
    /** first inner pipe of the bin (framer) */
    struct upipe *first_inner;
    /** last inner pipe of the bin (framer) */
    struct upipe *last_inner;
    /** output */
    struct upipe *output;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_autof, upipe, UPIPE_AUTOF_SIGNATURE)
UPIPE_HELPER_VOID(upipe_autof)
UPIPE_HELPER_UREFCOUNT(upipe_autof, urefcount, upipe_autof_no_input)
UPIPE_HELPER_INNER(upipe_autof, first_inner)
UPIPE_HELPER_BIN_INPUT(upipe_autof, first_inner, input_request_list)
UPIPE_HELPER_INNER(upipe_autof, last_inner)
UPIPE_HELPER_UPROBE(upipe_autof, urefcount_real, last_inner_probe, NULL)
UPIPE_HELPER_BIN_OUTPUT(upipe_autof, last_inner, output, output_request_list)

UBASE_FROM_TO(upipe_autof, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_autof_free(struct urefcount *urefcount_real);

/** @internal @This allocates an autof pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_autof_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_autof_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_autof *upipe_autof = upipe_autof_from_upipe(upipe);
    upipe_autof_init_urefcount(upipe);
    urefcount_init(upipe_autof_to_urefcount_real(upipe_autof),
                   upipe_autof_free);
    upipe_autof_init_last_inner_probe(upipe);
    upipe_autof_init_bin_input(upipe);
    upipe_autof_init_bin_output(upipe);
    upipe_autof->flow_def = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This allocates the framer.
 *
 * @param upipe description structure of the pipe
 * @param def flow definition string
 * @return pointer to framer
 */
static struct upipe *upipe_autof_alloc_framer(struct upipe *upipe,
                                              const char *def)
{
    struct upipe_autof_mgr *autof_mgr =
        upipe_autof_mgr_from_upipe_mgr(upipe->mgr);
    struct upipe_autof *autof = upipe_autof_from_upipe(upipe);

    if ((!ubase_ncmp(def, "block.mp2.") ||
         !ubase_ncmp(def, "block.mp3.") ||
         !ubase_ncmp(def, "block.aac.") ||
         !ubase_ncmp(def, "block.aac_latm.")) &&
        autof_mgr->mpgaf_mgr != NULL)
        return upipe_void_alloc(autof_mgr->mpgaf_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&autof->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "mpgaf"));

    if ((!ubase_ncmp(def, "block.ac3.") ||
         !ubase_ncmp(def, "block.eac3.")) &&
        autof_mgr->a52f_mgr != NULL)
        return upipe_void_alloc(autof_mgr->a52f_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&autof->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "a52f"));

    if ((!ubase_ncmp(def, "block.mpeg2video.") ||
         !ubase_ncmp(def, "block.mpeg1video.")) &&
        autof_mgr->mpgvf_mgr != NULL)
        return upipe_void_alloc(autof_mgr->mpgvf_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&autof->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "mpgvf"));

    if (!ubase_ncmp(def, "block.h264.") &&
        autof_mgr->h264f_mgr != NULL)
        return upipe_void_alloc(autof_mgr->h264f_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&autof->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "h264f"));

    if (!ubase_ncmp(def, "block.hevc.") &&
        autof_mgr->h265f_mgr != NULL)
        return upipe_void_alloc(autof_mgr->h265f_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&autof->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "h265f"));

    if (!ubase_ncmp(def, "block.dvb_teletext.") &&
        autof_mgr->telxf_mgr != NULL)
        return upipe_void_alloc(autof_mgr->telxf_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&autof->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "telxf"));

    if (!ubase_ncmp(def, "block.dvb_subtitle.") &&
        autof_mgr->dvbsubf_mgr != NULL)
        return upipe_void_alloc(autof_mgr->dvbsubf_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&autof->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "dvbsubf"));

    if (!ubase_ncmp(def, "block.opus.") &&
        autof_mgr->opusf_mgr != NULL)
        return upipe_void_alloc(autof_mgr->opusf_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&autof->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "opusf"));

    if (!ubase_ncmp(def, "block.s302m.") &&
        autof_mgr->s302f_mgr != NULL)
        return upipe_void_alloc(autof_mgr->s302f_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&autof->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "s302f"));

    upipe_warn_va(upipe, "unframed inner flow definition: %s", def);
    return upipe_void_alloc(autof_mgr->idem_mgr,
            uprobe_pfx_alloc(
                uprobe_use(&autof->last_inner_probe),
                UPROBE_LOG_VERBOSE, "idem"));
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_autof_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_autof *upipe_autof = upipe_autof_from_upipe(upipe);
    const char *def;
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 def == NULL))
        return UBASE_ERR_INVALID;

    if (upipe_autof->first_inner != NULL &&
        !uref_flow_cmp_def(upipe_autof->flow_def, flow_def)) {
        uref_free(upipe_autof->flow_def);
        upipe_autof->flow_def = uref_dup(flow_def);
        return upipe_set_flow_def(upipe_autof->first_inner, flow_def);
    }

    if (upipe_autof->flow_def != NULL)
        upipe_dbg_va(upipe, "respawning framer %s", def);
    uref_free(upipe_autof->flow_def);
    upipe_autof->flow_def = uref_dup(flow_def);
    upipe_autof_store_bin_input(upipe, NULL);
    upipe_autof_store_bin_output(upipe, NULL);

    struct upipe *inner = upipe_autof_alloc_framer(upipe, def);
    if (unlikely(inner == NULL)) {
        upipe_err_va(upipe, "couldn't allocate framer");
        return UBASE_ERR_ALLOC;
    }
    if (unlikely(!ubase_check(upipe_set_flow_def(inner, flow_def)))) {
        upipe_err_va(upipe, "couldn't set inner flow def");
        upipe_release(inner);
        return UBASE_ERR_UNHANDLED;
    }

    upipe_autof_store_bin_input(upipe, upipe_use(inner));
    upipe_autof_store_bin_output(upipe, inner);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a autof pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_autof_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_autof_set_flow_def(upipe, flow_def);
        }

        default:
            break;
    }

    int err = upipe_autof_control_bin_input(upipe, command, args);
    if (err == UBASE_ERR_UNHANDLED)
        return upipe_autof_control_bin_output(upipe, command, args);
    return err;
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_autof_free(struct urefcount *urefcount_real)
{
    struct upipe_autof *upipe_autof =
        upipe_autof_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_autof_to_upipe(upipe_autof);
    upipe_throw_dead(upipe);
    uref_free(upipe_autof->flow_def);
    upipe_autof_clean_last_inner_probe(upipe);
    urefcount_clean(urefcount_real);
    upipe_autof_clean_urefcount(upipe);
    upipe_autof_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_autof_no_input(struct upipe *upipe)
{
    struct upipe_autof *upipe_autof = upipe_autof_from_upipe(upipe);
    upipe_autof_clean_bin_input(upipe);
    upipe_autof_clean_bin_output(upipe);
    urefcount_release(upipe_autof_to_urefcount_real(upipe_autof));
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_autof_mgr_free(struct urefcount *urefcount)
{
    struct upipe_autof_mgr *autof_mgr =
        upipe_autof_mgr_from_urefcount(urefcount);
    upipe_mgr_release(autof_mgr->idem_mgr);
    upipe_mgr_release(autof_mgr->mpgaf_mgr);
    upipe_mgr_release(autof_mgr->a52f_mgr);
    upipe_mgr_release(autof_mgr->mpgvf_mgr);
    upipe_mgr_release(autof_mgr->h264f_mgr);
    upipe_mgr_release(autof_mgr->h265f_mgr);
    upipe_mgr_release(autof_mgr->telxf_mgr);
    upipe_mgr_release(autof_mgr->dvbsubf_mgr);
    upipe_mgr_release(autof_mgr->opusf_mgr);
    upipe_mgr_release(autof_mgr->s302f_mgr);

    urefcount_clean(urefcount);
    free(autof_mgr);
}

/** @This processes control commands on an autof manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_autof_mgr_control(struct upipe_mgr *mgr,
                                   int command, va_list args)
{
    struct upipe_autof_mgr *autof_mgr = upipe_autof_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_AUTOF_MGR_GET_##NAME##_MGR: {                            \
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUTOF_SIGNATURE)              \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = autof_mgr->name##_mgr;                                     \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_AUTOF_MGR_SET_##NAME##_MGR: {                            \
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUTOF_SIGNATURE)              \
            if (!urefcount_single(&autof_mgr->urefcount))                   \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(autof_mgr->name##_mgr);                       \
            autof_mgr->name##_mgr = upipe_mgr_use(m);                       \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(idem, IDEM)
        GET_SET_MGR(mpgaf, MPGAF)
        GET_SET_MGR(a52f, A52F)
        GET_SET_MGR(mpgvf, MPGVF)
        GET_SET_MGR(h264f, H264F)
        GET_SET_MGR(h265f, H265F)
        GET_SET_MGR(telxf, TELXF)
        GET_SET_MGR(dvbsubf, DVBSUBF)
        GET_SET_MGR(opusf, OPUSF)
        GET_SET_MGR(s302f, S302F)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all auto framers.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_autof_mgr_alloc(void)
{
    struct upipe_autof_mgr *autof_mgr = malloc(sizeof(struct upipe_autof_mgr));
    if (unlikely(autof_mgr == NULL))
        return NULL;

    memset(autof_mgr, 0, sizeof(*autof_mgr));
    autof_mgr->idem_mgr = upipe_idem_mgr_alloc();
    autof_mgr->mpgaf_mgr = upipe_mpgaf_mgr_alloc();
    autof_mgr->a52f_mgr = upipe_a52f_mgr_alloc();
    autof_mgr->mpgvf_mgr = upipe_mpgvf_mgr_alloc();
    autof_mgr->h264f_mgr = upipe_h264f_mgr_alloc();
    autof_mgr->h265f_mgr = upipe_h265f_mgr_alloc();
    autof_mgr->telxf_mgr = upipe_telxf_mgr_alloc();
    autof_mgr->dvbsubf_mgr = upipe_dvbsubf_mgr_alloc();
    autof_mgr->opusf_mgr = upipe_opusf_mgr_alloc();
    autof_mgr->s302f_mgr = upipe_s302f_mgr_alloc();

    urefcount_init(upipe_autof_mgr_to_urefcount(autof_mgr),
                   upipe_autof_mgr_free);
    autof_mgr->mgr.refcount = upipe_autof_mgr_to_urefcount(autof_mgr);
    autof_mgr->mgr.signature = UPIPE_AUTOF_SIGNATURE;
    autof_mgr->mgr.upipe_alloc = upipe_autof_alloc;
    autof_mgr->mgr.upipe_input = upipe_autof_bin_input;
    autof_mgr->mgr.upipe_control = upipe_autof_control;
    autof_mgr->mgr.upipe_mgr_control = upipe_autof_mgr_control;
    return upipe_autof_mgr_to_upipe_mgr(autof_mgr);
}
