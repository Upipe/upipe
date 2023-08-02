/*
 * Copyright (C) 2023 EasyTools
 *
 * Authors: Arnaud de Turckheim
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

#include <bitstream/id3/id3v2.h>

#include "upipe/ubase.h"
#include "upipe/uclock.h"
#include "upipe/urefcount.h"
#include "upipe/ubuf_block.h"
#include "upipe/uref_block.h"
#include "upipe/uref_clock.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_uref_stream.h"
#include "upipe-modules/upipe_id3v2_decaps.h"

#define EXPECTED_FLOW_DEF       "block."

/** @internal @This is the private context of the pipe. */
struct upipe_id3v2d {
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
    /** first retained uref for parsing */
    struct uref *next_uref;
    /** retained size */
    size_t next_uref_size;
    /** list of retained urefs for parsing */
    struct uchain urefs;
};

UPIPE_HELPER_UPIPE(upipe_id3v2d, upipe, UPIPE_ID3V2D_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_id3v2d, urefcount, upipe_id3v2d_free);
UPIPE_HELPER_VOID(upipe_id3v2d);
UPIPE_HELPER_OUTPUT(upipe_id3v2d, output, flow_def, output_state, requests);
UPIPE_HELPER_UREF_STREAM(upipe_id3v2d, next_uref, next_uref_size, urefs, NULL);

/** @internal @This throws an event when a tag is found.
 *
 * @param upipe description structure of the pipe
 * @param uref uref containing the tag
 * @return an error code
 */
static int upipe_id3v2d_throw_tag(struct upipe *upipe, struct uref *uref)
{
    upipe_dbg(upipe, "throw tag");
    return upipe_throw(upipe, UPROBE_ID3V2D_TAG, UPIPE_ID3V2D_SIGNATURE, uref);
}

/** @internal @This allocates an id3v2 decaps pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe.
 */
static struct upipe *upipe_id3v2d_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature,
                                        va_list args)
{
    struct upipe *upipe = upipe_id3v2d_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_id3v2d_init_urefcount(upipe);
    upipe_id3v2d_init_output(upipe);
    upipe_id3v2d_init_uref_stream(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This frees the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_id3v2d_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_id3v2d_clean_uref_stream(upipe);
    upipe_id3v2d_clean_output(upipe);
    upipe_id3v2d_clean_urefcount(upipe);
    upipe_id3v2d_free_void(upipe);
}

/** @internal @This scans for an ID3v2 header.
 *
 * @param upipe description structure of the pipe
 * @param dropped_p filled with the number of octets to drop before the ID3v2
 * header
 * @return true if an ID3v2 header was found
 */
static bool upipe_id3v2d_scan(struct upipe *upipe, size_t *dropped_p)
{
    struct upipe_id3v2d *upipe_id3v2d = upipe_id3v2d_from_upipe(upipe);
    if (upipe_id3v2d->next_uref == NULL)
        return false;
    return ubase_check(
        uref_block_scan(upipe_id3v2d->next_uref, dropped_p, 'I'));
}

/** @internal @This parses an ID3v2 tag.
 *
 * @param upipe description structure of the pipe
 */
static bool upipe_id3v2d_parse(struct upipe *upipe)
{
    struct upipe_id3v2d *upipe_id3v2d = upipe_id3v2d_from_upipe(upipe);
    int ret;

    uint8_t header[ID3V2_HEADER_SIZE];
    ret = uref_block_extract(
        upipe_id3v2d->next_uref, 0, ID3V2_HEADER_SIZE, header);
    if (!ubase_check(ret))
        return false;

    size_t total_size = id3v2_get_total_size(header);
    uint8_t tag[total_size];
    ret = uref_block_extract(
        upipe_id3v2d->next_uref, 0, total_size, tag);
    if (!ubase_check(ret))
        return false;

    struct uref *uref = upipe_id3v2d_extract_uref_stream(upipe, total_size);
    uint32_t output_size = 0;
    id3v2_undo_unsynchronise(tag, NULL, &output_size);
    if (output_size != total_size) {
        struct ubuf *ubuf = ubuf_block_alloc(uref->ubuf->mgr, output_size);
        if (unlikely(!ubuf)) {
            upipe_warn(upipe, "fail to allocate buffer");
            uref_free(uref);
            return true;
        }

        uint8_t *output;
        int size = -1;
        ret = ubuf_block_write(ubuf, 0, &size, &output);
        if (unlikely(!ubase_check(ret))) {
            upipe_warn(upipe, "fail to write in buffer");
            ubuf_free(ubuf);
            uref_free(uref);
            return true;
        }

        output_size = size;
        id3v2_undo_unsynchronise(tag, output, &output_size);
        ubuf_block_unmap(ubuf, 0);
        if (output_size != size) {
            upipe_warn(upipe, "fail to undo unsynchronise");
            ubuf_free(ubuf);
            uref_free(uref);
            return true;
        }

        uref_attach_ubuf(uref, ubuf);
    }
    else {
        ret = uref_block_merge(uref, uref->ubuf->mgr, 0, -1);
        if (unlikely(!ubase_check(ret))) {
            upipe_warn(upipe, "fail to merge block");
            uref_free(uref);
            return true;
        }
    }

    upipe_id3v2d_throw_tag(upipe, uref);
    uref_free(uref);
    return true;
}

/** @internal @This flushes all retained buffers.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_id3v2d_flush(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_id3v2d *upipe_id3v2d = upipe_id3v2d_from_upipe(upipe);

    while (upipe_id3v2d->next_uref) {
        struct uref *uref =
            upipe_id3v2d_extract_uref_stream(upipe,
                                             upipe_id3v2d->next_uref_size);
        if (uref)
            upipe_id3v2d_output(upipe, uref, upump_p);
    }
}

/** @internal @This looks for ID3v2 tag to decaps.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_id3v2d_work(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_id3v2d *upipe_id3v2d = upipe_id3v2d_from_upipe(upipe);

    size_t skipped = 0;
    while (upipe_id3v2d_scan(upipe, &skipped)) {
        uint8_t tag[3];
        int ret = uref_block_extract(upipe_id3v2d->next_uref, skipped, 3, tag);
        if (!ubase_check(ret))
            return;

        if (id3v2_check_tag(tag)) {
            if (skipped) {
                struct uref *uref =
                    upipe_id3v2d_extract_uref_stream(upipe, skipped);
                upipe_id3v2d_output(upipe, uref, upump_p);
                skipped = 0;
            }
            if (!upipe_id3v2d_parse(upipe))
                return;
        } else {
            skipped += 3;
        }
    }
    upipe_id3v2d_flush(upipe, upump_p);
}

/** @internal @This is called when there is input data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_id3v2d_input(struct upipe *upipe,
                               struct uref *uref,
                               struct upump **upump_p)
{
    if (unlikely(ubase_check(uref_flow_get_def(uref, NULL)))) {
        upipe_id3v2d_flush(upipe, upump_p);
        upipe_id3v2d_store_flow_def(upipe, uref);
    } else {
        upipe_id3v2d_append_uref_stream(upipe, uref);
        upipe_id3v2d_work(upipe, upump_p);
    }
}

/** @internal @This sets the flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to set
 * @return an error code
 */
static int upipe_id3v2d_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));
    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches the control commands.
 *
 * @param upipe description structure of the pipe
 * @param command command to dispatch
 * @param args optional arguments
 * @return an error code
 */
static int upipe_id3v2d_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_id3v2d_control_output(upipe, command, args));
    switch (command) {
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_id3v2d_set_flow_def(upipe, flow_def);
    }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the static structure for id3v2 pipe manager. */
static struct upipe_mgr upipe_id3v2d_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ID3V2D_SIGNATURE,
    .upipe_alloc = upipe_id3v2d_alloc,
    .upipe_input = upipe_id3v2d_input,
    .upipe_control = upipe_id3v2d_control,
};

/** @This returns the id3v2 pipe manager. */
struct upipe_mgr *upipe_id3v2d_mgr_alloc(void)
{
    return &upipe_id3v2d_mgr;
}
