/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 */

/** @file
 * @short Upipe module - htons
 */

#define _XOPEN_SOURCE

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_htons.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>

#define EXPECTED_FLOW_DEF "block."

/** upipe_htons structure */ 
struct upipe_htons {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_htons, upipe, UPIPE_HTONS_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_htons, urefcount, upipe_htons_free)
UPIPE_HELPER_VOID(upipe_htons);
UPIPE_HELPER_OUTPUT(upipe_htons, output, flow_def, output_state, request_list);

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_htons_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct ubuf *ubuf;
    size_t size = 0;
    int remain, bufsize = -1, offset = 0;
    uint8_t *buf = NULL;

#ifdef UPIPE_WORDS_BIGENDIAN
    upipe_htons_output(upipe, uref, upump_p);
    return;
#endif

    /* block size */
    if (unlikely(!ubase_check(uref_block_size(uref, &size)))) {
        upipe_warn(upipe, "could not read uref block size");
        uref_free(uref);
        return;
    }
    /* copy ubuf if shared or not 16b-unaligned or segmented */
    bufsize = -1;
    if (!ubase_check(uref_block_write(uref, 0, &bufsize, &buf)) ||
        ((uintptr_t)buf & 1) || bufsize != size) {
        ubuf = ubuf_block_copy(uref->ubuf->mgr, uref->ubuf, 0, size);
        if (unlikely(!ubuf)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        uref_attach_ubuf(uref, ubuf);
    } else {
        uref_block_unmap(uref, 0);
    }

    /* process ubuf chunks */
    while (size > 0) {
        bufsize = -1;
        if (unlikely(!ubase_check(uref_block_write(uref, offset, &bufsize,
                                  &buf)))) {
            upipe_warn(upipe, "unexpected buffer error");
            uref_free(uref);
            return;
        }
        if (unlikely((uintptr_t)buf & 1)) {
            upipe_warn_va(upipe, "unaligned buffer: %p", buf);
        }

        swab(buf, buf, bufsize & ~1);

        uref_block_unmap(uref, offset);
        offset += bufsize;
        size -= bufsize;
    }

    upipe_htons_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_htons_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    upipe_htons_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a skip pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_htons_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_htons_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_htons_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a skip pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_htons_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_htons_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_htons_init_urefcount(upipe);
    upipe_htons_init_output(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_htons_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_htons_clean_output(upipe);
    upipe_htons_clean_urefcount(upipe);
    upipe_htons_free_void(upipe);
}

static struct upipe_mgr upipe_htons_mgr = {
    .refcount = NULL,
    .signature = UPIPE_HTONS_SIGNATURE,

    .upipe_alloc = upipe_htons_alloc,
    .upipe_input = upipe_htons_input,
    .upipe_control = upipe_htons_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for skip pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_htons_mgr_alloc(void)
{
    return &upipe_htons_mgr;
}
