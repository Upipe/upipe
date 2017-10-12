/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe module syncing on a transport stream
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
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_output_size.h>
#include <upipe-ts/upipe_ts_sync.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** default number of packets to sync with */
#define DEFAULT_TS_SYNC 2
/** we only accept blocks */
#define EXPECTED_FLOW_DEF "block."
/** when configured with standard TS size, we output TS packets */
#define OUTPUT_FLOW_DEF "block.mpegts."
/** otherwise there is a suffix to decaps */
#define SUFFIX_OUTPUT_FLOW_DEF "block.mpegtssuffix."
/** TS synchronization word */
#define TS_SYNC 0x47

/** @internal @This is the private context of a ts_sync pipe. */
struct upipe_ts_sync {
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
    /** number of packets to sync with */
    unsigned int ts_sync;
    /** next uref to be processed */
    struct uref *next_uref;
    /** original size of the next uref */
    size_t next_uref_size;
    /** urefs received after next uref */
    struct uchain urefs;
    /** true if we have thrown the sync_acquired event */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_sync, upipe, UPIPE_TS_SYNC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_sync, urefcount, upipe_ts_sync_free)
UPIPE_HELPER_VOID(upipe_ts_sync)
UPIPE_HELPER_SYNC(upipe_ts_sync, acquired)
UPIPE_HELPER_UREF_STREAM(upipe_ts_sync, next_uref, next_uref_size, urefs, NULL)

UPIPE_HELPER_OUTPUT(upipe_ts_sync, output, flow_def, output_state, request_list)
UPIPE_HELPER_OUTPUT_SIZE(upipe_ts_sync, output_size)

/** @internal @This allocates a ts_sync pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_sync_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_sync_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    upipe_ts_sync_init_urefcount(upipe);
    upipe_ts_sync_init_sync(upipe);
    upipe_ts_sync_init_output(upipe);
    upipe_ts_sync_init_output_size(upipe, TS_SIZE);
    upipe_ts_sync->ts_sync = DEFAULT_TS_SYNC;
    upipe_ts_sync->next_uref = NULL;
    ulist_init(&upipe_ts_sync->urefs);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This checks the presence of the required number of sync words
 * in the working buffer.
 *
 * @param upipe description structure of the pipe
 * @param offset_p written with the offset of the potential first TS packet in
 * the working buffer
 * @return false if not enough sync words could be tested
 */
static bool upipe_ts_sync_check(struct upipe *upipe, size_t *offset_p)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    for ( ; ; ) {
        if (unlikely(!ubase_check(uref_block_scan(upipe_ts_sync->next_uref,
                                                  offset_p, TS_SYNC))))
            return false;

        /* first octet at *offset_p is a sync word */
        int ts_sync = upipe_ts_sync->ts_sync - 1;
        for (int offset = *offset_p + upipe_ts_sync->output_size; ts_sync;
             ts_sync--, offset += upipe_ts_sync->output_size) {
            const uint8_t *buffer;
            int size = 1;
            uint8_t word;
            if (unlikely(!ubase_check(uref_block_read(upipe_ts_sync->next_uref,
                                          offset, &size, &buffer))))
                /* not enough sync words could be tested */
                return false;
            assert(size == 1);
            word = *buffer;
            uref_block_unmap(upipe_ts_sync->next_uref, offset);
            if (word != TS_SYNC) {
                *offset_p += 1;
                break;
            }
        }
        if (!ts_sync)
            break;
    }

    return true;
}

/** @internal @This flushes all input buffers.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_sync_flush(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (upipe_ts_sync->acquired) {
        size_t offset = 0, size;
        while (upipe_ts_sync->next_uref != NULL &&
               ubase_check(uref_block_size(upipe_ts_sync->next_uref, &size)) &&
               size >= upipe_ts_sync->output_size &&
               ubase_check(uref_block_scan(upipe_ts_sync->next_uref, &offset, TS_SYNC)) &&
               !offset) {
            struct uref *output = upipe_ts_sync_extract_uref_stream(upipe,
                                                        upipe_ts_sync->output_size);
            if (unlikely(output == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                continue;
            }
            upipe_ts_sync_output(upipe, output, upump_p);
        }
    }

    upipe_ts_sync_clean_uref_stream(upipe);
    upipe_ts_sync_init_uref_stream(upipe);
}

/** @internal @This tries to find TS packets in the buffered input urefs.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_sync_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (unlikely(ubase_check(uref_flow_get_discontinuity(uref)))) {
        upipe_warn(upipe, "received discontinuity, flushing buffer");
        upipe_ts_sync_flush(upipe, upump_p);
    }

    upipe_ts_sync_append_uref_stream(upipe, uref);

    while (upipe_ts_sync->next_uref != NULL) {
        size_t offset = 0;
        bool ret = upipe_ts_sync_check(upipe, &offset);
        if (offset) {
            upipe_ts_sync_sync_lost(upipe);
            upipe_ts_sync_consume_uref_stream(upipe, offset);
        }
        if (!ret)
            break;

        /* upipe_ts_sync_check said there is at least one TS packet there. */
        upipe_ts_sync_sync_acquired(upipe);
        struct uref *output = upipe_ts_sync_extract_uref_stream(upipe,
                                                    upipe_ts_sync->output_size);
        if (unlikely(output == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            continue;
        }
        upipe_ts_sync_output(upipe, output, upump_p);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the flow definition is not handled
 */
static int upipe_ts_sync_set_flow_def(struct upipe *upipe,
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
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    UBASE_RETURN(uref_block_flow_set_size(flow_def_dup,
                                          upipe_ts_sync->output_size))
    UBASE_RETURN(uref_flow_set_def(flow_def_dup, OUTPUT_FLOW_DEF))
    upipe_ts_sync_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the configured number of packets to synchronize
 * with.
 *
 * @param upipe description structure of the pipe
 * @param sync_p filled in with number of packets
 * @return an error code
 */
static int _upipe_ts_sync_get_sync(struct upipe *upipe, int *sync_p)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    assert(sync_p != NULL);
    *sync_p = upipe_ts_sync->ts_sync;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the configured number of packets to synchronize with.
 * The higher the value, the slower the synchronization, but the fewer false
 * positives. The minimum (and default) value is 2.
 *
 * @param upipe description structure of the pipe
 * @param sync number of packets
 * @return an error code
 */
static int _upipe_ts_sync_set_sync(struct upipe *upipe, int sync)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (sync < DEFAULT_TS_SYNC)
        return UBASE_ERR_INVALID;
    upipe_ts_sync->ts_sync = sync;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts sync pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_sync_control(struct upipe *upipe,
                                 int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_sync_control_output(upipe, command, args));
    UBASE_HANDLED_RETURN(
        upipe_ts_sync_control_output_size(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_sync_set_flow_def(upipe, flow_def);
        }

        case UPIPE_TS_SYNC_GET_SYNC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_SYNC_SIGNATURE)
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_SYNC_SIGNATURE);
            int *sync_p = va_arg(args, int *);
            return _upipe_ts_sync_get_sync(upipe, sync_p);
        }
        case UPIPE_TS_SYNC_SET_SYNC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_SYNC_SIGNATURE)
            int sync = va_arg(args, int);
            return _upipe_ts_sync_set_sync(upipe, sync);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sync_free(struct upipe *upipe)
{
    upipe_ts_sync_flush(upipe, NULL);
    upipe_throw_dead(upipe);

    upipe_ts_sync_clean_uref_stream(upipe);
    upipe_ts_sync_clean_output(upipe);
    upipe_ts_sync_clean_output_size(upipe);
    upipe_ts_sync_clean_sync(upipe);
    upipe_ts_sync_clean_urefcount(upipe);
    upipe_ts_sync_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_sync_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SYNC_SIGNATURE,

    .upipe_alloc = upipe_ts_sync_alloc,
    .upipe_input = upipe_ts_sync_input,
    .upipe_control = upipe_ts_sync_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_sync pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_sync_mgr_alloc(void)
{
    return &upipe_ts_sync_mgr;
}
