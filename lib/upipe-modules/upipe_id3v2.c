/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 * Copyright (C) 2023 EasyTools
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

#include "upipe-modules/upipe_id3v2.h"
#include "upipe-modules/upipe_id3v2_decaps.h"
#include "upipe-modules/upipe_probe_uref.h"

#include "upipe/upipe_helper_bin_input.h"
#include "upipe/upipe_helper_bin_output.h"
#include "upipe/upipe_helper_inner.h"
#include "upipe/upipe_helper_uprobe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_urefcount_real.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_upipe.h"

#include "upipe/upipe.h"

#include "upipe/uprobe.h"
#include "upipe/uprobe_prefix.h"

#include "upipe/uref_block.h"
#include "upipe/uref_clock.h"

#include "upipe/urefcount.h"
#include "upipe/uclock.h"

#include <bitstream/id3/id3v2.h>
#include <bitstream/id3/frame_priv.h>

/** @internal @This is the private context of the pipe. */
struct upipe_id3v2 {
    /** upipe public structure */
    struct upipe upipe;
    /** external urefcount structure */
    struct urefcount urefcount;
    /** internal urefcount structure */
    struct urefcount urefcount_real;
    /** proxy probe for inner pipes */
    struct uprobe proxy_probe;
    /** decaps inner pipe */
    struct upipe *decaps;
    /** probe uref inner pipe */
    struct upipe *probe_uref;
    /** input request list */
    struct uchain input_requests;
    /** output pipe */
    struct upipe *output;
    /** output request list */
    struct uchain output_requests;

    /** current timestamp */
    uint64_t timestamp;
};

/** @hidden */
static int catch_proxy(struct uprobe *, struct upipe *, int, va_list);

UPIPE_HELPER_UPIPE(upipe_id3v2, upipe, UPIPE_ID3V2_SIGNATURE);
UPIPE_HELPER_VOID(upipe_id3v2);
UPIPE_HELPER_UREFCOUNT(upipe_id3v2, urefcount, upipe_id3v2_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_id3v2, urefcount_real, upipe_id3v2_free);
UPIPE_HELPER_UPROBE(upipe_id3v2, urefcount_real, proxy_probe, catch_proxy);
UPIPE_HELPER_INNER(upipe_id3v2, decaps);
UPIPE_HELPER_INNER(upipe_id3v2, probe_uref);
UPIPE_HELPER_BIN_INPUT(upipe_id3v2, decaps, input_requests);
UPIPE_HELPER_BIN_OUTPUT(upipe_id3v2, probe_uref, output, output_requests);

/** @internal @This catches events from the inner pipes.
 *
 * @param uprobe structure used to raise events
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args optional arguments of the event
 * @return an error code
 */
static int catch_proxy(struct uprobe *uprobe, struct upipe *inner,
                       int event, va_list args)
{
    struct upipe_id3v2 *upipe_id3v2 = upipe_id3v2_from_proxy_probe(uprobe);
    struct upipe *upipe = upipe_id3v2_to_upipe(upipe_id3v2);
    struct uref *uref;

    if (uprobe_id3v2d_check_tag(event, args, &uref)) {
        const uint8_t *id3v2;
        int block_size = -1;
        uref_block_read(uref, 0, &block_size, &id3v2);

        id3v2_each_frame(id3v2, frame) {
            if (id3v2_frame_get_id(frame) == ID3V2_FRAME_ID_PRIV &&
                id3v2_frame_validate_priv(frame)) {
                const char *owner = id3v2_frame_priv_get_owner(frame);
                const uint8_t *priv_data = id3v2_frame_priv_get_data(frame);
                if (!strcmp(owner, ID3V2_FRAME_PRIV_APPLE_TS_TIMESTAMP)) {
                    upipe_id3v2->timestamp =
                        ((uint64_t)priv_data[0] << 56) +
                        ((uint64_t)priv_data[1] << 48) +
                        ((uint64_t)priv_data[2] << 40) +
                        ((uint64_t)priv_data[3] << 32) +
                        ((uint64_t)priv_data[4] << 24) +
                        ((uint64_t)priv_data[5] << 16) +
                        ((uint64_t)priv_data[6] << 8) +
                        (uint64_t)priv_data[7];
                }
            }
        }
        uref_block_unmap(uref, 0);
        return UBASE_ERR_NONE;
    } else if (uprobe_probe_uref_check(event, args, &uref, NULL, NULL)) {
        if (upipe_id3v2->timestamp != UINT64_MAX) {
            uint64_t dts = upipe_id3v2->timestamp * UCLOCK_FREQ / 90000;
            upipe_dbg_va(upipe, "set timestamp to %" PRIu64, dts);
            upipe_id3v2->timestamp = UINT64_MAX;
            uref_clock_set_dts_orig(uref, dts);
            uref_clock_set_dts_prog(uref, dts);
            uref_clock_set_dts_pts_delay(uref, 0);
        }
        return UBASE_ERR_NONE;
    }

    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This allocates an id3v2 pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe.
 */
static struct upipe *upipe_id3v2_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature,
                                       va_list args)
{
    struct upipe *upipe = upipe_id3v2_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_id3v2_init_urefcount(upipe);
    upipe_id3v2_init_urefcount_real(upipe);
    upipe_id3v2_init_proxy_probe(upipe);
    upipe_id3v2_init_bin_input(upipe);
    upipe_id3v2_init_bin_output(upipe);

    upipe_throw_ready(upipe);

    struct upipe_id3v2 *upipe_id3v2 = upipe_id3v2_from_upipe(upipe);
    upipe_id3v2->timestamp = UINT64_MAX;

    /* allocate ID3v2 decaps */
    struct upipe_mgr *upipe_id3v2d_mgr = upipe_id3v2d_mgr_alloc();
    if (unlikely(!upipe_id3v2d_mgr)) {
        upipe_release(upipe);
        return NULL;
    }
    struct upipe *decaps = upipe_void_alloc(
        upipe_id3v2d_mgr,
        uprobe_pfx_alloc(
            uprobe_use(upipe_id3v2_to_proxy_probe(upipe_id3v2)),
            UPROBE_LOG_VERBOSE, "decaps"));
    upipe_mgr_release(upipe_id3v2d_mgr);
    if (unlikely(!decaps)) {
        upipe_release(upipe);
        return NULL;
    }
    upipe_id3v2_store_bin_input(upipe, upipe_use(decaps));

    /* allocate probe uref */
    struct upipe_mgr *upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
    if (unlikely(!upipe_probe_uref_mgr)) {
        upipe_release(decaps);
        upipe_release(upipe);
        return NULL;
    }
    struct upipe *probe_uref = upipe_void_chain_output(
        decaps, upipe_probe_uref_mgr,
        uprobe_pfx_alloc(
            uprobe_use(upipe_id3v2_to_proxy_probe(upipe_id3v2)),
            UPROBE_LOG_VERBOSE, "probe"));
    upipe_mgr_release(upipe_probe_uref_mgr);
    if (unlikely(!probe_uref)) {
        upipe_release(upipe);
        return NULL;
    }
    upipe_id3v2_store_bin_output(upipe, probe_uref);

    return upipe;
}

/** @internal @This frees the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_id3v2_free(struct upipe *upipe)
{
    upipe_id3v2_clean_proxy_probe(upipe);
    upipe_id3v2_clean_urefcount_real(upipe);
    upipe_id3v2_clean_urefcount(upipe);
    upipe_id3v2_free_void(upipe);
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_id3v2_no_ref(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_id3v2_clean_bin_input(upipe);
    upipe_id3v2_clean_bin_output(upipe);
    upipe_id3v2_release_urefcount_real(upipe);
}

/** @internal @This dispatches the control commands.
 *
 * @param upipe description structure of the pipe
 * @param command command to dispatch
 * @param args optional arguments
 * @return an error code
 */
static int upipe_id3v2_control(struct upipe *upipe,
                               int command,
                               va_list args)
{
    UBASE_HANDLED_RETURN(upipe_id3v2_control_bin_input(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_id3v2_control_bin_output(upipe, command, args));
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the static structure for id3v2 pipe manager. */
static struct upipe_mgr upipe_id3v2_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ID3V2_SIGNATURE,
    .upipe_alloc = upipe_id3v2_alloc,
    .upipe_input = upipe_id3v2_bin_input,
    .upipe_control = upipe_id3v2_control,
};

/** @This returns the id3v2 pipe manager. */
struct upipe_mgr *upipe_id3v2_mgr_alloc(void)
{
    return &upipe_id3v2_mgr;
}
