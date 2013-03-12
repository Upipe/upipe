/*****************************************************************************
 * upipe_x264.c: application interface for x264 module
 *****************************************************************************
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 *****************************************************************************/

/** @file
 * @short Upipe x264 module
 */

#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/udict.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf_block.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-x264/upipe_x264.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>

#include <x264.h>

#include <upipe/udict_dump.h>

#define EXPECTED_FLOW "pic."
#define OUT_FLOW "block.h264.pic."

/** @internal upipe_x264 private structure */
struct upipe_x264 {
    /** x264 encoder */
    x264_t *encoder;
    /** x264 params */
    x264_param_t params;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** output */
    struct upipe *output;
    /** output flow */
    struct uref *output_flow;
    /** has flow def been sent ?*/
    bool output_flow_sent;
    /** public structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_x264, upipe);
UPIPE_HELPER_UBUF_MGR(upipe_x264, ubuf_mgr);
UPIPE_HELPER_UREF_MGR(upipe_x264, uref_mgr);
UPIPE_HELPER_OUTPUT(upipe_x264, output, output_flow, output_flow_sent)


static void upipe_x264_input_pic(struct upipe *upipe, struct uref *uref, struct upump *upump);

static const enum uprobe_log_level loglevel_map[] = {
    [X264_LOG_ERROR] = UPROBE_LOG_ERROR,
    [X264_LOG_WARNING] = UPROBE_LOG_WARNING,
    [X264_LOG_INFO] = UPROBE_LOG_NOTICE,
    [X264_LOG_DEBUG] = UPROBE_LOG_DEBUG
};

/** @internal @This sends x264 logs to uprobe_log
 * @param upipe description structure of the pipe
 * @param loglevel x264 loglevel
 * @param format string format
 * @param args arguments
 */
static void upipe_x264_log(void *_upipe, int loglevel,
                             const char *format, va_list args)
{
    struct upipe *upipe = _upipe;
    char *string = NULL, *end = NULL;
    if (unlikely(loglevel < 0 || loglevel > X264_LOG_DEBUG)) {
        return;
    }

    vasprintf(&string, format, args);
    if (unlikely(!string)) {
        return;
    }
    end = string + strlen(string) - 1;
    if (isspace(*end)) {
        *end = '\0';
    }
    upipe_log(upipe, loglevel_map[loglevel], string);
    free(string);
}

/** @internal @This allocates a filter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_x264_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe)
{
    struct upipe_x264 *upipe_x264 = malloc(sizeof(struct upipe_x264));
    if (unlikely(upipe_x264 == NULL)) {
        return NULL;
    }
    struct upipe *upipe = upipe_x264_to_upipe(upipe_x264);
    upipe_init(upipe, mgr, uprobe);

    upipe_x264->encoder = NULL;
    x264_param_default(&upipe_x264->params);

    upipe_x264_init_ubuf_mgr(upipe);
    upipe_x264_init_uref_mgr(upipe);
    upipe_x264_init_output(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This opens x264 encoder.
 *
 * @param upipe description structure of the pipe
 * @param width image width
 * @param height image height
 */
static bool upipe_x264_open(struct upipe *upipe, int width, int height,
                            struct urational *aspect)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    x264_param_t *params = &upipe_x264->params;

    params->i_threads = 1;

    params->pf_log = upipe_x264_log;
    params->p_log_private = upipe;
    params->i_log_level = X264_LOG_DEBUG;

//    params->i_fps_num = 25;
//    params->i_fps_den = 1;
#if 0
    x264_param_default_preset(params, "veryfast", "film");

    params->i_keyint_max = 25;
    params->b_intra_refresh = 1;

    params->rc.i_rc_method = X264_RC_CRF;
    params->rc.f_rf_constant = 25;
    params->rc.f_rf_constant_max = 35;

    params->i_csp = X264_CSP_I420;

    params->b_repeat_headers = 1;
    params->b_annexb = 1;

    x264_param_apply_profile(params, "baseline");
#endif

    params->vui.i_sar_width = aspect->num;
    params->vui.i_sar_height = aspect->den;
    params->i_width = width;
    params->i_height = height;

    upipe_x264->encoder = x264_encoder_open(params);
    if (unlikely(!upipe_x264->encoder)) {
        return false;
    }

    return true;
}

/** @internal @This closes x264 encoder.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_x264_close(struct upipe *upipe)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    if (upipe_x264->encoder) {
        while(x264_encoder_delayed_frames(upipe_x264->encoder)) {
            upipe_x264_input_pic(upipe, NULL, NULL);
        }

        upipe_notice(upipe, "closing decoder");
        x264_encoder_close(upipe_x264->encoder);
    }
}

/** @internal @This processes pictures.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_x264_input_pic(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    size_t width, height;
    x264_picture_t pic;
    x264_nal_t *nals;
    const char *chroma = NULL;
    int i, nals_num, size = 0;
    size_t stride;
    struct uref *uref_block;
    uint8_t *buf = NULL;
    struct urational aspect;

    // init x264 picture
    x264_picture_init(&pic);
    pic.img.i_csp = X264_CSP_I420;

    if (likely(uref)) {
        aspect.den = aspect.num = 1;
        uref_pic_get_aspect(uref, &aspect);
        uref_pic_size(uref, &width, &height, NULL);

        //udict_dump(uref->udict, upipe->uprobe);

        // open decoder if not already opened
        if (unlikely(!upipe_x264->encoder)) {
            upipe_x264_open(upipe, width, height, &aspect);
            if (unlikely(!upipe_x264->encoder)) {
                upipe_err(upipe, "Could not open decoder");
                uref_free(uref);
                return;
            }
        }

        // map
        for (i=0; uref_pic_plane_iterate(uref, &chroma) && chroma; i++) {
            if (unlikely(!uref_pic_plane_size(uref, chroma, &stride, NULL, NULL, NULL))) {
                upipe_err_va(upipe, "Could not read origin chroma %s", chroma);
                uref_free(uref);
                return;
            }
            uref_pic_plane_read(uref, chroma, 0, 0, -1, -1, (const uint8_t **) &pic.img.plane[i]);
            pic.img.i_stride[i] = stride;
        }
        pic.img.i_plane = i;

        // encode frame !
        x264_encoder_encode(upipe_x264->encoder, &nals, &nals_num, &pic, &pic);

        // unmap
        for (chroma = NULL; uref_pic_plane_iterate(uref, &chroma) && chroma; ) {
            uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
        }
        uref_free(uref);
    } else { // NULL-uref, flushing delayed frame
        x264_encoder_encode(upipe_x264->encoder, &nals, &nals_num, NULL, &pic);
    }

    if (unlikely(nals < 0)) {
        upipe_warn(upipe, "Error encoding frame");
        return;
    }else if (unlikely(nals == 0)) {
        upipe_dbg(upipe, "No nal units returned");
        return;
    }

    for (i=0; i < nals_num; i++) {
        size += nals[i].i_payload;
    }

    // alloc ubuf, map, copy, unmap
    uref_block = uref_block_alloc(upipe_x264->uref_mgr, upipe_x264->ubuf_mgr, size);
    if (unlikely(uref_block == NULL)) {
        upipe_throw_aerror(upipe);
        return;
    }
    uref_block_write(uref_block, 0, &size, &buf);
    memcpy(buf, nals[0].p_payload, size);
    uref_block_unmap(uref_block, 0, size);

    upipe_x264_output(upipe, uref_block, upump);
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_x264_input(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);
    const char  *def = NULL;

    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(ubase_ncmp(def, EXPECTED_FLOW))) {
            upipe_throw_flow_def_error(upipe, uref);
            uref_free(uref);
            return;
        }

        udict_dump(uref->udict, upipe->uprobe);
        if (unlikely(!uref_flow_set_def(uref, OUT_FLOW))) {
            upipe_throw_aerror(upipe);
        }
        upipe_x264_store_flow_def(upipe, uref);
        return;
    }
    if (unlikely(uref_flow_get_end(uref))) {
        uref_free(uref);
        upipe_throw_need_input(upipe);
        return;
    }

    if (unlikely(!uref->ubuf)) { // no ubuf in uref
        upipe_warn(upipe, "dropping empty uref");
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_x264->ubuf_mgr)) {
        upipe_err(upipe, "No ubuf_mgr defined, dropping uref");
        uref_free(uref);
        return;
    }
    if (unlikely(!upipe_x264->uref_mgr)) {
        upipe_err(upipe, "No uref_mgr defined, dropping uref");
        uref_free(uref);
        return;
    }


    upipe_x264_input_pic(upipe, uref, upump);
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_x264_control(struct upipe *upipe,
                               enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_x264_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_x264_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_x264_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_x264_set_uref_mgr(upipe, uref_mgr);
        }

        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_x264_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_x264_set_output(upipe, output);
        }
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_x264_free(struct upipe *upipe)
{
    struct upipe_x264 *upipe_x264 = upipe_x264_from_upipe(upipe);

    upipe_x264_close(upipe);

    upipe_throw_dead(upipe);
    upipe_x264_clean_ubuf_mgr(upipe);
    upipe_x264_clean_uref_mgr(upipe);
    upipe_x264_clean_output(upipe);
    upipe_clean(upipe);
    free(upipe_x264);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_x264_mgr = {
    .signature = UPIPE_X264_SIGNATURE,
    .upipe_alloc = upipe_x264_alloc,
    .upipe_input = upipe_x264_input,
    .upipe_control = upipe_x264_control,
    .upipe_free = upipe_x264_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for glx_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_x264_mgr_alloc(void)
{
    return &upipe_x264_mgr;
}
