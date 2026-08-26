/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
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
#include "upipe/upipe_helper_flow_def.h"
#include "upipe/upipe_helper_flow_def_check.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_urefcount_real.h"
#include "upipe/upipe_helper_flow_format.h"
#include "upipe/upipe_helper_inner.h"
#include "upipe/upipe_helper_uprobe.h"
#include "upipe/upipe_helper_bin_input.h"
#include "upipe/upipe_helper_bin_output.h"
#include "upipe/upipe_helper_input.h"
#include "upipe-modules/upipe_setflowdef.h"
#include "upipe-modules/upipe_interlace.h"
#include "upipe-filters/upipe_filter_format.h"
#include "upipe-filters/upipe_filter_blend.h"
#include "upipe-swscale/upipe_sws.h"
#include "upipe-av/upipe_avfilter.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

/** @internal @This enumerates the supported surface types. */
enum upipe_ffmt_surface_type {
    SW,
    AV_VAAPI,
    AV_QSV,
    AV_NI_QUADRA,
};

/** @internal @This stores and extracts usefull attributes from a flow
 * definition packet.
 */
struct upipe_ffmt_format {
    /** flow definition packet */
    struct uref *flow_def;
    /** surface type */
    enum upipe_ffmt_surface_type surface_type;
    /** horizontal size */
    uint64_t hsize;
    /** vertical size */
    uint64_t vsize;
    /** fullrange? */
    bool fullrange;
    /** progressive? */
    bool progressive;
    /** top field first? */
    bool tff;
    /** surface type is hardware? */
    bool hw;
    /** bit depth */
    int bit_depth;
    /** pixel format name */
    const char *pix_fmt;
};

/** @internal @This initializes a upipe_ffmt_format structure.
 *
 * @param format structure to initialize
 */
static void upipe_ffmt_format_init(struct upipe_ffmt_format *format)
{
    if (format) {
        memset(format, 0, sizeof (*format));
    }
}

/** @internal @This cleans a upipe_ffmt format structure.
 *
 * @param format structure to clean
 */
static void upipe_ffmt_format_clean(struct upipe_ffmt_format *format)
{
    if (format) {
        uref_free(format->flow_def);
        upipe_ffmt_format_init(format);
    }
}

/** @internal @This stores input and output flow format and transformation
 * needed.
 */
struct upipe_ffmt_config {
    /** input flow format description */
    struct upipe_ffmt_format in;
    /** output flow format description */
    struct upipe_ffmt_format out;
    /** deinterlacing is needed? */
    bool need_deint;
    /** interlacing is needed? */
    bool need_interlace;
    /** format conversion is needed? */
    bool need_format;
    /** scaling is needed? */
    bool need_scale;
    /** range conversion is needed? */
    bool need_range;
    /** tonemap conversion is needed? */
    bool need_tonemap;
    /** hardware derivation is needed? */
    bool need_derive;
    /** hardware transfer is needed? */
    bool need_hw_transfer;
};

/** @internal @This initializes a configuration structure.
 *
 * @param config configuration structure to initialize
 */
static void upipe_ffmt_config_init(struct upipe_ffmt_config *config)
{
    if (config) {
        memset(config, 0, sizeof (*config));
        upipe_ffmt_format_init(&config->in);
        upipe_ffmt_format_init(&config->out);
    }
}

/** @internal @This cleans a configuration structure.
 *
 * @param config configuration structure to clean
 */
static void upipe_ffmt_config_clean(struct upipe_ffmt_config *config)
{
    if (config) {
        upipe_ffmt_format_clean(&config->out);
        upipe_ffmt_format_clean(&config->in);
        upipe_ffmt_config_init(config);
    }
}

/** @internal @This is the private context of a ffmt manager. */
struct upipe_ffmt_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pointer to swscale manager */
    struct upipe_mgr *sws_mgr;
    /** pointer to swresample manager */
    struct upipe_mgr *swr_mgr;
    /** pointer to interlace manager */
    struct upipe_mgr *interlace_mgr;
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

    /** current configuration */
    struct upipe_ffmt_config config;

    /** flow definition on the input */
    struct uref *flow_def_input;
    /** flow definition wanted on the output */
    struct uref *flow_def_wanted;
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
    char *deinterlace_vaapi_mode;
    /** scale_vaapi mode option */
    char *scale_vaapi_mode;
    /** vpp_qsv deinterlace option */
    char *vpp_qsv_deinterlace;
    /** vpp_qsv scale_mode option */
    char *vpp_qsv_scale_mode;
    /** ni_quadra_scale filterblit option */
    char *ni_quadra_scale_filterblit;
    /** zscale filter option */
    char *zscale_filter;
    /** tonemap tonemap option */
    char *tonemap_tonemap;
    /** tonemap param option */
    char *tonemap_param;
    /** tonemap desat option */
    char *tonemap_desat;

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
UPIPE_HELPER_UREFCOUNT_REAL(upipe_ffmt, urefcount_real, upipe_ffmt_free)
UPIPE_HELPER_FLOW_DEF(upipe_ffmt, flow_def_input, flow_def_wanted)
UPIPE_HELPER_FLOW_DEF_CHECK(upipe_ffmt, flow_def_provided)
UPIPE_HELPER_INPUT(upipe_ffmt, urefs, nb_urefs, max_urefs, blockers,
                  upipe_ffmt_handle)
UPIPE_HELPER_INNER(upipe_ffmt, first_inner)
UPIPE_HELPER_BIN_INPUT(upipe_ffmt, first_inner, input_request_list)
UPIPE_HELPER_INNER(upipe_ffmt, last_inner)
UPIPE_HELPER_UPROBE(upipe_ffmt, urefcount_real, proxy_probe, NULL)
UPIPE_HELPER_UPROBE(upipe_ffmt, urefcount_real, last_inner_probe, NULL)
UPIPE_HELPER_BIN_OUTPUT(upipe_ffmt, last_inner, output, output_request_list)
UPIPE_HELPER_FLOW_FORMAT(upipe_ffmt, request,
                         upipe_ffmt_check_flow_format,
                         upipe_ffmt_register_bin_output_request,
                         upipe_ffmt_unregister_bin_output_request)

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
    upipe_ffmt_init_urefcount_real(upipe);
    upipe_ffmt_init_flow_format(upipe);
    upipe_ffmt_init_input(upipe);
    upipe_ffmt_init_flow_def(upipe);
    upipe_ffmt_init_flow_def_provided(upipe);
    upipe_ffmt_init_proxy_probe(upipe);
    upipe_ffmt_init_last_inner_probe(upipe);
    upipe_ffmt_init_bin_input(upipe);
    upipe_ffmt_init_bin_output(upipe);

    upipe_ffmt_config_init(&upipe_ffmt->config);
    upipe_ffmt->sws_flags = 0;
    upipe_ffmt->deinterlace_vaapi_mode = NULL;
    upipe_ffmt->scale_vaapi_mode = NULL;
    upipe_ffmt->vpp_qsv_deinterlace = NULL;
    upipe_ffmt->vpp_qsv_scale_mode = NULL;
    upipe_ffmt->ni_quadra_scale_filterblit = NULL;
    upipe_ffmt->zscale_filter = NULL;
    upipe_ffmt->tonemap_tonemap = NULL;
    upipe_ffmt->tonemap_param = NULL;
    upipe_ffmt->tonemap_desat = NULL;
    upipe_ffmt->hw_type = NULL;
    upipe_ffmt->hw_device = NULL;
    upipe_throw_ready(upipe);

    upipe_ffmt_store_flow_def_attr(upipe, flow_def);

    return upipe;
}

/** @internal @This sets the input flow definition for real.
 *
 * @param upipe description structure of the pipe
 * @param flow_def input flow definition to set
 * @return an error code
 */
static int upipe_ffmt_set_flow_def_real(struct upipe *upipe,
                                        struct uref *flow_def)
{
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);

    if (upipe_ffmt_check_flow_def_input(upipe, flow_def)) {
        uref_free(flow_def);
        return UBASE_ERR_NONE;
    }

    flow_def = upipe_ffmt_store_flow_def_input(upipe, flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_ffmt_store_flow_def_input(upipe, NULL);
        return UBASE_ERR_ALLOC;
    }

    /** It is legal to have just "sound." in flow_def_wanted to avoid
     * changing unnecessarily the sample format. */
    const char *input_def = NULL;
    uref_flow_get_def(upipe_ffmt->flow_def_input, &input_def);
    if (input_def && !ubase_ncmp(input_def, UREF_SOUND_FLOW_DEF)) {
        const char *wanted_def = NULL;
        uref_flow_get_def(upipe_ffmt->flow_def_wanted, &wanted_def);
        if (wanted_def && !strcmp(wanted_def, UREF_SOUND_FLOW_DEF))
            uref_flow_set_def(flow_def, input_def);
    }

    upipe_ffmt_store_flow_def_provided(upipe, NULL);
    upipe_ffmt_require_flow_format(upipe, flow_def);
    return UBASE_ERR_NONE;
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
        int ret = upipe_ffmt_set_flow_def_real(upipe, uref);
        if (unlikely(!ubase_check(ret)))
            upipe_throw_fatal(upipe, ret);
        return true;
    }

    if (unlikely(!upipe_ffmt->flow_def_provided)) {
        if (unlikely(!upipe_ffmt->flow_def_input)) {
            upipe_warn_va(upipe, "no input flow def set, dropping...");
            uref_free(uref);
            return true;
        }
        /* wait for downstream reply */
        return false;
    }

    if (upipe_ffmt->first_inner == NULL) {
        upipe_warn_va(upipe, "dropping...");
        uref_free(uref);
        return true;
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

/** @internal @This pushes a new pipe in the inner pipeline.
 *
 * @param upipe description structure of the pipe
 * @param last_inner filled with last inner pipe in the pipeline
 * @param input new pipe to push
 */
static void upipe_ffmt_push(struct upipe *upipe, struct upipe **last_inner,
                            struct upipe *input)
{
    if (!*last_inner)
        upipe_ffmt_store_bin_input(upipe, upipe_use(input));
    else {
        upipe_set_output(*last_inner, input);
        upipe_release(*last_inner);
    }
    *last_inner = input;
}

/** @internal @This fills a upipe_ffmt format structure from a flow
 * definition packet.
 *
 * @param upipe description structure of the pipe
 * @param format format structure to fill
 * @param flow_def flow definition packet to read the format from
 * @return an error code
 */
static int upipe_ffmt_format_set(struct upipe *upipe,
                                 struct upipe_ffmt_format *format,
                                 struct uref *flow_def)
{
    struct upipe_ffmt_mgr *ffmt_mgr = upipe_ffmt_mgr_from_upipe_mgr(upipe->mgr);

    if (unlikely(!format || !flow_def))
        return UBASE_ERR_INVALID;

    format->flow_def = uref_dup(flow_def);
    if (unlikely(!format->flow_def))
        return UBASE_ERR_ALLOC;

    const char *surface_type = "";
    uref_pic_flow_get_surface_type(flow_def, &surface_type);
    if (!strcmp(surface_type, "av.vaapi"))
        format->surface_type = AV_VAAPI;
    else if (!strcmp(surface_type, "av.qsv"))
        format->surface_type = AV_QSV;
    else if (!strcmp(surface_type, "av.ni_quadra"))
        format->surface_type = AV_NI_QUADRA;
    else
        format->surface_type = SW;
    format->hw = format->surface_type != SW;
    format->hsize = 0;
    uref_pic_flow_get_hsize(flow_def, &format->hsize);
    format->vsize = 0;
    uref_pic_flow_get_vsize(flow_def, &format->vsize);
    format->fullrange = ubase_check(uref_pic_flow_get_full_range(flow_def));
    format->progressive = uref_pic_check_progressive(flow_def);
    format->tff = uref_pic_check_tff(flow_def);
    format->bit_depth = 0;
    uref_pic_flow_get_bit_depth(flow_def, &format->bit_depth);
    format->pix_fmt = "unknown";
    if (ffmt_mgr->avfilter_mgr) {
        upipe_avfilt_mgr_get_pixfmt_name(ffmt_mgr->avfilter_mgr, flow_def,
                                         &format->pix_fmt, true);
    } else {
        const struct uref_pic_flow_format *flow_format =
            uref_pic_flow_get_format(flow_def);
        if (likely(flow_format))
            format->pix_fmt = flow_format->name;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This fills a upipe_ffmt config structure from the input and
 * output flow definition packets, and determines which conversion steps
 * (deinterlace, interlace, scale, range, format, hw transfer, derive,
 * tonemap) are needed to go from one to the other.
 *
 * @param upipe description structure of the pipe
 * @param config config structure to fill
 * @param in input flow definition packet
 * @param out output flow definition packet
 * @return an error code
 */
static int upipe_ffmt_config_set(struct upipe *upipe,
                                 struct upipe_ffmt_config *config,
                                 struct uref *in, struct uref *out)
{
    int ret = upipe_ffmt_format_set(upipe, &config->in, in);
    if (unlikely(!ubase_check(ret)))
        return ret;
    ret = upipe_ffmt_format_set(upipe, &config->out, out);
    if (unlikely(!ubase_check(ret))) {
        upipe_ffmt_format_clean(&config->in);
        return ret;
    }

    config->need_deint = !config->in.progressive && config->out.progressive;
    config->need_interlace = config->in.progressive && !config->out.progressive;
    if (!config->in.progressive && !config->out.progressive &&
        ubase_check(uref_pic_get_tff(config->out.flow_def, NULL)) &&
        ((config->in.tff && !config->out.tff) ||
         (!config->in.tff && config->out.tff))) {
        config->need_deint = true;
        config->need_interlace = true;
    }
    config->need_scale = config->in.hsize != config->out.hsize ||
                         config->in.vsize != config->out.vsize;
    config->need_range = config->in.fullrange != config->out.fullrange;
    config->need_format = !uref_pic_flow_compare_format(config->in.flow_def,
                                                        config->out.flow_def);
    config->need_hw_transfer = (config->in.hw && !config->out.hw) ||
                               (!config->in.hw && config->out.hw);
    config->need_derive = config->in.surface_type == AV_VAAPI &&
                          config->out.surface_type == AV_QSV;
    config->need_tonemap =
        ubase_check(uref_pic_flow_check_hdr10(config->in.flow_def)) &&
        ubase_check(uref_pic_flow_check_sdr(config->out.flow_def));

    if (config->need_format)
        upipe_notice_va(upipe, "need format conversion %s → %s",
                        config->in.pix_fmt, config->out.pix_fmt);
    if (config->need_hw_transfer)
        upipe_notice_va(upipe, "need transfer %s → %s",
                        config->in.hw ? "hw" : "sw",
                        config->out.hw ? "hw" : "sw");
    if (config->need_scale)
        upipe_notice_va(
            upipe, "need scale %" PRIu64 "x%" PRIu64 " → %" PRIu64 "x%" PRIu64,
            config->in.hsize, config->in.vsize, config->out.hsize,
            config->out.vsize);
    if (config->need_range)
        upipe_notice_va(upipe, "need range conversion %s → %s",
                        config->in.fullrange ? "full" : "limited",
                        config->out.fullrange ? "full" : "limited");
    if (config->need_derive)
        upipe_notice(upipe, "need hw surface mapping vaapi → qsv");
    if (config->need_deint)
        upipe_notice(upipe, "need deinterlace");
    if (config->need_interlace)
        upipe_notice(upipe, "need interlace");
    if (config->need_tonemap)
        upipe_notice(upipe, "need tonemap hdr10 → sdr");

    return UBASE_ERR_NONE;
}

/** @internal @This checks whether two upipe_ffmt config structures are
 * equivalent, ie. would build the same inner pipeline (same output format
 * and same needed conversion steps).
 *
 * @param c1 first config structure, may be NULL
 * @param c2 second config structure, may be NULL
 * @return true if the two structures are equivalent
 */
static bool upipe_ffmt_config_check(const struct upipe_ffmt_config *c1,
                                    const struct upipe_ffmt_config *c2)
{
    if (!c1 || !c2)
        return c1 == c2;
    if (!c1->in.flow_def || !c1->out.flow_def ||
        !c2->in.flow_def || !c2->out.flow_def)
        return c1->in.flow_def == c2->in.flow_def &&
               c1->out.flow_def == c2->out.flow_def;
    if (!uref_pic_flow_compare_format(c1->out.flow_def, c2->out.flow_def))
        return false;
    if (c1->out.surface_type != c2->out.surface_type ||
        c1->out.hsize != c2->out.hsize || c1->out.vsize != c2->out.vsize ||
        c1->out.fullrange != c2->out.fullrange ||
        c1->out.progressive != c2->out.progressive ||
        c1->out.tff != c2->out.tff || c1->out.bit_depth != c2->out.bit_depth)
        return false;
    if (c1->in.surface_type != c2->in.surface_type)
        return false;
    if (c1->need_deint != c2->need_deint ||
        c1->need_interlace != c2->need_interlace ||
        c1->need_format != c2->need_format ||
        c1->need_range != c2->need_range || c1->need_scale != c2->need_scale ||
        c1->need_tonemap != c2->need_tonemap ||
        c1->need_derive != c2->need_derive ||
        c1->need_hw_transfer != c2->need_hw_transfer)
        return false;
    return true;
}

/** @internal @This builds the filter format inner pipeline.
 *
 * @param upipe description structure of the pipe
 * @param flow_def input flow definition packet
 * @param flow_def_dup output flow definition packet
 * @return an error code
 */
static int upipe_ffmt_build(struct upipe *upipe, struct uref *flow_def,
                            struct uref *flow_def_dup)
{
    struct upipe_ffmt_mgr *ffmt_mgr = upipe_ffmt_mgr_from_upipe_mgr(upipe->mgr);
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);
    struct uref *flow_def_wanted = upipe_ffmt->flow_def_wanted;

    if (unlikely(!flow_def || !flow_def_dup))
        return UBASE_ERR_INVALID;

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
            uref_pic_flow_copy_overscan(flow_def, flow_def_wanted);
            uref_pic_flow_infer_sar(flow_def, dar);
        }

        /* delete sar and visible sizes to let sws set it */
        if (!ubase_check(uref_pic_flow_get_sar(flow_def_wanted, NULL)) ||
            !ubase_check(uref_pic_flow_get_hsize(flow_def_wanted, NULL)) ||
            !ubase_check(uref_pic_flow_get_vsize(flow_def_wanted, NULL)))
            uref_pic_flow_delete_sar(flow_def_dup);
        uref_pic_flow_delete_hsize_visible(flow_def_dup);
        uref_pic_flow_delete_vsize_visible(flow_def_dup);

        struct upipe_ffmt_config config;
        upipe_ffmt_config_init(&config);
        UBASE_RETURN(
            upipe_ffmt_config_set(upipe, &config, flow_def, flow_def_dup))
        bool need_reconfigure =
            !upipe_ffmt_config_check(&upipe_ffmt->config, &config);
        upipe_ffmt_config_clean(&upipe_ffmt->config);
        upipe_ffmt->config = config;
        if (!need_reconfigure && upipe_ffmt->first_inner)
            return upipe_set_flow_def(upipe_ffmt->first_inner, flow_def);

        const struct upipe_ffmt_format *in = &upipe_ffmt->config.in;
        const struct upipe_ffmt_format *out = &upipe_ffmt->config.out;
        bool use_avfilter = false;
        bool use_deint = false;
        bool use_sws = false;
        bool use_interlace = false;

        if (in->hw || out->hw) {
            if (config.need_deint || config.need_scale || config.need_format ||
                config.need_range || config.need_hw_transfer ||
                config.need_derive) {
                if (ffmt_mgr->avfilter_mgr)
                    use_avfilter = true;
                else {
                    upipe_warn(upipe, "hardware surfaces need avfilter");
                    return UBASE_ERR_UNHANDLED;
                }
            }
        }

        if (config.need_tonemap) {
            if (ffmt_mgr->avfilter_mgr)
                use_avfilter = true;
            else {
                upipe_warn(upipe, "tonemap conversions need avfilter");
                return UBASE_ERR_UNHANDLED;
            }
        }

        if (config.need_format || config.need_scale || config.need_range) {
            if (use_avfilter)
                use_sws = false;
            else if (ffmt_mgr->sws_mgr)
                use_sws = true;
            else if (ffmt_mgr->avfilter_mgr)
                use_avfilter = true;
            else {
                upipe_warn(upipe, "no format conversion manager set");
                return UBASE_ERR_UNHANDLED;
            }
        }

        if (config.need_deint) {
            if (use_avfilter)
                use_deint = false;
            else if (ffmt_mgr->deint_mgr)
                use_deint = true;
            else if (ffmt_mgr->avfilter_mgr)
                use_avfilter = true;
            else {
                upipe_warn(upipe, "no deinterlace manager set");
                return UBASE_ERR_UNHANDLED;
            }
        }

        if (config.need_interlace) {
            if (ffmt_mgr->interlace_mgr) {
                if (out->hw) {
                    upipe_warn(upipe, "hardware interlacing is not supported");
                    return UBASE_ERR_UNHANDLED;
                }
                use_interlace = true;
            } else {
                upipe_warn(upipe, "no interlace manager set");
                return UBASE_ERR_UNHANDLED;
            }
        }

        struct upipe *last_inner = NULL;

        if (use_avfilter) {
            const char *pix_fmt = NULL;
            upipe_avfilt_mgr_get_pixfmt_name(ffmt_mgr->avfilter_mgr,
                                             flow_def_dup, &pix_fmt, false);

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

            bool in_10bit = in->bit_depth == 10;
            bool out_10bit = out->bit_depth == 10;
            const char *pix_fmt_planar_in =
                in_10bit ? "yuv420p10le" : "yuv420p";
            const char *pix_fmt_semiplanar_in = in_10bit ? "p010le" : "nv12";
            const char *pix_fmt_semiplanar_out = out_10bit ? "p010le" : "nv12";
            const char *range_out = out->fullrange ? "full" : "limited";
            bool need_deint = config.need_deint;
            bool need_scale = config.need_scale;
            bool need_format = config.need_format;
            bool need_range = config.need_range;
            bool need_tonemap = config.need_tonemap;

            char filters[512];
            int pos = 0;
            int opt;

#define add_filter(Name) \
            pos += (opt = 0, snprintf(filters + pos, sizeof(filters) - pos, \
                                      "%s%s", pos ? "," : "", Name))

#define add_option(Fmt, ...) \
            pos += snprintf(filters + pos, sizeof(filters) - pos, \
                            "%s" Fmt, opt++ ? ":" : "=", ##__VA_ARGS__)

            if (!in->hw && out->hw) {
                if (out->surface_type == AV_NI_QUADRA) {
                    if (need_deint) {
                        if (strcmp(in->pix_fmt, pix_fmt_planar_in)) {
                            add_filter("scale");
                            add_option("interl=-1");
                            add_filter("format");
                            add_option("%s", pix_fmt_planar_in);
                        }
                        add_filter("yadif");
                        add_option("deint=interlaced");
                        need_format =
                            strcmp(pix_fmt_planar_in, out->pix_fmt) != 0;
                    }
                } else {
                    add_filter("scale");
                    add_option("interl=-1");
                    add_filter("format");
                    add_option("%s", pix_fmt_semiplanar_in);
                }
                add_filter("hwupload");
            }
            if (in->surface_type == AV_QSV || out->surface_type == AV_QSV) {
                if (in->surface_type == AV_VAAPI) {
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
                    add_option("width=%"PRIu64, out->hsize);
                    add_option("height=%"PRIu64, out->vsize);
                }
                add_option("scale_mode=%s",
                           upipe_ffmt->vpp_qsv_scale_mode ?: "hq");
                if (need_format)
                    add_option("format=%s", out->pix_fmt);
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
            } else if (out->hw) {
                if (need_deint && out->surface_type != AV_NI_QUADRA) {
                    add_filter("deinterlace_vaapi");
                    add_option("auto=1");
                    if (upipe_ffmt->deinterlace_vaapi_mode)
                        add_option("mode=%s",
                                   upipe_ffmt->deinterlace_vaapi_mode);
                }
                if (need_scale || need_format || need_range) {
                    if (out->surface_type == AV_NI_QUADRA) {
                        add_filter("ni_quadra_scale");
                        if (need_scale)
                            add_option("size=%" PRIu64 "x%" PRIu64,
                                       out->hsize, out->vsize);
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
                            add_option("w=%"PRIu64, out->hsize);
                            add_option("h=%"PRIu64, out->vsize);
                        }
                        if (need_range)
                            add_option("out_range=%s", range_out);
                        if (!need_tonemap) {
                            if (color_primaries)
                                add_option("out_color_primaries=%s",
                                           color_primaries);
                            if (color_transfer)
                                add_option("out_color_transfer=%s",
                                           color_transfer);
                        }
                    }
                    if (!(need_tonemap && (in->surface_type == AV_VAAPI ||
                                           out->surface_type == AV_VAAPI))) {
                        if (color_matrix)
                            add_option("out_color_matrix=%s", color_matrix);
                        if (need_format)
                            add_option("format=%s", out->pix_fmt);
                    }
                }
                if (need_tonemap && (in->surface_type == AV_VAAPI ||
                                     out->surface_type == AV_VAAPI)) {
                    add_filter("tonemap_vaapi");
                    add_option("format=%s", out->pix_fmt);
                    if (color_matrix)
                        add_option("matrix=%s", color_matrix);
                    if (color_primaries)
                        add_option("primaries=%s", color_primaries);
                    if (color_transfer)
                        add_option("transfer=%s", color_transfer);
                }
            } else {
                if (need_tonemap) {
                    add_filter("zscale");
                    if (need_scale) {
                        add_option("width=%"PRIu64, out->hsize);
                        add_option("height=%"PRIu64, out->vsize);
                        add_option("filter=%s",
                                   upipe_ffmt->zscale_filter ?: "bicubic");
                    }
                    add_option("npl=100");
                    add_option("transfer=linear");
                    add_option("primaries=%s", color_primaries);
                    add_filter("tonemap");
                    add_option("tonemap=%s",
                               upipe_ffmt->tonemap_tonemap ?: "hable");
                    if (upipe_ffmt->tonemap_param)
                        add_option("param=%s", upipe_ffmt->tonemap_param);
                    if (upipe_ffmt->tonemap_desat)
                        add_option("desat=%s", upipe_ffmt->tonemap_desat);
                    add_filter("zscale");
                    add_option("range=%s", range_out);
                    add_option("transfer=%s", color_transfer);
                    add_option("matrix=%s", color_matrix);
                    add_filter("format");
                    add_option("%s", out->pix_fmt);
                } else if (need_scale || need_format || need_range) {
                    add_filter("scale");
                    add_option("interl=-1");
                    if (need_scale) {
                        add_option("w=%"PRIu64, out->hsize);
                        add_option("h=%"PRIu64, out->vsize);
                    }
                    if (need_range)
                        add_option("out_range=%s", range_out);
                    if (color_matrix)
                        add_option("out_color_matrix=%s", color_matrix);
                    if (color_primaries)
                        add_option("out_primaries=%s", color_primaries);
                    if (color_transfer)
                        add_option("out_transfer=%s", color_transfer);
                }
                if (need_deint) {
                    add_filter("yadif");
                    add_option("deint=interlaced");
                }
            }
            if (in->hw && !out->hw) {
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

                upipe_ffmt_push(upipe, &last_inner, avfilt);
            }
        }

        if (use_deint) {
            struct uref *flow_def_deint = uref_dup(flow_def_dup);
            uref_pic_set_progressive(flow_def_deint, true);
            struct upipe *input = upipe_flow_alloc(
                ffmt_mgr->deint_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->proxy_probe),
                                 UPROBE_LOG_VERBOSE, "deint"),
                flow_def_deint);
            if (unlikely(input == NULL)) {
                input = upipe_void_alloc(
                    ffmt_mgr->deint_mgr,
                    uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->proxy_probe),
                                     UPROBE_LOG_VERBOSE, "deint"));
                if (unlikely(input == NULL))
                    upipe_warn_va(upipe, "couldn't allocate deinterlace");
            }
            if (likely(input))
                upipe_ffmt_push(upipe, &last_inner, input);
            uref_free(flow_def_deint);
        }

        if (use_interlace) {
            struct upipe *input = upipe_flow_alloc(
                ffmt_mgr->interlace_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->proxy_probe),
                                 UPROBE_LOG_VERBOSE, "interlace"),
                flow_def_dup);
            if (unlikely(input == NULL))
                upipe_warn_va(upipe, "couldn't allocate interlace");
            else
                upipe_ffmt_push(upipe, &last_inner, input);
        }

        if (use_sws) {
            struct upipe *sws = upipe_flow_alloc(ffmt_mgr->sws_mgr,
                    uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->last_inner_probe),
                                     UPROBE_LOG_VERBOSE, "sws"),
                    flow_def_dup);
            if (unlikely(sws == NULL)) {
                upipe_warn_va(upipe, "couldn't allocate swscale");
                udict_dump(flow_def_dup->udict, upipe->uprobe);
            } else {
                if (upipe_ffmt->sws_flags)
                    upipe_sws_set_flags(sws, upipe_ffmt->sws_flags);

                upipe_ffmt_push(upipe, &last_inner, sws);
            }
        } else {
            struct upipe_mgr *setflowdef_mgr = upipe_setflowdef_mgr_alloc();
            struct upipe *setflowdef = upipe_void_alloc(setflowdef_mgr,
                    uprobe_pfx_alloc(uprobe_use(&upipe_ffmt->last_inner_probe),
                                     UPROBE_LOG_VERBOSE, "setflowdef"));
            upipe_mgr_release(setflowdef_mgr);
            if (unlikely(setflowdef == NULL)) {
                upipe_warn_va(upipe, "couldn't allocate setflowdef");
            } else {
                upipe_setflowdef_set_dict(setflowdef, flow_def_dup);
                upipe_ffmt_push(upipe, &last_inner, setflowdef);
            }
        }
        upipe_ffmt_store_bin_output(upipe, last_inner);

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

    return upipe_set_flow_def(upipe_ffmt->first_inner, flow_def);
}

/** @internal @This receives the result of a flow format request.
 *
 * @param upipe description structure of the pipe
 * @param flow_def_dup amended flow format
 * @return an error code
 */
static int upipe_ffmt_check_flow_format(struct upipe *upipe,
                                        struct uref *flow_def_provided)
{
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);
    struct uref *flow_def_input = upipe_ffmt->flow_def_input;

    if (unlikely(flow_def_provided == NULL || flow_def_input == NULL)) {
        uref_free(flow_def_provided);
        return UBASE_ERR_INVALID;
    }

    if (upipe_ffmt_check_flow_def_provided(upipe, flow_def_provided)) {
        uref_free(flow_def_provided);
        return UBASE_ERR_NONE;
    }
    upipe_ffmt_store_flow_def_provided(upipe, flow_def_provided);

    flow_def_provided = uref_dup(flow_def_provided);
    flow_def_input = uref_dup(flow_def_input);
    int err = UBASE_ERR_ALLOC;
    if (likely(flow_def_provided && flow_def_input))
        err = upipe_ffmt_build(upipe, flow_def_input, flow_def_provided);
    uref_free(flow_def_provided);
    uref_free(flow_def_input);

    if (unlikely(!ubase_check(err))) {
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

#define SET_OPTION_VALUE(Field, Value) \
    UBASE_RETURN(ubase_strdup(&upipe_ffmt->Field, Value))

#define SET_OPTION(Option, Field) \
    if (!strcmp(option, Option)) { \
        SET_OPTION_VALUE(Field, value) \
        return UBASE_ERR_NONE; \
    }

    SET_OPTION("deinterlace_vaapi/mode", deinterlace_vaapi_mode)
    SET_OPTION("scale_vaapi/mode", scale_vaapi_mode)
    SET_OPTION("vpp_qsv/deinterlace", vpp_qsv_deinterlace)
    SET_OPTION("vpp_qsv/scale_mode", vpp_qsv_scale_mode)
    SET_OPTION("ni_quadra_scale/filterblit", ni_quadra_scale_filterblit)
    SET_OPTION("zscale/filter", zscale_filter)
    SET_OPTION("tonemap/tonemap", tonemap_tonemap)
    SET_OPTION("tonemap/param", tonemap_param)
    SET_OPTION("tonemap/desat", tonemap_desat)

    if (!strcmp(option, "deinterlace-preset")) {
        if (!strcmp(value, "fast")) {
            SET_OPTION_VALUE(deinterlace_vaapi_mode, "bob");
            SET_OPTION_VALUE(vpp_qsv_deinterlace, "bob");
        } else if (!strcmp(value, "hq")) {
            SET_OPTION_VALUE(deinterlace_vaapi_mode, "motion_compensated");
            SET_OPTION_VALUE(vpp_qsv_deinterlace, "advanced");
        } else
            return UBASE_ERR_INVALID;
    } else if (!strcmp(option, "scale-preset")) {
        if (!strcmp(value, "fast")) {
            SET_OPTION_VALUE(scale_vaapi_mode, "fast");
            SET_OPTION_VALUE(vpp_qsv_scale_mode, "low_power");
            SET_OPTION_VALUE(ni_quadra_scale_filterblit, "0");
            SET_OPTION_VALUE(zscale_filter, "bilinear");
        } else if (!strcmp(value, "hq")) {
            SET_OPTION_VALUE(scale_vaapi_mode, "hq");
            SET_OPTION_VALUE(vpp_qsv_scale_mode, "hq");
            SET_OPTION_VALUE(ni_quadra_scale_filterblit, NULL);
            SET_OPTION_VALUE(zscale_filter, "bicubic");
        } else
            return UBASE_ERR_INVALID;
    } else
        return UBASE_ERR_INVALID;

#undef SET_OPTION_VALUE
#undef SET_OPTION

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
 * @param upipe description structure of the pipe
 */
static void upipe_ffmt_free(struct upipe *upipe)
{
    struct upipe_ffmt *upipe_ffmt = upipe_ffmt_from_upipe(upipe);

    upipe_throw_dead(upipe);

    free(upipe_ffmt->deinterlace_vaapi_mode);
    free(upipe_ffmt->scale_vaapi_mode);
    free(upipe_ffmt->vpp_qsv_deinterlace);
    free(upipe_ffmt->vpp_qsv_scale_mode);
    free(upipe_ffmt->ni_quadra_scale_filterblit);
    free(upipe_ffmt->zscale_filter);
    free(upipe_ffmt->tonemap_tonemap);
    free(upipe_ffmt->tonemap_param);
    free(upipe_ffmt->tonemap_desat);
    free(upipe_ffmt->hw_type);
    free(upipe_ffmt->hw_device);
    upipe_ffmt_config_clean(&upipe_ffmt->config);
    upipe_ffmt_clean_input(upipe);
    upipe_ffmt_clean_flow_format(upipe);
    upipe_ffmt_clean_proxy_probe(upipe);
    upipe_ffmt_clean_last_inner_probe(upipe);
    upipe_ffmt_clean_flow_def_provided(upipe);
    upipe_ffmt_clean_flow_def(upipe);
    upipe_ffmt_clean_urefcount_real(upipe);
    upipe_ffmt_clean_urefcount(upipe);
    upipe_ffmt_free_flow(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ffmt_no_ref(struct upipe *upipe)
{
    upipe_ffmt_clean_bin_input(upipe);
    upipe_ffmt_clean_bin_output(upipe);
    upipe_ffmt_release_urefcount_real(upipe);
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
    upipe_mgr_release(ffmt_mgr->interlace_mgr);
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
        GET_SET_MGR(interlace, INTERLACE)
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
    ffmt_mgr->interlace_mgr = upipe_interlace_mgr_alloc();
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
