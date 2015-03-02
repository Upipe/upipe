/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module checking that a buffer contains a given number of aligned TS packets
 *
 * This module also accepts @ref upipe_set_output_size, with the following
 * common values:
 * @table 2
 * @item size (in octets) @item description
 * @item 188 @item standard size of TS packets according to ISO/IEC 13818-1
 * @item 196 @item TS packet followed by an 8-octet timestamp or checksum
 * @item 204 @item TS packet followed by a 16-octet checksum
 * @end table
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_output_size.h>
#include <upipe-ts/upipe_ts_check.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** we only accept blocks */
#define EXPECTED_FLOW_DEF "block."
/** we only output TS packets */
#define OUTPUT_FLOW_DEF "block.mpegts."
/** TS synchronization word */
#define TS_SYNC 0x47

/** @internal @This is the private context of a ts_check pipe. */
struct upipe_ts_check {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** TS packet size */
    size_t output_size;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_check, upipe, UPIPE_TS_CHECK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_check, urefcount, upipe_ts_check_free)
UPIPE_HELPER_VOID(upipe_ts_check)
UPIPE_HELPER_OUTPUT(upipe_ts_check, output, flow_def, output_state, request_list)
UPIPE_HELPER_OUTPUT_SIZE(upipe_ts_check, output_size)

/** @internal @This allocates a ts_check pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_check_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_check_alloc_void(mgr, uprobe, signature,
                                                    args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_ts_check_init_urefcount(upipe);
    upipe_ts_check_init_output(upipe);
    upipe_ts_check_init_output_size(upipe, TS_SIZE);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This checks the presence of the sync word.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static bool upipe_ts_check_check(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    const uint8_t *buffer;
    int size = 1;
    uint8_t word;
    if (unlikely(!ubase_check(uref_block_read(uref, 0, &size, &buffer)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    assert(size == 1);
    word = *buffer;
    uref_block_unmap(uref, 0);
    if (word != TS_SYNC) {
        uref_free(uref);
        upipe_warn_va(upipe, "invalid TS sync 0x%"PRIx8, word);
        return false;
    }

    upipe_ts_check_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This tries to find TS packets in the buffered input urefs.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_check_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_ts_check *upipe_ts_check = upipe_ts_check_from_upipe(upipe);
    size_t size;
    if (unlikely(!ubase_check(uref_block_size(uref, &size)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    while (size > upipe_ts_check->output_size) {
        struct uref *output = uref_block_splice(uref, 0,
                                                upipe_ts_check->output_size);
        if (unlikely(output == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        if (!upipe_ts_check_check(upipe, output, upump_p)) {
            uref_free(uref);
            return;
        }

        uref_block_resize(uref, upipe_ts_check->output_size, -1);
        size -= upipe_ts_check->output_size;
    }
    if (size == upipe_ts_check->output_size)
        upipe_ts_check_check(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_check_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    struct upipe_ts_check *upipe_ts_check = upipe_ts_check_from_upipe(upipe);
    UBASE_RETURN(uref_block_flow_set_size(flow_def_dup,
                                          upipe_ts_check->output_size))
    UBASE_RETURN(uref_flow_set_def(flow_def_dup, OUTPUT_FLOW_DEF))
    upipe_ts_check_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts check pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_check_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_check_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_check_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_check_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_check_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_check_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_check_set_output(upipe, output);
        }
        case UPIPE_GET_OUTPUT_SIZE: {
            unsigned int *size_p = va_arg(args, unsigned int *);
            return upipe_ts_check_get_output_size(upipe, size_p);
        }
        case UPIPE_SET_OUTPUT_SIZE: {
            unsigned int size = va_arg(args, unsigned int);
            return upipe_ts_check_set_output_size(upipe, size);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_check_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_check_clean_output(upipe);
    upipe_ts_check_clean_output_size(upipe);
    upipe_ts_check_clean_urefcount(upipe);
    upipe_ts_check_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_check_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_CHECK_SIGNATURE,

    .upipe_alloc = upipe_ts_check_alloc,
    .upipe_input = upipe_ts_check_input,
    .upipe_control = upipe_ts_check_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_check pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_check_mgr_alloc(void)
{
    return &upipe_ts_check_mgr;
}
