/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
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

#include "upipe/ubase.h"
#include "upipe/uprobe.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uref.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_pic_flow_formats.h"
#include "upipe/uref_sound_flow.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_flow.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_flow_format.h"
#include "upipe/upipe_helper_inner.h"
#include "upipe/upipe_helper_uprobe.h"
#include "upipe/upipe_helper_bin_input.h"
#include "upipe/upipe_helper_bin_output.h"
#include "upipe/upipe_helper_input.h"
#include "upipe-modules/upipe_setflowdef.h"
#include "upipe-filters/upipe_filter_format.h"
#include "upipe-filters/upipe_filter_blend.h"
#include "upipe-swscale/upipe_sws.h"
#include "upipe-av/upipe_avfilter.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

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
    /** pointer to avfilter manager */
    struct upipe_mgr *avfilter_mgr;

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
    /** flow definition requested */
    struct uref *flow_def_requested;
    /** flow definition provided */
    struct uref *flow_def_provided;
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
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** swscale flags */
    int sws_flags;
    /** deinterlace_vaapi mode option */
    const char *deinterlace_vaapi_mode;
    /** scale_vaapi mode option */
    const char *scale_vaapi_mode;
    /** vpp_qsv deinterlace option */
    const char *vpp_qsv_deinterlace;
    /** vpp_qsv scale_mode option */
    const char *vpp_qsv_scale_mode;
    /** ni_quadra_scale filterblit option */
    const char *ni_quadra_scale_filterblit;

    /** avfilter hw config type */
    char *hw_type;
    /** avfilter hw config device */
    char *hw_device;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ffmt, upipe, UPIPE_FFMT_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ffmt, NULL)
UPIPE_HELPER_UREFCOUNT(upipe_ffmt, urefcount, upipe_ffmt_no_ref)
UPIPE_HELPER_INPUT(upipe_ffmt, urefs, nb_urefs, max_urefs, blockers,
                  upipe_ffmt_handle)
UPIPE_HELPER_INNER(upipe_ffmt, first_inner)
UPIPE_HELPER_BIN_INPUT(upipe_ffmt, first_inner, input_request_list)
UPIPE_HELPER_INNER(upipe_ffmt, last_inner)
UPIPE_HELPER_UPROBE(upipe_ffmt, urefcount_real, last_inner_probe, NULL)
UPIPE_HELPER_BIN_OUTPUT(upipe_ffmt, last_inner, output, output_request_list)
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
    upipe_ffmt_init_last_inner_probe(upipe);
    upipe_ffmt_init_bin_input(upipe);
    upipe_ffmt_init_bin_output(upipe);

    uprobe_init(&upipe_ffmt->proxy_probe, upipe_ffmt_proxy_probe, NULL);
    upipe_ffmt->proxy_probe.refcount =
        upipe_ffmt_to_urefcount_real(upipe_ffmt);
    upipe_ffmt->flow_def_input = NULL;
    upipe_ffmt->flow_def_wanted = flow_def;
    upipe_ffmt->flow_def_requested = NULL;
    upipe_ffmt->flow_def_provided = NULL;
    upipe_ffmt->sws_flags = 0;
    upipe_ffmt->deinterlace_vaapi_mode = NULL;
    upipe_ffmt->scale_vaapi_mode = NULL;
    upipe_ffmt->vpp_qsv_deinterlace = NULL;
    upipe_ffmt->vpp_qsv_scale_mode = NULL;
    upipe_ffmt->ni_quadra_scale_filterblit = NULL;
    upipe_ffmt->hw_type = NULL;
    upipe_ffmt->hw_device = NULL;
    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return true if the packet was handled
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
        uref_free(upipe_ffmt->flow_def_requested);
        upipe_ffmt->flow_def_input = uref_dup(uref);
        upipe_ffmt->flow_def_requested = NULL;
        if (unlikely(upipe_ffmt->flow_def_input == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return true;
        }

        /** It is legal to have just "sound." in flow_def_wanted to avoid
         * changing unnecessarily the sample format. */
        char *old_def = NULL;
        if (!ubase_ncmp(def, "sound."))
            old_def = strdup(def);
        uref_attr_import(uref, upipe_ffmt->flow_def_wanted);
        if (old_def != NULL &&
            (!ubase_check(uref_flow_get_def(uref, &def)) ||
             !strcmp(def, "sound.")))
            uref_flow_set_def(uref, old_def);
        free(old_def);

        upipe_ffmt_store_bin_input(upipe, NULL);
        upipe_ffmt_store_bin_output(upipe, NULL);
        uref_free(upipe_ffmt->flow_def_provided);
        upipe_ffmt->flow_def_provided = NULL;
        upipe_ffmt_require_flow_format(upipe, uref);
        return true;
    }

    if (upipe_ffmt->first_inner == NULL) {
        if (!upipe_ffmt->flow_def_input ||
            upipe_ffmt->flow_def_requested) {
            upipe_warn_va(upipe, "dropping...");
            uref_free(uref);
            return true;
        }
        return false;
    }

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
    struct uref *flow_def_wanted = upipe_ffmt->flow_def_wanted;
    if (flow_def_dup == NULL)
        return UBASE_ERR_INVALID;

    if (upipe_ffmt->flow_def_provided &&
        !udict_cmp(upipe_ffmt->flow_def_provided->udict, flow_def_dup->udict))
        return UBASE_ERR_NONE;

    uref_free(upipe_ffmt->flow_def_provided);
    upipe_ffmt->flow_def_provided = uref_dup(flow_def_dup);

    struct uref *flow_def = uref_dup(upipe_ffmt->flow_def_input);
    UBASE_ALLOC_RETURN(flow_def)
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))

    if (!ubase_ncmp(def, "pic.")) {
        /* check aspect ratio */
        struct urational sar, dar;
        if (ubase_check(uref_pic_flow_get_sar(flow_def_wanted, &sar)) &&
            sar.num) {
            struct urational input_sar;
            uint64_t hsize;
            if (!ubase_check(uref_pic_flow_get_hsize(flow_def_wanted, &hsize)) &&
                ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize)) &&
                ubase_check(uref_pic_flow_get_sar(flow_def, &input_sar)) &&
                input_sar.num) {
                struct urational sar_factor =
                    urational_divide(&input_sar, &sar);
                hsize = (hsize * sar_factor.num / sar_factor.den / 2) * 2;
                uref_pic_flow_set_hsize(flow_def_dup, hsize);
                uref_pic_flow_set_hsize_visible(flow_def_dup, hsize);
            }
            uref_pic_flow_set_sar(flow_def, sar);
        } else if (ubase_check(uref_pic_flow_get_dar(flow_def_wanted, &dar))) {
            bool overscan;
            if (ubase_check(uref_pic_flow_get_overscan(
                            flow_def_wanted, &overscan)))
                uref_pic_flow_set_overscan(flow_def, overscan);
            uref_pic_flow_infer_sar(flow_def, dar);
        }

        /* delete sar and visible sizes to let sws set it */
        if (!ubase_check(uref_pic_flow_get_sar(flow_def_wanted, NULL)) ||
            !ubase_check(uref_pic_flow_get_hsize(flow_def_wanted, NULL)) ||
            !ubase_check(uref_pic_flow_get_vsize(flow_def_wanted, NULL)))
            uref_pic_flow_delete_sar(flow_def_dup);
        uref_pic_flow_delete_hsize_visible(flow_def_dup);
        uref_pic_flow_delete_vsize_visible(flow_def_dup);

        const char *surface_type_in;
        if (!ubase_check(uref_pic_flow_get_surface_type(flow_def,
                                                        &surface_type_in)))
            surface_type_in = "";

        const char *surface_type_out;
        if (!ubase_check(uref_pic_flow_get_surface_type(flow_def_dup,
                                                        &surface_type_out)))
            surface_type_out = "";

        bool need_deint = ffmt_mgr->deint_mgr &&
            !ubase_check(uref_pic_get_progressive(flow_def)) &&
            ubase_check(uref_pic_get_progressive(flow_def_dup));
        bool need_scale =
            uref_pic_flow_cmp_hsize(flow_def, flow_def_dup) ||
            uref_pic_flow_cmp_vsize(flow_def, flow_def_dup);
        bool need_range = uref_pic_flow_cmp_full_range(flow_def, flow_def_dup);
        bool need_format =
            !uref_pic_flow_compare_format(flow_def, flow_def_dup);
        bool need_sws = ffmt_mgr->sws_mgr &&
            (need_scale || need_format || need_range);
        bool pic_vaapi_in = !strcmp(surface_type_in, "av.vaapi");
        bool pic_vaapi_out = !strcmp(surface_type_out, "av.vaapi");
        bool pic_qsv_in = !strcmp(surface_type_in, "av.qsv");
        bool pic_qsv_out = !strcmp(surface_type_out, "av.qsv");
        bool pic_quadra_in = !strcmp(surface_type_in, "av.ni_quadra");
        bool pic_quadra_out = !strcmp(surface_type_out, "av.ni_quadra");
        bool hw_in = pic_vaapi_in || pic_qsv_in || pic_quadra_in;
        bool hw_out = pic_vaapi_out || pic_qsv_out || pic_quadra_out;
        bool hw = hw_in || hw_out;
        int bit_depth_in = 0;
        int bit_depth_out = 0;
        uref_pic_flow_get_bit_depth(flow_def, &bit_depth_in);
        uref_pic_flow_get_bit_depth(flow_def_dup, &bit_depth_out);
        bool need_hw_transfer = (hw_in && !hw_out) || (!hw_in && hw_out);
        bool need_derive = pic_vaapi_in && pic_qsv_out;
        bool need_tonemap = ubase_check(uref_pic_flow_check_hdr10(flow_def)) &&
            ubase_check(uref_pic_flow_check_sdr(flow_def_dup));
        bool need_avfilter = ffmt_mgr->avfilter_mgr && hw &&
            (need_deint || need_scale || need_format || need_hw_transfer ||
             need_derive || need_range);

        if (need_avfilter) {
            const char *range_in =
                ubase_check(uref_pic_flow_get_full_range(flow_def)) ?
                "full" : "limited";
            const char *range_out =
                ubase_check(uref_pic_flow_get_full_range(flow_def_dup)) ?
                "full" : "limited";
            if (need_format) {
                const char *pix_fmt_in = "unknown";
                const char *pix_fmt_out = "unknown";
                upipe_avfilt_mgr_get_pixfmt_name(ffmt_mgr->avfilter_mgr,
                                                 flow_def, &pix_fmt_in,
                                                 true);
                upipe_avfilt_mgr_get_pixfmt_name(ffmt_mgr->avfilter_mgr,
                                                 flow_def_dup, &pix_fmt_out,
                                                 true);
                upipe_notice_va(upipe, "need format conversion %s → %s",
                                pix_fmt_in, pix_fmt_out);
            }
            if (need_hw_transfer) {
                upipe_notice_va(upipe, "need transfer %s → %s",
                                hw_in ? "hw" : "sw",
                                hw_out ? "hw" : "sw");
            }
            if (need_scale) {
                uint64_t hsize_in = 0, vsize_in = 0;
                uint64_t hsize_out = 0, vsize_out = 0;
                uref_pic_flow_get_hsize(flow_def, &hsize_in);
                uref_pic_flow_get_vsize(flow_def, &vsize_in);
                uref_pic_flow_get_hsize(flow_def_dup, &hsize_out);
                uref_pic_flow_get_vsize(flow_def_dup, &vsize_out);
                upipe_notice_va(upipe, "need scale %" PRIu64 "x%" PRIu64
                                " → %" PRIu64 "x%" PRIu64,
                                hsize_in, vsize_in, hsize_out, vsize_out);
            }
            if (need_range)
                upipe_notice_va(upipe, "need range conversion %s → %s",
                                range_in, range_out);
            if (need_derive)
                upipe_notice(upipe, "need hw surface mapping vaapi → qsv");
            if (need_deint)
                upipe_notice(upipe, "need deinterlace");
            if (need_tonemap)
                upipe_notice(upipe, "need tonemap hdr10 → sdr");

            uint64_t hsize = 0, vsize = 0;
            uref_pic_flow_get_hsize(flow_def_dup, &hsize);
            uref_pic_flow_get_vsize(flow_def_dup, &vsize);

            const char *pix_fmt = NULL;
            upipe_avfilt_mgr_get_pixfmt_name(ffmt_mgr->avfilter_mgr,
                                             flow_def_dup, &pix_fmt, false);
            const char *pix_fmt_sw = NULL;
            upipe_avfilt_mgr_get_pixfmt_name(ffmt_mgr->avfilter_mgr,
                                             flow_def_dup, &pix_fmt_sw, true);

            int val;

            const char *color_matrix = NULL;
            UBASE_RETURN(uref_pic_flow_get_matrix_coefficients_val(
                    flow_def_dup, &val))
            if (val != 2)
                UBASE_RETURN(upipe_avfilt_mgr_get_color_space_name(
                        ffmt_mgr->avfilter_mgr, val, &color_matrix))

            const char *color_primaries = NULL;
            UBASE_RETURN(uref_pic_flow_get_colour_primaries_val(
                    flow_def_dup, &val))
            if (val != 2)
                UBASE_RETURN(upipe_avfilt_mgr_get_color_primaries_name(
                        ffmt_mgr->avfilter_mgr, val, &color_primaries))

            const char *color_transfer = NULL;
            UBASE_RETURN(uref_pic_flow_get_transfer_characteristics_val(
                    flow_def_dup, &val))
            if (val != 2)
                UBASE_RETURN(upipe_avfilt_mgr_get_color_transfer_name(
                        ffmt_mgr->avfilter_mgr, val, &color_transfer))

            bool in_10bit = bit_depth_in == 10;
            bool out_10bit = bit_depth_out == 10;
            const char *pix_fmt_semiplanar_in = in_10bit ? "p010le" : "nv12";
            const char *pix_fmt_semiplanar_out = out_10bit ? "p010le" : "nv12";

            char filters[512];
            int pos = 0;
            int opt;

#define add_filter(Name) \
            pos += (opt = 0, snprintf(filters + pos, sizeof(filters) - pos, \
                                      "%s%s", pos ? "," : "", Name))

#define add_option(Fmt, ...) \
            pos += snprintf(filters + pos, sizeof(filters) - pos, \
                            "%s" Fmt, opt++ ? ":" : "=", ##__VA_ARGS__)

            if (!hw_in) {
                if (pic_quadra_out) {
                    if (need_deint) {
                        add_filter("yadif");
                        add_option("deint=interlaced");
                    }
                } else {
                    add_filter("scale");
                    add_option("interl=-1");
                    add_filter("format");
                    add_option("%s", pix_fmt_semiplanar_in);
                }
                add_filter("hwupload");
            }
            if (pic_qsv_in || pic_qsv_out) {
                if (pic_vaapi_in) {
                    add_filter("hwmap");
                    add_option("derive_device=qsv");
                    add_filter("format");
                    add_option("qsv");
                }
                add_filter("vpp_qsv");
                if (need_deint)
                    add_option("deinterlace=%s",
                               upipe_ffmt->vpp_qsv_deinterlace ?: "advanced");
                if (need_scale) {
                    add_option("width=%"PRIu64, hsize);
                    add_option("height=%"PRIu64, vsize);
                }
                add_option("scale_mode=%s",
                           upipe_ffmt->vpp_qsv_scale_mode ?: "hq");
                if (need_format)
                    add_option("format=%s", pix_fmt_sw);
                if (need_range)
                    add_option("out_range=%s", range_out);
                if (color_matrix)
                    add_option("out_color_matrix=%s", color_matrix);
                if (color_primaries)
                    add_option("out_color_primaries=%s", color_primaries);
                if (color_transfer)
                    add_option("out_color_transfer=%s", color_transfer);
                add_option("tonemap=%d", need_tonemap ? 1 : 0);
                add_option("async_depth=0");
            } else {
                if (need_deint && !pic_quadra_out) {
                    add_filter("deinterlace_vaapi");
                    add_option("auto=1");
                    if (upipe_ffmt->deinterlace_vaapi_mode)
                        add_option("mode=%s",
                                   upipe_ffmt->deinterlace_vaapi_mode);
                }
                if (need_scale || need_format || need_range) {
                    if (pic_quadra_out) {
                        add_filter("ni_quadra_scale");
                        if (need_scale)
                            add_option("size=%"PRIu64"x%"PRIu64, hsize, vsize);
                        if (upipe_ffmt->ni_quadra_scale_filterblit)
                            add_option("filterblit=%s",
                                       upipe_ffmt->ni_quadra_scale_filterblit);
                        else
                            add_option("autoselect=1");
                    } else {
                        add_filter("scale_vaapi");
                        add_option("mode=%s",
                                   upipe_ffmt->scale_vaapi_mode ?: "hq");
                        if (need_scale) {
                            add_option("w=%"PRIu64, hsize);
                            add_option("h=%"PRIu64, vsize);
                        }
                        if (need_range)
                            add_option("out_range=%s", range_out);
                        if (color_primaries)
                            add_option("out_color_primaries=%s",
                                       color_primaries);
                        if (color_transfer)
                            add_option("out_color_transfer=%s",
                                       color_transfer);
                    }
                    if (color_matrix)
                        add_option("out_color_matrix=%s", color_matrix);
                    if (need_format)
                        add_option("format=%s", pix_fmt_sw);
                }
                if (need_tonemap && (pic_vaapi_in || pic_vaapi_out)) {
                    add_filter("tonemap_vaapi");
                    add_option("format=%s", pix_fmt_sw);
                    if (color_matrix)
                        add_option("matrix=%s", color_matrix);
                    if (color_primaries)
                        add_option("primaries=%s", color_primaries);
                    if (color_transfer)
                        add_option("transfer=%s", color_transfer);
                }
            }
            if (!hw_out) {
                add_filter("hwmap");
                add_option("mode=read+direct");
                add_filter("format");
                add_option("%s", pix_fmt_semiplanar_out);
                if (pix_fmt != NULL && strcmp(pix_fmt, pix_fmt_semiplanar_out)) {
                    add_filter("scale");
                    add_option("interl=-1");
                    add_filter("format");
                    add_option("%s", pix_fmt);
                }
            }

#undef add_filter
#undef add_option

            if (pos >= sizeof(filters)) {
                upipe_err(upipe, "filtergraph too long");
                return UBASE_ERR_INVALID;
            }

            struct upipe *avfilt = upipe_void_alloc(
                ffmt_mgr->avfilter_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->last_inner_probe),
                                 UPROBE_LOG_VERBOSE, "avfilt"));
            if (avfilt == NULL)
                upipe_warn_va(upipe, "couldn't allocate deinterlace");
            else {
                if (upipe_ffmt->hw_type != NULL &&
                    !ubase_check(upipe_avfilt_set_hw_config(
                            avfilt, upipe_ffmt->hw_type,
                            upipe_ffmt->hw_device)))
                    upipe_err(upipe, "cannot set filters hw config");
                if (!ubase_check(upipe_avfilt_set_filters_desc(
                            avfilt, filters)))
                    upipe_err(upipe, "cannot set filters desc");

                upipe_ffmt_store_bin_output(upipe, avfilt);
                upipe_ffmt_store_bin_input(upipe, upipe_use(avfilt));
            }

            need_deint = false;
            need_sws = false;
        }

        if (need_deint) {
            upipe_notice(upipe, "need deinterlace");
            struct upipe *input = upipe_void_alloc(ffmt_mgr->deint_mgr,
                    uprobe_pfx_alloc(
                        need_sws ? uprobe_use(&upipe_ffmt->proxy_probe) :
                                   uprobe_use(&upipe_ffmt->last_inner_probe),
                        UPROBE_LOG_VERBOSE, "deint"));
            if (unlikely(input == NULL))
                upipe_warn_va(upipe, "couldn't allocate deinterlace");
            else if (!need_sws)
                upipe_ffmt_store_bin_output(upipe, upipe_use(input));
            upipe_ffmt_store_bin_input(upipe, input);
        }

        if (need_sws) {
            if (need_format) {
                const struct uref_pic_flow_format *from =
                    uref_pic_flow_get_format(flow_def);
                const struct uref_pic_flow_format *to =
                    uref_pic_flow_get_format(flow_def_dup);
                upipe_notice_va(upipe, "need format conversion %s → %s",
                                from ? from->name : "unknown",
                                to ? to->name : "unknown");
            }
            if (need_scale) {
                uint64_t hsize_in = 0, vsize_in = 0;
                uint64_t hsize_out = 0, vsize_out = 0;
                uref_pic_flow_get_hsize(flow_def, &hsize_in);
                uref_pic_flow_get_vsize(flow_def, &vsize_in);
                uref_pic_flow_get_hsize(flow_def_dup, &hsize_out);
                uref_pic_flow_get_vsize(flow_def_dup, &vsize_out);
                upipe_notice_va(upipe, "need scale %" PRIu64 "x%" PRIu64
                                " → %" PRIu64 "x%" PRIu64,
                                hsize_in, vsize_in, hsize_out, vsize_out);
            }
            if (need_range) {
                const char *from =
                    ubase_check(uref_pic_flow_get_full_range(flow_def)) ?
                    "full" : "limited";
                const char *to =
                    ubase_check(uref_pic_flow_get_full_range(flow_def_dup)) ?
                    "full" : "limited";
                upipe_notice_va(upipe, "need range conversion %s → %s",
                                from, to);
            }
            struct upipe *sws = upipe_flow_alloc(ffmt_mgr->sws_mgr,
                    uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->last_inner_probe),
                                     UPROBE_LOG_VERBOSE, "sws"),
                    flow_def_dup);
            if (unlikely(sws == NULL)) {
                upipe_warn_va(upipe, "couldn't allocate swscale");
                udict_dump(flow_def_dup->udict, upipe->uprobe);
            } else if (need_deint)
                upipe_set_output(upipe_ffmt->first_inner, sws);
            upipe_ffmt_store_bin_output(upipe, sws);
            if (!need_deint)
                upipe_ffmt_store_bin_input(upipe, upipe_use(sws));
            if (sws && upipe_ffmt->sws_flags)
                upipe_sws_set_flags(sws, upipe_ffmt->sws_flags);
        } else {
            struct upipe_mgr *setflowdef_mgr = upipe_setflowdef_mgr_alloc();
            struct upipe *setflowdef = upipe_void_alloc(setflowdef_mgr,
                    uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->last_inner_probe),
                                     UPROBE_LOG_VERBOSE, "setflowdef"));
            upipe_mgr_release(setflowdef_mgr);
            if (unlikely(setflowdef == NULL)) {
                upipe_warn_va(upipe, "couldn't allocate setflowdef");
            } else if (need_deint || need_avfilter)
                upipe_set_output(upipe_ffmt->first_inner, setflowdef);
            upipe_ffmt_store_bin_output(upipe, setflowdef);
            if (!need_deint && !need_avfilter)
                upipe_ffmt_store_bin_input(upipe, upipe_use(setflowdef));
            upipe_setflowdef_set_dict(setflowdef, flow_def_dup);
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
                upipe_ffmt_store_bin_output(upipe, input);
                upipe_ffmt_store_bin_input(upipe, upipe_use(input));
            }
        }
    }

    if (upipe_ffmt->first_inner == NULL) {
        struct upipe_mgr *setflowdef_mgr = upipe_setflowdef_mgr_alloc();
        struct upipe *input = upipe_void_alloc(setflowdef_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->last_inner_probe),
                                 UPROBE_LOG_VERBOSE, "setflowdef"));
        upipe_mgr_release(setflowdef_mgr);
        if (unlikely(input == NULL))
            upipe_warn_va(upipe, "couldn't allocate setflowdef");
        else {
            upipe_setflowdef_set_dict(input, flow_def_dup);
            upipe_ffmt_store_bin_output(upipe, input);
            upipe_ffmt_store_bin_input(upipe, upipe_use(input));
        }
    }
    upipe_ffmt->flow_def_requested = flow_def_dup;

    int err = upipe_set_flow_def(upipe_ffmt->first_inner, flow_def);
    uref_free(flow_def);

    if (!ubase_check(err)) {
        upipe_ffmt_store_bin_input(upipe, NULL);
        upipe_ffmt_store_bin_output(upipe, NULL);
    }

    bool was_buffered = !upipe_ffmt_check_input(upipe);
    upipe_ffmt_output_input(upipe);
    upipe_ffmt_unblock_input(upipe);
    if (was_buffered && upipe_ffmt_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_ffmt_input. */
        upipe_release(upipe);
    }
    return err;
}

/** @internal @This sets the filters options.
 *
 * @param upipe description structure of the pipe
 * @param option option name (filter name/option)
 * @param value value or NULL to use the default value
 * @return an error code
 */
static int upipe_ffmt_set_option(struct upipe *upipe,
                                 const char *option,
                                 const char *value)
{
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);

    if (!strcmp(option, "deinterlace_vaapi/mode"))
        // default bob weave motion_adaptive motion_compensated
        upipe_ffmt->deinterlace_vaapi_mode = value;
    else if (!strcmp(option, "scale_vaapi/mode"))
        // default fast hq nl_anamorphic
        upipe_ffmt->scale_vaapi_mode = value;
    else if (!strcmp(option, "vpp_qsv/deinterlace"))
        // bob advanced
        upipe_ffmt->vpp_qsv_deinterlace = value;
    else if (!strcmp(option, "vpp_qsv/scale_mode"))
        // auto low_power hq compute vd ve
        upipe_ffmt->vpp_qsv_scale_mode = value;
    else if (!strcmp(option, "ni_quadra_scale/filterblit"))
        // 0 1 2
        upipe_ffmt->ni_quadra_scale_filterblit = value;
    else if (!strcmp(option, "deinterlace-preset")) {
        if (!strcmp(value, "fast")) {
            upipe_ffmt->deinterlace_vaapi_mode = "bob";
            upipe_ffmt->vpp_qsv_deinterlace = "bob";
        } else if (!strcmp(value, "hq")) {
            upipe_ffmt->deinterlace_vaapi_mode = "motion_compensated";
            upipe_ffmt->vpp_qsv_deinterlace = "advanced";
        } else
            return UBASE_ERR_INVALID;
    } else if (!strcmp(option, "scale-preset")) {
        if (!strcmp(value, "fast")) {
            upipe_ffmt->scale_vaapi_mode = "fast";
            upipe_ffmt->vpp_qsv_scale_mode = "low_power";
            upipe_ffmt->ni_quadra_scale_filterblit = "0";
        } else if (!strcmp(value, "hq")) {
            upipe_ffmt->scale_vaapi_mode = "hq";
            upipe_ffmt->vpp_qsv_scale_mode = "hq";
            upipe_ffmt->ni_quadra_scale_filterblit = NULL;
        } else
            return UBASE_ERR_INVALID;
    } else
        return UBASE_ERR_INVALID;

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

/** @internal @This sets the avfilter hw config.
 *
 * @param upipe description structure of the pipe
 * @param hw_type hardware device type
 * @param hw_device hardware device (use NULL for default)
 * @return an error code
 */
static int upipe_ffmt_set_hw_config(struct upipe *upipe,
                                    const char *hw_type,
                                    const char *hw_device)
{
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);

    if (hw_type == NULL)
        return UBASE_ERR_INVALID;

    char *hw_type_tmp = strdup(hw_type);
    if (hw_type_tmp == NULL)
        return UBASE_ERR_ALLOC;
    char *hw_device_tmp = NULL;
    if (hw_device != NULL) {
        hw_device_tmp = strdup(hw_device);
        if (hw_device_tmp == NULL) {
            free(hw_type_tmp);
            return UBASE_ERR_ALLOC;
        }
    }

    free(upipe_ffmt->hw_type);
    upipe_ffmt->hw_type = hw_type_tmp;
    free(upipe_ffmt->hw_device);
    upipe_ffmt->hw_device = hw_device_tmp;

    if (upipe_ffmt->last_inner != NULL)
        return upipe_avfilt_set_hw_config(upipe_ffmt->last_inner,
                                          hw_type, hw_device);

    return UBASE_ERR_NONE;
}

static int upipe_ffmt_alloc_output_proxy(struct upipe *upipe,
                                         struct urequest *urequest)
{
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);
    struct urequest *proxy = urequest_alloc_proxy(urequest);
    UBASE_ALLOC_RETURN(proxy);

    if (urequest->type == UREQUEST_FLOW_FORMAT && urequest->uref) {
        /** It is legal to have just "sound." in flow_def_wanted to avoid
         * changing unnecessarily the sample format. */
        const char *def = NULL;
        uref_flow_get_def(urequest->uref, &def);

        char *old_def = NULL;
        if (!ubase_ncmp(def, "sound."))
            old_def = strdup(def);
        uref_attr_import(proxy->uref, upipe_ffmt->flow_def_wanted);
        if (old_def != NULL &&
            (!ubase_check(uref_flow_get_def(proxy->uref, &def)) ||
             !strcmp(def, "sound.")))
            uref_flow_set_def(proxy->uref, old_def);
        free(old_def);
    }
    return upipe_ffmt_register_bin_output_request(upipe, proxy);
}

static int upipe_ffmt_free_output_proxy(struct upipe *upipe,
                                        struct urequest *urequest)
{
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);
    struct urequest *proxy =
        urequest_find_proxy(urequest, &upipe_ffmt->output_request_list);
    if (unlikely(!proxy))
        return UBASE_ERR_INVALID;

    upipe_ffmt_unregister_bin_output_request(upipe, proxy);
    urequest_free_proxy(proxy);
    return UBASE_ERR_INVALID;
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
        case UPIPE_REGISTER_REQUEST: {
            va_list args_copy;
            va_copy(args_copy, args);
            struct urequest *request = va_arg(args_copy, struct urequest *);
            va_end(args_copy);

            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_ffmt_alloc_output_proxy(upipe, request);

            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_throw_provide_request(upipe, request);
            break;
        }
        case UPIPE_UNREGISTER_REQUEST: {
            va_list args_copy;
            va_copy(args_copy, args);
            struct urequest *request = va_arg(args_copy, struct urequest *);
            va_end(args_copy);

            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_ffmt_free_output_proxy(upipe, request);
            if (request->type == UREQUEST_UBUF_MGR)
                return UBASE_ERR_NONE;
            break;
        }

        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *value = va_arg(args, const char *);
            return upipe_ffmt_set_option(upipe, option, value);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ffmt_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SWS_SET_FLAGS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SWS_SIGNATURE)
            int flags = va_arg(args, int);
            return upipe_ffmt_set_sws_flags(upipe, flags);
        }
        case UPIPE_AVFILT_SET_HW_CONFIG: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFILT_SIGNATURE)
            const char *hw_type = va_arg(args, const char *);
            const char *hw_device = va_arg(args, const char *);
            return upipe_ffmt_set_hw_config(upipe, hw_type, hw_device);
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
    free(upipe_ffmt->hw_type);
    free(upipe_ffmt->hw_device);
    upipe_ffmt_clean_input(upipe);
    upipe_ffmt_clean_flow_format(upipe);
    uref_free(upipe_ffmt->flow_def_input);
    uref_free(upipe_ffmt->flow_def_wanted);
    uref_free(upipe_ffmt->flow_def_requested);
    uref_free(upipe_ffmt->flow_def_provided);
    uprobe_clean(&upipe_ffmt->proxy_probe);
    upipe_ffmt_clean_last_inner_probe(upipe);
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
    upipe_mgr_release(ffmt_mgr->avfilter_mgr);

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
        GET_SET_MGR(avfilter, AVFILTER)
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

    memset(ffmt_mgr, 0, sizeof(*ffmt_mgr));
    ffmt_mgr->sws_mgr = NULL;
    ffmt_mgr->swr_mgr = NULL;
    ffmt_mgr->deint_mgr = upipe_filter_blend_mgr_alloc();
    ffmt_mgr->avfilter_mgr = NULL;

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
