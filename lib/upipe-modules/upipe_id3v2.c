/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#include "id3v2.h"

#include <upipe-modules/upipe_id3v2.h>

#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>

#include <upipe/upipe.h>

#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>

#include <upipe/urefcount.h>
#include <upipe/uclock.h>

#define EXPECTED_FLOW_DEF       "block."
#define FRAME_OWNER             "com.apple.streaming.transportStreamTimestamp"

/** @internal @This is the private context of the pipe. */
struct upipe_id3v2 {
    /** upipe public structure */
    struct upipe upipe;
    /** urefcount structure */
    struct urefcount urefcount;
    /** output pipe */
    struct upipe *output;
    /** output flow def */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** output requests */
    struct uchain requests;

    /** id3v2 header */
    uint8_t header[ID3V2_HEADER_SIZE];
    /** header current size */
    size_t header_size;
    /** id3v2 tag */
    uint8_t *tag;
    /** id3v2 tag size */
    uint32_t tag_size;
    /** id3v2 tag current_size */
    uint32_t current_size;
    /** timestamp */
    uint64_t timestamp;
    /** timestamp was set */
    bool timestamp_set;
};

UPIPE_HELPER_UPIPE(upipe_id3v2, upipe, UPIPE_ID3V2_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_id3v2, urefcount, upipe_id3v2_no_ref);
UPIPE_HELPER_VOID(upipe_id3v2);
UPIPE_HELPER_OUTPUT(upipe_id3v2, output, flow_def, output_state, requests);

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
    upipe_id3v2_init_output(upipe);

    struct upipe_id3v2 *upipe_id3v2 = upipe_id3v2_from_upipe(upipe);
    upipe_id3v2->header_size = 0;
    upipe_id3v2->tag = NULL;
    upipe_id3v2->tag_size = 0;
    upipe_id3v2->timestamp = UINT64_MAX;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_id3v2_no_ref(struct upipe *upipe)
{
    struct upipe_id3v2 *upipe_id3v2 = upipe_id3v2_from_upipe(upipe);

    upipe_throw_dead(upipe);

    free(upipe_id3v2->tag);
    upipe_id3v2_clean_output(upipe);
    upipe_id3v2_clean_urefcount(upipe);
    upipe_id3v2_free_void(upipe);
}

/** @internal @This is called when there is input data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_id3v2_input(struct upipe *upipe,
                              struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_id3v2 *upipe_id3v2 = upipe_id3v2_from_upipe(upipe);
    int ret;

    if (unlikely(upipe_id3v2->header_size < ID3V2_HEADER_SIZE)) {
        size_t size = 0;

        ret = uref_block_size(uref, &size);
        if (unlikely(!ubase_check(ret))) {
            uref_free(uref);
            upipe_throw_fatal(upipe, ret);
            return;
        }

        if (likely(size > ID3V2_HEADER_SIZE - upipe_id3v2->header_size))
            size = ID3V2_HEADER_SIZE - upipe_id3v2->header_size;

        uint8_t *dst = upipe_id3v2->header + upipe_id3v2->header_size;
        ret = uref_block_extract(uref, 0, size, dst);
        if (unlikely(!ubase_check(ret))) {
            uref_free(uref);
            upipe_throw_fatal(upipe, ret);
            return;
        }
        ret = uref_block_resize(uref, size, -1);
        if (unlikely(!ubase_check(ret))) {
            uref_free(uref);
            upipe_throw_fatal(upipe, ret);
            return;
        }
        upipe_id3v2->header_size += size;

        if (unlikely(upipe_id3v2->header_size < ID3V2_HEADER_SIZE)) {
            uref_free(uref);
            return;
        }

        if (unlikely(!id3v2_check_tag(upipe_id3v2->header))) {
            upipe_err(upipe, "invalid id3v2 tag");
            uref_free(uref);
            upipe_id3v2->header_size = 0;
            return;
        }

        upipe_dbg(upipe, "get id3v2 header");
        upipe_id3v2->tag_size =
            sizeof(upipe_id3v2->header) +
            id3v2_get_size(upipe_id3v2->header) +
            id3v2_footer_get_size(upipe_id3v2->header);
        upipe_id3v2->current_size = sizeof(upipe_id3v2->header);
        upipe_id3v2->tag = malloc(upipe_id3v2->tag_size);
        if (unlikely(upipe_id3v2->tag == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        memcpy(upipe_id3v2->tag, upipe_id3v2->header,
               sizeof (upipe_id3v2->header));
    }

    if (unlikely(upipe_id3v2->current_size < upipe_id3v2->tag_size)) {
        if (unlikely(upipe_id3v2->tag == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        size_t size = 0;
        ret = uref_block_size(uref, &size);
        if (unlikely(!ubase_check(ret))) {
            uref_free(uref);
            upipe_throw_fatal(upipe, ret);
            return;
        }

        if (likely(size > upipe_id3v2->tag_size - upipe_id3v2->current_size))
            size = upipe_id3v2->tag_size - upipe_id3v2->current_size;

        uint8_t *dst = upipe_id3v2->tag + upipe_id3v2->current_size;
        ret = uref_block_extract(uref, 0, size, dst);
        if (unlikely(!ubase_check(ret))) {
            uref_free(uref);
            upipe_throw_fatal(upipe, ret);
            return;
        }
        ret = uref_block_resize(uref, size, -1);
        if (unlikely(!ubase_check(ret))) {
            uref_free(uref);
            upipe_throw_fatal(upipe, ret);
            return;
        }
        upipe_id3v2->current_size += size;

        if (unlikely(upipe_id3v2->current_size < upipe_id3v2->tag_size)) {
            uref_free(uref);
            return;
        }

        upipe_dbg_va(upipe, "get id3v2 tag (%u)", upipe_id3v2->tag_size);
        upipe_id3v2->timestamp = UINT64_MAX;

        struct id3v2_frame frame;
        memset(&frame, 0, sizeof (frame));
        while (id3v2_get_frame(upipe_id3v2->tag, &frame)) {
            struct id3v2_frame_priv frame_priv;
            if (id3v2_get_frame_priv(&frame, &frame_priv)) {
                if (!strcmp(frame_priv.owner, FRAME_OWNER)) {
                    if (frame_priv.size == 8) {
                        uint64_t ts = 0;
                        for (uint8_t i = 0; i < frame_priv.size; i++)
                            ts = (ts << 8) + frame_priv.data[i];
                        upipe_id3v2->timestamp = ts * UCLOCK_FREQ / 90000;
                        upipe_dbg_va(upipe, "found timestamp %"PRIu64, ts);
                    }
                    else
                        upipe_err(upipe, "invalid timestamp size");
                }
                else
                    upipe_dbg_va(upipe, "ignore priv frame %s",
                                 frame_priv.owner);
            }
            else
                upipe_dbg_va(upipe, "ignore frame %c%c%c%c",
                             frame.id[0], frame.id[1],
                             frame.id[2], frame.id[3]);
        }

        upipe_id3v2->timestamp_set = false;
    }

    if (likely(upipe_id3v2->timestamp != UINT64_MAX)) {
        if (upipe_id3v2->timestamp_set == false) {
            upipe_dbg_va(upipe, "set timestamp %"PRIu64, upipe_id3v2->timestamp);
            uref_clock_set_date_orig(uref, upipe_id3v2->timestamp, UREF_DATE_DTS);
            uref_clock_set_dts_pts_delay(uref, 0);
        }
        upipe_id3v2->timestamp_set = true;
    }
    else
        upipe_warn(upipe, "cannot date uref");

    if (unlikely(ubase_check(uref_block_get_end(uref))))
        upipe_id3v2->header_size = 0;

    upipe_id3v2_output(upipe, uref, upump_p);
}

/** @internal @This sets the flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to set
 * @return an error code
 */
static int upipe_id3v2_set_flow_def(struct upipe *upipe,
                                    struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));
    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_id3v2_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
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
    UBASE_HANDLED_RETURN(upipe_id3v2_control_output(upipe, command, args));
    switch (command) {
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_id3v2_set_flow_def(upipe, flow_def);
    }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the static structure for id3v2 pipe manager. */
static struct upipe_mgr upipe_id3v2_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ID3V2_SIGNATURE,
    .upipe_alloc = upipe_id3v2_alloc,
    .upipe_input = upipe_id3v2_input,
    .upipe_control = upipe_id3v2_control,
};

/** @This returns the id3v2 pipe manager. */
struct upipe_mgr *upipe_id3v2_mgr_alloc(void)
{
    return &upipe_id3v2_mgr;
}
