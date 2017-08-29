/*
 * Copyright (C) 2014-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Arnaud de Turckheim
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
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uclock.h>
#include <upipe/uref_void_flow.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_urefcount_real.h>

#include <upipe-modules/upipe_blank_source.h>
#include <upipe-modules/upipe_void_source.h>
#include <upipe-modules/upipe_video_blank.h>
#include <upipe-modules/upipe_audio_blank.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

/** @internal @This is the private context of a blank source pipe. */
struct upipe_blksrc {
    /** refcount management structure */
    struct urefcount urefcount;
    /** real refcount management structure */
    struct urefcount urefcount_real;

    /** public upipe structure */
    struct upipe upipe;

    /** input bin pipe */
    struct upipe *src;
    /** input request list */
    struct uchain src_requests;
    /** input bin pipe probe */
    struct uprobe src_probe;

    /** output bin pipe */
    struct upipe *blk;
    /** output request list */
    struct uchain blk_requests;
    /** output bin pipe probe */
    struct uprobe blk_probe;

    /** output pipe */
    struct upipe *output;
};

UPIPE_HELPER_UPIPE(upipe_blksrc, upipe, UPIPE_BLKSRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_blksrc, urefcount, upipe_blksrc_no_ref)
UPIPE_HELPER_UREFCOUNT_REAL(upipe_blksrc, urefcount_real, upipe_blksrc_free);
UPIPE_HELPER_FLOW(upipe_blksrc, "")

UPIPE_HELPER_UPROBE(upipe_blksrc, urefcount_real, src_probe, NULL);
UPIPE_HELPER_INNER(upipe_blksrc, src);
UPIPE_HELPER_BIN_INPUT(upipe_blksrc, src, src_requests);

UPIPE_HELPER_UPROBE(upipe_blksrc, urefcount_real, blk_probe, NULL);
UPIPE_HELPER_INNER(upipe_blksrc, blk);
UPIPE_HELPER_BIN_OUTPUT(upipe_blksrc, blk, output, blk_requests);

/** @internal @This processes control commands on a blank source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_blksrc_control(struct upipe *upipe, int command, va_list args)
{
    int ret = upipe_blksrc_control_bin_input(upipe, command, args);
    if (ret != UBASE_ERR_UNHANDLED)
        return ret;
    return upipe_blksrc_control_bin_output(upipe, command, args);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_blksrc_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_blksrc_clean_bin_output(upipe);
    upipe_blksrc_clean_bin_input(upipe);
    upipe_blksrc_clean_blk_probe(upipe);
    upipe_blksrc_clean_src_probe(upipe);
    upipe_blksrc_clean_urefcount_real(upipe);
    upipe_blksrc_clean_urefcount(upipe);
    upipe_blksrc_free_flow(upipe);
}

/** @internal @This is called when there is no more reference on the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_blksrc_no_ref(struct upipe *upipe)
{
    upipe_blksrc_store_bin_output(upipe, NULL);
    upipe_blksrc_store_bin_input(upipe, NULL);
    upipe_blksrc_release_urefcount_real(upipe);
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

    upipe_blksrc_init_urefcount(upipe);
    upipe_blksrc_init_urefcount_real(upipe);
    upipe_blksrc_init_src_probe(upipe);
    upipe_blksrc_init_blk_probe(upipe);
    upipe_blksrc_init_bin_input(upipe);
    upipe_blksrc_init_bin_output(upipe);

    upipe_throw_ready(upipe);

    struct upipe_blksrc *upipe_blksrc = upipe_blksrc_from_upipe(upipe);

    uint64_t duration = 0;
    if (ubase_check(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF))) {
        struct urational fps;
        if (unlikely(!ubase_check(uref_pic_flow_get_fps(flow_def, &fps)))) {
            uref_free(flow_def);
            upipe_release(upipe);
            return NULL;
        }
        duration = (uint64_t)UCLOCK_FREQ * fps.den / fps.num;
    }
    else if (ubase_check(uref_flow_match_def(flow_def, UREF_SOUND_FLOW_DEF))) {
        uint64_t samples, rate;
        if (unlikely(!ubase_check(uref_sound_flow_get_samples(flow_def,
                                                              &samples))) ||
            unlikely(!ubase_check(uref_sound_flow_get_rate(flow_def,
                                                           &rate)))) {
            uref_free(flow_def);
            upipe_release(upipe);
            return NULL;
        }
        duration = samples * UCLOCK_FREQ / rate;
    }
    else {
        upipe_warn(upipe, "unsupported flow def");
        uref_free(flow_def);
        upipe_release(upipe);
        return NULL;
    }

    struct uref *src_flow_def = uref_void_flow_alloc_def(flow_def->mgr);
    if (unlikely(!src_flow_def)) {
        upipe_err(upipe, "fail to allocate source pipe flow def");
        uref_free(flow_def);
        upipe_release(upipe);
        return NULL;
    }

    if (unlikely(!ubase_check(uref_clock_set_duration(src_flow_def,
                                                      duration)))) {
        uref_free(src_flow_def);
        uref_free(flow_def);
        upipe_release(upipe);
        return NULL;
    }

    struct upipe_mgr *upipe_voidsrc_mgr = upipe_voidsrc_mgr_alloc();
    if (unlikely(!upipe_voidsrc_mgr)) {
        upipe_err(upipe, "fail to get void source manager");
        uref_free(src_flow_def);
        uref_free(flow_def);
        upipe_release(upipe);
        return NULL;
    }

    struct upipe *src =
        upipe_flow_alloc(upipe_voidsrc_mgr,
                         uprobe_pfx_alloc(
                            uprobe_use(&upipe_blksrc->src_probe),
                                       UPROBE_LOG_VERBOSE,
                                       "src"),
                         src_flow_def);
    upipe_mgr_release(upipe_voidsrc_mgr);
    uref_free(src_flow_def);
    if (unlikely(!src)) {
        upipe_err(upipe, "fail to allocate source pipe");
        uref_free(flow_def);
        upipe_release(upipe);
        return NULL;
    }

    upipe_blksrc_store_bin_input(upipe, src);

    struct upipe_mgr *upipe_blk_mgr = NULL;
    if (ubase_check(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF))) {
        upipe_blk_mgr = upipe_vblk_mgr_alloc();
    }
    else if (ubase_check(uref_flow_match_def(flow_def, UREF_SOUND_FLOW_DEF))) {
        upipe_blk_mgr = upipe_ablk_mgr_alloc();
    }

    if (unlikely(!upipe_blk_mgr)) {
        upipe_err(upipe, "fail to get blank generator manager");
        uref_free(flow_def);
        upipe_release(upipe);
        return NULL;
    }

    struct upipe *blk =
        upipe_flow_alloc(upipe_blk_mgr,
                         uprobe_pfx_alloc(
                            uprobe_use(&upipe_blksrc->blk_probe),
                                       UPROBE_LOG_VERBOSE,
                                       "blk"),
                         flow_def);
    upipe_mgr_release(upipe_blk_mgr);
    uref_free(flow_def);
    if (unlikely(!blk)) {
        upipe_err(upipe, "fail to allocate blank generator pipe");
        upipe_release(upipe);
        return NULL;
    }

    upipe_blksrc_store_bin_output(upipe, blk);

    if (unlikely(!ubase_check(upipe_set_output(src, blk)))) {
        upipe_release(upipe);
        return NULL;
    }

    return upipe;
}

/** module manager static descriptor */
static struct upipe_mgr upipe_blksrc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_BLKSRC_SIGNATURE,

    .upipe_alloc = upipe_blksrc_alloc,
    .upipe_input = upipe_blksrc_bin_input,
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
