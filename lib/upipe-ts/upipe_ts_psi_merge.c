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
 * @short Upipe module merging PSI sections from TS input
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_psi_merge.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>

/** we only accept formerly TS packets that contain PSI sections. */
#define EXPECTED_FLOW_DEF "block.mpegtspsi."

/** @internal @This is the private context of a ts_psim pipe. */
struct upipe_ts_psim {
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** next uref to be processed */
    struct uref *next_uref;
    /** true if we have thrown the sync_acquired event */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psim, upipe, UPIPE_TS_PSIM_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_psim, EXPECTED_FLOW_DEF)
UPIPE_HELPER_SYNC(upipe_ts_psim, acquired)

UPIPE_HELPER_OUTPUT(upipe_ts_psim, output, flow_def, flow_def_sent)

/** @internal @This allocates a ts_psim pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psim_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_psim_alloc_flow(mgr, uprobe, signature,
                                                   args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_psim *upipe_ts_psim = upipe_ts_psim_from_upipe(upipe);
    upipe_ts_psim_init_sync(upipe);
    upipe_ts_psim_init_output(upipe);
    upipe_ts_psim->next_uref = NULL;
    upipe_ts_psim_store_flow_def(upipe, flow_def);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This flushes all input buffers.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psim_flush(struct upipe *upipe)
{
    struct upipe_ts_psim *upipe_ts_psim = upipe_ts_psim_from_upipe(upipe);
    if (upipe_ts_psim->next_uref != NULL) {
        uref_free(upipe_ts_psim->next_uref);
        upipe_ts_psim->next_uref = NULL;
    }
    upipe_ts_psim_sync_lost(upipe);
}

/** @internal @This merges a PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref pointing to (part of) a PSI section
 * @param upump pump that generated the buffer
 * @return false if the uref has been entirely consumed
 */
static bool upipe_ts_psim_merge(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_ts_psim *upipe_ts_psim = upipe_ts_psim_from_upipe(upipe);
    if (upipe_ts_psim->next_uref != NULL) {
        struct ubuf *ubuf = ubuf_dup(uref->ubuf);
        if (unlikely(ubuf == NULL ||
                     !uref_block_append(upipe_ts_psim->next_uref, ubuf))) {
            upipe_ts_psim_flush(upipe);
            if (ubuf != NULL)
                ubuf_free(ubuf);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return false;
        }
    } else {
        /* Check for stuffing */
        uint8_t table_id;
        if (unlikely(!uref_block_extract(uref, 0, 1, &table_id) ||
                     table_id == 0xff)) {
            return false;
        }

        upipe_ts_psim->next_uref = uref_dup(uref);
        if (unlikely(upipe_ts_psim->next_uref == NULL)) {
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return false;
        }
    }

    bool ret;
    size_t size;
    ret = uref_block_size(upipe_ts_psim->next_uref, &size);
    assert(ret);
    if (size < PSI_HEADER_SIZE)
        return false;

    uint8_t buffer[PSI_HEADER_SIZE];
    const uint8_t *psi_header = uref_block_peek(upipe_ts_psim->next_uref,
                                                0, PSI_HEADER_SIZE, buffer);
    assert(psi_header != NULL);

    uint16_t length = psi_get_length(psi_header);
    ret = uref_block_peek_unmap(upipe_ts_psim->next_uref, 0, buffer,
                                psi_header);
    assert(ret);

    if (unlikely(length + PSI_HEADER_SIZE > PSI_PRIVATE_MAX_SIZE)) {
        upipe_warn(upipe, "wrong PSI header");
        upipe_ts_psim_flush(upipe);
        return false;
    }

    if (length + PSI_HEADER_SIZE > size)
        return false;

    ret = uref_block_resize(upipe_ts_psim->next_uref, 0,
                            length + PSI_HEADER_SIZE);
    assert(ret);
    upipe_ts_psim_output(upipe, upipe_ts_psim->next_uref, upump);
    upipe_ts_psim->next_uref = NULL;
    if (length + PSI_HEADER_SIZE == size)
        return false;

    size_t uref_size;
    ret = uref_block_size(uref, &uref_size);
    assert(ret);
    ret = uref_block_resize(uref, length + PSI_HEADER_SIZE - (size - uref_size),
                            -1);
    assert(ret);
    return true;
}

/** @internal @This takes the payload of a TS packet and finds PSI sections
 * inside it.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_psim_work(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_ts_psim *upipe_ts_psim = upipe_ts_psim_from_upipe(upipe);
    if (unlikely(uref_flow_get_discontinuity(uref)))
        upipe_ts_psim_flush(upipe);

    if (uref_block_get_start(uref)) {
        if (likely(upipe_ts_psim->acquired)) {
            /* just remove pointer_field */
            if (unlikely(!uref_block_resize(uref, 1, -1))) {
                uref_free(uref);
                upipe_ts_psim_flush(upipe);
                return;
            }
        } else {
            /* jump to the start of the next section */
            uint8_t pointer_field;
            if (unlikely(!uref_block_extract(uref, 0, 1, &pointer_field) ||
                         !uref_block_resize(uref, 1 + pointer_field, -1))) {
                uref_free(uref);
                return;
            }
            upipe_ts_psim_sync_acquired(upipe);
        }
        bool ret = uref_block_delete_start(uref);
        assert(ret);

    } else if (unlikely(upipe_ts_psim->next_uref == NULL)) {
        uref_free(uref);
        upipe_ts_psim_flush(upipe);
        return;
    }

    while (upipe_ts_psim_merge(upipe, uref, upump));
    uref_free(uref);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_psim_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    if (unlikely(uref->ubuf == NULL)) {
        upipe_ts_psim_output(upipe, uref, upump);
        return;
    }

    upipe_ts_psim_work(upipe, uref, upump);
}

/** @internal @This processes control commands on a ts psim pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_psim_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_psim_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psim_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_psim_set_output(upipe, output);
        }
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psim_free(struct upipe *upipe)
{
    struct upipe_ts_psim *upipe_ts_psim = upipe_ts_psim_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_ts_psim_clean_output(upipe);
    upipe_ts_psim_clean_sync(upipe);

    if (upipe_ts_psim->next_uref != NULL)
        uref_free(upipe_ts_psim->next_uref);
    upipe_ts_psim_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_psim_mgr = {
    .signature = UPIPE_TS_PSIM_SIGNATURE,

    .upipe_alloc = upipe_ts_psim_alloc,
    .upipe_input = upipe_ts_psim_input,
    .upipe_control = upipe_ts_psim_control,
    .upipe_free = upipe_ts_psim_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all ts_psim pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psim_mgr_alloc(void)
{
    return &upipe_ts_psim_mgr;
}
