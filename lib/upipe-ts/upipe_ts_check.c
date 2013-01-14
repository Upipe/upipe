/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module checking that a buffer contains a given number of
 * aligned TS packets
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_linear_output.h>
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
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** TS packet size */
    size_t ts_size;
    /** true if we have thrown the ready event */
    bool ready;
    /** true if we have thrown the check_acquired event */
    bool acquired;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_check, upipe)

UPIPE_HELPER_LINEAR_OUTPUT(upipe_ts_check, output, flow_def, flow_def_sent)

/** @internal @This allocates a ts_check pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param ulog structure used to output logs
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_check_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         struct ulog *ulog)
{
    struct upipe_ts_check *upipe_ts_check = malloc(sizeof(struct upipe_ts_check));
    if (unlikely(upipe_ts_check == NULL))
        return NULL;
    struct upipe *upipe = upipe_ts_check_to_upipe(upipe_ts_check);
    upipe_init(upipe, uprobe, ulog);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_TS_CHECK_SIGNATURE;
    urefcount_init(&upipe_ts_check->refcount);
    upipe_ts_check_init_output(upipe);
    upipe_ts_check->ts_size = TS_SIZE;
    upipe_ts_check->ready = false;
    return upipe;
}

/** @internal @This checks the presence of the sync word.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the packet is invalid
 */
static bool upipe_ts_check_check(struct upipe *upipe, struct uref *uref)
{
    const uint8_t *buffer;
    int size = 1;
    uint8_t word;
    if (unlikely(!uref_block_read(uref, 0, &size, &buffer))) {
        uref_free(uref);
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return false;
    }
    assert(size == 1);
    word = *buffer;
    uref_block_unmap(uref, 0, size);
    if (word != TS_SYNC) {
        uref_free(uref);
        ulog_warning(upipe->ulog, "invalid TS sync 0x%"PRIx8, word);
        return false;
    }

    upipe_ts_check_output(upipe, uref);
    return true;
}

/** @internal @This tries to find TS packets in the buffered input urefs.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_ts_check_work(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_check *upipe_ts_check = upipe_ts_check_from_upipe(upipe);
    size_t size;
    if (unlikely(!uref_block_size(uref, &size))) {
        uref_free(uref);
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return;
    }

    while (size > upipe_ts_check->ts_size) {
        struct uref *output = uref_dup(uref);
        if (unlikely(output == NULL)) {
            uref_free(uref);
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            return;
        }
        uref_block_resize(output, 0, upipe_ts_check->ts_size);
        if (!upipe_ts_check_check(upipe, output)) {
            uref_free(uref);
            return;
        }

        uref_block_resize(uref, upipe_ts_check->ts_size, -1);
        size -= upipe_ts_check->ts_size;
    }
    if (size == upipe_ts_check->ts_size)
        upipe_ts_check_check(upipe, uref);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_ts_check_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_check *upipe_ts_check = upipe_ts_check_from_upipe(upipe);

    const char *flow, *def, *def_flow;
    if (unlikely(!uref_flow_get_name(uref, &flow))) {
       ulog_warning(upipe->ulog, "received a buffer outside of a flow");
       uref_free(uref);
       return false;
    }

    if (unlikely(uref_flow_get_delete(uref))) {
        upipe_ts_check_set_flow_def(upipe, NULL);
        uref_free(uref);
        return true;
    }

    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(upipe_ts_check->flow_def != NULL))
            ulog_warning(upipe->ulog,
                         "received flow definition without delete first");

        if (unlikely(strncmp(def, EXPECTED_FLOW_DEF,
                             strlen(EXPECTED_FLOW_DEF)))) {
            ulog_warning(upipe->ulog,
                         "received an incompatible flow definition");
            uref_free(uref);
            upipe_ts_check_set_flow_def(upipe, NULL);
            return false;
        }

        ulog_debug(upipe->ulog, "flow definition for %s: %s", flow, def);
        /* FIXME make it dependant on the packet size */
        uref_flow_set_def(uref, OUTPUT_FLOW_DEF);
        upipe_ts_check_set_flow_def(upipe, uref);
        return true;
    }

    if (unlikely(upipe_ts_check->flow_def == NULL)) {
        ulog_warning(upipe->ulog, "pipe has no registered input flow");
        uref_free(uref);
        return false;
    }

    bool ret = uref_flow_get_name(upipe_ts_check->flow_def, &def_flow);
    assert(ret);
    if (unlikely(strcmp(def_flow, flow))) {
        ulog_warning(upipe->ulog,
                     "received a buffer not matching the current flow");
        uref_free(uref);
        return false;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return true;
    }

    upipe_ts_check_work(upipe, uref);
    return true;
}

/** @internal @This returns the configured size of TS packets.
 *
 * @param upipe description structure of the pipe
 * @param size_p filled in with the configured size, in octets
 * @return false in case of error
 */
static bool _upipe_ts_check_get_size(struct upipe *upipe, int *size_p)
{
    struct upipe_ts_check *upipe_ts_check = upipe_ts_check_from_upipe(upipe);
    assert(size_p != NULL);
    *size_p = upipe_ts_check->ts_size;
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
static bool _upipe_ts_check_set_size(struct upipe *upipe, int size)
{
    struct upipe_ts_check *upipe_ts_check = upipe_ts_check_from_upipe(upipe);
    if (size < 0)
        return false;
    /* FIXME change the flow definition */
    upipe_ts_check->ts_size = size;
    return true;
}

/** @internal @This processes control commands on a ts check pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_ts_check_control(struct upipe *upipe,
                                   enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_LINEAR_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_check_get_output(upipe, p);
        }
        case UPIPE_LINEAR_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_check_set_output(upipe, output);
        }

        case UPIPE_TS_CHECK_GET_SIZE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_CHECK_SIGNATURE);
            int *size_p = va_arg(args, int *);
            return _upipe_ts_check_get_size(upipe, size_p);
        }
        case UPIPE_TS_CHECK_SET_SIZE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_CHECK_SIGNATURE);
            int size = va_arg(args, int);
            return _upipe_ts_check_set_size(upipe, size);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on a ts check pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_check_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    if (likely(command == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        return upipe_ts_check_input(upipe, uref);
    }

    if (unlikely(!_upipe_ts_check_control(upipe, command, args)))
        return false;

    struct upipe_ts_check *upipe_ts_check = upipe_ts_check_from_upipe(upipe);
    if (likely(!upipe_ts_check->ready)) {
        upipe_ts_check->ready = true;
        upipe_throw_ready(upipe);
    }

    return true;
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_check_use(struct upipe *upipe)
{
    struct upipe_ts_check *upipe_ts_check = upipe_ts_check_from_upipe(upipe);
    urefcount_use(&upipe_ts_check->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_check_release(struct upipe *upipe)
{
    struct upipe_ts_check *upipe_ts_check = upipe_ts_check_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_ts_check->refcount))) {
        upipe_ts_check_clean_output(upipe);

        upipe_clean(upipe);
        urefcount_clean(&upipe_ts_check->refcount);
        free(upipe_ts_check);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_check_mgr = {
    .upipe_alloc = upipe_ts_check_alloc,
    .upipe_control = upipe_ts_check_control,
    .upipe_use = upipe_ts_check_use,
    .upipe_release = upipe_ts_check_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all ts_check pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_check_mgr_alloc(void)
{
    return &upipe_ts_check_mgr;
}
