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
#include "upipe/ubuf_block.h"
#include "upipe/ulist.h"
#include "upipe/uref.h"
#include "upipe/uref_block.h"
#include "upipe/uref_clock.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_urefcount_real.h"
#include "upipe/upipe_helper_subpipe.h"
#include "upipe/upipe_helper_output.h"

#include "upipe-modules/upipe_id3v2_encaps.h"

#define EXPECTED_FLOW_DEF       "block."

/** @internal @This is the private context of the sub pipe. */
struct upipe_id3v2e_sub {
    /** upipe public structure */
    struct upipe upipe;
    /** urefcount structure */
    struct urefcount urefcount;
    /** chain to the super pipe list */
    struct uchain uchain;
    /** list of input buffers */
    struct uchain urefs;
};

UPIPE_HELPER_UPIPE(upipe_id3v2e_sub, upipe, UPIPE_ID3V2E_SUB_SIGNATURE);
UPIPE_HELPER_VOID(upipe_id3v2e_sub);
UPIPE_HELPER_UREFCOUNT(upipe_id3v2e_sub, urefcount, upipe_id3v2e_sub_free);

/** @internal @This is the private context of the pipe. */
struct upipe_id3v2e {
    /** upipe public structure */
    struct upipe upipe;
    /** external urefcount structure */
    struct urefcount urefcount;
    /** internal urefcount structure */
    struct urefcount urefcount_real;
    /** manager of the sub pipes */
    struct upipe_mgr sub_mgr;
    /** list of sub pipes */
    struct uchain subs;
    /** output pipe */
    struct upipe *output;
    /** output flow def */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** output requests */
    struct uchain requests;
};

UPIPE_HELPER_UPIPE(upipe_id3v2e, upipe, UPIPE_ID3V2E_SIGNATURE);
UPIPE_HELPER_VOID(upipe_id3v2e);
UPIPE_HELPER_UREFCOUNT(upipe_id3v2e, urefcount, upipe_id3v2e_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_id3v2e, urefcount_real, upipe_id3v2e_free);
UPIPE_HELPER_OUTPUT(upipe_id3v2e, output, flow_def, output_state, requests);

UPIPE_HELPER_SUBPIPE(upipe_id3v2e, upipe_id3v2e_sub,
                     input, sub_mgr, subs, uchain);

/** @internal @This allocates an id3v2 encaps sub pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe.
 */
static struct upipe *upipe_id3v2e_sub_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature,
                                            va_list args)
{
    struct upipe *upipe =
        upipe_id3v2e_sub_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_id3v2e_sub_init_urefcount(upipe);
    upipe_id3v2e_sub_init_sub(upipe);

    struct upipe_id3v2e_sub *upipe_id3v2e_sub =
        upipe_id3v2e_sub_from_upipe(upipe);
    ulist_init(&upipe_id3v2e_sub->urefs);

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_id3v2e_sub_free(struct upipe *upipe)
{
    struct upipe_id3v2e_sub *upipe_id3v2e_sub =
        upipe_id3v2e_sub_from_upipe(upipe);
    struct uchain *uchain;

    upipe_throw_dead(upipe);

    while ((uchain = ulist_pop(&upipe_id3v2e_sub->urefs)))
        uref_free(uref_from_uchain(uchain));

    upipe_id3v2e_sub_clean_sub(upipe);
    upipe_id3v2e_sub_clean_urefcount(upipe);
    upipe_id3v2e_sub_free_void(upipe);
}

/** @internal @This is called when there is input data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_id3v2e_sub_input(struct upipe *upipe,
                                   struct uref *uref,
                                   struct upump **upump_p)
{
    struct upipe_id3v2e_sub *upipe_id3v2e_sub =
        upipe_id3v2e_sub_from_upipe(upipe);

    uint64_t dts = 0;
    uref_clock_get_dts_prog(uref, &dts);

    uint8_t header[ID3V2_HEADER_SIZE];
    int ret = uref_block_extract(uref, 0, ID3V2_HEADER_SIZE, header);
    if (unlikely(!ubase_check(ret))) {
        upipe_warn(upipe, "invalid ID3 tag header");
        uref_free(uref);
        return;
    }

    uint32_t total_size = id3v2_get_total_size(header);
    uint8_t tag[total_size];
    ret = uref_block_extract(uref, 0, total_size, tag);
    if (unlikely(!ubase_check(ret))) {
        upipe_warn(upipe, "incomplete ID3 tag");
        uref_free(uref);
        return;
    }

    if (!id3v2_validate(tag, total_size)) {
        upipe_warn(upipe, "invalid ID3 tag");
        uref_free(uref);
        return;
    }

    uint32_t output_size = 0;
    id3v2_unsynchronise(tag, NULL, &output_size);
    if (output_size != total_size) {
        struct ubuf *ubuf = ubuf_block_alloc(uref->ubuf->mgr, output_size);
        if (unlikely(!ubuf)) {
            upipe_warn(upipe, "fail to unsynchronise ID3 tag");
            uref_free(uref);
            return;
        }

        uint8_t *output;
        int unsync_size = -1;
        ret = ubuf_block_write(ubuf, 0, &unsync_size, &output);
        if (unlikely(!ubase_check(ret))) {
            upipe_warn(upipe, "fail to write unsynchronise ID3 tag");
            ubuf_free(ubuf);
            uref_free(uref);
            return;
        }

        output_size = unsync_size;
        id3v2_unsynchronise(tag, output, &output_size);
        ubuf_block_unmap(ubuf, 0);
        if (output_size != unsync_size) {
            upipe_warn(upipe, "fail to unsynchronise ID3 tag");
            ubuf_free(ubuf);
            uref_free(uref);
            return;
        }

        uref_attach_ubuf(uref, ubuf);
    }

    struct uchain *next = NULL;
    struct uchain *uchain;
    ulist_foreach(&upipe_id3v2e_sub->urefs, uchain) {
        uint64_t next_dts = 0;
        uref_clock_get_dts_prog(uref_from_uchain(uchain), &next_dts);
        if (dts < next_dts) {
            next = uchain;
            break;
        }
    }
    if (next)
        ulist_insert(next->prev, next, uref_to_uchain(uref));
    else
        ulist_add(&upipe_id3v2e_sub->urefs, uref_to_uchain(uref));
}

/** @internal @This sets the flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to set
 * @return an error code
 */
static int upipe_id3v2e_sub_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches the control commands.
 *
 * @param upipe description structure of the pipe
 * @param command command to dispatch
 * @param args optional arguments
 * @return an error code
 */
static int upipe_id3v2e_sub_control(struct upipe *upipe,
                                    int command,
                                    va_list args)
{
    UBASE_HANDLED_RETURN(upipe_id3v2e_sub_control_super(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_id3v2e_sub_set_flow_def(upipe, flow_def);
        }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This initializes the sub manager for a id3v2_encaps pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_id3v2e_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_id3v2e *upipe_id3v2e = upipe_id3v2e_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = upipe_id3v2e_to_sub_mgr(upipe_id3v2e);
    memset(sub_mgr, 0, sizeof (*sub_mgr));
    sub_mgr->refcount = upipe_id3v2e_to_urefcount_real(upipe_id3v2e);
    sub_mgr->signature = UPIPE_ID3V2E_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_id3v2e_sub_alloc;
    sub_mgr->upipe_input = upipe_id3v2e_sub_input;
    sub_mgr->upipe_control = upipe_id3v2e_sub_control;
}

/** @internal @This allocates an id3v2 encaps pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe.
 */
static struct upipe *upipe_id3v2e_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature,
                                        va_list args)
{
    struct upipe *upipe = upipe_id3v2e_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_id3v2e_init_urefcount(upipe);
    upipe_id3v2e_init_urefcount_real(upipe);
    upipe_id3v2e_init_output(upipe);
    upipe_id3v2e_init_sub_inputs(upipe);
    upipe_id3v2e_init_sub_mgr(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This free the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_id3v2e_free(struct upipe *upipe)
{
    upipe_id3v2e_clean_sub_inputs(upipe);
    upipe_id3v2e_clean_output(upipe);
    upipe_id3v2e_clean_urefcount_real(upipe);
    upipe_id3v2e_clean_urefcount(upipe);
    upipe_id3v2e_free_void(upipe);
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_id3v2e_no_ref(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_id3v2e_release_urefcount_real(upipe);
}

/** @internal @This is called when there is input data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_id3v2e_input(struct upipe *upipe,
                               struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_id3v2e *upipe_id3v2e = upipe_id3v2e_from_upipe(upipe);

    uint64_t dts = 0;
    uref_clock_get_dts_prog(uref, &dts);

    struct ubuf *ubuf = NULL;
    struct uchain *sub;
    ulist_foreach(&upipe_id3v2e->subs, sub) {
        struct upipe_id3v2e_sub *upipe_id3v2e_sub =
            upipe_id3v2e_sub_from_uchain(sub);

        struct uchain *uchain, *tmp;
        ulist_delete_foreach(&upipe_id3v2e_sub->urefs, uchain, tmp) {
            struct uref *current_uref = uref_from_uchain(uchain);
            uint64_t current_dts = 0;
            uref_clock_get_dts_prog(current_uref, &current_dts);

            if (current_dts <= dts) {
                struct ubuf *current_ubuf = uref_detach_ubuf(current_uref);
                ulist_delete(uchain);
                uref_free(current_uref);
                if (!ubuf)
                    ubuf = current_ubuf;
                else
                    if (unlikely(!ubase_check(
                                ubuf_block_append(ubuf, current_ubuf)))) {
                        upipe_warn(upipe, "fail to append tag");
                        ubuf_free(current_ubuf);
                    }
            } else
                break;
        }
    }

    if (ubuf) {
        struct ubuf *current_ubuf = uref_detach_ubuf(uref);
        if (unlikely(!ubase_check(ubuf_block_append(ubuf, current_ubuf)))) {
            upipe_warn(upipe, "fail to append buffer");
            ubuf_free(ubuf);
            uref_attach_ubuf(uref, current_ubuf);
        } else
            uref_attach_ubuf(uref, ubuf);
    }
    upipe_id3v2e_output(upipe, uref, upump_p);
}

/** @internal @This sets the flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to set
 * @return an error code
 */
static int upipe_id3v2e_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));
    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_id3v2e_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches the control commands.
 *
 * @param upipe description structure of the pipe
 * @param command command to dispatch
 * @param args optional arguments
 * @return an error code
 */
static int upipe_id3v2e_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_id3v2e_control_inputs(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_id3v2e_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_id3v2e_set_flow_def(upipe, flow_def);
        }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the static structure for id3v2 pipe manager. */
static struct upipe_mgr upipe_id3v2e_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ID3V2E_SIGNATURE,
    .upipe_alloc = upipe_id3v2e_alloc,
    .upipe_input = upipe_id3v2e_input,
    .upipe_control = upipe_id3v2e_control,
};

/** @This returns the id3v2 pipe manager. */
struct upipe_mgr *upipe_id3v2e_mgr_alloc(void)
{
    return &upipe_id3v2e_mgr;
}
