/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short common functions for libav wrappers
 */

#include "upipe/udeal.h"
#include "upipe/uprobe.h"
#include "upipe/upipe.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_pic_flow.h"
#include "upipe-av/upipe_av.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mastering_display_metadata.h>

#include "upipe_av_internal.h"

#include <stdbool.h>

/** structure to protect exclusive access to avcodec_open() */
struct udeal upipe_av_deal;
/** @internal true if only avcodec was initialized */
static bool avcodec_only = false;
/** @internal probe used by upipe_av_vlog, defined in upipe_av_init() */
static struct uprobe *logprobe = NULL;

/** @internal @This replaces av_log_default_callback
 *
 * @param avcl A pointer to an arbitrary struct of which the first field is a
 * pointer to an AVClass struct.
 * @param level The importance level of the message, lower values signifying
 * higher importance.
 * @param fmt The format string (printf-compatible) that specifies how
 * subsequent arguments are converted to output.
 * @param args optional arguments
 */
UBASE_FMT_PRINTF(3, 0)
static void upipe_av_vlog(void *avcl, int level, const char *fmt, va_list args)
{
    enum uprobe_log_level loglevel = UPROBE_LOG_VERBOSE;

    if (level <= AV_LOG_ERROR)
        loglevel = UPROBE_LOG_ERROR;
    else if (level <= AV_LOG_WARNING)
        loglevel = UPROBE_LOG_WARNING;
    else if (level <= AV_LOG_INFO)
        loglevel = UPROBE_LOG_INFO;
    else if (level <= AV_LOG_VERBOSE)
        loglevel = UPROBE_LOG_DEBUG;

    assert(logprobe);
    uprobe_vlog(logprobe, NULL, loglevel, fmt, args);
}

/** @This initializes non-reentrant parts of avcodec and avformat. Call it
 * before allocating managers from this library.
 *
 * @param init_avcodec_only if set to true, avformat source and sink may not
 * be used (saves memory)
 * @param uprobe uprobe to print libav messages
 * @return false in case of error
 */
bool upipe_av_init(bool init_avcodec_only, struct uprobe *uprobe)
{
    avcodec_only = init_avcodec_only;

    if (unlikely(!udeal_init(&upipe_av_deal))) {
        uprobe_release(uprobe);
        return false;
    }

    if (unlikely(avcodec_only)) {
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 10, 100)
        avcodec_register_all();
#endif
    } else {
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100)
        av_register_all(); /* does avcodec_register_all() behind our back */
#endif
        avformat_network_init();
    }

    if (uprobe) {
        logprobe = uprobe;
        av_log_set_callback(upipe_av_vlog);
    }
    return true;
}

/** @This cleans up memory allocated by @ref upipe_av_init. Call it when all
 * avformat- and avcodec-related managers have been freed.
 */
void upipe_av_clean(void)
{
    if (likely(!avcodec_only))
        avformat_network_deinit();
    udeal_clean(&upipe_av_deal);
    if (logprobe)
        uprobe_release(logprobe);
}

/** @This sets frame properties from flow definition and uref packets.
 *
 * @param upipe upipe used for logging
 * @param frame av frame to setup
 * @param flow_def flow definition packet
 * @param uref uref structure
 * @return an error code
 */
int upipe_av_set_frame_properties(struct upipe *upipe,
                                  AVFrame *frame,
                                  struct uref *flow_def,
                                  struct uref *uref)
{
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(58, 7, 100)
    frame->key_frame = ubase_check(uref_pic_get_key(uref));
    frame->interlaced_frame = !ubase_check(uref_pic_check_progressive(uref));
    frame->top_field_first = ubase_check(uref_pic_check_tff(uref));
#else
    if (ubase_check(uref_pic_get_key(uref)))
        frame->flags |= AV_FRAME_FLAG_KEY;
    else
        frame->flags &= ~AV_FRAME_FLAG_KEY;
    if (!ubase_check(uref_pic_check_progressive(uref)))
        frame->flags |= AV_FRAME_FLAG_INTERLACED;
    else
        frame->flags &= ~AV_FRAME_FLAG_INTERLACED;
    if (ubase_check(uref_pic_check_tff(uref)))
        frame->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    else
        frame->flags &= ~AV_FRAME_FLAG_TOP_FIELD_FIRST;
#endif
    frame->color_range = ubase_check(uref_pic_flow_get_full_range(
            flow_def)) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    int val;
    if (ubase_check(uref_pic_flow_get_colour_primaries_val(flow_def, &val)))
        frame->color_primaries = val;
    if (ubase_check(uref_pic_flow_get_transfer_characteristics_val(flow_def, &val)))
        frame->color_trc = val;
    if (ubase_check(uref_pic_flow_get_matrix_coefficients_val(flow_def, &val)))
        frame->colorspace = val;

    uint64_t max_cll;
    uint64_t max_fall;
    if (ubase_check(uref_pic_flow_get_max_cll(flow_def, &max_cll)) &&
        ubase_check(uref_pic_flow_get_max_fall(flow_def, &max_fall))) {
        AVContentLightMetadata *clm =
            av_content_light_metadata_create_side_data(frame);
        if (!clm) {
            upipe_err(upipe, "unable to create content light metadata");
            return UBASE_ERR_EXTERNAL;
        }
        clm->MaxCLL = max_cll;
        clm->MaxFALL = max_fall;
    }

    struct uref_pic_mastering_display mdcv;
    if (ubase_check(uref_pic_flow_get_mastering_display(flow_def, &mdcv))) {
        AVMasteringDisplayMetadata *mdm =
            av_mastering_display_metadata_create_side_data(frame);
        if (!mdm) {
            upipe_err(upipe, "unable to create mastering display metadata");
            return UBASE_ERR_EXTERNAL;
        }
        int chroma = 50000;
        int luma = 10000;
        mdm->display_primaries[0][0] = av_make_q(mdcv.red_x, chroma);
        mdm->display_primaries[0][1] = av_make_q(mdcv.red_y, chroma);
        mdm->display_primaries[1][0] = av_make_q(mdcv.green_x, chroma);
        mdm->display_primaries[1][1] = av_make_q(mdcv.green_y, chroma);
        mdm->display_primaries[2][0] = av_make_q(mdcv.blue_x, chroma);
        mdm->display_primaries[2][1] = av_make_q(mdcv.blue_y, chroma);
        mdm->white_point[0] = av_make_q(mdcv.white_x, chroma);
        mdm->white_point[1] = av_make_q(mdcv.white_y, chroma);
        mdm->has_primaries = 1;
        mdm->max_luminance = av_make_q(mdcv.max_luminance, luma);
        mdm->min_luminance = av_make_q(mdcv.min_luminance, luma);
        mdm->has_luminance = 1;
    }

    return UBASE_ERR_NONE;
}
