/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module syncing on a transport stream
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
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** TS packet size */
    size_t ts_size;
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

UPIPE_HELPER_OUTPUT(upipe_ts_sync, output, flow_def, flow_def_sent)

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
    upipe_ts_sync->ts_size = TS_SIZE;
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
        if (unlikely(!uref_block_scan(upipe_ts_sync->next_uref, offset_p,
                                      TS_SYNC)))
            return false;

        /* first octet at *offset_p is a sync word */
        int ts_sync = upipe_ts_sync->ts_sync - 1;
        for (int offset = *offset_p + upipe_ts_sync->ts_size; ts_sync;
             ts_sync--, offset += upipe_ts_sync->ts_size) {
            const uint8_t *buffer;
            int size = 1;
            uint8_t word;
            if (unlikely(!uref_block_read(upipe_ts_sync->next_uref,
                                          offset, &size, &buffer)))
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
 * @param upump pump that generated the buffer
 */
static void upipe_ts_sync_flush(struct upipe *upipe, struct upump *upump)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (upipe_ts_sync->acquired) {
        size_t offset = 0, size;
        while (upipe_ts_sync->next_uref != NULL &&
               uref_block_size(upipe_ts_sync->next_uref, &size) &&
               size >= upipe_ts_sync->ts_size &&
               uref_block_scan(upipe_ts_sync->next_uref, &offset, TS_SYNC) &&
               !offset) {
            struct uref *output = upipe_ts_sync_extract_uref_stream(upipe,
                                                        upipe_ts_sync->ts_size);
            if (unlikely(output == NULL)) {
                upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
                continue;
            }
            upipe_ts_sync_output(upipe, output, upump);
        }
    }

    upipe_ts_sync_clean_uref_stream(upipe);
    upipe_ts_sync_init_uref_stream(upipe);
}

/** @internal @This tries to find TS packets in the buffered input urefs.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_sync_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (unlikely(uref_flow_get_discontinuity(uref)))
        upipe_ts_sync_flush(upipe, upump);

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
                                                    upipe_ts_sync->ts_size);
        if (unlikely(output == NULL)) {
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            continue;
        }
        upipe_ts_sync_output(upipe, output, upump);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the flow definition is not handled
 */
static bool upipe_ts_sync_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return false;
    if (!uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
        return false;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }
    /* FIXME make it dependant on the output size */
    if (unlikely(!uref_flow_set_def(flow_def_dup, OUTPUT_FLOW_DEF)))
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
    upipe_ts_sync_store_flow_def(upipe, flow_def_dup);
    return true;
}

/** @internal @This returns the configured size of TS packets.
 *
 * @param upipe description structure of the pipe
 * @param size_p filled in with the configured size, in octets
 * @return false in case of error
 */
static bool _upipe_ts_sync_get_size(struct upipe *upipe, int *size_p)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    assert(size_p != NULL);
    *size_p = upipe_ts_sync->ts_size;
    return true;
}

/** @internal @This sets the configured size of TS packets. Common values are:
 * @table 2
 * @item size (in octets) @item description
 * @item 188 @item standard size of TS packets according to ISO/IEC 13818-1
 * @item 196 @item TS packet followed by an 8-octet timestamp or checksum
 * @item 204 @item TS packet followed by a 16-octet checksum
 * @end table
 *
 * @param upipe description structure of the pipe
 * @param size configured size, in octets
 * @return false in case of error
 */
static bool _upipe_ts_sync_set_size(struct upipe *upipe, int size)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (size < 0)
        return false;
    upipe_ts_sync->ts_size = size;
    /* FIXME change flow definition */
    return true;
}

/** @internal @This returns the configured number of packets to synchronize
 * with.
 *
 * @param upipe description structure of the pipe
 * @param sync_p filled in with number of packets
 * @return false in case of error
 */
static bool _upipe_ts_sync_get_sync(struct upipe *upipe, int *sync_p)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    assert(sync_p != NULL);
    *sync_p = upipe_ts_sync->ts_sync;
    return true;
}

/** @internal @This sets the configured number of packets to synchronize with.
 * The higher the value, the slower the synchronization, but the fewer false
 * positives. The minimum (and default) value is 2.
 *
 * @param upipe description structure of the pipe
 * @param sync number of packets
 * @return false in case of error
 */
static bool _upipe_ts_sync_set_sync(struct upipe *upipe, int sync)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (sync < DEFAULT_TS_SYNC)
        return false;
    upipe_ts_sync->ts_sync = sync;
    return true;
}

/** @internal @This processes control commands on a ts sync pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_sync_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_sync_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_sync_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_sync_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_sync_set_output(upipe, output);
        }

        case UPIPE_TS_SYNC_GET_SIZE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_SYNC_SIGNATURE);
            int *size_p = va_arg(args, int *);
            return _upipe_ts_sync_get_size(upipe, size_p);
        }
        case UPIPE_TS_SYNC_SET_SIZE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_SYNC_SIGNATURE);
            int size = va_arg(args, int);
            return _upipe_ts_sync_set_size(upipe, size);
        }
        case UPIPE_TS_SYNC_GET_SYNC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_SYNC_SIGNATURE);
            int *sync_p = va_arg(args, int *);
            return _upipe_ts_sync_get_sync(upipe, sync_p);
        }
        case UPIPE_TS_SYNC_SET_SYNC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_SYNC_SIGNATURE);
            int sync = va_arg(args, int);
            return _upipe_ts_sync_set_sync(upipe, sync);
        }
        default:
            return false;
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
    .upipe_control = upipe_ts_sync_control
};

/** @This returns the management structure for all ts_sync pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_sync_mgr_alloc(void)
{
    return &upipe_ts_sync_mgr;
}
