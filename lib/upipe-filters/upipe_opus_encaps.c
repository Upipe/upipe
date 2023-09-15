/*
 * Copyright (C) 2023 EasyTools S.A.S.
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

/** @file
 * @short Upipe module encapsulating Opus access units
 */

#include "upipe/ubase.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_flow.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/uref_block.h"
#include "upipe/ubuf_block.h"
#include "upipe-filters/uref_opus_flow.h"
#include "upipe-filters/upipe_opus_encaps.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_sound_flow.h"

/** @internal @This is the expected input flow definition. */
#define EXPECTED_FLOW_DEF   "block.opus."

/** @internal @This is the private context of a opus_encaps pipe. */
struct upipe_opus_encaps {
    /** refcount management structure */
    struct urefcount urefcount;
    /** output pipe */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** output request list */
    struct uchain request_list;
    /** output encapsulation */
    enum uref_opus_encaps encaps;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_opus_encaps, upipe, UPIPE_OPUS_ENCAPS_SIGNATURE);
UPIPE_HELPER_FLOW(upipe_opus_encaps, EXPECTED_FLOW_DEF);
UPIPE_HELPER_UREFCOUNT(upipe_opus_encaps, urefcount,
                       upipe_opus_encaps_free);
UPIPE_HELPER_OUTPUT(upipe_opus_encaps, output, flow_def, output_state,
                    request_list);

/** @internal @This allocates a opus_encaps pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated and initialized opus_encaps pipe
 */
static struct upipe *upipe_opus_encaps_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature,
                                             va_list args)
{
    struct uref *flow_def = NULL;
    struct upipe *upipe =
        upipe_opus_encaps_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(!upipe))
        return NULL;

    upipe_opus_encaps_init_urefcount(upipe);
    upipe_opus_encaps_init_output(upipe);

    uint8_t encaps = UREF_OPUS_ENCAPS_RAW;
    if (ubase_check(uref_opus_flow_get_encaps(flow_def, &encaps))) {
        switch (encaps) {
            case UREF_OPUS_ENCAPS_TS:
            case UREF_OPUS_ENCAPS_RAW:
                break;
            default:
                encaps = UREF_OPUS_ENCAPS_RAW;
        }
    }

    struct upipe_opus_encaps *upipe_opus_encaps =
        upipe_opus_encaps_from_upipe(upipe);
    upipe_opus_encaps->encaps = encaps;

    upipe_throw_ready(upipe);

    uref_free(flow_def);

    return upipe;
}

/** @internal @This frees a opus_encaps pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_opus_encaps_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_opus_encaps_clean_output(upipe);
    upipe_opus_encaps_clean_urefcount(upipe);
    upipe_opus_encaps_free_flow(upipe);
}

/** @internal @This handles the input buffers.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer to handle
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_opus_encaps_input(struct upipe *upipe,
                                    struct uref *uref,
                                    struct upump **upump_p)
{
    if (!uref)
        return;

    size_t uref_size = 0;
    if (unlikely(!ubase_check(uref_block_size(uref, &uref_size)))) {
        upipe_warn(upipe, "fail to get buffer size");
        uref_free(uref);
        return;
    }

    if (unlikely(!uref->ubuf)) {
        upipe_warn(upipe, "no buffer found");
        uref_free(uref);
        return;
    }

    uint8_t nb_payload_bytes = (uref_size / 255) + 1;
    struct ubuf *au = ubuf_block_alloc(uref->ubuf->mgr, 2 + nb_payload_bytes);
    if (unlikely(!au)) {
        uref_free(uref);
        upipe_err(upipe, "fail to allocate access unit");
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *header;
    int size = -1;
    if (unlikely(!ubase_check(ubuf_block_write(au, 0, &size, &header)))) {
        ubuf_free(au);
        uref_free(uref);
        upipe_warn(upipe, "fail to write access unit");
        return;
    }

    header[0] = 0x7f;
    header[1] = 0xe0;
    uint8_t *tmp = header + 2;
    while (uref_size > 255) {
        *tmp = 255;
        uref_size -= 255;
        tmp++;
    }
    *tmp = uref_size;
    ubuf_block_unmap(au, 0);

    if (unlikely(!ubase_check(ubuf_block_append(au, uref_detach_ubuf(uref))))) {
        ubuf_free(au);
        uref_free(uref);
        upipe_warn(upipe, "fail to prepend access unit");
        return;
    }
    ubuf_block_merge(au->mgr, &au, 0, -1);
    uref_attach_ubuf(uref, au);
    upipe_opus_encaps_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def input flow definition to set
 * @return an error code
 */
static int upipe_opus_encaps_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    struct upipe_opus_encaps *upipe_opus_encaps =
        upipe_opus_encaps_from_upipe(upipe);

    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));
    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);

    uint64_t rate = 0;
    uint64_t samples = 0;
    uref_sound_flow_get_rate(flow_def, &rate);
    uref_sound_flow_get_samples(flow_def, &samples);

    uint64_t octetrate = 0;
    if (ubase_check(uref_block_flow_get_octetrate(flow_def, &octetrate))) {
        uint64_t au_per_sec = rate / samples;
        uint64_t header_size = 2 + ((octetrate / au_per_sec) / 255) + 1;
        octetrate += header_size * au_per_sec;
        uref_block_flow_set_octetrate(flow_def_dup, octetrate);
    }
    uref_opus_flow_set_encaps(flow_def_dup, upipe_opus_encaps->encaps);
    upipe_opus_encaps_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This handles the control commands of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param cmd type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_opus_encaps_control(struct upipe *upipe,
                                     int cmd, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_opus_encaps_control_output(upipe, cmd, args));

    switch (cmd) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_opus_encaps_set_flow_def(upipe, flow_def);
        }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the static management structure for opus_encaps
 * pipes.
 */
static struct upipe_mgr upipe_opus_encaps_mgr = {
    .refcount = NULL,
    .signature = UPIPE_OPUS_ENCAPS_SIGNATURE,

    .upipe_alloc = upipe_opus_encaps_alloc,
    .upipe_input = upipe_opus_encaps_input,
    .upipe_control = upipe_opus_encaps_control,
};

/** @This returns the management structure for all opus_encaps pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_opus_encaps_mgr_alloc(void)
{
    return &upipe_opus_encaps_mgr;
}
