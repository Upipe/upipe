/*
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
 */

/** @file
 * @short Upipe module - htons
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe-modules/upipe_htons.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <arpa/inet.h>

#define EXPECTED_FLOW_DEF "block."

/** upipe_htons structure */ 
struct upipe_htons {
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_htons, upipe, UPIPE_HTONS_SIGNATURE);
UPIPE_HELPER_FLOW(upipe_htons, EXPECTED_FLOW_DEF)
UPIPE_HELPER_UBUF_MGR(upipe_htons, ubuf_mgr)
UPIPE_HELPER_OUTPUT(upipe_htons, output, flow_def, flow_def_sent);

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_htons_input(struct upipe *upipe, struct uref *uref,
                             struct upump *upump)
{
    struct upipe_htons *upipe_htons = upipe_htons_from_upipe(upipe);
    struct ubuf *ubuf;
    size_t size = 0;
    int remain, bufsize = -1, offset = 0;
    uint8_t *buf = NULL;

    if (unlikely(uref->ubuf == NULL)) {
        upipe_htons_output(upipe, uref, upump);
        return;
    }

    /* block size */
    if (unlikely(!uref_block_size(uref, &size))) {
        upipe_warn(upipe, "could not read uref block size");
        uref_free(uref);
        return;
    }
    /* copy ubuf if shared or 16b-unaligned */
    bufsize = -1;
    if (!uref_block_write(uref, 0, &bufsize, &buf) || ((uintptr_t)buf & 1)) {
        ubuf = ubuf_block_copy(upipe_htons->ubuf_mgr, uref->ubuf, 0, size);
        if (unlikely(!ubuf)) {
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            uref_free(uref);
            return;
        uref_attach_ubuf(uref, ubuf);
        }
    } else {
        uref_block_unmap(uref, 0);
    }

    /* process ubuf chunks */
    while(size > 0) {
        bufsize = -1;
        uref_block_write(uref, offset, &bufsize, &buf);
        if (unlikely((uintptr_t)buf & 1)) {
            upipe_warn_va(upipe, "unaligned buffer: %p", buf);
        }
        for (remain = bufsize; remain > 1; remain -= 2) {
            *(uint16_t *)buf = htons(*(uint16_t *)buf);
            buf += 2;
        }

        uref_block_unmap(uref, offset);
        offset += bufsize;
        size -= bufsize;
    }

    upipe_htons_output(upipe, uref, upump);
}

/** @internal @This processes control commands on a skip pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_htons_control(struct upipe *upipe,
                               enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_htons_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_htons_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_htons_set_output(upipe, output);
        }
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_htons_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_htons_set_ubuf_mgr(upipe, ubuf_mgr);
        }

        default:
            return false;
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
    struct uref *flow_def;
    struct upipe *upipe = upipe_htons_alloc_flow(mgr, uprobe, signature, args,
                                                &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_htons_init_output(upipe);
    upipe_htons_init_ubuf_mgr(upipe);

    upipe_htons_store_flow_def(upipe, flow_def);
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
    upipe_htons_clean_ubuf_mgr(upipe);
    upipe_htons_free_flow(upipe);
}

static struct upipe_mgr upipe_htons_mgr = {
    .signature = UPIPE_HTONS_SIGNATURE,

    .upipe_alloc = upipe_htons_alloc,
    .upipe_input = upipe_htons_input,
    .upipe_control = upipe_htons_control,
    .upipe_free = upipe_htons_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for skip pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_htons_mgr_alloc(void)
{
    return &upipe_htons_mgr;
}
